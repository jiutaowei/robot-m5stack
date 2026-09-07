"""米宝一号（Stackstan）FastAPI 后端入口。

启动：python -m app.main 或 uvicorn app.main:app
"""
from __future__ import annotations

from pathlib import Path

from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles

from . import config, database
from .routers import ai_config, device_config, iot, recording, transcribe

app = FastAPI(
    title="米宝一号后端",
    description="Stackstan 桌面机器人：录音管理 + 百炼转写/纪要 + IoT 执行器代理",
    version="0.2.0",
)

database.init_db()

app.include_router(recording.router)
app.include_router(transcribe.router)
app.include_router(iot.router)
app.include_router(device_config.router)
app.include_router(ai_config.router)


@app.get("/api/health")
def health():
    return {
        "status": "ok",
        "service": "mibao-server",
        "iot_target": config.IOT_TARGET_URL,
        # 百炼配置状态（仅掩码，绝不输出原文）
        "dashscope_key": config.mask_key(config.DASHSCOPE_KEY),
        "qwen_model": config.QWEN_MODEL,
        "asr_model": config.ASR_MODEL,
    }


# Web 管理页（纯静态 HTML + 原生 JS，无前端构建链）
_STATIC_DIR = Path(__file__).resolve().parent / "static"
app.mount("/", StaticFiles(directory=_STATIC_DIR, html=True), name="static")


def main() -> None:
    import uvicorn

    uvicorn.run("app.main:app", host=config.HOST, port=config.PORT, reload=False)


if __name__ == "__main__":
    main()
