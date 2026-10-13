#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Kokoro TTS 常驻 worker（跑在独立 venv 里，模型只加载一次）。

为什么要单独进程：kokoro-onnx 依赖 librosa/numba/scipy，体积大且会和主服务端
的 numpy/onnxruntime 打架，所以隔离在自己的 venv（见 .tools/kokoro-venv），
服务端通过本 worker 的 stdin/stdout 请求合成。

协议（二进制 stdin/stdout，按行）::

    请求   <voice>\t<speed>\t<base64(text)>\n
    成功   "OK <nbytes>\n" + nbytes 字节 WAV(16bit/24kHz/单声道)
    失败   "ERR <message>\n"
    退出   "QUIT\n"

用法::

    python kokoro_worker.py <model.onnx> <voices.json> [threads]
"""

import base64
import io
import os
import sys
import traceback
import wave

DEFAULT_THREADS = 4

# 中文 G2P：必须用 misaki 的 ZHG2P（Kokoro 训练时就是这套音素）。
# kokoro-onnx 内置对 lang="cmn" 走 espeak 音素，音素集不匹配 → 听起来不像普通话。
#   实测 misaki: 'ni↓xau↓, wo↓ ʂɨ↘ mi↓ pau↓ i→xau↘.'
#   espeak     : 'niχˈɑu, wˈo s.ˈi. mˈi pˈɑu jiɜχˈɑu.'
_G2P = None


def zh_phonemes(text: str) -> str:
    global _G2P
    if _G2P is None:
        from misaki import zh

        _G2P = zh.ZHG2P()
    phonemes, _ = _G2P(text)
    return phonemes


class _SessShim:
    """onnx-community 导出的模型输入名是 input_ids，kokoro-onnx 按官方模型喂 tokens。"""

    def __init__(self, sess):
        self._sess = sess

    def run(self, output_names, feed):
        if "tokens" in feed and "input_ids" not in feed:
            feed["input_ids"] = feed.pop("tokens")
        return self._sess.run(output_names, feed)

    def __getattr__(self, name):
        return getattr(self._sess, name)


def _log(msg):
    print(msg, file=sys.stderr, flush=True)


def build_kokoro(model_path, voices_path, threads):
    import numpy as np  # noqa: F401
    import onnxruntime as ort
    from kokoro_onnx import Kokoro

    so = ort.SessionOptions()
    so.intra_op_num_threads = threads
    so.inter_op_num_threads = 1
    # 实测：ORT_ENABLE_ALL 建会话要 52.7s（82M 模型全图优化太慢），关掉图优化后
    # 0.8s，推理速度基本不变（短句 ~1.0-1.3s、长句 ~3.7s）。启动快更重要。
    so.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
    # 关掉 ORT 的内存池/内存复用：默认会预留大块 arena，本机实测 worker 峰值 1.15GB，
    # 关掉后显著下降（这台机器内存本来就紧张，出现「卡死」要先降占用）。
    so.enable_cpu_mem_arena = False
    so.enable_mem_pattern = False
    sess = ort.InferenceSession(
        model_path, sess_options=so, providers=["CPUExecutionProvider"]
    )

    k = Kokoro(model_path, voices_path)
    k.sess = _SessShim(sess)
    return k


def _watch_parent():
    """父进程（服务端）退出后自动结束，避免留下孤儿 worker（之前实测有残留）。"""
    import time

    parent = os.getppid()
    while True:
        time.sleep(5)
        try:
            if os.getppid() != parent:
                _log("父进程已退出，worker 自行退出")
                os._exit(0)
        except Exception:
            os._exit(0)


def main():
    if len(sys.argv) < 3:
        _log("用法: kokoro_worker.py <model.onnx> <voices.json> [threads]")
        return 2

    model_path, voices_path = sys.argv[1], sys.argv[2]
    threads = int(sys.argv[3]) if len(sys.argv) > 3 else DEFAULT_THREADS
    os.environ.setdefault("OMP_NUM_THREADS", str(threads))

    import numpy as np

    try:
        k = build_kokoro(model_path, voices_path, threads)
    except Exception as e:
        _log(f"模型加载失败: {type(e).__name__}: {e}")
        traceback.print_exc(file=sys.stderr)
        return 3

    # 预热一次，避免第一句特别慢
    try:
        k.create("预热", voice=os.environ.get("KOKORO_WARMUP_VOICE", "zf_xiaoxiao"),
                 speed=1.0, lang="cmn")
    except Exception as e:
        _log(f"预热失败（忽略）: {e}")

    out = sys.stdout.buffer
    out.write(b"READY\n")
    out.flush()
    _log(f"worker 就绪，线程={threads}")

    import threading

    threading.Thread(target=_watch_parent, daemon=True).start()

    for raw in sys.stdin.buffer:
        line = raw.decode("utf-8", "replace").strip()
        if not line:
            continue
        if line == "QUIT":
            break
        try:
            parts = line.split("\t")
            if len(parts) < 3:
                raise ValueError("请求格式错误")
            voice, speed_s, b64 = parts[0], parts[1], parts[2]
            text = base64.b64decode(b64).decode("utf-8")
            speed = float(speed_s) if speed_s else 1.0
            phonemes = zh_phonemes(text)  # 用 misaki，别用内置 espeak
            audio, sr = k.create(text, voice=voice, speed=speed, lang="cmn",
                                 phonemes=phonemes)
            pcm = np.clip(np.asarray(audio).reshape(-1), -1.0, 1.0)
            buf = io.BytesIO()
            with wave.open(buf, "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(int(sr))
                w.writeframes((pcm * 32767).astype(np.int16).tobytes())
            data = buf.getvalue()
            out.write(f"OK {len(data)}\n".encode("ascii"))
            out.write(data)
            out.flush()
        except Exception as e:
            _log(f"合成失败: {type(e).__name__}: {e}")
            try:
                out.write(f"ERR {type(e).__name__}: {e}\n".encode("utf-8"))
                out.flush()
            except Exception:
                break
    return 0


if __name__ == "__main__":
    sys.exit(main())
