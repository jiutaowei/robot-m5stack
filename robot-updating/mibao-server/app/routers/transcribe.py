"""转写 / 纪要模块：百炼（DashScope）paraformer 语音转写 + qwen 纪要总结。

安全约束：API key 仅来自环境变量 MIBAO_DASHSCOPE_KEY（.env 注入，已被 .gitignore 排除），
严禁入库 / 入 git / 出现在日志或报告中；对外展示一律使用 config.mask_key() 掩码。

实现要点：
- WAV（16k 单声道 16bit）用标准库 wave 去头取裸 PCM，交由 dashscope
  paraformer-realtime-v2（HTTP，支持本地文件）转写，结果写 transcripts 表。
- 转写文本交由 qwen-plus（OpenAI 兼容端点 /chat/completions）按 type 生成
  会议纪要 / 灵感整理，写 summaries 表。
"""
from __future__ import annotations

import json
import logging
import tempfile
import threading
import wave
from pathlib import Path

import httpx
from fastapi import APIRouter, BackgroundTasks, HTTPException

from .. import config, database

logger = logging.getLogger("mibao.transcribe")
router = APIRouter(prefix="/api/recording", tags=["transcribe"])


# --------------------------------------------------------------------------- #
# 工具
# --------------------------------------------------------------------------- #
def _require_key() -> None:
    if not config.DASHSCOPE_KEY:
        raise HTTPException(
            status_code=500,
            detail="未配置 MIBAO_DASHSCOPE_KEY（百炼 API Key），无法调用转写/总结",
        )


def _resolve_wav(rec_id: int) -> tuple[dict, Path]:
    record = database.get_recording(rec_id)
    if not record:
        raise HTTPException(status_code=404, detail="录音不存在")
    path = config.RECORDINGS_DIR / record["rel_path"]
    if not path.is_file():
        raise HTTPException(status_code=404, detail="录音文件已丢失")
    return record, path


def _wav_to_pcm(path: Path) -> tuple[bytes, int]:
    """WAV 去头取裸 PCM，返回 (pcm_bytes, sample_rate)。"""
    with wave.open(str(path), "rb") as wf:
        pcm = wf.readframes(wf.getnframes())
        rate = wf.getframerate()
    if not pcm:
        raise ValueError("WAV 无音频数据")
    return pcm, rate


# --------------------------------------------------------------------------- #
# 转写（后台线程执行 dashscope，避免阻塞事件循环）
# --------------------------------------------------------------------------- #
def _run_asr(rec_id: int, wav_path: Path) -> None:
    """实际执行转写的后台任务（线程池中运行）。"""
    pcm_path = None
    try:
        pcm, rate = _wav_to_pcm(wav_path)
        # 裸 PCM 写入临时文件交给 dashscope（format='pcm'）
        with tempfile.NamedTemporaryFile(suffix=".pcm", delete=False) as tmp:
            tmp.write(pcm)
            pcm_path = tmp.name

        import dashscope
        from dashscope.audio.asr import Recognition

        dashscope.api_key = config.DASHSCOPE_KEY
        dashscope.base_http_api_url = config.DASHSCOPE_BASE_URL

        recognition = Recognition(
            model=config.ASR_MODEL,
            format="pcm",
            sample_rate=rate,
            callback=None,
        )
        result = recognition.call(pcm_path)

        status = getattr(result, "status_code", None)
        if status != 200:
            raise RuntimeError(
                f"百炼转写失败 status={status} message={getattr(result, 'message', '')}"
            )

        sentences = result.get_sentence() or []
        if isinstance(sentences, dict):
            sentences = [sentences]
        text = "".join(s.get("text", "") for s in sentences).strip()
        speaker_ids = sorted(
            {s.get("speaker_id") for s in sentences if s.get("speaker_id") is not None}
        )
        speakers_json = json.dumps(speaker_ids) if speaker_ids else None

        database.upsert_transcript(
            rec_id, "done", text=text, speakers=speakers_json, error=None
        )
        logger.info("transcribe done rec_id=%s chars=%d", rec_id, len(text))
    except Exception as exc:  # noqa: BLE001 - 后台任务需兜底并记录状态
        logger.exception("transcribe failed rec_id=%s", rec_id)
        database.upsert_transcript(rec_id, "error", error=str(exc))
    finally:
        if pcm_path:
            try:
                Path(pcm_path).unlink(missing_ok=True)
            except OSError:
                pass


@router.post("/{rec_id}/transcribe")
def start_transcribe(rec_id: int, background_tasks: BackgroundTasks):
    """发起异步转写任务。立即返回 processing，前端轮询 transcript 接口查状态。"""
    _require_key()
    _, wav_path = _resolve_wav(rec_id)

    existing = database.get_transcript(rec_id)
    if existing and existing["status"] == "processing":
        return {"success": True, "status": "processing", "message": "转写进行中，请稍候"}

    database.upsert_transcript(rec_id, "processing", text="", error=None)
    # dashscope Recognition 为阻塞调用，放入独立线程执行
    threading.Thread(target=_run_asr, args=(rec_id, wav_path), daemon=True).start()
    return {
        "success": True,
        "status": "processing",
        "message": "已发起转写（paraformer）",
        "model": config.ASR_MODEL,
    }


