"""TTS 优先级链 provider：按顺序尝试多个 TTS，前面的失败自动用后面的。

典型用法（在线优先、离线兜底）::

    selected_module:
      TTS: FallbackTTS
    TTS:
      FallbackTTS:
        type: fallback
        chain: [EdgeTTS, KokoroTTS]      # 从左到右尝试
        output_dir: tmp/
      EdgeTTS:
        type: edge
        voice: zh-CN-XiaoxiaoNeural
      KokoroTTS:
        type: kokoro
        ...

为什么需要它：Edge TTS（微软朗读）免费、无需 key、音色最好，但大陆部分出口会被
403（2026-10 实测某办公网 100% 403、另一张网正常）。有了这条链，能连上就用
晓晓的好音色，连不上就自动落到本地 Kokoro/Piper，不会出现「只有文字没有声音」。
"""

import asyncio
import os
import threading
import time
import uuid
from datetime import datetime

from config.logger import setup_logging
from core.providers.tts.base import TTSProviderBase
TAG = __name__
logger = setup_logging()

# 熔断状态放模块级：TTS provider 是「每条连接各建一个」，若把状态放在实例上，
# 每次新对话都会重新白试一次被墙的在线 TTS（实测每段新会话多等 ~2×4s）。
# 必须是 RLock：_mark_fail/_mark_ok 已经持锁，内部还会调也会加锁的 _state()，
# 用普通 Lock 会自我死锁（现场：第一次失败后 TTS 线程永久冻住，无声音无日志）。
_BREAKER = {}
_BREAKER_LOCK = threading.RLock()


def _server_root() -> str:
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def _load_full_config() -> dict:
    """取完整配置（优先用服务端启动时已加载的那份，避免重复解析）。"""
    try:
        from config.settings import config as loaded  # 启动时已 import 过

        if isinstance(loaded, dict) and loaded:
            return loaded
    except Exception:
        pass
    try:
        import yaml

        for name in ("data/.config.yaml", "config.yaml"):
            path = os.path.join(_server_root(), *name.split("/"))
            if os.path.exists(path):
                with open(path, encoding="utf-8") as f:
                    data = yaml.safe_load(f) or {}
                if data:
                    return data
    except Exception as e:
        logger.bind(tag=TAG).warning(f"读取配置失败: {e}")
    return {}


