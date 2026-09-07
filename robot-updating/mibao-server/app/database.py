"""SQLite 元数据存储（标准库 sqlite3，避免额外依赖）。"""
from __future__ import annotations

import sqlite3
import threading
from contextlib import closing
from typing import Any

from . import config

_lock = threading.Lock()

_SCHEMA = """
CREATE TABLE IF NOT EXISTS recordings (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    filename    TEXT    NOT NULL,
    type        TEXT    NOT NULL,
    device_id   TEXT    NOT NULL,
    size_bytes  INTEGER NOT NULL,
    duration_s  REAL    NOT NULL DEFAULT 0,
    rel_path    TEXT    NOT NULL,
    created_at  TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))
);
CREATE INDEX IF NOT EXISTS idx_recordings_type ON recordings(type);

CREATE TABLE IF NOT EXISTS transcripts (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    recording_id INTEGER NOT NULL UNIQUE,
    status       TEXT    NOT NULL DEFAULT 'pending',
    text         TEXT    NOT NULL DEFAULT '',
    speakers     TEXT,
    error        TEXT,
    created_at   TEXT    NOT NULL DEFAULT (datetime('now', 'localtime')),
    updated_at   TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))
);

CREATE TABLE IF NOT EXISTS summaries (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    recording_id INTEGER NOT NULL UNIQUE,
    rec_type     TEXT    NOT NULL,
    content      TEXT    NOT NULL DEFAULT '',
    model        TEXT,
    created_at   TEXT    NOT NULL DEFAULT (datetime('now', 'localtime')),
    updated_at   TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))
);

-- 设备配置（单行记录，id 恒为 1）：设备端开机拉取，写入固件 NVS
CREATE TABLE IF NOT EXISTS device_config (
    id          INTEGER PRIMARY KEY CHECK (id = 1),
    upload_url  TEXT NOT NULL DEFAULT '',
    iot_url     TEXT NOT NULL DEFAULT '',
    ota_url     TEXT NOT NULL DEFAULT '',
    updated_at  TEXT NOT NULL DEFAULT (datetime('now', 'localtime'))
);
"""


def _connect() -> sqlite3.Connection:
    conn = sqlite3.connect(config.DB_PATH, timeout=10)
    conn.row_factory = sqlite3.Row
    return conn


def init_db() -> None:
    config.DATA_DIR.mkdir(parents=True, exist_ok=True)
    config.RECORDINGS_DIR.mkdir(parents=True, exist_ok=True)
    with _lock, closing(_connect()) as conn:
        conn.executescript(_SCHEMA)


def insert_recording(
    filename: str,
    rec_type: str,
    device_id: str,
    size_bytes: int,
    duration_s: float,
    rel_path: str,
) -> dict[str, Any]:
    with _lock, closing(_connect()) as conn:
        cur = conn.execute(
            "INSERT INTO recordings (filename, type, device_id, size_bytes,"
            " duration_s, rel_path) VALUES (?, ?, ?, ?, ?, ?)",
            (filename, rec_type, device_id, size_bytes, duration_s, rel_path),
        )
        conn.commit()
        row = conn.execute(
            "SELECT * FROM recordings WHERE id = ?", (cur.lastrowid,)
        ).fetchone()
    return dict(row)


def get_recording(rec_id: int) -> dict[str, Any] | None:
    with _lock, closing(_connect()) as conn:
        row = conn.execute(
            "SELECT * FROM recordings WHERE id = ?", (rec_id,)
        ).fetchone()
    return dict(row) if row else None


def list_recordings(
    rec_type: str | None, page: int, page_size: int
) -> tuple[int, list[dict[str, Any]]]:
    offset = (page - 1) * page_size
    where, params = "", []
    if rec_type:
        where = "WHERE r.type = ?"
        params.append(rec_type)
    with _lock, closing(_connect()) as conn:
        total = conn.execute(
            f"SELECT COUNT(*) FROM recordings r {where}", params
        ).fetchone()[0]
        rows = conn.execute(
            "SELECT r.*, t.status AS transcript_status,"
            " (CASE WHEN s.recording_id IS NOT NULL THEN 1 ELSE 0 END) AS has_summary"
            " FROM recordings r"
            " LEFT JOIN transcripts t ON t.recording_id = r.id"
            " LEFT JOIN summaries s ON s.recording_id = r.id"
            f" {where} ORDER BY r.id DESC LIMIT ? OFFSET ?",
            (*params, page_size, offset),
        ).fetchall()
    return total, [dict(r) for r in rows]


