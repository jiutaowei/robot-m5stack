"""百炼三件套真实调用验证（key 从 data/.config.yaml 读取，不回显）。"""
import asyncio
import json
import ssl
import sys

import websockets
import yaml

cfg = yaml.safe_load(open("data/.config.yaml", encoding="utf-8"))
key = cfg["LLM"]["AliyunQwenLLM"]["api_key"]
base_url = cfg["LLM"]["AliyunQwenLLM"]["base_url"]
asr_ws_url = cfg["ASR"]["AliyunBLStreamASR"].get("ws_url", "wss://dashscope.aliyuncs.com/api-ws/v1/inference")
tts_ws_url = cfg["TTS"]["AliBLTTS"].get("ws_url", asr_ws_url)
# 与运行时 provider（aliyunbl_stream.py / alibl_stream.py）一致：WebSocket 握手带 Bearer 鉴权
ws_headers = {"Authorization": f"Bearer {key}"}

async def test_llm():
    import httpx
    print("[LLM] POST", base_url + "/chat/completions", "model=qwen-plus")
    async with httpx.AsyncClient(timeout=30) as c:
        r = await c.post(
            base_url + "/chat/completions",
            headers={"Authorization": f"Bearer {key}"},
            json={"model": "qwen-plus", "max_tokens": 40,
                  "messages": [{"role": "user", "content": "用一句话自我介绍你的农业专家身份"}]},
        )
    print("[LLM] HTTP", r.status_code)
    body = r.json()
    if r.status_code == 200:
        print("[LLM] reply:", body["choices"][0]["message"]["content"][:80])
        return True
    print("[LLM] error:", json.dumps(body, ensure_ascii=False)[:200])
    return False

async def test_asr():
    ws_url = asr_ws_url
    print("[ASR] connect", ws_url)
    try:
        async with websockets.connect(ws_url, ssl=ssl.create_default_context(), additional_headers=ws_headers) as ws:
            run_task = {
                "header": {"action": "run-task", "task_id": "t1", "streaming": "duplex"},
                "payload": {"task_group": "audio", "task": "asr", "function": "recognition",
                            "model": "paraformer-realtime-v2",
                            "parameters": {"format": "pcm", "sample_rate": 16000},
                            "input": {}},
            }
            await ws.send(json.dumps(run_task))
            # 发送 0.1s 静音
            await ws.send(b"\x00" * 3200)
            await ws.send(json.dumps({"header": {"action": "finish-task", "task_id": "t1", "streaming": "duplex"}, "payload": {"input": {}}}))
            async for msg in ws:
                d = json.loads(msg) if isinstance(msg, str) else None
                if d:
                    ev = d.get("header", {}).get("event")
                    print("[ASR] event:", ev, d.get("header", {}).get("error_message", ""))
                    if ev == "task-finished":
                        return True
                    if ev == "task-failed":
                        return False
    except Exception as e:
        print("[ASR] exception:", e)
        return False

async def test_tts():
    print("[TTS] connect", tts_ws_url)
    try:
        async with websockets.connect(tts_ws_url, ssl=ssl.create_default_context(), additional_headers=ws_headers) as ws:
            run_task = {
                "header": {"action": "run-task", "task_id": "t2", "streaming": "duplex"},
                "payload": {"task_group": "audio", "task": "tts", "function": "SpeechSynthesizer",
                            "model": "cosyvoice-v2",
                            "parameters": {"text_type": "PlainText", "voice": "longcheng_v2",
                                           "format": "pcm", "sample_rate": 16000},
                            "input": {}},
            }
            await ws.send(json.dumps(run_task))
            text_msg = {"header": {"action": "continue-task", "task_id": "t2", "streaming": "duplex"},
                        "payload": {"input": {"text": "你好"}}}
            await ws.send(json.dumps(text_msg))
            await ws.send(json.dumps({"header": {"action": "finish-task", "task_id": "t2", "streaming": "duplex"}, "payload": {"input": {}}}))
            got_audio, failed = False, False
            async for msg in ws:
                if isinstance(msg, bytes):
                    got_audio = True
                    continue
                d = json.loads(msg)
                ev = d.get("header", {}).get("event")
                print("[TTS] event:", ev, d.get("header", {}).get("error_message", ""))
                if ev == "task-finished":
                    print("[TTS] audio bytes:", got_audio)
                    return got_audio
                if ev == "task-failed":
                    return False
    except Exception as e:
        print("[TTS] exception:", e)
        return False

async def main():
    ok_llm = await test_llm()
    ok_asr = await test_asr()
    ok_tts = await test_tts()
    print(f"RESULT: LLM={'OK' if ok_llm else 'FAIL'} ASR={'OK' if ok_asr else 'FAIL'} TTS={'OK' if ok_tts else 'FAIL'}")

asyncio.run(main())