class TTSProvider(TTSProviderBase):
    def __init__(self, config, delete_audio_file):
        super().__init__(config, delete_audio_file)
        # 子 provider 一律按「返回字节」模式建，便于我们统一写文件/回传
        self.chain = config.get("chain") or ["EdgeTTS", "KokoroTTS"]
        self.providers = []

        full = _load_full_config().get("TTS", {}) or {}
        from core.utils.tts import create_instance

        for name in self.chain:
            block = full.get(name)
            if not isinstance(block, dict):
                logger.bind(tag=TAG).warning(f"TTS 链里的 '{name}' 没有配置块，跳过")
                continue
            try:
                prov = create_instance(block.get("type"), block, True)
                self.audio_file_type = prov.audio_file_type
                self.providers.append((name, prov))
            except Exception as e:
                logger.bind(tag=TAG).warning(f"TTS '{name}' 初始化失败，跳过: {e}")

        if not self.providers:
            raise ValueError(f"TTS 链 {self.chain} 里没有任何可用 provider")
        logger.bind(tag=TAG).info(
            f"TTS 链就绪: {' -> '.join(n for n, _ in self.providers)}"
        )

        # 熔断：某个 provider 连续失败 threshold 次后冷却 cooldown 秒不再尝试。
        # 目的是别让「在线被墙」每句都白等一次 403；冷却到期会自动再试，
        # 所以换了网络/换电脑后无需改配置即可自动用回在线音色。
        # 状态放在模块级 _BREAKER（跨连接共享），否则每段新对话都要重新白试。
        self.fail_threshold = int(config.get("failure_streak_threshold", 2) or 2)
        self.cooldown = float(config.get("cooldown_seconds", 600) or 600)
        # 单个 provider 的硬超时：上游 EdgeTTS 的网络调用没有超时，网络一变就可能
        # 「连上了但永不返回」，于是整条链卡在第一个 provider 上：既不出声也不报错
        # （现场：探针等 41s、0 音频帧、日志里连一句失败都没有）。超时即视为失败。
        self.sub_timeout = float(config.get("sub_provider_timeout", 15) or 15)

    def _state(self, name: str) -> dict:
        with _BREAKER_LOCK:
            st = _BREAKER.get(name)
            if st is None:
                st = {"streak": 0, "skip_until": 0.0}
                _BREAKER[name] = st
            return st

    def _available(self, name: str) -> bool:
        return time.time() >= self._state(name)["skip_until"]

    def _mark_fail(self, name: str):
        with _BREAKER_LOCK:
            s = self._state(name)
            s["streak"] += 1
            if self.fail_threshold > 0 and s["streak"] >= self.fail_threshold:
                s["skip_until"] = time.time() + self.cooldown
                logger.bind(tag=TAG).warning(
                    f"TTS '{name}' 连续失败 {s['streak']} 次，冷却 {self.cooldown:.0f}s 后再试"
                )

    def _mark_ok(self, name: str):
        with _BREAKER_LOCK:
            s = self._state(name)
            if s["streak"]:
                logger.bind(tag=TAG).info(f"TTS '{name}' 恢复正常")
            s["streak"] = 0
            s["skip_until"] = 0.0

    def generate_filename(self, extension=".wav"):
        return os.path.join(
            self.output_file,
            f"tts-{datetime.now().date()}@{uuid.uuid4().hex}{extension}",
        )

    def _call_with_hard_timeout(self, prov, text: str, timeout: float):
        """在独立线程 + 独立事件循环里调用某个 provider，并用 join 做硬超时。

        为什么不能用 asyncio.wait_for：上游 EdgeTTS 内部存在同步阻塞调用，一旦卡住
        会把事件循环整个堵死，wait_for 的超时根本没有机会触发（现场：日志停在
        「尝试 EdgeTTS ...」之后，既不出声也没有任何失败/超时记录）。
        放进独立线程后，卡住的那个线程被放弃，主流程照常超时切换到下一个 provider。
        """
        box = {}

        def run():
            try:
                box["data"] = asyncio.run(prov.text_to_speak(text, None))
            except Exception as e:  # noqa: BLE001
                box["err"] = e

        t = threading.Thread(target=run, daemon=True, name="tts-sub")
        t.start()
        t.join(timeout)
        if t.is_alive():
            raise TimeoutError(f"{timeout:.0f}s 无响应（已放弃该线程）")
        if "err" in box:
            raise box["err"]
        return box.get("data")

    async def text_to_speak(self, text, output_file):
        last_err = None
        tried = 0
        for name, prov in self.providers:
            if not self._available(name):
                continue
            tried += 1
            try:
                # 直接同步调用：基类是在自己的线程里 asyncio.run(text_to_speak) 的，
                # 这里再用 await asyncio.to_thread 反而会卡在事件循环/线程池调度上
                # （实测：链入口有日志，但 8s 超时再也不触发）。
                data = self._call_with_hard_timeout(prov, text, self.sub_timeout)
                if not data:
                    raise Exception("返回空音频")
                # 基类随后会用 self.audio_file_type 解码，必须跟着命中的 provider 走
                self.audio_file_type = prov.audio_file_type
                self._mark_ok(name)
                if output_file:
                    os.makedirs(os.path.dirname(output_file) or ".", exist_ok=True)
                    with open(output_file, "wb") as f:
                        f.write(data)
                    return None
                return data
            except Exception as e:
                last_err = e
                self._mark_fail(name)
                logger.bind(tag=TAG).warning(f"TTS '{name}' 失败，尝试下一个: {e}")
                continue
        if tried == 0:
            # 全在冷却中：临时忽略冷却，保证一定有声音
            skipped = [n for n, _ in self.providers if not self._available(n)]
            logger.bind(tag=TAG).info(f"所有 provider 均在冷却中（{skipped}），本次强制重试")
            for name, prov in self.providers:
                try:
                    data = self._call_with_hard_timeout(prov, text, self.sub_timeout)
                    if data:
                        self.audio_file_type = prov.audio_file_type
                        self._mark_ok(name)
                        if output_file:
                            os.makedirs(os.path.dirname(output_file) or ".", exist_ok=True)
                            with open(output_file, "wb") as f:
                                f.write(data)
                            return None
                        return data
                except Exception as e:
                    last_err = e
                    continue
        raise Exception(f"TTS 链全部失败，最后错误: {last_err}")