def delete_recording(rec_id: int) -> None:
    with _lock, closing(_connect()) as conn:
        conn.execute("DELETE FROM recordings WHERE id = ?", (rec_id,))
        conn.execute("DELETE FROM transcripts WHERE recording_id = ?", (rec_id,))
        conn.execute("DELETE FROM summaries WHERE recording_id = ?", (rec_id,))
        conn.commit()


# ---------------------------------------------------------------------------
# 转写 / 纪要（百炼）存储
# ---------------------------------------------------------------------------
def upsert_transcript(
    recording_id: int,
    status: str,
    text: str = "",
    speakers: str | None = None,
    error: str | None = None,
) -> None:
    """新增或更新转写记录（status: pending/processing/done/error）。"""
    with _lock, closing(_connect()) as conn:
        conn.execute(
            "INSERT INTO transcripts (recording_id, status, text, speakers, error,"
            " updated_at) VALUES (?, ?, ?, ?, ?, datetime('now','localtime'))"
            " ON CONFLICT(recording_id) DO UPDATE SET"
            "  status=excluded.status, text=excluded.text,"
            "  speakers=excluded.speakers, error=excluded.error,"
            "  updated_at=datetime('now','localtime')",
            (recording_id, status, text, speakers, error),
        )
        conn.commit()


def get_transcript(recording_id: int) -> dict[str, Any] | None:
    with _lock, closing(_connect()) as conn:
        row = conn.execute(
            "SELECT * FROM transcripts WHERE recording_id = ?", (recording_id,)
        ).fetchone()
    return dict(row) if row else None


def upsert_summary(
    recording_id: int, rec_type: str, content: str, model: str | None
) -> None:
    with _lock, closing(_connect()) as conn:
        conn.execute(
            "INSERT INTO summaries (recording_id, rec_type, content, model,"
            " updated_at) VALUES (?, ?, ?, ?, datetime('now','localtime'))"
            " ON CONFLICT(recording_id) DO UPDATE SET"
            "  rec_type=excluded.rec_type, content=excluded.content,"
            "  model=excluded.model, updated_at=datetime('now','localtime')",
            (recording_id, rec_type, content, model),
        )
        conn.commit()


def get_summary(recording_id: int) -> dict[str, Any] | None:
    with _lock, closing(_connect()) as conn:
        row = conn.execute(
            "SELECT * FROM summaries WHERE recording_id = ?", (recording_id,)
        ).fetchone()
    return dict(row) if row else None


# ---------------------------------------------------------------------------
# 设备配置（单行记录，id=1）
# ---------------------------------------------------------------------------
def get_device_config() -> dict[str, Any]:
    with _lock, closing(_connect()) as conn:
        row = conn.execute(
            "SELECT * FROM device_config WHERE id = 1"
        ).fetchone()
    if row:
        return dict(row)
    return {"id": 1, "upload_url": "", "iot_url": "", "ota_url": ""}


def upsert_device_config(
    upload_url: str, iot_url: str, ota_url: str
) -> dict[str, Any]:
    with _lock, closing(_connect()) as conn:
        conn.execute(
            "INSERT INTO device_config (id, upload_url, iot_url, ota_url, updated_at)"
            " VALUES (1, ?, ?, ?, datetime('now','localtime'))"
            " ON CONFLICT(id) DO UPDATE SET"
            "  upload_url=excluded.upload_url, iot_url=excluded.iot_url,"
            "  ota_url=excluded.ota_url, updated_at=datetime('now','localtime')",
            (upload_url, iot_url, ota_url),
        )
        conn.commit()
        row = conn.execute(
            "SELECT * FROM device_config WHERE id = 1"
        ).fetchone()
    return dict(row)
