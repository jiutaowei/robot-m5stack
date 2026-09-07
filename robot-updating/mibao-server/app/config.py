"""米宝一号后端配置：全部走环境变量，可选 .env 文件。"""
from __future__ import annotations

import os
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent


def _load_dotenv() -> None:
    """轻量 .env 加载（避免引入 python-dotenv 依赖），仅支持 KEY=VALUE。"""
    env_file = BASE_DIR / ".env"
    if not env_file.is_file():
        return
    try:
        for line in env_file.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, _, value = line.partition("=")
            key, value = key.strip(), value.strip().strip("'\"")
            os.environ.setdefault(key, value)
    except OSError:
        pass


_load_dotenv()

# 服务端口（设备端 upload_url 将指向 http://<电脑IP>:8000）
PORT = int(os.environ.get("MIBAO_PORT", os.environ.get("PORT", "8000")))
HOST = os.environ.get("MIBAO_HOST", "0.0.0.0")

# 数据目录：sqlite 与录音文件均位于 data/ 下
DATA_DIR = Path(os.environ.get("MIBAO_DATA_DIR", str(BASE_DIR / "data")))
RECORDINGS_DIR = DATA_DIR / "recordings"
DB_PATH = DATA_DIR / "mibao.db"

# AI(ASR/LLM/TTS) 配置 yaml 路径：指向 xiaozhi-server 的本地覆盖配置
AI_CONFIG_PATH = BASE_DIR.parent / "xiaozhi-server" / "data" / ".config.yaml"

# IoT 代理目标：树莓派 Flask 执行器控制接口
IOT_TARGET_URL = os.environ.get(
    "MIBAO_IOT_TARGET_URL", "http://10.51.1.205:5000/actuator_control"
)
IOT_TIMEOUT = float(os.environ.get("MIBAO_IOT_TIMEOUT", "5"))

# 合法执行器与动作（与设备端协议一致）
ACTUATOR_IDS = ("fan", "pump", "light", "heat")
ACTUATOR_ACTIONS = ("on", "off")
ACTUATOR_MAX_DURATION = 3600

# 录音类型（文件名前缀 -> type）
RECORDING_TYPES = ("meeting", "personal")

# ---------------------------------------------------------------------------
# 百炼（DashScope）配置：用于录音转写与纪要总结
# API key 严禁入库 / 入 git / 出现在日志或报告中，对外展示一律用 mask_key() 掩码。
# ---------------------------------------------------------------------------
DASHSCOPE_KEY = os.environ.get("MIBAO_DASHSCOPE_KEY", "").strip()

# 原生 DashScope API 端点（paraformer 语音转写）。专属业务空间可指向自定义域名。
DASHSCOPE_BASE_URL = os.environ.get(
    "MIBAO_DASHSCOPE_BASE_URL", "https://dashscope.aliyuncs.com/api/v1"
).rstrip("/")

# OpenAI 兼容端点（qwen 文本生成 / 纪要总结）。
DASHSCOPE_OPENAI_BASE_URL = os.environ.get(
    "MIBAO_DASHSCOPE_OPENAI_BASE_URL",
    "https://dashscope.aliyuncs.com/compatible-mode/v1",
).rstrip("/")

QWEN_MODEL = os.environ.get("MIBAO_QWEN_MODEL", "qwen-plus")
ASR_MODEL = os.environ.get("MIBAO_ASR_MODEL", "paraformer-realtime-v2")
# 转写 / 总结超时（秒）
LLM_TIMEOUT = float(os.environ.get("MIBAO_LLM_TIMEOUT", "120"))


def mask_key(key: str) -> str:
    """API key 掩码（仅用于日志/接口展示，严禁输出原文）。"""
    if not key:
        return "(未配置)"
    prefix = key.split("-", 1)[0] if "-" in key else key[:2]
    return f"{prefix}-***"
