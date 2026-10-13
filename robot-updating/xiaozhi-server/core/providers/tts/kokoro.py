"""Kokoro 本地神经 TTS provider（离线中文，比 Piper/SAPI 自然）。

架构
----
kokoro-onnx 依赖 librosa/numba/scipy，体积大（~575MB）且与主服务端的
numpy/onnxruntime 版本有冲突，因此它跑在**独立 venv** 里，由
``scripts/kokoro_worker.py`` 常驻进程提供合成（模型只加载一次，实测启动 0.8s、
短句 ~1s、长句 ~3.7s）。本 provider 负责与 worker 的行协议通信。

配置示例（data/.config.yaml）::

    selected_module:
      TTS: KokoroTTS
    TTS:
      KokoroTTS:
        type: kokoro
        python: D:\\work\\code\\robot-m5stack\\.tools\\kokoro-venv\\Scripts\\python.exe
        worker: scripts/kokoro_worker.py       # 相对服务端根目录
        model: models/kokoro/kokoro-v1.0.onnx
        voices: models/kokoro/voices.json
        voice: zf_xiaoxiao    # 可选 zf_xiaobei / zm_yunxi
        speed: 1.0
        threads: 4            # 4 线程实测最快（8 线程超线程争用反而慢）
        output_dir: tmp/

未配置 python 时按顺序找：KOKORO_PYTHON 环境变量、服务端目录下 .kokoro-venv、
仓库 .tools/kokoro-venv。
"""

import asyncio
import base64
import os
import subprocess
import threading
import time
import uuid
from concurrent.futures import ThreadPoolExecutor, TimeoutError as FutureTimeout
from datetime import datetime

from config.logger import setup_logging
from core.providers.tts.base import TTSProviderBase

TAG = __name__
logger = setup_logging()

# 同一份 worker 配置全局共享（TTS provider 是每条连接各建一个，但 worker 只起一个）
_WORKER = None
_WORKER_KEY = None
_WORKER_LOCK = threading.RLock()  # 保护 worker 的创建/重启
_REQ_LOCK = threading.Lock()      # 串行化对 worker 的请求（行协议不支持并发）
# 请求放进线程池执行，超时才能「取消等待」——否则 worker 卡住会把整条 TTS 调用挂死
_EXECUTOR = ThreadPoolExecutor(max_workers=1, thread_name_prefix="kokoro")


def _server_root() -> str:
    # core/providers/tts/kokoro.py -> 上溯三级
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def _find_python(configured: str) -> str:
    if configured:
        return os.path.abspath(configured)
    env = os.environ.get("KOKORO_PYTHON")
    if env and os.path.exists(env):
        return env
    candidates = [
        os.path.join(_server_root(), ".kokoro-venv", "Scripts", "python.exe"),
        os.path.join(_server_root(), "..", "..", ".tools", "kokoro-venv", "Scripts", "python.exe"),
    ]
    for c in candidates:
        c = os.path.abspath(c)
        if os.path.exists(c):
            return c
    return candidates[0]


