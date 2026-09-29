"""广西省级植保 · S2S 数字员工知识工具（米宝一号）

用途
----
把 hezor 的 Widget S2S 接口（数字化员工「广西省级植保」）接进米宝的 AI 对话：
米宝遇到植保/病虫害/农技类问题时调用本工具，拿到专业回答后再用米宝的口吻复述。

接口（已在 2026-09-28 实测）
--------------------------
    POST {base_url}/widget/chat        # base_url 默认 https://hezor.com/api/v1
    Headers:
        X-META-INFO : MetaInfo JWT（Ed25519 签名，本插件负责签发）
        X-APP-NAME  : 与 Casdoor 注册的应用名一致（phyto_province_gx_app）
    Body: {"message": ..., "mode": "widget", "stream": true, "workerId": optional}
    响应: SSE，逐行 `data: {"type":...,"content":{...},"metadata":{...}}`
          type=text  -> content.text 增量文本
          type=done  -> metadata.conversation_id / message_id / model
          type=status & content.type=="error" -> 本轮出错，视为流终止
    鉴权失败返回 401 {"message":"MetaInfo verification failed"}；
    workerId 不在应用 allowed_worker_ids 白名单返回 403。

配置（写在 xiaozhi-server 的 config.yaml / data/.config.yaml，勿提交密钥）
----------------------------------------------------------------------
    Intent:
      function_call:
        functions:
          - ...                 # 需把 ask_guangxi_phyto 加进列表才会启用

    plugins:
      ask_guangxi_phyto:
        description: "广西省级植保数字员工"      # 可选，给 LLM 的补充说明
        base_url: https://hezor.com/api/v1
        app_name: phyto_province_gx_app
        worker_id: ""                          # 可选，UUID，需在 allowed_worker_ids 白名单
        # 私钥三选一：
        #   1) 环境变量 HEZOR_CERT_CONTENT（文档推荐，PEM 内容，\n 转义）
        #   2) 文件：private_key_path: .keys/private_key.pem
        #   3) 直接写 private_key_pem: |
        #        -----BEGIN ENCRYPTED PRIVATE KEY-----
        private_key_path: .keys/private_key.pem
        private_key_password: ""               # 留空则试 HEZOR_CLIENT_SECRET / HEZOR2_HEADER_PK_PASSWORD
        token_ttl_seconds: 3600                # 文档建议 ≤ 1 小时
        token_refresh_margin_seconds: 300      # 提前 5 分钟重签
        timeout_seconds: 60
        max_answer_chars: 800
        # claims 与官方文档一致（注意是 subject，不是标准 JWT 的 sub）
        claims:
          subject: 广西省级植保
          subject_code: phyto_province_gx_app
          # 2026-09-29 实测：平台按 caller_id 自动建号并计额度。
          # 机器人专用用户自带额度（正常工作）。
          # 备选：.env.local 原值 guangxi_ai1234@phyto_province_gx_app.local 实测同样可用；
          # 但更早试过的 guangxi_ai123@…（少一个 4）是「已配路由、余额不足」的老号，不要用。
          caller_id: guangxi_ai_robot_m5stack@phyto_province_gx_app.local
          creation_slug: guangxi_ai_chat
          creation_name: 广西AI助手
        # 需要透传业务标签时（文档：extras 里的键名用 var_* 前缀）
        # meta_extras:
        #   var_device_id: mibao-01

凭证来源
--------
应用凭证（cert_content 私钥 + client_secret 密码）由 Hezor 管理员在 Casdoor 创建，
可在平台调用 `GET /app-certs` 获取。S2S 调用**不做 Origin/域名白名单校验**
（域名白名单只作用于浏览器 iframe），因此服务端集成无需配置 affiliation_url。

401 排查（官方故障表）：检查 Token 有效期，并确认 cert_content 与 client_secret 是否匹配。

没有私钥时本工具不报错崩溃：会返回一句明确提示，对话其余功能不受影响。
"""

import asyncio
import json
import os
import re
import time
from typing import TYPE_CHECKING, Any, Dict, Optional

import httpx

from config.logger import setup_logging
from plugins_func.register import Action, ActionResponse, ToolType, register_function

if TYPE_CHECKING:
    from core.connection import ConnectionHandler

TAG = __name__
logger = setup_logging()