# --------------------------------------------------------------------------- #
# 纪要 / 灵感总结（qwen-plus，OpenAI 兼容端点）
# --------------------------------------------------------------------------- #
_MEETING_SYSTEM = (
    "你是一名资深会议秘书，擅长把会议录音转写整理成结构化会议纪要。"
    "请严格基于用户给出的转写文本整理，不要编造转写中没有的内容；"
    "若某项信息无法从文本中识别，请标注“未提及”。输出使用 Markdown。"
)
_MEETING_USER = (
    "以下是一场会议的语音转写文本：\n\n{text}\n\n"
    "请生成会议纪要，按以下结构输出：\n"
    "## 会议议题\n## 关键结论\n## 待办事项（尽量标注责任人）\n## 下一步行动"
)
_PERSONAL_SYSTEM = (
    "你是一名个人灵感整理助手，帮助把零散的口述想法整理成清晰、可执行的要点。"
    "请严格基于转写文本整理，不要编造没有的内容。输出使用 Markdown。"
)
_PERSONAL_USER = (
    "以下是一段个人灵感的语音转写文本：\n\n{text}\n\n"
    "请整理为：\n## 核心要点\n## 可行动项"
)


async def _qwen_chat(system_prompt: str, user_prompt: str) -> str:
    """调用 qwen（OpenAI 兼容端点）返回文本内容。"""
    url = config.DASHSCOPE_OPENAI_BASE_URL + "/chat/completions"
    headers = {
        "Authorization": f"Bearer {config.DASHSCOPE_KEY}",
        "Content-Type": "application/json",
    }
    payload = {
        "model": config.QWEN_MODEL,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_prompt},
        ],
        "temperature": 0.3,
    }
    try:
        async with httpx.AsyncClient(timeout=config.LLM_TIMEOUT) as client:
            resp = await client.post(url, json=payload, headers=headers)
    except httpx.HTTPError as exc:
        raise HTTPException(status_code=502, detail=f"qwen 请求异常: {exc.__class__.__name__}")

    if resp.status_code != 200:
        raise HTTPException(
            status_code=502,
            detail=f"qwen 调用失败 {resp.status_code}: {resp.text[:200]}",
        )
    try:
        data = resp.json()
        return data["choices"][0]["message"]["content"].strip()
    except (ValueError, KeyError, IndexError):
        raise HTTPException(status_code=502, detail="qwen 响应解析失败")


@router.post("/{rec_id}/summarize")
async def summarize(rec_id: int):
    """基于转写文本生成纪要（meeting）/ 灵感整理（personal），并落库。"""
    _require_key()
    record, _ = _resolve_wav(rec_id)

    transcript = database.get_transcript(rec_id)
    if not transcript or transcript["status"] != "done" or not transcript["text"].strip():
        raise HTTPException(status_code=400, detail="请先完成转写，再执行总结")

    rec_type = record["type"]
    if rec_type == "meeting":
        system, user = _MEETING_SYSTEM, _MEETING_USER
    else:
        system, user = _PERSONAL_SYSTEM, _PERSONAL_USER

    content = await _qwen_chat(system, user.format(text=transcript["text"]))
    database.upsert_summary(rec_id, rec_type, content, config.QWEN_MODEL)
    logger.info("summarize done rec_id=%s type=%s", rec_id, rec_type)
    return {
        "success": True,
        "recording_id": rec_id,
        "type": rec_type,
        "model": config.QWEN_MODEL,
        "content": content,
    }


# --------------------------------------------------------------------------- #
# 查询：转写 + 总结
# --------------------------------------------------------------------------- #
@router.get("/{rec_id}/transcript")
def get_transcript(rec_id: int):
    record = database.get_recording(rec_id)
    if not record:
        raise HTTPException(status_code=404, detail="录音不存在")

    transcript = database.get_transcript(rec_id)
    summary = database.get_summary(rec_id)

    transcript_view = None
    if transcript:
        speakers = None
        if transcript.get("speakers"):
            try:
                speakers = json.loads(transcript["speakers"])
            except ValueError:
                speakers = None
        transcript_view = {
            "status": transcript["status"],
            "text": transcript["text"],
            "speakers": speakers,
            "error": transcript["error"],
            "created_at": transcript["created_at"],
            "updated_at": transcript["updated_at"],
        }

    summary_view = None
    if summary:
        summary_view = {
            "type": summary["rec_type"],
            "content": summary["content"],
            "model": summary["model"],
            "created_at": summary["created_at"],
            "updated_at": summary["updated_at"],
        }

    return {
        "recording_id": rec_id,
        "filename": record["filename"],
        "type": record["type"],
        "transcript": transcript_view,
        "summary": summary_view,
    }
