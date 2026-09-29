#!/usr/bin/env python3
"""广西植保 S2S 工具自检 / 联调脚本

用法（在 xiaozhi-server 目录下运行）:
    python scripts/test_hezor_widget.py --check-config          # 只看配置是否齐备
    python scripts/test_hezor_widget.py --self-test             # 临时生成 Ed25519 密钥自检签名链路（预期 HTTP 401）
    python scripts/test_hezor_widget.py "稻飞虱怎么防治"          # 用 data/.config.yaml 配置真实调用并打印 SSE

说明:
  - 真实调用需要 private_key.pem（MetaInfo 签名私钥）与已配置的 app_name；
  - 脚本不会打印私钥或完整 token；
  - 退出码：0 成功，1 失败（方便 CI/脚本判断）。
"""

import argparse
import asyncio
import os
import sys

SERVER_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, SERVER_ROOT)

from plugins_func.functions.ask_guangxi_phyto import (  # noqa: E402
    build_meta_info_token,
    cert_source,
    is_configured,
    load_plugin_config,
    load_private_key,
    widget_chat,
)


def mask(token: str) -> str:
    return f"{token[:18]}…({len(token)} chars)" if token else "(empty)"


def check_config(cfg: dict) -> bool:
    print("插件配置 plugins.ask_guangxi_phyto:")
    for k in ("base_url", "app_name", "worker_id", "token_ttl_seconds",
              "token_refresh_margin_seconds", "private_key_path"):
        print(f"  {k:28} = {cfg.get(k)!r}")
    ok = is_configured(cfg)
    print(f"  {'私钥来源':28} = {cert_source(cfg)}")
    print(f"  {'有效载荷 claims':28} = {cfg.get('claims') or '(默认)'}")
    print("\n提示：还需在 Intent.function_call.functions 里加上 ask_guangxi_phyto 才会对 LLM 生效。")
    print("      S2S 调用不需要配域名白名单（那只作用于浏览器 iframe）。")
    return ok


def fingerprint(cfg: dict) -> int:
    """打印私钥公钥指纹与 JWT 头，用于和 Casdoor 侧核对（不打印私钥本身）。

    401 排查时最有用：管理员可比对指纹确认「发给你的 cert_content 是不是应用那一把」。
    """
    import hashlib

    import jwt
    from cryptography.hazmat.primitives import serialization

    if not is_configured(cfg):
        print("✗ 未配置私钥（先看 --check-config）")
        return 1

    key = load_private_key(cfg)
    pub_der = key.public_key().public_bytes(
        encoding=serialization.Encoding.DER,
        format=serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    fp = hashlib.sha256(pub_der).hexdigest()
    print(f"私钥类型      : {type(key).__name__}")
    print(f"公钥 SHA-256  : {':'.join(fp[i:i+2] for i in range(0, 32, 2))}…")
    print(f"私钥来源      : {cert_source(cfg)}")
    print(f"JWT Header    : {jwt.get_unverified_header(build_meta_info_token(cfg, force=True))}")
    print(f"Claims        : {cfg.get('claims') or '(默认: subject/subject_code/caller_id/creation_slug/creation_name)'}")
    print("\n若真实调用返回 401，请把上面的「公钥 SHA-256」发给 Hezor 管理员核对。")
    return 0


async def self_test(cfg: dict) -> int:
    """用临时密钥验证签名与请求链路（预期 401，证明请求形状正确）。"""
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ed25519
    import jwt

    tmp_dir = os.path.join(SERVER_ROOT, "tmp")
    os.makedirs(tmp_dir, exist_ok=True)
    key_path = os.path.join(tmp_dir, "_selftest_ed25519.pem")

    private_key = ed25519.Ed25519PrivateKey.generate()
    with open(key_path, "wb") as f:
        f.write(private_key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.PKCS8,
            encryption_algorithm=serialization.NoEncryption(),
        ))
    print(f"[1/4] 已生成临时 Ed25519 私钥：{os.path.relpath(key_path, SERVER_ROOT)}")

    test_cfg = dict(cfg)
    test_cfg["private_key_path"] = key_path
    test_cfg["private_key_password"] = ""

    token = build_meta_info_token(test_cfg, force=True)
    print(f"[2/4] 已签发 X-META-INFO：{mask(token)}")

    public_key = private_key.public_key()
    payload = jwt.decode(token, public_key, algorithms=["EdDSA"], options={"verify_aud": False})
    print(f"[3/4] 签名自验通过，claims = {payload}")

    print("[4/4] 调用 widget 接口（用临时密钥，预期 401 MetaInfo verification failed）…")
    result = await widget_chat(test_cfg, "自检消息：请忽略", "")
    print(f"      ok={result['ok']} error={result['error'] or '(none)'}")
    if "401" in result["error"] or "MetaInfo" in result["error"]:
        print("      ✓ 请求链路正确：服务端收到了合法形状的请求，只是签名不受信")
        return 0
    if "403" in result["error"]:
        # 配置了 worker_id 时，白名单校验可能先于签名校验返回 403 —— 同样证明请求已到达接口
        print("      ✓ 请求到达接口（403 为 workerId 白名单校验，与临时密钥无关）")
        return 0
    if result["ok"]:
        print("      ⚠️ 未预期的成功（临时密钥竟被接受？请与管理员确认）")
        return 0
    print("      ✗ 请求链路异常，请检查 base_url / 网络")
    return 1


