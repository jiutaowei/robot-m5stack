"""从 xlsx 提取百炼 API key 并生成 data/.config.yaml（key 不回显）。"""
import re
import sys
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

XLSX = Path(sys.argv[1])
OUT = Path(sys.argv[2])
NS = {"m": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}

with zipfile.ZipFile(XLSX) as z:
    shared = []
    if "xl/sharedStrings.xml" in z.namelist():
        root = ET.fromstring(z.read("xl/sharedStrings.xml"))
        for si in root.findall("m:si", NS):
            shared.append("".join(t.text or "" for t in si.iter(
                "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}t")))
    sheet_names = sorted(n for n in z.namelist() if n.startswith("xl/worksheets/sheet"))
    values = []
    for sn in sheet_names:
        root = ET.fromstring(z.read(sn))
        for row in root.iter("{%s}row" % NS["m"]):
            vals = []
            for c in row.iter("{%s}c" % NS["m"]):
                v = c.find("m:v", NS)
                if v is None or v.text is None:
                    vals.append("")
                elif c.get("t") == "s":
                    vals.append(shared[int(v.text)])
                else:
                    vals.append(v.text)
            values.append(" | ".join(vals))

keys = []
for line in values:
    for m in re.finditer(r"sk-[A-Za-z0-9._\-]{16,}", line):
        if m.group(0) not in keys:
            keys.append(m.group(0))

if not keys:
    print("ERROR: no sk- key found"); sys.exit(1)

key = keys[0]
print(f"found {len(keys)} key(s), using first: {key[:3]}***{key[-4:]}")

# xlsx 中的 OpenAI 兼容端点（工作空间专属 MaaS），没有则回退 dashscope 公网
custom_base = next(
    (l.split(" | ")[1].strip() for l in values
     if "openAiCompatible" in l and "http" in l),
    "https://dashscope.aliyuncs.com/compatible-mode/v1",
)
print(f"llm base_url: {custom_base}")

# 专属业务空间的 WebSocket 推理端点（ASR/TTS 流式）
api_host = next(
    (l.split(" | ")[1].strip() for l in values if "apiHost" in l), ""
)
ws_url = f"wss://{api_host}/api-ws/v1/inference" if api_host else "wss://dashscope.aliyuncs.com/api-ws/v1/inference"
print(f"ws_url: {ws_url}")

config = f"""# 米宝一号本地覆盖配置（深合并到 config.yaml）—— 含密钥，严禁提交
server:
  ip: 0.0.0.0
  port: 8001
  http_port: 8003

prompt: |
  你是「米宝一号」，金禾天成的农业智能助手，一位经验丰富的农业专家。
  你精通大田与设施农业：作物栽培、水肥管理、病虫害防治、温室环控、农机与灌溉设备。
  回答要求：专业准确、贴近生产实际，先给结论再给操作建议；涉及用药用肥要给出安全注意事项。
  语言简洁口语化，便于语音播报，单次回答一般不超过 120 字；不确定的内容如实说明。

selected_module:
  ASR: AliyunBLStreamASR
  LLM: AliyunQwenLLM
  TTS: AliBLTTS

ASR:
  AliyunBLStreamASR:
    type: aliyunbl_stream
    api_key: {key}
    model: paraformer-realtime-v2
    format: pcm
    sample_rate: 16000
    ws_url: {ws_url}

LLM:
  AliyunQwenLLM:
    type: openai
    base_url: {custom_base}
    model_name: qwen-plus
    api_key: {key}
    temperature: 0.6
    max_tokens: 500

TTS:
  AliBLTTS:
    type: alibl_stream
    api_key: {key}
    model: "cosyvoice-v2"
    voice: "longcheng_v2"
    output_dir: tmp/
    ws_url: {ws_url}
"""
OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_text(config, encoding="utf-8")
print(f"written {OUT} ({OUT.stat().st_size} bytes)")
