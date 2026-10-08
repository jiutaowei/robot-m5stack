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
# 接口地址也可用环境变量覆盖（vendor 的 .env 就是这个命名）——
# 便于把 base_url 指向本地模拟服务做联调，不消耗 Hezor 额度。
BASE_URL_ENV_NAMES = ("HEZOR2_API_BASE_URL", "HEZOR_API_BASE_URL")
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
    # 查询期间主动播报提示语（直接入队 TTS，不写对话历史，见 _speak_now）
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
    # ── 异步模式（2026-09-30）──────────────────────────────────────────
    # 查询要 1~2 分钟，"阻塞式"会把整轮对话占住：用户一插话这轮就作废，
    # 而且框架的工具超时也一直悬着。开启异步后：
    #   工具**立即返回**一句"正在查询" → 后台继续查 → 数据回来**主动播报**，
    #   期间用户还能继续问别的、机器人正常应答。
    # 设 False 可退回原来的阻塞式（数据直接交给大模型组织回答）。
    "async_mode": True,
    "async_ack_message": "正在为您查询数据，请稍等一下，查到后我马上告诉您。",
    "async_pending_message": "这个还在查，马上就好。",
    "async_prefix": "您问的植保数据回来了，",
    # 超过 late_announce_seconds 才返回时，换一句更体贴的开场（不要把"久等"归咎于用户）
    "async_late_prefix": "不好意思让您久等了，",
    # 数据回来后，等"用户没在说话 且 机器人当前这轮已说完"再插话；最多等这么多秒
    "async_speak_wait_seconds": 30,
    # 把最近一次查询的**完整返回**写到 xiaozhi-server/tmp/last_phyto_answer.md，
    # 便于排查"提取出来的播报不对"这类问题（不用再花额度复现）。tmp/ 已 gitignore。
    "debug_dump_answer": True,
    # 数字员工回了致歉（非数据）时，最多再重试几次（用新会话，避免上下文被污染）
    "retry_on_nonanswer": 1,
    # 播报要求：附在查询原文前面交给机器人的大模型，约束它"只播报要点、不念表格"。
    # 只影响"怎么念"，不影响查到的数据内容（明细仍完整保留在结果里）。
    # 设为空字符串可关闭。
    "speak_hint": DEFAULT_SPEAK_HINT,
    # ── 查询期间的进度播报（2026-10-08 用户体验优化）────────────────────
    # 数据查询实测 11~308 秒；只在开头播一句"正在查询"会让用户干等几分钟。
    # 这里按时间分级播报，让等待"有反馈"。文案刻意不带技术细节，到点后择机播。
    "progress_enabled": True,
    # 2026-10-08 实测：成功的查询 85s，卡死的查询能拖满 360s。等待语收敛成 3 句，
    # 最后一句是"终止式"（不再反复说"还在查"，避免用户听到一长串等待语）。
    "progress_messages": [
        {"at": 20, "text": "数据还在查，我盯着呢，稍等一下。"},
        {"at": 60, "text": "这份省级植保数据一般要一分钟左右，您稍等。"},
        {"at": 150, "text": "还在查，这次后台比较慢；一有结果我马上告诉您。"},
    ],
    "progress_min_gap_seconds": 15.0,      # 两条进度之间的最小间隔，防串话
    "progress_quiet_wait_seconds": 6.0,    # 每次播报前最多等这么久让用户说完
    # ── 有界等待 ──────────────────────────────────────────────────────
    # 单次查询的总时长上限（秒，0=不限）。超时则中止并给出可操作提示，
    # 避免"无限等待"。注意框架的 timeout_seconds 是**单次读超时**，对持续有
    # 流量的流式接口无效（实测跑到 308 秒都没触发），所以必须自己设总时限。
    "query_deadline_seconds": 360,
    # 超过这个时长才返回，播报时改口"不好意思久等了"
    "late_announce_seconds": 90,
    # ── 卡死早停 + 换会话重试（2026-10-08）──────────────────────────────
    # 实测平台会进"工具调用死循环"：一次 360 秒里 tool_call=22、thinking=10360、
    # **一个字都没吐**；而同一句话成功时 85 秒就出正文（7.4 秒就有输出）。
    # 所以判据是"长时间零输出"，不是"慢"：到点中止，并用**新会话**重试一次
    # （新会话大概率不进那个循环）。tool_call 次数爆掉也按卡死处理。
    "stall_no_output_seconds": 150,
    "stall_max_tool_calls": 25,          # 0 = 关闭该判据
    "retry_on_stall": 1,                 # 卡死后的重试次数
    # ── 短时结果缓存（2026-10-08）──────────────────────────────────────
    # 省级周报一周才更新一次；同一问题短时间内重复问很常见（实测一天问了 5 次同一句）。
    # 命中缓存直接播报：不查平台、不花额度、秒回。0 = 关闭。
    "result_cache_seconds": 600,
    "cache_hit_prefix": "刚才查过了，",
    "model": "",                                # 可选：指定模型，留空由服务端决策
    "kb_ids": [],                               # 可选：限定知识库 ID
}