# 默认 MetaInfo 载荷。字段名以官方文档为准（S2S.doc「应用凭证/签发代码」）：
#   payload = {"subject": ..., "subject_code": ..., "caller_id": ...,
#              "creation_slug": ..., "creation_name": ...}
# 注意是 subject，不是标准 JWT 的 sub。
DEFAULT_CLAIMS = {
    "subject": "广西省级植保",
    "subject_code": "phyto_province_gx_app",
    # 机器人专用 caller（自带额度）；
    # 备选：.env.local 的 guangxi_ai1234@phyto_province_gx_app.local 实测同样可用
    "caller_id": "guangxi_ai_robot_m5stack@phyto_province_gx_app.local",
    "creation_slug": "guangxi_ai_chat",
    "creation_name": "广西AI助手",
}

# 环境变量名：文档一套（HEZOR_*）+ 本项目 .env 一套（HEZOR2_*），按顺序尝试
PASSWORD_ENV_NAMES = ("HEZOR_CLIENT_SECRET", "HEZOR2_HEADER_PK_PASSWORD")
CERT_ENV_NAMES = ("HEZOR_CERT_CONTENT", "HEZOR2_CERT_CONTENT")
PATH_ENV_NAMES = ("HEZOR2_HEADER_PK_FILEPATH", "HEZOR_CERT_PATH")
APP_NAME_ENV_NAMES = ("HEZOR_APP_NAME", "HEZOR2_APP_NAME")
# 2026-09-29 实测结论：新用户（机器人专用 caller）自带额度但没有 mode 路由，
# 因此必须显式传 workerId 直连白名单内的数字员工。这两个变量与 guangxi_ai_chat 的 .env 同名。
WORKER_ID_ENV_NAMES = ("HEZOR2_WORKER_ID", "WORKER_ID", "HEZOR_WORKER_ID")
CLAIM_ENV_NAMES = {
    "subject": ("HEZOR2_META_SUBJECT", "HEZOR_META_SUBJECT"),
    "subject_code": ("HEZOR2_META_SUBJECT_CODE", "HEZOR_META_SUBJECT_CODE"),
    "caller_id": ("HEZOR2_META_CALLER_ID", "HEZOR_META_CALLER_ID"),
    "creation_slug": ("HEZOR2_META_CREATION_SLUG", "HEZOR_META_CREATION_SLUG"),
    "creation_name": ("HEZOR2_META_CREATION_NAME", "HEZOR_META_CREATION_NAME"),
}

# 数据类问题内部工具调用多，实测可超 60s，故 120s 起步
DEFAULT_SPEAK_HINT = (
    "【播报要求·重要】用户是语音对话，看不到屏幕。"
    "如果是数据结果：只用 1~2 句话、总共不超过约 60 字，直接说出最关键的数字和结论"
    "（例如最新一期的发生面积、发生程度、升降趋势），不要念表格、不要逐条罗列、"
    "不要出现 Markdown 符号（| # * ）,也不要说“根据查询结果”这类套话。"
    "如果是需要向用户澄清的问题：照原意用一句话问回去即可。"
    "完整明细已在后台保留，用户追问时再展开。"
)

# 插件默认配置：私钥可给「PEM 内容」（文档推荐的 cert_content 环境变量）
# 或「文件路径」（本项目 .env 的 HEZOR2_HEADER_PK_FILEPATH 风格）
DEFAULT_PLUGIN_CONFIG: Dict[str, Any] = {
    "base_url": "https://hezor.com/api/v1",
    "app_name": "phyto_province_gx_app",
    "mode": "widget",
    "worker_id": "",
    "private_key_pem": "",                      # 直接给 PEM 内容
    "private_key_pem_env": "",                  # 或指定环境变量名（默认试 CERT_ENV_NAMES）
    "private_key_path": ".keys/private_key.pem",   # 或给文件路径
    "private_key_password": "",
    "private_key_password_env": "",             # 留空则试 PASSWORD_ENV_NAMES
    "token_ttl_seconds": 3600,                  # 文档建议 ≤ 1 小时
    "token_refresh_margin_seconds": 300,        # 提前 5 分钟重签（文档推荐策略）
    # 数据类问题要跑很多次内部工具调用，实测单个问题可超过 60s，故 120s 起步
    "timeout_seconds": 120,
    "max_answer_chars": 800,
    # 平台流式语义（2026-09-29 实测）：正常片段 2~495 字不等，**长度不能当判据**；
    # 数字员工重新生成时会把整篇文档从头再发一遍，那一片与当前文档开头有 ≥16 字
    # 完全相同的公共前缀。只有满足「长度 >= 该阈值 且 开头重复」才按快照替换，
    # 否则纯增量拼接会出现 2~3 份重复/半截交错内容。设为 0 可关闭该判定。
    "snapshot_min_chars": 16,
    # 发送前剥掉「防治建议/用药方案」类措辞：实测这类词会触发上游大模型
    # content_filter，让数字员工整轮失败；数据照查，防治建议由机器人自己回答。
    "strip_control_requests": True,
    # ── 交互体验：数据类查询要 30~110 秒，不能让机器人干等着 ──────────────
    # 查询期间主动播报提示语（用框架的 speak_txt，会显示并朗读）
    "interim_message": "正在为您查询数据，请稍等一下。",
    "interim_delay_seconds": 0,          # 0 = 立刻说
    "second_interim_message": "数据还在查询中，请再稍等一下。",
    "second_interim_delay_seconds": 45,  # 45 秒还没回来就再说一句
    # 工具运行期间刷新 conn.last_activity_time，避免服务端
    # close_connection_no_voice_time(默认 120s) 判定"用户没说话"而道别并断开
    "keepalive_activity": True,
    # 同一会话内、完全相同的问题并发调用时只真正发起一次（实测机器人一次会并发
    # 发 2~3 个相同调用，等于 2~3 倍额度）。只挡"同时重复"，不跨时间复用结果。
    "dedupe_concurrent": True,
    # 播报要求：附在查询原文前面交给机器人的大模型，约束它"只播报要点、不念表格"。
    # 只影响"怎么念"，不影响查到的数据内容（明细仍完整保留在结果里）。
    # 设为空字符串可关闭。
    "speak_hint": DEFAULT_SPEAK_HINT,
    "model": "",                                # 可选：指定模型，留空由服务端决策
    "kb_ids": [],                               # 可选：限定知识库 ID
}

