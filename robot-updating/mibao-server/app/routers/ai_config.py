"""AI(ASR/LLM/TTS) 配置管理：读写 xiaozhi-server 的 .config.yaml。

安全约束：api_key 在读取/回显时一律用 config.mask_key() 掩码（sk-***），
仅在用户提交了新的明文 key 时才写入 yaml；留空则保持原值。
"""
from __future__ import annotations

from fastapi import APIRouter
from pydantic import BaseModel

import yaml

from .. import config

router = APIRouter(tags=["ai_config"])

# 每个 category 关注的模块名（与 .config.yaml 中 selected_module 一致）
_CATEGORIES = ("ASR", "LLM", "TTS")

# 由前端可编辑的标量字段（非 api_key）
_SCALAR_FIELDS = ("type", "model", "model_name", "base_url", "ws_url", "voice")


def _load_yaml() -> dict:
    path = config.AI_CONFIG_PATH
    if not path.is_file():
        return {}
    with path.open("r", encoding="utf-8") as f:
        return yaml.safe_load(f) or {}


def _write_yaml(data: dict) -> None:
    path = config.AI_CONFIG_PATH
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        yaml.safe_dump(data, f, allow_unicode=True, sort_keys=False)


def _mask_module(cfg: dict) -> dict:
    out = dict(cfg)
    if "api_key" in out:
        out["api_key"] = config.mask_key(out["api_key"] or "")
    return out


class ModuleConfig(BaseModel):
    module: str = ""
    type: str = ""
    api_key: str = ""
    model: str = ""
    model_name: str = ""
    base_url: str = ""
    ws_url: str = ""
    voice: str = ""
    temperature: float | None = None


class AIConfigRequest(BaseModel):
    ASR: ModuleConfig
    LLM: ModuleConfig
    TTS: ModuleConfig


@router.get("/api/admin/ai")
def get_ai_config():
    data = _load_yaml()
    sel = data.get("selected_module", {})
    result: dict = {}
    for cat in _CATEGORIES:
        mod_name = sel.get(cat)
        mod = data.get(cat, {})
        if mod_name and isinstance(mod, dict) and mod_name in mod:
            result[cat] = {"module": mod_name, **_mask_module(mod[mod_name])}
        else:
            result[cat] = {
                "module": mod_name or "",
                "api_key": config.mask_key(""),
            }
    return result


@router.post("/api/admin/ai")
def save_ai_config(req: AIConfigRequest):
    data = _load_yaml()
    sel = data.setdefault("selected_module", {})

    for cat in _CATEGORIES:
        cfg: ModuleConfig = getattr(req, cat)
        mod_name = cfg.module or sel.get(cat)
        if not mod_name:
            continue
        current = (data.get(cat) or {}).get(mod_name) or {}
        new = dict(current)
        for field in _SCALAR_FIELDS:
            val = getattr(cfg, field)
            if val:
                new[field] = val
        if cfg.temperature is not None:
            new["temperature"] = cfg.temperature
        # api_key：仅当提交值非空、且不是掩码值时覆盖
        mask = config.mask_key(current.get("api_key", ""))
        if cfg.api_key and cfg.api_key != mask:
            new["api_key"] = cfg.api_key
        data.setdefault(cat, {})[mod_name] = new
        sel[cat] = mod_name

    _write_yaml(data)
    return get_ai_config()