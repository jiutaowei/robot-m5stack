"""设备配置模块：upload_url / iot_url / ota_url 的读写。

- /api/device/config —— 设备端开机拉取（明文，供固件写入 NVS）
- /api/admin/device —— 管理页读取回显 / 保存
"""
from __future__ import annotations

from fastapi import APIRouter
from pydantic import BaseModel

from .. import database

router = APIRouter(tags=["device_config"])


class DeviceConfigRequest(BaseModel):
    upload_url: str = ""
    iot_url: str = ""
    ota_url: str = ""


@router.get("/api/device/config")
def device_config():
    """设备端开机拉取：明文返回，供固件写入 NVS。"""
    cfg = database.get_device_config()
    return {
        "upload_url": cfg["upload_url"],
        "iot_url": cfg["iot_url"],
        "ota_url": cfg["ota_url"],
    }


@router.get("/api/admin/device")
def admin_get_device():
    """管理页读取当前配置（供表单回显）。"""
    return database.get_device_config()


@router.post("/api/admin/device")
def admin_save_device(req: DeviceConfigRequest):
    cfg = database.upsert_device_config(
        req.upload_url.strip(),
        req.iot_url.strip(),
        req.ota_url.strip(),
    )
    return {"success": True, "config": cfg}