class _KokoroWorker:
    """常驻 worker 进程：模型加载一次，按行收发。"""

    def __init__(self, python, script, model, voices, threads, log_path):
        self.python = python
        self.script = script
        self.model = model
        self.voices = voices
        self.threads = threads
        self.log_path = log_path
        self.proc = None
        self.log_file = None

    def start(self, timeout=180):
        os.makedirs(os.path.dirname(self.log_path) or ".", exist_ok=True)
        self.log_file = open(self.log_path, "ab", buffering=0)
        env = dict(os.environ)
        env["OMP_NUM_THREADS"] = str(self.threads)
        env["PYTHONIOENCODING"] = "utf-8"
        env["PYTHONUTF8"] = "1"
        self.proc = subprocess.Popen(
            [self.python, self.script, self.model, self.voices, str(self.threads)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=self.log_file,
            bufsize=0,
            env=env,
        )
        line = self.proc.stdout.readline().decode("utf-8", "replace").strip()
        if line != "READY":
            raise Exception(f"Kokoro worker 启动失败（收到 {line!r}），见 {self.log_path}")

    def alive(self):
        return self.proc is not None and self.proc.poll() is None

    def request(self, text: str, voice: str, speed: float, timeout: float) -> bytes:
        payload = "".join([voice, "\t", str(speed), "\t",
                           base64.b64encode(text.encode("utf-8")).decode("ascii"), "\n"])
        self.proc.stdin.write(payload.encode("ascii"))
        self.proc.stdin.flush()
        header = self.proc.stdout.readline().decode("utf-8", "replace").strip()
        if header.startswith("OK "):
            n = int(header.split()[1])
            data = b""
            while len(data) < n:
                chunk = self.proc.stdout.read(n - len(data))
                if not chunk:
                    raise Exception("Kokoro worker 提前结束")
                data += chunk
            return data
        raise Exception(f"Kokoro 合成失败: {header or '空响应'}")

    def stop(self):
        try:
            if self.proc and self.proc.poll() is None:
                self.proc.stdin.write(b"QUIT\n")
                self.proc.stdin.flush()
                self.proc.wait(timeout=5)
        except Exception:
            try:
                self.proc.kill()
            except Exception:
                pass
        finally:
            try:
                if self.log_file:
                    self.log_file.close()
            except Exception:
                pass


def _get_worker(python, script, model, voices, threads, log_path):
    global _WORKER, _WORKER_KEY
    key = (python, script, model, voices, threads)
    with _WORKER_LOCK:
        if _WORKER is not None and _WORKER_KEY == key and _WORKER.alive():
            return _WORKER
        if _WORKER is not None:
            _WORKER.stop()
        w = _KokoroWorker(python, script, model, voices, threads, log_path)
        t0 = time.time()
        w.start()
        logger.bind(tag=TAG).info(f"Kokoro worker 已启动（{time.time()-t0:.1f}s）: {script}")
        _WORKER, _WORKER_KEY = w, key
        return w


class TTSProvider(TTSProviderBase):
    TTS_PARAM_CONFIG = [
        ("ttsVolume", "gain", -20.0, 20.0, 0.0, float),
        ("ttsRate", "speed", 0.5, 2.0, 1.0, lambda v: round(float(v), 2)),
    ]

    def __init__(self, config, delete_audio_file):
        super().__init__(config, delete_audio_file)
        self.audio_file_type = "wav"

        root = _server_root()
        self.python = _find_python(config.get("python", ""))
        self.script = os.path.abspath(
            config.get("worker") or os.path.join(root, "scripts", "kokoro_worker.py")
        )
        self.model = os.path.abspath(
            config.get("model") or os.path.join(root, "models", "kokoro", "kokoro-v1.0.onnx")
        )
        self.voices = os.path.abspath(
            config.get("voices") or os.path.join(root, "models", "kokoro", "voices.json")
        )
        self.voice = config.get("private_voice") or config.get("voice") or "zf_xiaoxiao"
        self.speed = float(config.get("speed", 1.0) or 1.0)
        self.threads = int(config.get("threads", 4) or 4)
        self.log_path = os.path.join(self.output_file, "kokoro_worker.log")
        self._apply_percentage_params(config)

        for path, what in ((self.python, "python"), (self.script, "worker 脚本"),
                           (self.model, "模型"), (self.voices, "音色文件")):
            if not os.path.exists(path):
                raise ValueError(f"Kokoro {what} 不存在: {path}")

        # 后台预热 worker：首次启动要 ~12s（import librosa/numba + 建会话），
        # 第一条连接建立时就开始加载，等用户真正开口时通常已就绪。
        threading.Thread(target=self._prewarm, daemon=True).start()

    def _prewarm(self):
        try:
            _get_worker(self.python, self.script, self.model, self.voices,
                        self.threads, self.log_path)
        except Exception as e:
            logger.bind(tag=TAG).warning(f"Kokoro worker 预热失败（首次请求会重试）: {e}")

    def generate_filename(self, extension=".wav"):
        return os.path.join(
            self.output_file,
            f"tts-{datetime.now().date()}@{uuid.uuid4().hex}{extension}",
        )

    def _synthesize_blocking(self, text: str) -> bytes:
        global _WORKER
        timeout = max(5.0, float(self.tts_timeout) * 2)  # 含首句可能的排队
        worker = _get_worker(self.python, self.script, self.model, self.voices,
                             self.threads, self.log_path)
        with _REQ_LOCK:
            for attempt in (1, 2):
                try:
                    fut = _EXECUTOR.submit(worker.request, text, self.voice,
                                           self.speed, timeout)
                    return fut.result(timeout=timeout)
                except FutureTimeout:
                    # worker 卡住：杀掉它（阻塞的读会随之结束），重启后重试一次
                    logger.bind(tag=TAG).warning(
                        f"Kokoro worker 超过 {timeout:.0f}s 无响应，杀掉重启（第 {attempt} 次）"
                    )
                    with _WORKER_LOCK:
                        worker.stop()
                        _WORKER = None
                    worker = _get_worker(self.python, self.script, self.model,
                                         self.voices, self.threads, self.log_path)
                except Exception as e:
                    logger.bind(tag=TAG).warning(f"Kokoro worker 请求失败，重启后重试: {e}")
                    with _WORKER_LOCK:
                        worker.stop()
                        _WORKER = None
                    worker = _get_worker(self.python, self.script, self.model,
                                         self.voices, self.threads, self.log_path)
            raise Exception("Kokoro worker 连续两次无响应")

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
            raise Exception(f"Kokoro 请求失败: {e}")