GX_PHYTO_FUNCTION_DESC = {
    "type": "function",
    "function": {
        "name": "ask_guangxi_phyto",
        "description": (
            "查询「广西省级植保」数字员工的**省级病虫害测报数据**：发生面积、发生程度、"
            "虫情测报，也涵盖植保政策类问题。"
            "⚠️ 只要用户问的是「某地某病虫 近期/最新/最近一周/最近一个月/本周/第几期 "
            "发生情况、发生面积、发生程度、虫情、测报数据」，**必须优先调用本工具**，"
            "不要用 web_search 代替 —— 网上搜不到省级植保测报数据，搜出来的都是旧闻或"
            "泛泛而谈。只有用户明确要求「上网查/搜一下新闻」时才用 web_search。\n"
            "⏱️ 查询耗时与索要的字段数量强相关（实测：字段少 11~38 秒，字段多 48~64 秒）。"
            "因此请**只查用户实际问到的内容**：默认只要「发生面积 + 发生程度」，"
            "不要自行追加灯诱虫量、褐飞虱比例、分市/县明细、防治面积、防治效果等。\n"
            "🎯 命中率关键：省级数据实际来自《水稻病虫周报表》（按周填报），"
            "《五天一报表》《稻飞虱模式报表》经常整期未填报 —— 平台在空报表上反复试会拖到"
            "几分钟甚至卡住。所以**优先用「最新一期」表述**，例如"
            "「广西 稻飞虱 最新一期 发生面积和发生程度」；用户说了时段时才写「最近一周」。"
        ),
        "parameters": {
            "type": "object",
            "properties": {
                "question": {
                    "type": "string",
                    "description": (
                        "要查的数据，写清【病虫】+【地区】+【时段】三要素，例如"
                        "「广西 稻飞虱 最新一期 发生面积和发生程度」。"
                        "⚠️ ① 时段**优先写「最新一期」**（周报按周填报，写「最近一周」常落到"
                        "还没填报的时段而查不到）；用户明确说了时段才按用户说的写；"
                        "② 只写用户实际问到的要素，**不要补字段**（多一个字段多几十秒）；"
                        "③ **不要**在问题里要求「防治建议/防治方案/用药方案」——"
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
    if not merged.get("base_url"):
        merged["base_url"] = _first_env(BASE_URL_ENV_NAMES)
    env_base = _first_env(BASE_URL_ENV_NAMES)
    if env_base:
        # 环境变量优先：便于临时指向本地模拟服务
        merged["base_url"] = env_base

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
    """剥掉工具调用过程/数字员工工作过程，只留可播报的答案。

    2026-10-08 实测：平台把数字员工的**工作过程**也当正文流出来 —— 一次 84.9 秒的
    查询里，前 517 字全是「我来处理您的请求…加载查询规则…已定位到数据工具…」，
    真正的答案从 `## 查询结果` 才开始。这些过程叙述对用户毫无价值（念出来还很吵），
    而且会让"正文已开始"的判定误触发（进度播报因此被掐死），所以这里直接切掉。
    """
    if "<tool_call>" in text or "<function=" in text:
        text = _TOOL_CALL_BLOCK_RE.sub("", text)
        text = _TOOL_FUNC_BLOCK_RE.sub("", text)
        text = re.sub(r"\n{3,}", "\n\n", text)
    return _strip_agent_narration(text.strip())


# 答案正文的结构化开头：`## 标题` / `### 标题` / Markdown 表格行
# （不能用 `**`，因为过程叙述里也会出现 **2026-10-02 ~ 2026-10-08** 这种加粗）
ANSWER_START_RE = re.compile(r"(?m)^(?:#{2,3}\s|\|.+\|)")
_NARRATION_CUT_MIN_CHARS = 60      # 结构化标记出现在这个位置之后才认为是"过程叙述"


def _strip_agent_narration(text: str) -> str:
    """切掉答案开头之前的数字员工过程叙述；没有结构化答案头时原样返回。"""
    if not text:
        return text
    m = ANSWER_START_RE.search(text)
    if m and m.start() >= _NARRATION_CUT_MIN_CHARS:
        rest = text[m.start():].strip()
        if len(rest) >= 30:          # 防止把整段都是叙述的内容切空
            return rest
    return text


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
    """播报一句提示语，且**不写对话历史**。

    不能用框架的 speak_txt：它最后会执行 conn.dialogue.put(Message(role="assistant"))，
    而此刻 assistant(tool_calls) 对应的 tool 结果还没回填 —— 插入这条 assistant 消息会
    破坏 tool_calls → tool(result) 的配对，框架随后用
    {"status": "interrupted", "message": "动作已取消/被打断"} 顶替真实结果。
    实测：工具只跑了 7 秒就被判"被打断"，机器人回答"要我再查一次吗？"，
    30~100 秒的查询永远拿不到结果。

    这里照 core/handle/receiveAudioHandle.max_out_size 的做法直接入队 TTS，
    只影响"出声"，不碰对话历史。
    """
    from core.providers.tts.dto.dto import (  # 延迟导入，避免拉起整套 TTS 依赖
        ContentType,
        SentenceType,
        TTSMessageDTO,
    )

    conn.tts.store_tts_text(conn.sentence_id, text)
    conn.tts.tts_text_queue.put(
        TTSMessageDTO(
            sentence_id=conn.sentence_id,
            sentence_type=SentenceType.FIRST,
            content_type=ContentType.ACTION,
        )
    )
    conn.tts.tts_one_sentence(conn, ContentType.TEXT, content_detail=text)
    conn.tts.tts_text_queue.put(
        TTSMessageDTO(
            sentence_id=conn.sentence_id,
            sentence_type=SentenceType.LAST,
            content_type=ContentType.ACTION,
        )
    )
    # 刻意不调用 conn.dialogue.put(...)：见上面 docstring 说明


async def _keepalive_activity(conn, stop: "asyncio.Event", interval: float = 15.0) -> None:
    """工具运行期间周期性刷新活动时间，避免触发空闲告别。"""
    logger.bind(tag=TAG).info(f"活动保活已启动（每 {interval:.0f} 秒刷新一次）")
    ticks = 0
    while not stop.is_set():
        try:
            conn.last_activity_time = time.time() * 1000
            ticks += 1
        except Exception as exc:
            logger.bind(tag=TAG).warning(f"活动保活异常退出：{exc}")
            return
        try:
            await asyncio.wait_for(stop.wait(), timeout=interval)
        except asyncio.TimeoutError:
            logger.bind(tag=TAG).info(f"活动保活刷新第 {ticks} 次（last_activity_time 已更新）")
            continue
    logger.bind(tag=TAG).info(f"活动保活结束（共刷新 {ticks} 次）")


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
# 查询期间的进度播报 + 分阶段埋点（2026-10-08 用户体验优化）
# ---------------------------------------------------------------------------
# 依据 robot-updating/对话机器人等待体验最佳实践.md：
#   0.1s 确认收到 → 2s 先应答 → 10s 内给真实进度 → 超 10s 异步化 + 可打断
# 现状：植保数据查询实测 11~308 秒且方差极大，只在开头播一句"正在查询"，
#       用户会在几十秒到几分钟里完全不知道机器人还在不在干活。
# 做法：① 按时间里程碑分级播报（文案可配、到点择机播）；
#       ② 用平台真实事件（thinking/tool_call）埋点，便于事后统计 p50/p95
#          以及定位"哪种问法慢"；③ 总时长上限兜底，避免无限等待。


def _extract_tool_name(content: Dict[str, Any]) -> str:
    """从平台 tool_call 事件的 content 里尽量取出工具名（结构未文档化，做多键兜底）。

    只认"像工具名"的键：title/displayName/label 之类是给人看的文案，
    混进埋点会把工具名记成一句话，反而误导排查。
    """
    if not isinstance(content, dict):
        return ""
    for key in ("name", "toolName", "tool_name"):
        val = content.get(key)
        if isinstance(val, str) and val.strip():
            return val.strip()[:40]
    for key in ("tool", "function", "action"):
        inner = content.get(key)
        name = _extract_tool_name(inner) if isinstance(inner, dict) else ""
        if name:
            return name
    return ""


class _QueryState:
    """一次查询的实时状态：进度播报与埋点共用；只做赋值，不阻塞。

    ⚠️ 重试时**必须复用同一个对象**（调 begin_attempt()），不能新建：
    `_progress_loop` 持有的是本对象的引用，换对象会让进度播报读到过期的
    `first_text_at`（第一次尝试流过正文后就会一直 break，重试期间一句进度都不播）。
    计数器是**跨尝试累计**的，`first_text_at` 则只表示"当前尝试"。
    """

    def __init__(self, question: str = "") -> None:
        self.question = question
        self.started = time.time()
        self.first_text_at = 0.0
        self.first_text_at_abs = 0.0     # 绝对时间戳，供埋点在拿不到 stats 时回退
        self.answer_head = ""            # 累积开头若干字，用于判断"答案正文是否开始"
        self.attempts = 0
        self.text_events = 0
        self.thinking_events = 0
        self.tool_calls = 0
        self.tool_names: list = []
        self.steps: list = []
        self.announced = 0

    def begin_attempt(self) -> None:
        """开始一次（重）尝试：只重置"当前尝试"的时间轴，累计计数保留。"""
        self.attempts += 1
        self.started = time.time()
        self.first_text_at = 0.0
        self.answer_head = ""

    def answer_started(self) -> bool:
        """答案正文是否已经出现在流里（用于决定"还要不要插进度播报"）。

        ⚠️ 绝不能用"出现了 text 片段"当判据：平台会把数字员工的工作过程也当正文
        发出来（实测 84.9 秒那次前 517 字都是过程叙述），那样会在 7 秒就把进度
        播报掐死 —— 用户就只能在 ack 之后干等 80 多秒。
        """
        return bool(ANSWER_START_RE.search(self.answer_head))

    def on_event(self, event: Dict[str, Any]) -> None:
        """widget_chat 每收到一个 SSE 事件就回调这里：必须极快、绝不抛异常。"""
        try:
            etype = event.get("type")
            content = event.get("content") or {}
            if etype == "text":
                piece = content.get("text") if isinstance(content, dict) else None
                if not piece or not str(piece).strip():
                    return          # 空/纯空白 text 不算"首字"，否则进度播报会提前停
                self.text_events += 1
                if not self.first_text_at:
                    self.first_text_at = time.time()
                    self.first_text_at_abs = time.time()
                if len(self.answer_head) < 900:
                    self.answer_head += str(piece)
            elif etype == "thinking":
                self.thinking_events += 1
                title = content.get("stepTitle") if isinstance(content, dict) else None
                if title and title not in self.steps and len(self.steps) < 12:
                    self.steps.append(title)
            elif etype == "tool_call":
                self.tool_calls += 1
                name = _extract_tool_name(content)
                if name and name not in self.tool_names and len(self.tool_names) < 8:
                    self.tool_names.append(name)
        except Exception:
            pass

    def elapsed(self) -> float:
        return time.time() - self.started

    def first_text_seconds(self) -> float:
        """当前尝试的首字耗时（拿不到 widget_chat stats 时的回退）。"""
        return round(self.first_text_at_abs - self.started, 3) if self.first_text_at_abs else 0.0

    def summary(self) -> str:
        parts = [
            f"text片={self.text_events}",
            f"tool_call={self.tool_calls}",
            f"thinking={self.thinking_events}",
            f"进度播报={self.announced}次",
        ]
        if self.attempts > 1:
            parts.append(f"尝试内累计={self.attempts}次")
        if self.tool_names:
            parts.append(f"工具={self.tool_names}")
        if self.steps:
            parts.append(f"阶段={self.steps}")
        return " ".join(parts)


def _cfg_float(cfg: Dict[str, Any], key: str, default: float) -> float:
    """取浮点配置。

    ⚠️ 不能用 `float(cfg.get(k, d) or d)`：那样显式配成 0 会被 `or` 换成默认值
    （实测 `progress_min_gap_seconds: 0` 曾经永远变成 15）。
    """
    val = cfg.get(key, default)
    if val is None or val == "":
        return float(default)
    try:
        return float(val)
    except (TypeError, ValueError):
        return float(default)


def _progress_schedule(cfg: Dict[str, Any]) -> list:
    """把配置里的进度文案整理成按时间升序的合法列表。"""
    raw = cfg.get("progress_messages") or []
    out = []
    if isinstance(raw, (list, tuple)):
        for item in raw:
            if not isinstance(item, dict):
                continue
            try:
                at = float(item.get("at", 0) or 0)
            except (TypeError, ValueError):
                continue
            text = str(item.get("text") or "").strip()
            if text:
                out.append({"at": max(0.0, at), "text": text})
    out.sort(key=lambda x: x["at"])
    return out


# ---------------------------------------------------------------------------
# 同一会话的播报串行化（并发查询防"抢话"）
# ---------------------------------------------------------------------------
# 实测用户会连问两个问题（2026-10-08 10:15 桂林 + 广西），于是同一会话里同时有
# 多个查询在飞，每个都会"主动播报"：不串行化时两段语音会互相打断/交错；加上
# 进度播报后条目更多，更容易刷屏。这里的做法：
#   ① 同一会话的"开口"用一把 asyncio.Lock 串行化，保证不重叠；
#   ② 只让**最早的那一次**查询播报进度，后进的查询安静等到结果再播报。
_speak_locks: Dict[str, "asyncio.Lock"] = {}
_active_queries: Dict[str, int] = {}
_MAX_TRACKED_SESSIONS = 512


def _session_key(conn) -> str:
    return str(getattr(conn, "session_id", "") or id(conn))


def _acquire_query_slot(key: str) -> int:
    """登记一次在飞的查询，返回它是第几个（1 = 最早的）。"""
    if len(_speak_locks) > _MAX_TRACKED_SESSIONS:
        # 只回收"当前没有被占用"的锁，避免把正在用的锁清掉导致两段播报重叠
        for _k in [k for k, lk in _speak_locks.items() if not lk.locked()]:
            _speak_locks.pop(_k, None)
            if len(_speak_locks) <= _MAX_TRACKED_SESSIONS // 2:
                break
    _active_queries[key] = _active_queries.get(key, 0) + 1
    return _active_queries[key]


def _release_query_slot(key: str) -> None:
    left = _active_queries.get(key, 1) - 1
    if left <= 0:
        _active_queries.pop(key, None)
    else:
        _active_queries[key] = left


def _speak_lock(key: str) -> "asyncio.Lock":
    lock = _speak_locks.get(key)
    if lock is None:
        lock = asyncio.Lock()
        _speak_locks[key] = lock
    return lock


async def _announce_serialized(conn, key: str, text: str,
                               stop: "Optional[asyncio.Event]" = None,
                               timeout: float = 20.0) -> bool:
    """同一会话内串行播报，避免两段语音重叠。

    带超时：万一另一段播报卡在写 socket/等队列上，不能让本会话后续播报永远排不上
    （宁可少播一句，也不能把用户的后续问题全哑掉）。
    """
    async def _do() -> bool:
        async with _speak_lock(key):
            return await _announce(conn, text, stop=stop)

    try:
        return await asyncio.wait_for(_do(), timeout=max(1.0, float(timeout)))
    except asyncio.TimeoutError:
        logger.bind(tag=TAG).warning(
            f"等待播报锁超时，本次跳过播报（避免卡住后续播报）：{text[:40]}"
        )
        return False


async def _progress_loop(conn, cfg: Dict[str, Any], stop: "asyncio.Event", state: _QueryState,
                         key: str = "") -> None:
    """按时间里程碑播报进度；一旦**答案正文**（`## 查询结果`/表格）出现就停止。

    ⚠️ 判据是"结构化答案头出现了"（见 _QueryState.answer_started），不是"出现了 text"：
    平台会把数字员工的工作过程也当正文发（实测 84.9 秒那次 7.4 秒就开始了），
    用 text 做判据会让进度播报在 7 秒被掐死，用户干等 80 多秒 —— 这正是本轮要修的。

    与 keepalive 并行，由同一个 stop 事件结束；播报前等用户说完，
    且两条之间保持最小间隔，避免抢话。
    """
    schedule = _progress_schedule(cfg)
    if not schedule:
        return
    gap = _cfg_float(cfg, "progress_min_gap_seconds", 15.0)
    quiet_wait = _cfg_float(cfg, "progress_quiet_wait_seconds", 6.0)

    async def _sleep(seconds: float) -> None:
        """可被 stop 唤醒的等待（避免空转/丢里程碑）。"""
        try:
            await asyncio.wait_for(stop.wait(), timeout=max(0.05, seconds))
        except asyncio.TimeoutError:
            pass

    idx = 0
    last_said = 0.0
    while not stop.is_set() and idx < len(schedule):
        if not _conn_alive(conn):        # 连接没了就别再播报
            logger.bind(tag=TAG).info("连接已关闭，进度播报停止")
            break
        elapsed = state.elapsed()
        item = schedule[idx]
        if elapsed < item["at"]:
            await _sleep(min(1.0, item["at"] - elapsed))
            continue
        if state.answer_started():      # 答案正文已经开始返回，别再插进度
            break
        gap_left = gap - (time.time() - last_said)
        if gap_left > 0:
            # 最小间隔还没到：等一会儿再看，**不丢这条**（早期版本会静默跳过）
            await _sleep(min(gap_left, 1.0))
            continue
        if stop.is_set():
            break
        try:
            await _wait_until_quiet(conn, quiet_wait)
            if stop.is_set() or state.answer_started():
                break
            _clear_stale_abort(conn)
            if not await _announce_serialized(conn, key, item["text"], stop=stop):
                break                    # 已被结束：不再入队，避免排在答案之后
            state.announced += 1
            last_said = time.time()
            idx += 1                     # 只有真正播出去才前进
            logger.bind(tag=TAG).info(
                f"进度播报({state.elapsed():.0f}s)：{item['text']}｜{state.summary()}"
            )
        except asyncio.CancelledError:
            raise
        except Exception as exc:     # 播报失败绝不影响查询；也不要在同一条上死循环
            logger.bind(tag=TAG).warning(f"进度播报失败：{exc}")
            idx += 1
    if state.announced:
        logger.bind(tag=TAG).info(f"进度播报结束（共 {state.announced} 次）")


# ---------------------------------------------------------------------------
# 卡死看门狗 + 短时结果缓存（2026-10-08）
# ---------------------------------------------------------------------------


async def _stall_watchdog(state: _QueryState, stall_seconds: float, max_tool_calls: int = 0,
                          poll: float = 2.0) -> str:
    """判断"平台卡死了"：长时间**零正文输出**，或工具调用次数爆掉。

    实测（2026-10-08 同一句话两次）：
      成功：7.4 秒就出正文，85 秒返回（tool_call=18、thinking=1874）
      卡死：360 秒 text片=0（tool_call=22、thinking=10360）—— 平台在内部循环，永不产出
    因此判据是"零输出"，不是"慢"；有输出就一直等（实测最慢一次首字 77 秒）。
    """
    while True:
        await asyncio.sleep(poll)
        if state.first_text_at:
            return "alive"
        if max_tool_calls and state.tool_calls >= max_tool_calls:
            return "stall"
        if state.elapsed() >= stall_seconds:
            return "stall"


# 省级周报一周更新一次；同一问题短时间内重复问很常见（实测一天问了 5 次同一句）。
# 命中缓存直接播报：不查平台、不花额度、秒回。
_result_cache: Dict[str, tuple] = {}
_CACHE_MAX = 128


def _cache_key(question: str) -> str:
    """归一化问题：大小写无关地去空白/标点，并去掉与"广西"重复的"全区"。"""
    s = re.sub(r"[\s，,。.、；;?？!！:：]+", "", question or "")
    return s.replace("全区", "")


def _cache_get(question: str, cfg: Dict[str, Any]) -> Optional[Dict[str, Any]]:
    ttl = _cfg_float(cfg, "result_cache_seconds", 600.0)
    if ttl <= 0:
        return None
    key = _cache_key(question)
    item = _result_cache.get(key)
    if not item:
        return None
    ts, payload = item
    if time.time() - ts > ttl:
        _result_cache.pop(key, None)
        return None
    return payload


def _cache_put(question: str, result: Dict[str, Any], cfg: Dict[str, Any]) -> None:
    ttl = _cfg_float(cfg, "result_cache_seconds", 600.0)
    if ttl <= 0 or not (result or {}).get("text"):
        return
    if len(_result_cache) >= _CACHE_MAX:
        try:
            _result_cache.pop(next(iter(_result_cache)), None)   # 淘汰最旧
        except StopIteration:
            _result_cache.clear()
    _result_cache[_cache_key(question)] = (time.time(), {
        "text": result.get("text", ""),
        "conversation_id": result.get("conversation_id", ""),
    })


# ---------------------------------------------------------------------------
# 异步模式：立即应答 + 后台查询 + 数据回来主动播报
# ---------------------------------------------------------------------------


def _parse_tables(lines):
    """把 Markdown 里的所有表格解析成 [(header, rows), ...]。"""
    tables, i = [], 0
    while i < len(lines):
        cur = lines[i]
        nxt = lines[i + 1] if i + 1 < len(lines) else ""
        if cur.startswith("|") and cur.count("|") >= 3 and nxt.startswith("|"):
            sep = [c.strip() for c in nxt.strip("|").split("|")]
            if sep and all(c and set(c) <= set("-: ") for c in sep):
                header = [c.strip() for c in cur.strip("|").split("|")]
                rows, k = [], i + 2
                while k < len(lines) and lines[k].startswith("|") and lines[k].count("|") >= 3:
                    rows.append([c.strip() for c in lines[k].strip("|").split("|")])
                    k += 1
                if rows:
                    tables.append((header, rows))
                i = k
                continue
        i += 1
    return tables


def _num(text: str) -> float:
    try:
        return float(re.sub(r"[^\d.\-]", "", text) or 0)
    except ValueError:
        return 0.0


def _spoken_field(name: str, value: str) -> str:
    """把「当前发生面积(万亩) 155」改成适合朗读的「当前发生面积 155 万亩」。

    「发生程度（本周） 3 级」这种：括号里不是计量单位（没有 亩/头/级/粒/% 等），
    就把括号丢掉，读成「发生程度 3 级」。
    """
    name = (name or "").strip()
    value = (value or "").strip()
    m = re.match(r"^(.*?)[（(]([^）)]*)[）)]\s*$", name)
    if m:
        base, unit = m.group(1).strip(), m.group(2).strip()
        if base and unit and re.search(r"[亩头级粒%％万千克升]", unit) and unit not in value:
            return f"{base} {value} {unit}"
        if base:
            return f"{base} {value}"
    return f"{name} {value}"


# 播报时优先取的字段，以及要排除的（累计/防治/对比/预测类）
_PICK_WORDS = ("面积", "程度", "密度", "虫量", "比例", "卵量")
_SKIP_WORDS = ("累计", "防治", "比上年", "比上周", "新增", "仍需", "下周", "预测", "来源", "地区", "条数")


def _has_digit(s: str) -> bool:
    return bool(re.search(r"\d", s or ""))


# 数字员工偶尔会直接回一句致歉 —— 实测原话：
#   "抱歉，这个问题我这次没能给出满意的答案，要不你换个方式再问我一次？"
# 这不是数据，不能照念给用户听；应当重试一次。而"请问您要查哪个时段？"这类
# 澄清式反问属于有效回复，要原意转达。
# ⚠️ 别把裸的"抱歉"当判据：实测它还回过
#   "抱歉，广西壮族自治区最近一周（2026-10-02 至 2026-10-08）暂无稻飞虱监测数据覆盖。"
#   —— 那是有信息量的有效回答（明确告知该时段无数据），误判会把整句丢掉。
_APOLOGY_HINTS = ("没能给出满意的答案", "没法给出", "无法给出", "换个方式再问", "换一种方式再问")
_ASK_HINTS = ("请问", "哪个时段", "哪个市", "哪个县", "哪个地区", "请告知", "请提供", "需要哪个", "哪一周")


def _conn_alive(conn) -> bool:
    """连接是否还在（框架 close() 会 set stop_event）。

    注意：框架的 close() **不会**取消插件后台任务（任务与连接生命周期脱钩），
    所以这里只用来"别再往死连接播报"，不做任务回收。
    """
    ev = getattr(conn, "stop_event", None)
    if ev is None:
        return True
    try:
        return not ev.is_set()
    except Exception:
        return True


def _strip_md(text: str) -> str:
    """去掉播报文本里的 Markdown 符号（TTS 会把 ** 一起念出来或读成停顿）。"""
    s = re.sub(r"\*\*|__|`|~~", "", text or "")
    s = re.sub(r"(?m)^\s*#{1,6}\s*", "", s)
    s = re.sub(r"(?m)^\s*[-*+]\s+", "", s)
    s = re.sub(r"[ \t]{2,}", " ", s)
    return s.strip()


def _humanize_error(err: str) -> str:
    """把技术异常换成用户能听懂的话（不要把 ReadTimeout 念给用户）。"""
    low = (err or "").lower()
    if any(h in low for h in ("timeout", "timed out", "connecterror", "readerror",
                              "remoteprotocolerror", "networkerror", "connection")):
        return "和后台数据服务的连接超时了"
    if "余额" in err or "充值" in err:
        return "查询额度用完了，需要充值后继续"
    return err[:40]


def _is_non_answer(text: str) -> bool:
    t = (text or "").strip()
    if not t:
        return True
    if any(p in t for p in _ASK_HINTS):
        return False
    if any(p in t for p in _APOLOGY_HINTS):
        return True
    if len(t) < 40 and not _has_digit(t):
        return True
    return False


def _humanize_period(raw: str) -> str:
    """把 '2026-09-21~09-27' / '09~21~09~27' 这类时段改成适合朗读的中文。

    不改的话 TTS 会念成"零九波浪线二一…"，还会被切成单独的「27」分段。
    """
    s = (raw or "").strip()
    m = re.search(r"(\d{4})-(\d{1,2})-(\d{1,2})\s*[~～至\-—]\s*(?:(\d{4})-)?(\d{1,2})-(\d{1,2})", s)
    if m:
        _, mo1, d1, _, mo2, d2 = m.groups()
        return f"{int(mo1)}月{int(d1)}日至{int(mo2)}月{int(d2)}日"
    m = re.search(r"(\d{1,2})[~～](\d{1,2})[~～](\d{1,2})[~～](\d{1,2})", s)
    if m:
        mo1, d1, mo2, d2 = m.groups()
        return f"{int(mo1)}月{int(d1)}日至{int(mo2)}月{int(d2)}日"
    m = re.search(r"第\s*(\d+)\s*期", s)
    if m:
        return f"第{m.group(1)}期"
    return s


def _spoken_summary(text: str, cfg: Dict[str, Any]) -> str:
    """从返回的 Markdown 里取「最新一期」的关键数字，拼成可直接念的一两句。

    异步模式不再经过大模型，所以这里做**确定性提取**：稳定、快、不额外耗额度。
    取值策略（实测踩过的坑：分县明细表会排在汇总表后面，直接取最后一行会念出
    某个小县的 0.86 万亩）：
      1) 优先选**带时段/期次列**的表 → 那是按时间排的汇总表，取最后一行
      2) 否则选带面积列的表，取**面积最大**的一行（= 全区合计，而不是某个小县）
    """
    lines = [ln.strip() for ln in (text or "").splitlines() if ln.strip()]
    tables = _parse_tables(lines)

    # 「键：值」列表（有些返回没有表格，只有 "- **统计时段**：…" 这样的列表）
    kv = []
    for ln in lines:
        m = re.match(r"^[-*\s>]*\**\s*([^：:*]{2,24}?)\s*\**\s*[：:]\s*(.+?)\s*$", ln)
        if m:
            kv.append((m.group(1).strip(), m.group(2).strip()))

    period = ""
    for prefer in ("实际", "覆盖"):          # 「实际数据覆盖时段」比「请求时段」更准
        for k, v in kv:
            if "时段" in k and prefer in k:
                period = v
                break
        if period:
            break
    if not period:
        for k, v in kv:
            if "时段" in k or "期次" in k:
                period = v
                break

    picked = []

    # 形状一：表头里带字段名（行=各时段或各地区）
    for h, r in tables:
        if not any(w in c for c in h for w in _PICK_WORDS):
            continue
        area_idx = next((i for i, n in enumerate(h) if "面积" in n and "累计" not in n), None)
        has_period_col = any(("时段" in c or "期次" in c or "时间" in c) for c in h)
        if has_period_col:
            row = r[-1]                      # 按时间排序，最后一行即最新一期
        elif area_idx is not None:
            row = max(r, key=lambda x: _num(x[area_idx]) if area_idx < len(x) else 0)
        else:
            row = r[-1]
        for idx, name in enumerate(h):
            if idx >= len(row) or not row[idx] or row[idx] in ("—", "-", "–"):
                continue
            for want in ("时段", "期次", "面积", "程度", "密度"):
                if want not in name:
                    continue
                if want == "面积" and "累计" in name:
                    break
                if want in ("时段", "期次"):
                    period = period or row[idx]
                else:
                    picked.append(_spoken_field(name, row[idx]))
                break
            if len(picked) >= 3:
                break
        if picked:
            break

    # 形状二：两列「指标 / 数值」表 —— 字段名在**行**里
    # （实测桂林稻纵卷叶螟的返回就是 | 指标 | 数值 |，表头里一个字段名都没有）
    if not picked:
        for h, r in tables:
            for row in r:
                if len(row) < 2:
                    continue
                name, val = row[0].strip(), row[1].strip()
                if not name or not val or not _has_digit(val):
                    continue
                if any(w in name for w in _SKIP_WORDS):
                    continue
                if any(w in name for w in _PICK_WORDS):
                    picked.append(_spoken_field(name, val))
                if len(picked) >= 3:
                    break
            if picked:
                break

    # 形状三：没有表格时，从「键：值」列表里挑
    if not picked:
        for k, v in kv:
            if any(w in k for w in _SKIP_WORDS) or not _has_digit(v):
                continue
            if any(w in k for w in _PICK_WORDS):
                picked.append(_spoken_field(k, v))
            if len(picked) >= 3:
                break

    if picked:
        lead = f"最新一期 {_humanize_period(period)}：" if period else ""
        # 去掉 Markdown 符号：实测播报里出现过「**本周发生程度** 3级」这种，
        # TTS 会把星号一起念出来（或读成停顿），很怪。
        return _strip_md(lead + "，".join(picked)) + "。"

    # 一个字段都没认出来时：给一段可读的截断，而不是"查到了…的数据"这种废话
    plain = re.sub(r"[|#*>`_~📍📊\[\]（）()]+", " ", text or "")
    plain = re.sub(r"\s+", " ", plain).strip()
    if plain:
        return plain[:120] + ("…" if len(plain) > 120 else "")
    return "植保数据这次没有返回内容。"


async def _wait_until_quiet(conn, max_seconds: float) -> None:
    """等「用户没在说话 且 机器人当前这轮已说完」再插话；最多等 max_seconds 秒。"""
    deadline = time.time() + max(0.0, float(max_seconds or 0))
    while time.time() < deadline:
        busy = False
        try:
            if getattr(conn, "client_have_voice", False):
                busy = True
            # client_abort 为 True 时，TTS 线程会把新入队的句子直接丢掉
            # （core/providers/tts/base.py:372），所以要等它清掉
            if getattr(conn, "client_abort", False):
                busy = True
            tts = getattr(conn, "tts", None)
            if tts is not None and (
                tts.tts_text_queue.qsize() > 0 or tts.tts_audio_queue.qsize() > 0
            ):
                busy = True
        except Exception:
            busy = False
        if not busy:
            return
        await asyncio.sleep(1.0)


def _clear_stale_abort(conn) -> None:
    """播报前清掉上一轮遗留的打断标志。

    环境噪音会不断把 conn.client_abort 置 True；如果不清，主动播报的句子会被
    TTS 线程当成"被打断"丢弃 —— 实测 10:32 那次就是这样：入队成功但没出声。
    """
    try:
        if getattr(conn, "client_abort", False):
            conn.client_abort = False
            logger.bind(tag=TAG).info("已清掉遗留的打断标志，准备播报")
    except Exception:
        pass


async def _guard_ack(conn, seconds: float = 2.5, interval: float = 0.4) -> None:
    """工具刚返回时，"ack"由框架负责播报，但设备常在用户说完话后补发一条
    barge-in abort，正好把 ack 吞掉 —— 实测 11:35 那次：ack 合成成功
    （"语音生成成功"）却没有"发送音频消息"，设备气泡只停在 %ask_guangxi_phyto。

    这里在开头这几秒里反复清掉打断标志（只在开头，不影响之后真正的插话打断）。

    ⚠️ 用户正在说话（client_have_voice）时**不清**：那是真实的插话，清了就等于
    把用户的打断吞掉，机器人会继续说下去。
    """
    end = time.time() + max(0.0, seconds)
    while time.time() < end:
        if not getattr(conn, "client_have_voice", False):
            _clear_stale_abort(conn)
        await asyncio.sleep(interval)


async def _announce(conn, text: str, stop: "Optional[asyncio.Event]" = None) -> bool:
    """主动播报：先让设备进入"说话中"状态，再入队 TTS。返回是否真的发起了播报。

    ⚠️ 关键：设备必须先收到 {"type":"tts","state":"start"} 才会进入播放状态。
    框架的正常回答路径是 send_stt_message() 里发的（core/handle/sendAudioHandle.py:349
    及其注释"发送start消息后客户端状态会处于说话中状态"）。
    只往 tts_text_queue 塞音频的话，设备会收到音频包、气泡也显示文字，但**不发声** ——
    实测就是这样：ack 有声（走框架），主动播报没声（走这里）。

    stop：可选。已置位时**立刻放弃**，用于防止"进度句"排在最终答案之后才入队
    （查询结束与进度播报之间存在取消竞态，见 _stop_background 的说明）。
    """
    if stop is not None and stop.is_set():
        return False
    if not _conn_alive(conn):
        logger.bind(tag=TAG).info("连接已关闭，跳过本次播报")
        return False
    try:
        from core.handle.sendAudioHandle import send_tts_message

        await send_tts_message(conn, "start")
        # 立刻把文字推给设备气泡；否则气泡会一直残留上一句（如"请稍等一下"），
        # 要等第一段音频合成完才更新（实测用户看到的就是这个滞后）。
        await send_tts_message(conn, "sentence_start", text)
        conn.client_is_speaking = True
    except Exception as exc:  # 发不出去也要继续尝试播报
        logger.bind(tag=TAG).warning(f"发送 tts start 失败：{exc}")
    if stop is not None and stop.is_set():
        # 刚被结束：不再入队音频，避免"还在查…"出现在答案之后
        return False
    try:
        await asyncio.to_thread(_speak_now, conn, text)
    except Exception as exc:
        # _speak_now 可能因 TTS 未就绪/执行器关闭而抛错。这里必须兜住：
        # 抛到 _run_async_query 会被当成"查询异常"，把一次**成功**的查询播成
        # "植保数据查询出错了"，而且会跳过 [PHYTO-TIMING] 埋点。
        logger.bind(tag=TAG).warning(f"入队播报失败（不影响查询结果）：{exc}")
        return False
    if getattr(conn, "client_abort", False):
        # 设备可能在入队后立刻打断（例如用户插话）：此时音频会被 TTS 线程丢掉，
        # 记一笔方便排查"入队成功但没出声"。
        logger.bind(tag=TAG).info("播报入队后 client_abort 为真，本次可能被打断")
    return True


async def _run_async_query(conn, cfg, question: str, session_id: str, key: str) -> None:
    """后台完成查询，并在合适时机主动把结果说出来。

    用户体贴优化（2026-10-08）：
      ① 查询期间按时间里程碑播报进度（progress_messages），不再让用户干等；
      ② 设总时长上限（query_deadline_seconds），避免无限等待；
      ③ 等待超过 late_announce_seconds 时换一句更体贴的开场；
      ④ 每次查询输出一行 [PHYTO-TIMING] 埋点，便于统计 p50/p95 与定位慢查询。
    """
    overall_start = time.time()
    stop = asyncio.Event()
    bg_tasks = []
    state = _QueryState(question)
    sess_key = _session_key(conn)
    slot = _acquire_query_slot(sess_key)      # 1 = 本会话最早的这次查询
    if cfg.get("keepalive_activity", True):
        bg_tasks.append(asyncio.create_task(_keepalive_activity(conn, stop)))
    if cfg.get("progress_enabled", True):
        if slot == 1:
            bg_tasks.append(asyncio.create_task(_progress_loop(conn, cfg, stop, state, sess_key)))
        else:
            # 同一会话并发多个查询时，只让最早那次刷进度句，避免听感上全是"还在查"
            logger.bind(tag=TAG).info("本会话已有查询在播报进度，本次只播最终结果")

    try:
        deadline = _cfg_float(cfg, "query_deadline_seconds", 360.0)
    except Exception:
        deadline = 360.0
    late_after = _cfg_float(cfg, "late_announce_seconds", 90.0)
    # 总时限覆盖**所有重试**：否则 2 次重试最坏要等 2×360 秒，
    # 与"有界等待"的初衷相悖（overall_start 不在重试时重置）。
    hard_deadline = overall_start + deadline if deadline > 0 else 0.0

    async def _stop_background() -> None:
        """查询一结束就停掉进度播报，避免与最终播报抢话（可重复调用）。

        先置 stop 再给已在进行中的播报最多 1 秒收尾时间，然后才取消 —— 直接取消
        可能让正在执行的 `to_thread(_speak_now)` 把"还在查…"排到最终答案之后。
        """
        stop.set()
        if not bg_tasks:
            return
        try:
            await asyncio.wait(bg_tasks, timeout=1.0)
        except Exception:
            pass
        for _t in bg_tasks:
            if not _t.done():
                _t.cancel()
        await asyncio.gather(*bg_tasks, return_exceptions=True)

    # 这些在 try 之前先占位：finally 里的埋点会引用它们
    tries = 1
    used = 0
    usable = False
    cancelled = False
    failed = False
    result: Dict[str, Any] = {"ok": False, "text": "", "conversation_id": "",
                              "error": "", "stats": {}}

    try:
        try:
            retry_n = int(cfg.get("retry_on_nonanswer", 1) or 0)
        except (TypeError, ValueError):
            retry_n = 1          # 配置写错不该让整轮查询崩掉
        try:
            stall_n = int(cfg.get("retry_on_stall", 1) or 0)
        except (TypeError, ValueError):
            stall_n = 1
        stall_s = _cfg_float(cfg, "stall_no_output_seconds", 150.0)
        try:
            stall_calls = int(cfg.get("stall_max_tool_calls", 25) or 0)
        except (TypeError, ValueError):
            stall_calls = 0
        nonanswer_left = max(0, retry_n)
        stall_left = max(0, stall_n)
        tries = 1 + nonanswer_left + stall_left

        async def _call_once(conv: str, budget: float) -> Dict[str, Any]:
            """一次真实请求：与"卡死看门狗"竞速，谁先结束听谁的。

            ⚠️ 看门狗返回的是**判定结果**（alive/stall），不是"请求结束"：
            首字一到它就返回 alive —— 这时必须**继续等请求**，不能当成卡死
            （否则每次正常查询都会被误判，实测踩过这个坑）。
            """
            budget_deadline = (time.time() + budget) if budget > 0 else None
            call = asyncio.ensure_future(
                widget_chat(cfg, question, conv, on_event=state.on_event))
            watch = asyncio.ensure_future(_stall_watchdog(state, stall_s, stall_calls))

            async def _cancel_all() -> None:
                for _t in (call, watch):
                    if not _t.done():
                        _t.cancel()
                await asyncio.gather(call, watch, return_exceptions=True)

            def _timeout_result() -> Dict[str, Any]:
                return {"ok": False, "text": "", "conversation_id": "", "timed_out": True,
                        "error": f"查询超过 {deadline:.0f} 秒还没有返回", "stats": {}}

            try:
                while True:
                    remaining = (None if budget_deadline is None
                                 else max(0.0, budget_deadline - time.time()))
                    done, _pending = await asyncio.wait(
                        {call, watch}, timeout=remaining, return_when=asyncio.FIRST_COMPLETED)

                    if call in done:                      # 请求先结束 → 用它的结果
                        watch.cancel()
                        await asyncio.gather(watch, return_exceptions=True)
                        return call.result()

                    if watch in done:                     # 看门狗先给判定
                        verdict = watch.result()
                        if verdict == "stall":
                            await _cancel_all()
                            return {"ok": False, "text": "", "conversation_id": "",
                                    "stalled": True,
                                    "error": f"平台 {stall_s:.0f} 秒没有任何输出"
                                             f"（疑似工具调用死循环）",
                                    "stats": {}}
                        # alive：已有正文，撤掉看门狗，安心把预算用完
                        watch.cancel()
                        await asyncio.gather(watch, return_exceptions=True)
                        if budget_deadline is None:
                            return await call
                        rest = max(0.1, budget_deadline - time.time())
                        try:
                            return await asyncio.wait_for(call, timeout=rest)
                        except asyncio.TimeoutError:
                            await _cancel_all()
                            return _timeout_result()

                    await _cancel_all()                   # 预算用尽
                    return _timeout_result()
            except asyncio.CancelledError:
                await _cancel_all()
                raise

        attempt = 0
        first_attempt = True
        while True:
            attempt += 1
            conv = _get_conv_id(session_id) if first_attempt else ""   # 重试用新会话，别被污染
            first_attempt = False
            # 重试只重置"进度文案的时间轴"（复用同一个 state 对象，见 _QueryState 说明），
            # 总时限不重置（覆盖所有重试）
            state.begin_attempt()
            budget = (hard_deadline - time.time()) if hard_deadline else 0.0
            if hard_deadline and budget <= 1.0:
                result = {
                    "ok": False, "text": "", "conversation_id": "",
                    "error": f"查询超过 {deadline:.0f} 秒还没有返回",
                    "timed_out": True, "stats": {},
                }
                logger.bind(tag=TAG).warning(
                    f"总时限 {deadline:.0f} 秒已到，不再重试｜{state.summary()}"
                )
                break
            used += 1            # 只有真正发出请求才计数（否则会出现"只发 1 次却报 2/2"）
            result = await _call_once(conv, budget)
            _set_conv_id(session_id, result.get("conversation_id", ""))

            # ① 卡死：长时间零输出 → 换新会话重试（新会话大概率不进那个内部循环）
            if result.get("stalled") and stall_left > 0:
                stall_left -= 1
                _set_conv_id(session_id, "")     # 丢掉这次会话，下次不带 conversationId
                logger.bind(tag=TAG).warning(
                    f"判为平台卡死（{stall_s:.0f}s 零输出，tool_call={state.tool_calls}、"
                    f"thinking={state.thinking_events}），换新会话重试｜{state.summary()}"
                )
                continue
            if result.get("stalled"):
                logger.bind(tag=TAG).error(f"平台卡死且不再重试｜{state.summary()}")
                break

            usable = (
                result.get("ok")
                and result.get("text")
                and not _is_non_answer(result["text"])
            )
            if usable:
                break
            # ② 硬错误（余额不足 / HTTP 错误 / 超时）重试毫无意义，只会白烧一次额度
            if result.get("error"):
                logger.bind(tag=TAG).warning(
                    f"查询硬错误，不再重试：{(result.get('error') or '')[:40]}"
                )
                break
            # ③ 软失败（数字员工回了致歉/反问）→ 用新会话再试一次
            if nonanswer_left > 0:
                nonanswer_left -= 1
                logger.bind(tag=TAG).info(
                    f"数字员工没给出有效答案，重试（剩余 {nonanswer_left} 次）："
                    f"{(result.get('text') or '')[:40]}"
                )
                continue
            break

        await _stop_background()      # 先停进度播报，再播最终结果

        if cfg.get("debug_dump_answer", True) and result.get("text"):
            try:  # 落盘最近一次返回，便于排查播报提取问题（失败不影响主流程）
                dump_path = os.path.join(SERVER_ROOT, "tmp", "last_phyto_answer.md")
                with open(dump_path, "w", encoding="utf-8") as fh:
                    fh.write(f"<!-- question: {question} -->\n\n{result['text']}")
            except Exception:
                pass

        # 成功结果进短时缓存：同一问题短时间内再问 → 直接播报，不再查平台
        if usable:
            _cache_put(question, result, cfg)

        wait_s = _cfg_float(cfg, "async_speak_wait_seconds", 30.0)
        waited = time.time() - overall_start
        prefix = str(cfg.get("async_prefix") or "").strip()
        if late_after > 0 and waited >= late_after:
            prefix = str(cfg.get("async_late_prefix") or prefix).strip()

        usable = (
            result.get("ok")
            and result.get("text")
            and not _is_non_answer(result["text"])
        )
        if usable:
            body = _spoken_summary(result["text"], cfg)
            say = f"{prefix}{body}" if prefix else body
            await _wait_until_quiet(conn, wait_s)
            _clear_stale_abort(conn)
            await _announce_serialized(conn, sess_key, say)
            if getattr(conn, "client_abort", False):
                logger.bind(tag=TAG).warning("播报后 client_abort 又为真，本次播报可能未出声")
            logger.bind(tag=TAG).info(f"异步播报：{say[:100]}")
        else:
            reason = (result.get("error") or result.get("text") or "没查到").strip()[:60]
            err = (result.get("error") or "").strip()
            if result.get("stalled"):
                # 平台内部死循环：说清是"后台卡住"，别让用户以为是自己问错了
                say = ("不好意思，后台数据服务这次卡住了，没能返回结果。"
                       "过一会儿再问一次，或者换个市试试。")
            elif result.get("timed_out"):
                # 超时不要说"查询超过 360 秒还没有返回"这种技术话术，
                # 给一句体贴且**可操作**的收尾（并明确不是用户的问题）
                say = ("不好意思，这次查得太久了，我先不等了。"
                       "您可以问窄一点，比如只问某个市最近一周的发生面积。")
            elif err:
                # 真实错误（余额不足 / 网络超时 / HTTP xxx）要直接说出来，
                # 否则用户会以为是自己的问法不对（实测就是这样被误导的）。
                # 但不要把 ReadTimeout 这种异常名念给用户听。
                say = f"{prefix}植保数据这次没查到：{_humanize_error(err)}。"
            else:
                # 省级周报是"按周填报"的：问"最近一周"时常碰到最新一期还没填报的时段，
                # 平台会回"暂无数据覆盖"。这里引导到真正有数据的口径（最新一期 / 单市）。
                say = (f"{prefix}这次没查到这一期的数据。"
                       "您可以改问「最新一期」，或者只问某个市最近一周的发生面积。")
            await _wait_until_quiet(conn, min(wait_s, 10.0))
            _clear_stale_abort(conn)
            await _announce_serialized(conn, sess_key, say)
            logger.bind(tag=TAG).error(f"异步查询未获得有效数据：{reason}｜最终播报：{say}")
    except asyncio.CancelledError:
        cancelled = True
        logger.bind(tag=TAG).info("异步查询已取消（连接结束或会话关闭）")
        try:
            await _stop_background()
        except BaseException:
            pass
        raise
    except Exception as exc:
        failed = True
        logger.bind(tag=TAG).error(f"异步查询异常：{exc}")
        try:
            await _stop_background()          # 先停进度播报，别和报错播报抢话
            await _wait_until_quiet(conn, 5.0)
            _clear_stale_abort(conn)
            await _announce_serialized(conn, sess_key, "植保数据查询出错了，稍后再试。")
        except Exception:
            pass
    finally:
        _release_query_slot(sess_key)         # 先释放（同步、不会被打断）再 await
        # ── 埋点放在 finally：取消/异常路径也要有样本，否则最慢、最失败的案例
        #    会从 p50/p95 里消失（幸存者偏差）──
        try:
            st = result.get("stats") or {}
            iface_s = st.get("total_seconds") or round(time.time() - overall_start, 1)
            first_s = st.get("first_text_seconds") or state.first_text_seconds()
            outcome = "ok" if usable else (
                "卡死" if result.get("stalled") else
                "取消" if cancelled else
                "异常" if failed else "fail")
            logger.bind(tag=TAG).info(
                "[PHYTO-TIMING] "
                f"问题={question} | 接口={iface_s}s"
                f"(响应头{st.get('connect_seconds', 0)}s 首字{first_s}s) | "
                f"总等待={time.time() - overall_start:.1f}s | 尝试={used}/{tries} | "
                f"{state.summary()} | 结果={outcome}"
            )
        except Exception:
            pass
        try:
            await _stop_background()
        except BaseException:
            pass


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


async def widget_chat(cfg: Dict[str, Any], message: str, conversation_id: str = "",
                      on_event: "Optional[Any]" = None) -> Dict[str, Any]:
    """调 Widget S2S 接口并把 SSE 聚合成完整文本。

    返回 {"ok": bool, "text": str, "conversation_id": str, "error": str, "stats": dict}

    on_event: 可选回调，每收到一个 SSE 事件调用一次（进度播报/埋点用）。
              回调必须极快且不抛异常；这里的调用也包了 try/except，
              保证"埋点与进度"永远不会影响主流程。
    """
    base_url = (cfg.get("base_url") or "https://hezor.com/api/v1").rstrip("/")
    url = f"{base_url}/widget/chat"
    timeout_s = float(cfg.get("timeout_seconds", 60) or 60)

    t_start = time.perf_counter()
    stats: Dict[str, Any] = {
        "connect_seconds": 0.0, "first_text_seconds": 0.0, "total_seconds": 0.0,
        "text_events": 0, "thinking_events": 0, "tool_calls": 0,
        "tool_names": [], "steps": [], "http_status": 0,
    }

    def _emit(event: Dict[str, Any]) -> None:
        if on_event is None:
            return
        try:
            on_event(event)
        except Exception:
            pass

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
                stats["connect_seconds"] = round(time.perf_counter() - t_start, 3)
                stats["http_status"] = resp.status_code
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
                    stats["total_seconds"] = round(time.perf_counter() - t_start, 3)
                    return {"ok": False, "text": "", "conversation_id": conv_id,
                            "error": msg, "stats": stats}

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
                    _emit(event)
                    if etype == "text":
                        # 只统计**非空** text：空片段既不该算首字，也不该算片段数
                        if (content.get("text") or "") if isinstance(content, dict) else False:
                            stats["text_events"] += 1
                            if not stats["first_text_seconds"]:
                                stats["first_text_seconds"] = round(time.perf_counter() - t_start, 3)
                    elif etype == "thinking":
                        stats["thinking_events"] += 1
                        title = content.get("stepTitle") if isinstance(content, dict) else None
                        if title and title not in stats["steps"] and len(stats["steps"]) < 12:
                            stats["steps"].append(title)
                    elif etype == "tool_call":
                        stats["tool_calls"] += 1
                        name = _extract_tool_name(content)
                        if name and name not in stats["tool_names"] and len(stats["tool_names"]) < 8:
                            stats["tool_names"].append(name)
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

    stats["total_seconds"] = round(time.perf_counter() - t_start, 3)
    text = _clean_answer_text("".join(chunks))
    if stream_error and text:
        error = error or stream_error
    if error and not text:
        return {"ok": False, "text": "", "conversation_id": conv_id, "error": error,
                "stats": stats}
    if not text:
        return {
            "ok": False,
            "text": "",
            "conversation_id": conv_id,
            "error": error or ("数字员工只返回了工具调用过程，未生成答案" if done else "流中断且无内容"),
            "stats": stats,
        }
    return {"ok": True, "text": text, "conversation_id": conv_id, "error": error,
            "stats": stats}


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
    if len(_sessions) >= _MAX_SESSIONS and session_id not in _sessions:
        # 淘汰最早写入的一个，而不是整体 clear（整体 clear 会让**所有**会话
        # 同时丢掉 conversationId，追问就接不上上下文了）
        try:
            _sessions.pop(next(iter(_sessions)), None)
        except StopIteration:
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

    # 埋点：查询耗时与字段数量强相关（实测字段少 11~38s、字段多 48~64s）。
    # 工具描述已要求"只查用户实际问到的内容"，这里把仍然被加进来的扩展字段记下来，
    # 用来判断提示词有没有生效、以及哪种问法慢。
    extra_fields = [k for k in ("灯诱", "褐飞虱比例", "分县", "各县区", "分市", "明细",
                                "累计防治", "防治效果", "周报")
                    if k in question]
    if extra_fields:
        logger.bind(tag=TAG).info(f"查询含扩展字段（耗时可能更长）：{extra_fields}｜{question}")

    # 并发去重：机器人的大模型一次会并发发出 2~3 个**完全相同**的工具调用
    # （2026-09-29 实测：13:30:06/:09/:10 三次同问），每个都是一次完整 S2S 请求，
    # 等于同一问题花 2~3 倍额度，而且三条并发各自播报一遍提示语、互相干扰。
    # 这里让"同一会话 + 同一问题"只真正发一次，其余等这一次的结果。
    key = ""
    if cfg.get("dedupe_concurrent", True):
        # 归一化：去掉空白与标点，避免"同一句话被 ASR 加了不同标点"逃过去重
        normalized = re.sub(r"[\s，,。.、；;?？!！]+", "", question)
        key = f"{session_id}|{normalized}"

    # ── 短时缓存：同一问题短时间内再问 → 直接播报（不查平台、不花额度、秒回）──
    # 实测用户会把同一句问 5 次；省级周报一周才更新，10 分钟内复用没有信息损失。
    cached_hit = _cache_get(question, cfg)
    if cached_hit:
        body = _spoken_summary(cached_hit.get("text", ""), cfg)
        hit_prefix = str(cfg.get("cache_hit_prefix") or "").strip()
        say = f"{hit_prefix}{body}" if hit_prefix else body
        logger.bind(tag=TAG).info(
            f"命中 {_cfg_float(cfg, 'result_cache_seconds', 600.0):.0f}s 结果缓存，直接播报：{say[:60]}"
        )
        return ActionResponse(Action.RESPONSE, None, say)

    # ── 异步模式：立刻回一句，后台查，查到主动说 ────────────────────────
    # 好处：查询期间用户能继续聊别的；插话不再作废这一轮；
    #       也不再有"框架工具超时"这条悬着的线。
    if cfg.get("async_mode", True):
        running = _inflight.get(key) if key else None
        if running is not None and not running.done():
            logger.bind(tag=TAG).info("同一问题仍在查询中，回复'稍等'提示")
            return ActionResponse(
                Action.RESPONSE,
                None,
                str(cfg.get("async_pending_message") or "这个还在查，马上就好。"),
            )
        task = asyncio.ensure_future(_run_async_query(conn, cfg, question, session_id, key))
        if key:
            _inflight[key] = task
            task.add_done_callback(
                lambda t, _k=key: _inflight.pop(_k, None) if _inflight.get(_k) is t else None
            )
        # ack 由框架播报；开头几秒守住打断标志，别让设备补发的 barge-in abort 把它吞掉
        asyncio.create_task(_guard_ack(conn))
        logger.bind(tag=TAG).info(f"异步查询已启动：{question}")
        return ActionResponse(
            Action.RESPONSE,
            None,
            str(cfg.get("async_ack_message") or "正在为您查询数据，请稍等一下，查到后我马上告诉您。"),
        )

    stop = asyncio.Event()
    bg_tasks = []
    task, created = _get_or_create_query_task(cfg, question, _get_conv_id(session_id), key)

    if created:
        # 只有真正发起请求的那一次才播报/保活，避免并发播报多遍
        # ⚠️ 阻塞模式（async_mode: false）**不用**分级进度播报：框架对工具调用只做
        #    future.result(timeout=tool_call_timeout)（实测有效值 180s）且**不取消**协程，
        #    超时后它会先播"工具调用超时"，我们再补进度句就自相矛盾了。这里沿用原来的
        #    单句/两句提示（interim_message / second_interim_message）。
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

    _st = result.get("stats") or {}
    logger.bind(tag=TAG).info(
        f"[PHYTO-TIMING] 问题={question} | 模式=blocking | 接口={_st.get('total_seconds', 0)}s"
        f"(响应头{_st.get('connect_seconds', 0)}s 首字{_st.get('first_text_seconds', 0)}s) | "
        f"text片={_st.get('text_events', 0)} tool_call={_st.get('tool_calls', 0)} "
        f"thinking={_st.get('thinking_events', 0)} | 结果={'ok' if result.get('text') else 'fail'}"
    )

    if not result["ok"] and not result["text"]:
        # 必须打日志：这条分支以前是静默的，导致"工具瞬间返回却没有结果"难以定位
        logger.bind(tag=TAG).error(f"植保知识库查询失败：{result['error']}")
        return ActionResponse(Action.REQLLM, None, f"植保知识库查询失败：{result['error']}")

    _set_conv_id(session_id, result.get("conversation_id", ""))

    answer = result["text"]
    try:
        limit = int(cfg.get("max_answer_chars", 800) or 800)
    except (TypeError, ValueError):
        limit = 800
    if len(answer) > limit:
        answer = answer[:limit] + "…"

    # 播报要求：查询原文是给大模型看的（含表格/明细），但用户是"听"的。
    # 不约束的话它会照着念表格，800 字能念 1~2 分钟。
    hint = str(cfg.get("speak_hint") or "").strip()
    carried = f"{hint}\n\n---\n\n{answer}" if hint else answer

    logger.bind(tag=TAG).info(f"植保数字员工回答 {len(answer)} 字：{answer[:60]}…")
    return ActionResponse(Action.REQLLM, carried, None)
