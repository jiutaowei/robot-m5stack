"""决策期垫话：大模型迟迟不吐字时，先垫一句，别让用户干等。

背景（2026-10-08 实测）
----------------------
米宝的链路是「说完 → ASR → 大模型决定要不要调工具 → 应答/播报」。其中
**ASR 出文本到"第一个输出"之间有 1~22 秒的空档**（全量日志 58 个样本：中位 5 秒，
尾部 10/13/18/20/22 秒），这段时间用户听不到任何声音。数据类问题后面还有插件的
ack 兜着，但 ack 最早也要等"决定调工具"之后才出现。

做法
----
在 `ConnectionHandler.chat()` 真正开始拉 LLM 流之前起一个一次性计时器；
LLM **第一个响应到达**（`for response in llm_responses` 的第一次迭代）或
任何一方开始说话（`send_tts_message`）时取消它。到点未被取消才垫一句。

三条硬约束（沿用插件里验证过的做法）
------------------------------------
1. **不写 conn.dialogue**：此刻 assistant(tool_calls) 还没回填 tool 结果，
   插入 assistant 消息会破坏 tool_calls → tool(result) 配对，框架会用
   {"status":"interrupted"} 顶替真实结果（插件侧踩过这个坑）。
2. 必须先发 `{"type":"tts","state":"start"}` + `sentence_start`，设备才会真正发声。
3. 用户正在说话（client_have_voice）或被中断（client_abort）时不插嘴。

配置（data/.config.yaml，顶层键）
-------------------------------
    latency_interim:
      enabled: true
      seconds: 5.0          # 实测中位 5s：设太小会把正常闲聊也垫一句话
      texts: ["稍等，我看一下。", "嗯，我查一下。"]
"""

import asyncio
import random
import time
from typing import Any, Dict, Optional

from config.logger import setup_logging

TAG = __name__
logger = setup_logging()

DEFAULT_SECONDS = 5.0
DEFAULT_TEXTS = ["稍等，我看一下。", "嗯，我查一下。"]
_FUT_ATTR = "_latency_interim_future"


def _cfg(conn) -> Dict[str, Any]:
    try:
        cfg = (getattr(conn, "config", None) or {}).get("latency_interim") or {}
        return cfg if isinstance(cfg, dict) else {}
    except Exception:
        return {}


def _opts(conn) -> Optional[tuple]:
    """返回 (seconds, texts)；关闭时返回 None。"""
    cfg = _cfg(conn)
    if not cfg.get("enabled", True):
        return None
    try:
        seconds = float(cfg.get("seconds", DEFAULT_SECONDS))
    except (TypeError, ValueError):
        seconds = DEFAULT_SECONDS
    if seconds <= 0:
        return None
    raw = cfg.get("texts")
    texts = [str(t).strip() for t in raw if str(t).strip()] if isinstance(raw, (list, tuple)) else []
    return seconds, (texts or DEFAULT_TEXTS)


def _submit(conn, coro):
    """把协程放到 conn 的事件循环上；已经在循环里就直接 create_task。"""
    try:
        running = asyncio.get_running_loop()
    except RuntimeError:
        running = None
    if running is not None:
        return running.create_task(coro)
    loop = getattr(conn, "loop", None)
    if loop is None or loop.is_closed():
        coro.close()
        return None
    return asyncio.run_coroutine_threadsafe(coro, loop)


def cancel(conn) -> None:
    """撤销本次垫话（幂等；LLM 一出字、或任何一方开始说话就调它）。"""
    fut = getattr(conn, _FUT_ATTR, None)
    if fut is None:
        return
    try:
        setattr(conn, _FUT_ATTR, None)
    except Exception:
        pass
    try:
        if not fut.done():
            fut.cancel()
            logger.bind(tag=TAG).debug("决策期垫话已取消（LLM 已开始输出或已在说话）")
    except Exception:
        pass


def start(conn, depth: int = 0, user_turn: bool = True) -> None:
    """开始计时。

    depth>0（工具调用后的递归轮）或非用户轮次（query=None 的内部轮）不垫话，
    避免在用户根本没提问时冒出一句"稍等"。
    """
    cancel(conn)                      # 新一轮先清掉上一轮遗留的
    if depth != 0 or not user_turn:
        return
    opts = _opts(conn)
    if not opts:
        return
    seconds, texts = opts
    try:
        fut = _submit(conn, _wait_and_speak(conn, seconds, texts))
        if fut is not None:
            setattr(conn, _FUT_ATTR, fut)
    except Exception as exc:
        logger.bind(tag=TAG).warning(f"决策期垫话启动失败：{exc}")


async def _wait_and_speak(conn, seconds: float, texts) -> None:
    started = time.time()
    try:
        await asyncio.sleep(seconds)
        if getattr(conn, "client_abort", False):
            return
        if getattr(conn, "client_have_voice", False):
            return                    # 用户正在说话，别插嘴
        text = random.choice(texts)
        await _speak(conn, text)
        logger.bind(tag=TAG).info(
            f"决策期垫话：LLM {time.time() - started:.1f}s 未出字，已垫「{text}」"
        )
    except asyncio.CancelledError:
        raise
    except Exception as exc:
        logger.bind(tag=TAG).warning(f"决策期垫话失败：{exc}")


def _enqueue_tts(conn, text: str) -> None:
    """直接入队 TTS，且**不写对话历史**（见模块 docstring 约束 1）。"""
    from core.providers.tts.dto.dto import ContentType, SentenceType, TTSMessageDTO

    conn.tts.store_tts_text(conn.sentence_id, text)
    conn.tts.tts_text_queue.put(
        TTSMessageDTO(sentence_id=conn.sentence_id, sentence_type=SentenceType.FIRST,
                      content_type=ContentType.ACTION)
    )
    conn.tts.tts_one_sentence(conn, ContentType.TEXT, content_detail=text)
    conn.tts.tts_text_queue.put(
        TTSMessageDTO(sentence_id=conn.sentence_id, sentence_type=SentenceType.LAST,
                      content_type=ContentType.ACTION)
    )


async def _speak(conn, text: str) -> None:
    # 先摘掉句柄：下面 send_tts_message 会调 cancel()，否则它会把"正在执行这句话的
    # 自己"取消掉（cancel() 在下一个 await 点生效，音频可能入不了队）。
    try:
        setattr(conn, _FUT_ATTR, None)
    except Exception:
        pass
    try:
        from core.handle.sendAudioHandle import send_tts_message
        await send_tts_message(conn, "start")
        await send_tts_message(conn, "sentence_start", text)
        conn.client_is_speaking = True
    except Exception as exc:
        logger.bind(tag=TAG).warning(f"垫话前发送 tts start 失败：{exc}")
    await asyncio.to_thread(_enqueue_tts, conn, text)
