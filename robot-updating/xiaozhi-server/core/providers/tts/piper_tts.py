"""Piper 本地神经 TTS provider——离线、零 key、比 Windows SAPI 自然得多。

依赖
----
    uv pip install piper-tts        # 自带 espeak-ng 音素数据与 onnxruntime 运行时

模型（中文只有 huayan 一个音色，medium 约 60MB）
------------------------------------------------
    models/piper/zh_CN-huayan-medium.onnx
    models/piper/zh_CN-huayan-medium.onnx.json
下载（HuggingFace 主站大陆不可达时用镜像）::

    https://hf-mirror.com/rhasspy/piper-voices/resolve/main/zh/zh_CN/huayan/medium/zh_CN-huayan-medium.onnx

配置示例（data/.config.yaml）::

    selected_module:
      TTS: PiperTTS
    TTS:
      PiperTTS:
        type: piper_tts
        model: models/piper/zh_CN-huayan-medium.onnx
        length_scale: 1.0   # 语速：>1 更慢、<1 更快（默认 1.0）
        volume: 1.0         # 音量倍率
        output_dir: tmp/

说明
----
· PiperVoice 加载一次约 2s，这里按模型路径缓存复用（进程内单例），不会每句都加载。
· 合成是 CPU 同步计算，放在线程里跑，避免阻塞事件循环。
· 输出 16bit 单声道 WAV，交给基类转 opus（WAV 解码不需要 ffmpeg）。
"""

import asyncio
import io
import os
import threading
import uuid
import wave
from datetime import datetime

from config.logger import setup_logging
from core.providers.tts.base import TTSProviderBase

TAG = __name__
logger = setup_logging()

# 模型路径 -> PiperVoice（进程内缓存；加载一次约 2s）
_VOICE_CACHE: dict = {}
_VOICE_LOCK = threading.Lock()


def _load_voice(model_path: str, use_cuda: bool = False):
    with _VOICE_LOCK:
        voice = _VOICE_CACHE.get(model_path)
        if voice is None:
            from piper import PiperVoice

            voice = PiperVoice.load(model_path, use_cuda=use_cuda)
            _VOICE_CACHE[model_path] = voice
            logger.bind(tag=TAG).info(f"Piper 模型已加载并缓存: {model_path}")
        return voice


class TTSProvider(TTSProviderBase):
    TTS_PARAM_CONFIG = [
        ("ttsVolume", "volume", 0.0, 2.0, 1.0, float),
        ("ttsRate", "length_scale", 0.5, 2.0, 1.0, lambda v: round(v, 2)),
    ]

    def __init__(self, config, delete_audio_file):
        super().__init__(config, delete_audio_file)
        self.audio_file_type = "wav"

        default_model = os.path.join("models", "piper", "zh_CN-huayan-medium.onnx")
        self.model_path = config.get("model") or config.get("model_path") or default_model
        if not os.path.isabs(self.model_path):
            # 相对路径按「服务端根目录」解析，便携版启动器把 cwd 设为 app/ 也成立
            self.model_path = os.path.abspath(self.model_path)
        if not os.path.exists(self.model_path):
            raise ValueError(
                f"Piper 模型不存在: {self.model_path}（先下载 zh_CN-huayan-medium.onnx）"
            )

        self.use_cuda = bool(config.get("use_cuda", False))

        length_scale = config.get("length_scale", None)
        if length_scale in (None, ""):
            # 兼容 rate（+ 越快）写法：rate 2 -> 0.88
            try:
                rate = float(config.get("rate", 0) or 0)
            except (TypeError, ValueError):
                rate = 0.0
            length_scale = max(0.5, min(2.0, 1.0 - rate * 0.06))
        self.length_scale = float(length_scale)

        try:
            self.volume = float(config.get("volume", 1.0) or 1.0)
        except (TypeError, ValueError):
            self.volume = 1.0

        self.speaker_id = config.get("speaker_id", None)
        self._apply_percentage_params(config)

    def generate_filename(self, extension=".wav"):
        return os.path.join(
            self.output_file,
            f"tts-{datetime.now().date()}@{uuid.uuid4().hex}{extension}",
        )

    def _syn_config(self):
        from piper import SynthesisConfig

        cfg = SynthesisConfig()
        cfg.length_scale = self.length_scale
        cfg.volume = self.volume
        if self.speaker_id is not None:
            try:
                cfg.speaker_id = int(self.speaker_id)
            except (TypeError, ValueError):
                pass
        return cfg

    def _synthesize_blocking(self, text: str) -> bytes:
        voice = _load_voice(self.model_path, self.use_cuda)
        cfg = self._syn_config()
        buf = io.BytesIO()
        with wave.open(buf, "wb") as wav_file:
            voice.synthesize_wav(text, wav_file, syn_config=cfg)
        data = buf.getvalue()
        if len(data) <= 44:
            raise Exception("Piper 没有产生有效音频")
        return data

    async def text_to_speak(self, text, output_file):
        try:
            data = await asyncio.to_thread(self._synthesize_blocking, text)
            if output_file:
                os.makedirs(os.path.dirname(output_file) or ".", exist_ok=True)
                with open(output_file, "wb") as f:
                    f.write(data)
                return None
            return data
        except Exception as e:
            raise Exception(f"Piper 请求失败: {e}")