async def real_call(cfg: dict, question: str) -> int:
    if not is_configured(cfg):
        print("✗ 私钥未就位，无法真实调用。先看 --check-config，或先跑 --self-test。")
        return 1

    token = build_meta_info_token(cfg, force=False)
    print(f"X-META-INFO: {mask(token)}")
    print(f"提问：{question}\n--- SSE 聚合结果 ---")
    result = await widget_chat(cfg, question, "")
    if result["ok"]:
        print(result["text"])
        print("---")
        print(f"✓ 成功，conversation_id={result['conversation_id'] or '(none)'}")
        return 0
    print(f"✗ 失败：{result['error']}")
    if result["text"]:
        print(f"（部分内容）{result['text']}")
    return 1


async def raw_stream(cfg: dict, question: str) -> int:
    """打印原始 SSE 事件（逐行），用于把服务端返回原样交给 Hezor 排查。"""
    import json as _json

    import httpx

    from plugins_func.functions.ask_guangxi_phyto import build_meta_info_token, _resolve_app_name

    base_url = (cfg.get("base_url") or "https://hezor.com/api/v1").rstrip("/")
    body = {"message": question, "mode": cfg.get("mode", "widget"), "stream": True}
    if cfg.get("worker_id"):
        body["workerId"] = cfg["worker_id"]
    headers = {
        "Content-Type": "application/json",
        "X-META-INFO": build_meta_info_token(cfg),
        "X-APP-NAME": _resolve_app_name(cfg),
    }
    print(f"POST {base_url}/widget/chat")
    print(f"X-APP-NAME: {headers['X-APP-NAME']}   body: {_json.dumps(body, ensure_ascii=False)}")
    timeout = httpx.Timeout(float(cfg.get("timeout_seconds", 60) or 60), connect=8.0)
    async with httpx.AsyncClient(timeout=timeout) as client:
        async with client.stream("POST", f"{base_url}/widget/chat", headers=headers, json=body) as resp:
            print(f"HTTP {resp.status_code}  Content-Type: {resp.headers.get('content-type')}")
            async for line in resp.aiter_lines():
                if line.strip():
                    print(f"  {line}")
    return 0


async def tool_test(cfg: dict) -> int:
    """模拟框架调用插件（假 conn），验证与 xiaozhi-server 的契约。

    走的是真实入口 ask_guangxi_phyto(conn, question=...)，检查返回的 ActionResponse：
      - 未配置私钥 → Action.REQLLM + response 提示（不抛异常、不影响对话）
      - 配置了私钥但服务端 401 → 同样返回 REQLLM + 错误说明
    """
    from types import SimpleNamespace

    import plugins_func.functions.ask_guangxi_phyto as plugin
    from plugins_func.register import Action

    conn = SimpleNamespace(config={"plugins": {"ask_guangxi_phyto": cfg}}, session_id="selftest-session")
    resp = await plugin.ask_guangxi_phyto(conn, question="稻飞虱怎么防治？")
    print(f"ActionResponse.action   = {resp.action}")
    print(f"ActionResponse.result   = {(resp.result or '')[:120]!r}")
    print(f"ActionResponse.response = {(resp.response or '')[:160]!r}")

    ok = resp.action == Action.REQLLM
    if not plugin.is_configured(cfg):
        expect = "知识库" in (resp.response or "") or "植保" in (resp.response or "")
        print("→ 未配置私钥时的降级路径:", "✓ 正常（返回提示、未抛异常）" if expect and ok else "✗ 异常")
        return 0 if (ok and expect) else 1

    if resp.result:
        print("→ 已配置私钥且拿到回答:", "✓")
        return 0
    print("→ 已配置私钥但调用失败（预期 401 直到凭证正确）:", (resp.response or "")[:120])
    return 0 if ok else 1


def main() -> int:
    parser = argparse.ArgumentParser(description="广西植保 S2S 工具自检/联调")
    parser.add_argument("question", nargs="?", default="", help="要咨询的问题")
    parser.add_argument("--check-config", action="store_true", help="只检查配置")
    parser.add_argument("--self-test", action="store_true", help="用临时密钥自检签名与请求链路")
    parser.add_argument("--fingerprint", action="store_true", help="打印公钥指纹/JWT 头（401 排查用）")
    parser.add_argument("--tool-test", action="store_true", help="模拟框架调用插件，验证 ActionResponse 契约")
    parser.add_argument("--raw", action="store_true", help="与问题一起使用时，打印原始 SSE 事件")
    args = parser.parse_args()

    cfg = load_plugin_config()
    if args.check_config:
        return 0 if check_config(cfg) else 1
    if args.raw:
        if not args.question:
            parser.error("--raw 需要同时给出问题文本")
        return asyncio.run(raw_stream(cfg, args.question))
    if args.fingerprint:
        return fingerprint(cfg)
    if args.tool_test:
        return asyncio.run(tool_test(cfg))
    if args.self_test:
        return asyncio.run(self_test(cfg))
    if not args.question:
        parser.error("请给出问题文本，或使用 --check-config / --fingerprint / --tool-test / --self-test")
    return asyncio.run(real_call(cfg, args.question))


if __name__ == "__main__":
    sys.exit(main())
