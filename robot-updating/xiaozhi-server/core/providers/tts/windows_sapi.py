"""Windows 本地 TTS（SAPI）provider——离线、零 key、不依赖外网。

背景
----
2026-10-12 起 Edge TTS 在部分大陆出口稳定返回 403（大陆访问微软朗读接口的已知
问题），机器人只出文字没有声音。Windows 自带中文语音（Huihui / Yaoyao / Kangkang）
可以完全离线合成，用它兜底。

配置示例（data/.config.yaml）::

    selected_module:
      TTS: WindowsSAPI
    TTS:
      WindowsSAPI:
        type: windows_sapi
        voice: Microsoft Huihui Desktop   # 见 GetInstalledVoices() 列表
        rate: 0        # 语速 -10..10（0 为默认）
        volume: 100    # 音量 0..100
        output_dir: tmp/

音质说明：SAPI 的中文音色（Huihui/Yaoyao）比 Edge 的晓晓机械，但没有网络依赖，
也不会 403；适合作为离线兜底或断网现场使用。
"""

import asyncio
import base64
import os
import subprocess
import uuid
from datetime import datetime

from config.logger import setup_logging
from core.providers.tts.base import TTSProviderBase

TAG = __name__
logger = setup_logging()

DEFAULT_VOICE = "Microsoft Huihui Desktop"


class TTSProvider(TTSProviderBase):
    TTS_PARAM_CONFIG = [
        ("ttsVolume", "volume", 0, 100, 100, int),
        ("ttsRate", "rate", -10, 10, 0, int),
    ]

    def __init__(self, config, delete_audio_file):
        super().__init__(config, delete_audio_file)
        self.audio_file_type = "wav"  # SAPI 直接输出 PCM WAV

        if config.get("private_voice"):
            self.voice = config.get("private_voice")
        else:
            self.voice = config.get("voice") or DEFAULT_VOICE

        rate = config.get("rate", 0)
        try:
            self.rate = max(-10, min(10, int(float(rate)))) if rate not in (None, "") else 0
        except (TypeError, ValueError):
            self.rate = 0

        volume = config.get("volume", 100)
        try:
            self.volume = max(0, min(100, int(float(volume)))) if volume not in (None, "") else 100
        except (TypeError, ValueError):
            self.volume = 100

        self._apply_percentage_params(config)

    def generate_filename(self, extension=".wav"):
        return os.path.join(
            self.output_file,
            f"tts-{datetime.now().date()}@{uuid.uuid4().hex}{extension}",
        )

    @staticmethod
    def _powershell_exe():
        """优先 pwsh（PowerShell 7）。

        实测（本机 Windows 11）：
          · powershell.exe 5.1 只注册了 "Microsoft Huihui Desktop" + Zira，
            选 Kangkang/Yaoyao/Huihui 会抛 "No matching voice is installed"；
          · pwsh 7 能看到全部 5 个（Huihui Desktop/Huihui/Yaoyao/Kangkang/Zira）。
        目标机没装 pwsh 时会自动回退到 powershell.exe（用默认音色，不报错）。
        """
        for name in ("pwsh.exe", "pwsh", "powershell.exe", "powershell"):
            probe = subprocess.run(["where", name], capture_output=True, text=True, shell=False)
            if probe.returncode == 0 and probe.stdout.strip():
                return probe.stdout.strip().splitlines()[0]
        return "powershell.exe"

    def _synthesize_blocking(self, text: str, wav_path: str) -> None:
        # 中文文本走 stdin（UTF-8），脚本本体用 -EncodedCommand 传，避免命令行编码问题
        voice = self.voice.replace("'", "''")
        script = (
            "[Console]::InputEncoding=[System.Text.Encoding]::UTF8;"
            "Add-Type -AssemblyName System.Speech;"
            "$s = New-Object System.Speech.Synthesis.SpeechSynthesizer;"
            f"try {{ $s.SelectVoice('{voice}') }} catch {{ Write-Output ('SELFAIL=' + $_.Exception.Message) }};"
            "Write-Output ('USED=' + $s.Voice.Name);"
            f"$s.Rate = {self.rate};"
            f"$s.Volume = {self.volume};"
            f"$s.SetOutputToWaveFile('{wav_path}');"
            "$s.Speak([Console]::In.ReadToEnd());"
            "$s.Dispose();"
        )
        encoded = base64.b64encode(script.encode("utf-16-le")).decode("ascii")
        proc = subprocess.run(
            [
                self._powershell_exe(),
                "-NoProfile",
                "-NonInteractive",
                "-ExecutionPolicy",
                "Bypass",
                "-EncodedCommand",
                encoded,
            ],
            input=text.encode("utf-8"),
            capture_output=True,
            timeout=max(15, self.tts_timeout),
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        if proc.returncode != 0:
            stderr = proc.stderr.decode("utf-8", "replace")[:300]
            raise Exception(f"Windows SAPI 合成失败(exit {proc.returncode}): {stderr}")
        # 音色没选中时（老 powershell.exe 只认 Huihui Desktop）给出提示，但不中断
        stdout = proc.stdout.decode("utf-8", "replace")
        used = ""
        for line in stdout.splitlines():
            if line.startswith("USED="):
                used = line[5:].strip()
            elif line.startswith("SELFAIL="):
                logger.bind(tag=TAG).warning(
                    f"音色 '{self.voice}' 不可用，回退默认音色: {line[8:].strip()[:120]}"
                )
        if used and self.voice and used != self.voice:
            logger.bind(tag=TAG).warning(f"请求音色 '{self.voice}' 未生效，实际使用 '{used}'")
        if not os.path.exists(wav_path) or os.path.getsize(wav_path) <= 44:
            raise Exception("Windows SAPI 没有产生有效音频（检查 voice 名是否存在）")

    async def text_to_speak(self, text, output_file):
        tmp_path = None
        target = output_file
        if not target:
            tmp_path = self.generate_filename()
            target = tmp_path
        try:
            os.makedirs(os.path.dirname(target) or ".", exist_ok=True)
            await asyncio.to_thread(self._synthesize_blocking, text, target)
            if tmp_path:
                with open(tmp_path, "rb") as f:
                    data = f.read()
                return data
            return None
        except Exception as e:
            raise Exception(f"Windows SAPI 请求失败: {e}")
        finally:
            if tmp_path and os.path.exists(tmp_path):
                try:
                    os.remove(tmp_path)
                except OSError:
                    pass
