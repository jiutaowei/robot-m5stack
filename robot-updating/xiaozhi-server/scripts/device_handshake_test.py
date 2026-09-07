"""模拟 xiaozhi-esp32 设备：OTA 检查 + WebSocket hello 握手。"""
import asyncio
import json
import sys

import httpx
import websockets

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
OTA = f"http://{HOST}:8003/xiaozhi/ota/"
MAC = "11:22:33:44:55:66"


async def ota_check():
    print("[OTA] POST", OTA)
    headers = {
        "Device-Id": MAC,
        "Client-Id": "00000000-0000-0000-0000-000000000000",
        "Content-Type": "application/json",
    }
    body = {
        "firmware_version": "1.4.3",
        "chip_model_name": "esp32s3",
        "application": "xiaozhi",
        "mac_address": MAC,
    }
    async with httpx.AsyncClient(timeout=10) as c:
        r = await c.post(OTA, headers=headers, json=body)
    print("[OTA] HTTP", r.status_code)
    data = r.json()
    ws_url = data.get("websocket", {}).get("url")
    print("[OTA] websocket url ->", ws_url)
    print("[OTA] activation:", json.dumps(data.get("activation", {}), ensure_ascii=False)[:120])
    return ws_url


async def ws_hello(ws_url):
    ws_url = ws_url.replace("ws://你的ip或者域名:端口号", f"ws://{HOST}:8001")
    print("[WS] connect", ws_url)
    headers = {"Device-Id": MAC, "Client-Id": "00000000-0000-0000-0000-000000000000"}
    async with websockets.connect(ws_url, additional_headers=headers) as ws:
        hello = {
            "type": "hello",
            "version": 1,
            "transport": "websocket",
            "audio_params": {"format": "opus", "sample_rate": 16000,
                             "channels": 1, "frame_duration": 60},
        }
        await ws.send(json.dumps(hello))
        print("[WS] sent hello")
        resp = await asyncio.wait_for(ws.recv(), timeout=15)
        data = json.loads(resp)
        print("[WS] recv type:", data.get("type"), "| message:", str(data.get("message"))[:60])
        return data.get("type") == "hello"


async def main():
    ws_url = await ota_check()
    ok = await ws_hello(ws_url or f"ws://{HOST}:8001/xiaozhi/v1/")
    print("RESULT:", "HANDSHAKE_OK" if ok else "HANDSHAKE_FAIL")


asyncio.run(main())
