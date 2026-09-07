"""IoT 代理模块：设备清单 + 转发树莓派 Flask 执行器控制。

设备端协议（已固化）：POST actuator_control，
body {"actuator_id":"fan|pump|light|heat","action":"on|off","duration":0..3600}，
5s 超时，返回含 success 的 JSON。本代理 body 格式保持完全一致。
"""
from __future__ import annotations

import httpx
from fastapi import APIRouter
from pydantic import BaseModel, Field

from .. import config

router = APIRouter(prefix="/api/iot", tags=["iot"])

# 执行器静态清单（id 与设备端协议一致）
DEVICES = [
    {"id": "fan", "name": "硅基一号的风扇"},
    {"id": "pump", "name": "硅基一号的水泵"},
    {"id": "light", "name": "硅基一号的生长灯"},
    {"id": "heat", "name": "硅基一号的加热垫"},
]


class ControlRequest(BaseModel):
    actuator_id: str = Field(..., description="fan / pump / light / heat")
    action: str = Field(..., description="on / off")
    duration: int = Field(0, ge=0, le=config.ACTUATOR_MAX_DURATION)


@router.get("/devices")
def list_devices():
    return {"devices": DEVICES}


@router.post("/control")
async def control_device(req: ControlRequest):
    if req.actuator_id not in config.ACTUATOR_IDS:
        return {
            "success": False,
            "error": f"actuator_id 无效，允许值: {list(config.ACTUATOR_IDS)}",
        }
    if req.action not in config.ACTUATOR_ACTIONS:
        return {
            "success": False,
            "error": f"action 无效，允许值: {list(config.ACTUATOR_ACTIONS)}",
        }

    # body 与设备端直连格式完全一致，原样转发
    payload = {
        "actuator_id": req.actuator_id,
        "action": req.action,
        "duration": req.duration,
    }
    try:
        async with httpx.AsyncClient(timeout=config.IOT_TIMEOUT) as client:
            resp = await client.post(config.IOT_TARGET_URL, json=payload)
    except httpx.TimeoutException:
        return {"success": False, "error": f"IoT 目标超时（{config.IOT_TIMEOUT}s）"}
    except httpx.HTTPError as exc:
        return {"success": False, "error": f"IoT 目标不可达: {exc.__class__.__name__}"}

    # 透传树莓派响应；非 JSON 时包装为 success 结构
    try:
        return resp.json()
    except ValueError:
        return {
            "success": 200 <= resp.status_code < 300,
            "status_code": resp.status_code,
            "raw": resp.text[:500],
        }
