"""录音模块：上传 / 列表 / 下载 / 删除。

录音文件规范（设备端已固化）：
- 16kHz 单声道 16bit WAV
- 文件名 meeting_YYYYMMDD_HHMMSS.wav（会议）/ personal_YYYYMMDD_HHMMSS.wav（个人灵感）
- type 未显式提供时按文件名前缀解析兜底
"""
from __future__ import annotations

import re
import unicodedata
import wave
from datetime import datetime
from pathlib import Path

from fastapi import APIRouter, File, Form, HTTPException, Query, UploadFile
from fastapi.responses import FileResponse

from .. import config, database

router = APIRouter(prefix="/api/recording", tags=["recording"])

_NAME_RE = re.compile(r"^(meeting|personal)_\d{8}_\d{6}\.wav$", re.IGNORECASE)
_MAX_UPLOAD_BYTES = 500 * 1024 * 1024  # 500MB 上限保护


def _parse_type_from_name(filename: str) -> str | None:
    """按文件名前缀解析 type；规范格式强校验，非规范名仅取前缀兜底。"""
    lower = filename.lower()
    if _NAME_RE.match(lower):
        return lower.split("_", 1)[0]
    if lower.startswith("meeting_"):
        return "meeting"
    if lower.startswith("personal_"):
        return "personal"
    return None


def _wav_duration(path: Path) -> float:
    """解析 WAV 头计算时长（秒）；非标准 WAV 返回 0。"""
    try:
        with wave.open(str(path), "rb") as wf:
            frames, rate = wf.getnframes(), wf.getframerate()
            return round(frames / rate, 3) if rate else 0.0
    except (wave.Error, EOFError, OSError):
        return 0.0


def _safe_filename(filename: str) -> str:
    """清洗文件名，防止路径穿越。"""
    name = Path(unicodedata.normalize("NFC", filename or "")).name
    return name or "recording.wav"


@router.post("/upload")
async def upload_recording(
    file: UploadFile = File(...),
    type: str | None = Form(None),
    device_id: str = Form("mibao-01"),
):
    filename = _safe_filename(file.filename)
    if not filename.lower().endswith(".wav"):
        raise HTTPException(status_code=400, detail="仅支持 .wav 文件")

    rec_type = (type or "").strip().lower() or _parse_type_from_name(filename)
    if rec_type not in config.RECORDING_TYPES:
        raise HTTPException(
            status_code=400,
            detail=f"type 必须为 {config.RECORDING_TYPES} 之一，"
            "或文件名符合 meeting_/personal_ 前缀规范",
        )

    # 落盘：data/recordings/YYYY-MM-DD/<原文件名>（同名加序号避免覆盖）
    day_dir = config.RECORDINGS_DIR / datetime.now().strftime("%Y-%m-%d")
    day_dir.mkdir(parents=True, exist_ok=True)
    target = day_dir / filename
    if target.exists():
        stem, suffix = target.stem, target.suffix
        for i in range(1, 1000):
            candidate = day_dir / f"{stem}_{i}{suffix}"
            if not candidate.exists():
                target = candidate
                break

    size = 0
    with target.open("wb") as out:
        while chunk := await file.read(1024 * 1024):
            size += len(chunk)
            if size > _MAX_UPLOAD_BYTES:
                out.close()
                target.unlink(missing_ok=True)
                raise HTTPException(status_code=413, detail="文件过大（>500MB）")
            out.write(chunk)
    if size == 0:
        target.unlink(missing_ok=True)
        raise HTTPException(status_code=400, detail="上传文件为空")

    duration = _wav_duration(target)
    rel_path = str(target.relative_to(config.RECORDINGS_DIR))
    record = database.insert_recording(
        filename=target.name,
        rec_type=rec_type,
        device_id=(device_id or "mibao-01").strip(),
        size_bytes=size,
        duration_s=duration,
        rel_path=rel_path,
    )
    return {"success": True, "recording": record}


@router.get("/list")
def list_recordings(
    type: str | None = Query(None, description="meeting / personal"),
    page: int = Query(1, ge=1),
    page_size: int = Query(20, ge=1, le=100),
):
    if type and type not in config.RECORDING_TYPES:
        raise HTTPException(status_code=400, detail="type 无效")
    total, items = database.list_recordings(type, page, page_size)
    return {
        "total": total,
        "page": page,
        "page_size": page_size,
        "items": items,
    }


def _resolve_file(rec_id: int) -> tuple[dict, Path]:
    record = database.get_recording(rec_id)
    if not record:
        raise HTTPException(status_code=404, detail="录音不存在")
    path = config.RECORDINGS_DIR / record["rel_path"]
    if not path.is_file():
        raise HTTPException(status_code=404, detail="录音文件已丢失")
    return record, path


@router.get("/{rec_id}")
def download_recording(rec_id: int):
    record, path = _resolve_file(rec_id)
    return FileResponse(
        path,
        media_type="audio/wav",
        filename=record["filename"],
    )


@router.delete("/{rec_id}")
def delete_recording(rec_id: int):
    record = database.get_recording(rec_id)
    if not record:
        raise HTTPException(status_code=404, detail="录音不存在")
    path = config.RECORDINGS_DIR / record["rel_path"]
    path.unlink(missing_ok=True)
    database.delete_recording(rec_id)
    return {"success": True, "id": rec_id}