GX_PHYTO_FUNCTION_DESC = {
    "type": "function",
    "function": {
        "name": "ask_guangxi_phyto",
        "description": (
            "查询「广西省级植保」数字员工的**省级病虫害测报数据**：发生面积、发生程度、"
            "灯诱/田间虫量、褐飞虱比例、防治面积与防治效果、分市/县明细、植保政策等。"
            "当用户问到本地病虫害当前发生情况、测报数据这类需要真实数据的问题时调用。"
        ),
        "parameters": {
            "type": "object",
            "properties": {
                "question": {
                    "type": "string",
                    "description": (
                        "要查的数据，务必写清【作物/病虫】+【地区】+【时段】三要素，例如"
                        "「广西 稻飞虱 最近一个月 全区 发生面积和发生程度」。"
                        "⚠️ 只问数据，**不要**在问题里要求「防治建议/防治方案/用药方案」——"
                        "实测这类措辞会触发上游大模型的内容过滤（content_filter）导致整轮失败；"
                        "防治与用药建议请你自己根据查到的数据来回答。"
                    ),
                }
            },
            "required": ["question"],
        },
    },
}

# ---------------------------------------------------------------------------
# 配置读取
# ---------------------------------------------------------------------------


def load_plugin_config(config: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    """取 plugins.ask_guangxi_phyto 配置；config 为空时自行加载（给独立测试脚本用）。

    注意：config.settings.load_config 是 async 的（app.py 里 `await load_config()`）。
    插件在对话中被调用时由 conn.config 传入，不会走这里的加载分支。
    """
    if config is None:
        try:
            from config.settings import load_config

            loaded = load_config()
            if asyncio.iscoroutine(loaded):
                # 独立脚本场景（当前线程没有运行中的事件循环）
                loaded = asyncio.run(loaded)
            config = loaded
        except Exception as exc:  # 独立运行时不允许把整个进程带崩
            logger.bind(tag=TAG).warning(f"加载配置失败：{exc}")
            config = {}
    cfg = (config or {}).get("plugins", {}).get("ask_guangxi_phyto", {}) or {}
    merged = dict(DEFAULT_PLUGIN_CONFIG)
    merged.update(cfg)

    # 环境变量兜底（与 guangxi_ai_chat 的 .env 命名一致，方便直接把那份 .env 拿来用）
    if not merged.get("worker_id"):
        merged["worker_id"] = _first_env(WORKER_ID_ENV_NAMES)
    if not merged.get("app_name"):
        merged["app_name"] = _first_env(APP_NAME_ENV_NAMES)

    # claims = 默认值 + 配置 + 环境变量（后者优先，缺省才用前者的值）
    claims = dict(DEFAULT_CLAIMS)
    claims.update(merged.get("claims") or {})
    for key, names in CLAIM_ENV_NAMES.items():
        value = _first_env(names)
        if value:
            claims[key] = value
    merged["claims"] = claims
    return merged


# 相对路径（如 .keys/private_key.pem）以 xiaozhi-server 目录为基准
SERVER_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def _first_env(names) -> str:
    for name in names:
        if not name:
            continue
        value = os.environ.get(name, "")
        if value:
            return value
    return ""


def _cert_path_candidates(cfg: Dict[str, Any]):
    """私钥文件候选路径：配置里的优先，其次是 .env 风格的环境变量。"""
    candidates = []
    cfg_path = (cfg.get("private_key_path") or "").strip()
    if cfg_path:
        candidates.append(cfg_path)
    env_path = _first_env(PATH_ENV_NAMES).strip()
    if env_path and env_path not in candidates:
        candidates.append(env_path)
    return candidates


def _resolve_path(path: str) -> str:
    return path if os.path.isabs(path) else os.path.join(SERVER_ROOT, path)


def _cert_pem(cfg: Dict[str, Any]) -> Optional[bytes]:
    """拿到 Ed25519 私钥 PEM：优先直接给的内容（文档 cert_content），其次按候选路径找文件。"""
    pem = (cfg.get("private_key_pem") or "").strip()
    if not pem:
        env_name = cfg.get("private_key_pem_env") or ""
        pem = _first_env(([env_name] if env_name else []) + list(CERT_ENV_NAMES)).strip()
    if pem:
        # 环境变量里常用 "\n" 表示换行
        return pem.replace("\\n", "\n").encode("utf-8")

    for path in _cert_path_candidates(cfg):
        full = _resolve_path(path)
        if os.path.isfile(full):
            with open(full, "rb") as f:
                return f.read()
    return None


def _resolve_password(cfg: Dict[str, Any]) -> Optional[bytes]:
    pwd = cfg.get("private_key_password") or ""
    if not pwd:
        env_name = cfg.get("private_key_password_env") or ""
        pwd = _first_env(([env_name] if env_name else []) + list(PASSWORD_ENV_NAMES))
    return pwd.encode("utf-8") if pwd else None


def _resolve_app_name(cfg: Dict[str, Any]) -> str:
    return (cfg.get("app_name") or "").strip() or _first_env(APP_NAME_ENV_NAMES) or "phyto_province_gx_app"


def is_configured(cfg: Dict[str, Any]) -> bool:
    return _cert_pem(cfg) is not None


def load_private_key(cfg: Dict[str, Any]):
    """加载 Ed25519 私钥对象（自检脚本用；不返回/不打印私钥内容）。"""
    from cryptography.hazmat.primitives import serialization

    pem = _cert_pem(cfg)
    if pem is None:
        return None
    password = _resolve_password(cfg)
    try:
        return serialization.load_pem_private_key(pem, password=password)
    except TypeError:
        # 私钥其实是未加密的 PEM，但环境变量里存在密码（如自检用临时密钥时）——
        # 此时按无密码再试一次，避免误报。
        if password:
            return serialization.load_pem_private_key(pem, password=None)
        raise


def cert_source(cfg: Dict[str, Any]) -> str:
    """返回私钥来源描述，便于脚本/日志排查（不泄密）。"""
    if (cfg.get("private_key_pem") or "").strip():
        return "配置内 private_key_pem"
    env_name = cfg.get("private_key_pem_env") or ""
    names = ([env_name] if env_name else []) + list(CERT_ENV_NAMES)
    used = _first_env(names)
    if used:
        return f"环境变量 {names[0] if env_name else CERT_ENV_NAMES[0]}（PEM 内容）"

    tried = []
    for path in _cert_path_candidates(cfg):
        full = _resolve_path(path)
        if os.path.isfile(full):
            return f"文件 {full}（存在 ✓）"
        tried.append(full)
    if tried:
        return "文件 " + " / ".join(tried) + "（均不存在 ✗）"
    return "未配置"


# ---------------------------------------------------------------------------
# MetaInfo JWT 签名（Ed25519 / EdDSA）
# ---------------------------------------------------------------------------

_token_cache: Dict[str, Any] = {"token": "", "exp": 0.0}


def build_meta_info_token(cfg: Dict[str, Any], force: bool = False) -> str:
    """签发 X-META-INFO（EdDSA/Ed25519）。带缓存，过期前按 refresh margin 自动重签。"""
    now = time.time()
    margin = int(cfg.get("token_refresh_margin_seconds", 300) or 300)
    if not force and _token_cache["token"] and _token_cache["exp"] - margin > now:
        return _token_cache["token"]

    import jwt  # PyJWT

    # cert_content 是加密的 PKCS#8（-----BEGIN ENCRYPTED PRIVATE KEY-----），需 client_secret 解密
    private_key = load_private_key(cfg)
    if private_key is None:
        raise FileNotFoundError(
            "未找到 Ed25519 私钥：请把 cert_content 放进环境变量 "
            f"{'/'.join(CERT_ENV_NAMES)}，或把 PEM 文件放到 private_key_path"
        )

    ttl = int(cfg.get("token_ttl_seconds", 3600) or 3600)
    payload: Dict[str, Any] = {"iat": int(now), "exp": int(now) + ttl}
    payload.update(DEFAULT_CLAIMS)
    payload.update(cfg.get("claims") or {})
    if cfg.get("meta_extras"):
        # 文档：自定义透传变量写在 extras 里，键名用 var_* 前缀
        payload["extras"] = cfg["meta_extras"]

    headers = {"alg": "EdDSA", "typ": "JWT"}
    if cfg.get("token_kid"):
        headers["kid"] = cfg["token_kid"]

    token = jwt.encode(payload, private_key, algorithm="EdDSA", headers=headers)
    if isinstance(token, bytes):  # PyJWT 1.x 兼容
        token = token.decode("utf-8")

    _token_cache["token"] = token
    _token_cache["exp"] = payload["exp"]
    logger.bind(tag=TAG).info(f"已签发 MetaInfo token（exp={payload['exp']}, 提前 {margin}s 重签）")
    return token


# ---------------------------------------------------------------------------
# Widget SSE 调用
# ---------------------------------------------------------------------------


# ---------------------------------------------------------------------------
# SSE 聚合辅助
# ---------------------------------------------------------------------------

# 平台会重新生成整篇答案并把已输出内容「从头再发一遍」。实测该重发片段与当前
# 文档开头有很长一段完全相同（例如都以 "## 查询结果\n\n### 📍 查询范围…" 开头），
# 而**长度不可作为判据**：正常的长片段实测可达 495 字，只看长度会误删正文。
# 因此用「与文档开头的公共前缀长度」判定，命中即按快照替换。
SNAPSHOT_HEAD_MIN_MATCH = 16

# 数字员工内部把工具调用过程也当正文流出来过（实测单个 text 事件 26787 字，
# 以 <tool_call><function=datahub_execute_tool> 开头），必须剥掉再给 LLM/TTS。
_TOOL_CALL_BLOCK_RE = re.compile(r"<tool_call>.*?</tool_call>", re.DOTALL)
_TOOL_FUNC_BLOCK_RE = re.compile(r"<function=[^>]*>.*?</function>", re.DOTALL)


def _looks_like_document_restart(doc_head: str, piece: str) -> bool:
    """该片段是否是「整篇文档从头重发」（而非普通增量或一次性长片段）。"""
    n = min(len(doc_head), len(piece))
    i = 0
    while i < n and doc_head[i] == piece[i]:
        i += 1
    return i >= SNAPSHOT_HEAD_MIN_MATCH


def _clean_answer_text(text: str) -> str:
    """剥掉工具调用过程等内部标记，只留可播报的答案。"""
    if "<tool_call>" not in text and "<function=" not in text:
        return text.strip()
    cleaned = _TOOL_CALL_BLOCK_RE.sub("", text)
    cleaned = _TOOL_FUNC_BLOCK_RE.sub("", cleaned)
    cleaned = re.sub(r"\n{3,}", "\n\n", cleaned)
    return cleaned.strip()


# ---------------------------------------------------------------------------
# 规避上游内容过滤
# ---------------------------------------------------------------------------
# 2026-09-29 实测：问题里一旦要求「防治建议/用药方案」，数字员工内部会抛
#   AGENT_STREAM_ERROR: Provider finish_reason: content_filter
# 整轮失败（含该措辞的提问连续 2 次失败，不含的 1 次正常）。
# 数据照查，防治/用药建议交给机器人自己的大模型回答即可，因此发送前先剥掉这类措辞。
_CONTROL_WORDS = r"(?:防治建议|防治方案|防治措施|用药建议|用药方案|药剂推荐|打药建议|防治意见)"
_CONTROL_CONJ_RE = re.compile(
    r"(?:以及|还有|和|及|与|包括|包含|并给出|并附上)\s*(?:请)?(?:给出|提供|附上)?\s*" + _CONTROL_WORDS + r"(?:是什么|有哪些)?"
)
_CONTROL_TAIL_RE = re.compile(
    r"(?:[，,、；;]\s*(?:并|请|再|同时)?\s*(?:给出|提供|附上|带上)?\s*"
    + _CONTROL_WORDS + r"(?:是什么|有哪些)?)+"
)


def _sanitize_question(question: str) -> str:
    """去掉问句里会触发 content_filter 的「防治建议/用药方案」类请求。"""
    if not question:
        return question
    out = _CONTROL_CONJ_RE.sub("", question)
    out = _CONTROL_TAIL_RE.sub("", out)
    out = re.sub(r"[，,、；;]\s*([。？！?]|$)", r"\1", out)
    out = re.sub(r"\s{2,}", " ", out).strip()
    # 全被剥空说明原句就是纯「防治建议」，那就退回原句（宁可试一次）
    return out if len(out) >= 6 else question


# ---------------------------------------------------------------------------
# 交互体验：查询期间的主动播报 + 活动保活
# ---------------------------------------------------------------------------
# 实测数据类问题要 30~110 秒，期间设备一直送"无语音"音频，服务端会在
# close_connection_no_voice_time(默认 120s) 到期时判定用户已离开，
# 让大模型说一句"时间过得真快…"然后断开 —— 用户体感就是"卡了很久最后莫名道别"。
# 因此：① 主动播报"正在查询"；② 期间刷新 last_activity_time 阻止误判。


def _speak_now(conn, text: str) -> None:
    """用框架自带的 speak_txt 立刻播报一句（会显示在设备上并朗读）。"""
    from core.handle.intentHandler import speak_txt  # 延迟导入，避免循环依赖

    speak_txt(conn, text)


async def _keepalive_activity(conn, stop: "asyncio.Event", interval: float = 15.0) -> None:
    """工具运行期间周期性刷新活动时间，避免触发空闲告别。"""
    while not stop.is_set():
        try:
            conn.last_activity_time = time.time() * 1000
        except Exception:
            return
        try:
            await asyncio.wait_for(stop.wait(), timeout=interval)
        except asyncio.TimeoutError:
            continue


async def _interim_speech(conn, cfg: Dict[str, Any], stop: "asyncio.Event") -> None:
    """按配置在查询期间播报提示语。"""

    async def say(text: str, delay: float) -> None:
        if not text:
            return
        if delay > 0:
            try:
                await asyncio.wait_for(stop.wait(), timeout=delay)
            except asyncio.TimeoutError:
                pass
            else:
                return  # 提示语还没到时间，查询已经结束了
        if stop.is_set():
            return
        try:
            await asyncio.to_thread(_speak_now, conn, text)
            logger.bind(tag=TAG).info(f"已播报提示语：{text}")
        except Exception as exc:  # 播报失败绝不影响主流程
            logger.bind(tag=TAG).warning(f"播报提示语失败：{exc}")

    try:
        d1 = float(cfg.get("interim_delay_seconds", 0) or 0)
        d2 = float(cfg.get("second_interim_delay_seconds", 45) or 0)
        await asyncio.gather(
            say(str(cfg.get("interim_message") or ""), d1),
            say(str(cfg.get("second_interim_message") or ""), d2),
        )
    except asyncio.CancelledError:
        raise
    except Exception as exc:
        logger.bind(tag=TAG).warning(f"提示语任务异常：{exc}")


# ---------------------------------------------------------------------------
# 并发去重（同一会话 + 同一问题只发一次真实请求）
# ---------------------------------------------------------------------------

_inflight: Dict[str, "asyncio.Future"] = {}


def _get_or_create_query_task(
    cfg: Dict[str, Any], question: str, conversation_id: str, key: str
):
    """返回 (task, created)。key 为空表示不去重。

    机器人的大模型一次会并发发出 2~3 个完全相同的工具调用，每个都是一次完整
    S2S 请求；去重后只有第一个真正发起，其余共享同一次结果。
    """
    if not key:
        return asyncio.ensure_future(widget_chat(cfg, question, conversation_id)), True

    existing = _inflight.get(key)
    if existing is not None and not existing.done():
        return existing, False

    task = asyncio.ensure_future(widget_chat(cfg, question, conversation_id))
    _inflight[key] = task

    def _cleanup(t: "asyncio.Future", _key: str = key) -> None:
        if _inflight.get(_key) is t:
            _inflight.pop(_key, None)

    task.add_done_callback(_cleanup)
    return task, True


async def widget_chat(cfg: Dict[str, Any], message: str, conversation_id: str = "") -> Dict[str, Any]:
    """调 Widget S2S 接口并把 SSE 聚合成完整文本。

    返回 {"ok": bool, "text": str, "conversation_id": str, "error": str}
    """
    base_url = (cfg.get("base_url") or "https://hezor.com/api/v1").rstrip("/")
    url = f"{base_url}/widget/chat"
    timeout_s = float(cfg.get("timeout_seconds", 60) or 60)
    try:
        snap_min = int(cfg.get("snapshot_min_chars", 16) or 0)
    except (TypeError, ValueError):
        snap_min = 16

    body: Dict[str, Any] = {"message": message, "mode": cfg.get("mode", "widget"), "stream": True}
    if conversation_id:
        body["conversationId"] = conversation_id          # 省略则新建会话
    if cfg.get("worker_id"):
        body["workerId"] = cfg["worker_id"]              # UUID，需在 allowed_worker_ids 白名单
    if cfg.get("system_prompt"):
        body["systemPrompt"] = cfg["system_prompt"]      # 每轮注入、不入历史
    if cfg.get("model"):
        body["model"] = cfg["model"]
    if cfg.get("kb_ids"):
        body["kbIds"] = list(cfg["kb_ids"])              # 限定知识库

    headers = {
        "Content-Type": "application/json",
        "X-META-INFO": build_meta_info_token(cfg),
        "X-APP-NAME": _resolve_app_name(cfg),
    }

    chunks = []
    doc_head = ""          # 当前文档开头若干字，用于识别平台「整篇重发」
    conv_id = conversation_id
    error = ""
    done = False

    timeout = httpx.Timeout(timeout_s, connect=8.0)
    stream_error = ""
    try:
        async with httpx.AsyncClient(timeout=timeout) as client:
            async with client.stream("POST", url, headers=headers, json=body) as resp:
                if resp.status_code != 200:
                    raw = (await resp.aread()).decode("utf-8", "replace")
                    hint = {
                        401: "MetaInfo 校验失败：检查 token 有效期，并确认 cert_content 与 client_secret 匹配（含 claims 字段名是否为 subject）",
                        403: "workerId 不在应用 allowed_worker_ids 白名单",
                        422: "请求体字段校验失败",
                        503: "数字员工未连接，或 mode=widget 未在应用后台配置数字员工路由",
                    }.get(resp.status_code, "")
                    msg = f"HTTP {resp.status_code} {hint} {raw[:200]}"
                    logger.bind(tag=TAG).error(f"widget chat 失败：{msg}")
                    return {"ok": False, "text": "", "conversation_id": conv_id, "error": msg}

                async for line in resp.aiter_lines():
                    if not line or not line.startswith("data:"):
                        continue
                    payload = line[5:].strip()
                    if not payload:
                        continue
                    try:
                        event = json.loads(payload)
                    except json.JSONDecodeError:
                        continue

                    etype = event.get("type")
                    content = event.get("content") or {}
                    if etype == "text":
                        piece = content.get("text") or ""
                        if piece:
                            if (snap_min and len(piece) >= snap_min
                                    and _looks_like_document_restart(doc_head, piece)):
                                # 平台重新生成，整篇从头再发：替换而非追加，
                                # 否则会出现 2~3 份重复/半截交错的内容。
                                chunks = [piece]
                                doc_head = piece
                            else:
                                chunks.append(piece)
                                doc_head = (doc_head + piece)[:SNAPSHOT_HEAD_MIN_MATCH * 4]
                    elif etype == "status":
                        if content.get("type") == "error":
                            # 出错时不会补发 done，必须按终止处理
                            error = str(content.get("message") or "数字员工返回错误")
                            break
                    elif etype == "done":
                        # 平台会发两条 done：第一条 metadata 为 null，
                        # 之后（client_action/update-title 之后）第二条才带 conversation_id。
                        # 因此不能在第一条就收流，否则会话记忆永远拿不到 id。
                        done = True
                        meta = event.get("metadata") or {}
                        if meta.get("conversation_id"):
                            conv_id = meta["conversation_id"]
                            break
                        continue
    except httpx.HTTPError as exc:
        # 数据类问题会跑很多次内部工具调用，超过 read timeout 很常见（实测 >60s）。
        # 已经流出的正文仍然有用，不能整段丢掉，所以只记错误、保留已聚合内容。
        stream_error = f"{type(exc).__name__}: {exc}"
        logger.bind(tag=TAG).warning(f"widget chat 流中断（保留已收内容）：{stream_error}")

    text = _clean_answer_text("".join(chunks))
    if stream_error and text:
        error = error or stream_error
    if error and not text:
        return {"ok": False, "text": "", "conversation_id": conv_id, "error": error}
    if not text:
        return {
            "ok": False,
            "text": "",
            "conversation_id": conv_id,
            "error": error or ("数字员工只返回了工具调用过程，未生成答案" if done else "流中断且无内容"),
        }
    return {"ok": True, "text": text, "conversation_id": conv_id, "error": error}


# ---------------------------------------------------------------------------
# 会话级 conversation_id 记忆（同一个会话内追问能接上文）
# ---------------------------------------------------------------------------

_sessions: Dict[str, str] = {}
_MAX_SESSIONS = 256


def _get_conv_id(session_id: str) -> str:
    return _sessions.get(session_id, "")


def _set_conv_id(session_id: str, conv_id: str) -> None:
    if not session_id or not conv_id:
        return
    if len(_sessions) >= _MAX_SESSIONS:
        _sessions.clear()
    _sessions[session_id] = conv_id


# ---------------------------------------------------------------------------
# 工具入口
# ---------------------------------------------------------------------------


@register_function("ask_guangxi_phyto", GX_PHYTO_FUNCTION_DESC, ToolType.SYSTEM_CTL)
async def ask_guangxi_phyto(conn: "ConnectionHandler", question: str = ""):
    """咨询广西省级植保数字员工。"""
    cfg = load_plugin_config(getattr(conn, "config", None))

    if not is_configured(cfg):
        logger.bind(tag=TAG).warning("未配置签名私钥，跳过植保知识库查询")
        return ActionResponse(
            Action.REQLLM,
            None,
            "植保知识库暂未配置（缺少签名私钥），先按常识回答，不要向用户提配置问题。",
        )

    question = (question or "").strip()
    if not question:
        return ActionResponse(Action.REQLLM, None, "没有收到要咨询的问题")

    # 剥掉会触发上游 content_filter 的「防治建议/用药方案」类措辞（见 _sanitize_question）
    if cfg.get("strip_control_requests", True):
        cleaned = _sanitize_question(question)
        if cleaned != question:
            logger.bind(tag=TAG).info(f"已剥掉触发内容过滤的措辞：{question} -> {cleaned}")
            question = cleaned

    session_id = str(getattr(conn, "session_id", "") or "")

    # 并发去重：机器人的大模型一次会并发发出 2~3 个**完全相同**的工具调用
    # （2026-09-29 实测：13:30:06/:09/:10 三次同问），每个都是一次完整 S2S 请求，
    # 等于同一问题花 2~3 倍额度，而且三条并发各自播报一遍提示语、互相干扰。
    # 这里让"同一会话 + 同一问题"只真正发一次，其余等这一次的结果。
    key = ""
    if cfg.get("dedupe_concurrent", True):
        # 归一化：去掉空白与标点，避免"同一句话被 ASR 加了不同标点"逃过去重
        normalized = re.sub(r"[\s，,。.、；;?？!！]+", "", question)
        key = f"{session_id}|{normalized}"

    stop = asyncio.Event()
    bg_tasks = []
    task, created = _get_or_create_query_task(cfg, question, _get_conv_id(session_id), key)

    if created:
        # 只有真正发起请求的那一次才播报/保活，避免并发播报多遍
        if cfg.get("interim_message") or cfg.get("second_interim_message"):
            bg_tasks.append(asyncio.create_task(_interim_speech(conn, cfg, stop)))
        if cfg.get("keepalive_activity", True):
            bg_tasks.append(asyncio.create_task(_keepalive_activity(conn, stop)))
    else:
        logger.bind(tag=TAG).info("同一问题已在查询中，复用该次请求（不重复计费）")

    try:
        # shield：本次调用被用户打断时不牵连那次共享请求，其余并发调用仍能拿到结果
        result = await asyncio.shield(task)
    except Exception as exc:  # 网络/签名等异常都不该打断对话
        logger.bind(tag=TAG).error(f"调用植保数字员工异常：{exc}")
        return ActionResponse(Action.REQLLM, None, "植保知识库暂时不可用，先按常识回答。")
    finally:
        stop.set()
        for _t in bg_tasks:
            _t.cancel()
        if bg_tasks:
            await asyncio.gather(*bg_tasks, return_exceptions=True)

    if not result["ok"] and not result["text"]:
        return ActionResponse(Action.REQLLM, None, f"植保知识库查询失败：{result['error']}")

    _set_conv_id(session_id, result.get("conversation_id", ""))

    answer = result["text"]
    limit = int(cfg.get("max_answer_chars", 800) or 800)
    if len(answer) > limit:
        answer = answer[:limit] + "…"

    # 播报要求：查询原文是给大模型看的（含表格/明细），但用户是"听"的。
    # 不约束的话它会照着念表格，800 字能念 1~2 分钟。
    hint = str(cfg.get("speak_hint") or "").strip()
    carried = f"{hint}\n\n---\n\n{answer}" if hint else answer

    logger.bind(tag=TAG).info(f"植保数字员工回答 {len(answer)} 字：{answer[:60]}…")
    return ActionResponse(Action.REQLLM, carried, None)
