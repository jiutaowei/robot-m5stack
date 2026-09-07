# 米宝一号 xiaozhi-server（AI 对话服务端）

fork 自开源项目 [xinnan-tech/xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server)，
为米宝一号（xiaozhi-esp32 v1.4.3 固件）提供 xiaozhi 协议服务端：设备语音 → ASR → LLM → TTS 全链路走阿里云百炼（专属业务空间）。

## 部署结构

```
xiaozhi-server/
├── app.py / core/ ...          # 开源服务端本体（xiaozhi 协议：WebSocket + OTA）
├── config.yaml                 # 上游默认配置（未改动）
├── data/.config.yaml           # 本地覆盖配置（深合并，含百炼密钥，.gitignore 排除）
├── scripts/
│   ├── install_deps.sh             # 建 py3.10 venv 并安装依赖
│   ├── extract_key_and_gen_config.py  # 从 04_API KEY/*.xlsx 提取密钥生成 data/.config.yaml
│   ├── verify_bailian.py           # 百炼三件套真实调用验证（LLM/ASR/TTS）
│   └── device_handshake_test.py    # 模拟设备：OTA 检查 + WebSocket hello 握手
└── .venv/                      # Python 3.10 虚拟环境（.gitignore 排除）
```

对上游的本地改动（最小补丁，便于跟随上游升级）：

- `core/providers/asr/aliyunbl_stream.py`：`ws_url` 可配置（百炼专属业务空间需指向专属 WebSocket 端点）
- `core/providers/tts/alibl_stream.py`：同上
- `.gitignore`：修正 `data/` 排除路径

## 百炼三件套（全部现成 provider，无需新增插件）

| 环节 | provider type | 模型 | 说明 |
| --- | --- | --- | --- |
| ASR | `aliyunbl_stream`（AliyunBLStreamASR） | paraformer-realtime-v2 | WebSocket 双工流式，16k PCM |
| LLM | `openai`（AliyunQwenLLM） | qwen-plus | 百炼 OpenAI 兼容端点 `/compatible-mode/v1` |
| TTS | `alibl_stream`（AliBLTTS） | cosyvoice-v2 / longcheng_v2 | WebSocket 双工流式，输出 PCM 再转 opus 下发 |

**专属业务空间注意**：本项目 API key 为 `sk-ws-` 前缀的业务空间密钥，所有调用必须走专属端点
`ws-6eh7u980zm72yhe0.cn-beijing.maas.aliyuncs.com`（已在 `data/.config.yaml` 配置 LLM base_url 与 ASR/TTS 的 `ws_url`），不能用公网 `dashscope.aliyuncs.com`。

system prompt（`data/.config.yaml` → `prompt`）：金禾天成农业专家人设，精通作物栽培/水肥/植保/温室环控，回答口语化便于语音播报。

## 启动命令

```bash
cd xiaozhi-server
bash scripts/install_deps.sh        # 首次：Python 3.10 venv + 依赖（含 torch，较大）
source .venv/bin/activate
python app.py                       # 启动：WebSocket 8001，HTTP(OTA) 8003
```

> 依赖锁了 torch==2.2.2 等旧版本，**必须用 Python 3.10**（本机 `/opt/homebrew/opt/python@3.10`），
> Python 3.14 无对应 wheel。

密钥重新生成（xlsx 变更后）：

```bash
python scripts/extract_key_and_gen_config.py "/path/to/04_API KEY/默认业务空间-apiKey-6466462.xlsx" data/.config.yaml
```

## 设备对接（xiaozhi-esp32 v1.4.3）

1. 设备端把 `ota_url` 设为：

   ```
   http://<电脑IP>:8003/xiaozhi/ota/
   ```

   - 配网热点页 advanced 配置：`http://192.168.4.1/advanced/config` 中填写 ota_url（已支持）；
   - 或后续固件批次的设备设置页。

2. 设备 POST OTA 后，服务端返回 `websocket.url`（自动生成为 `ws://<电脑IP>:8001/xiaozhi/v1/`），设备据此建立 WebSocket 通道。

3. 音频链路：设备上行 opus / 16kHz / 单声道（帧长 60ms），服务端 hello 握手时按 `audio_params` 自适应；下行 TTS 由服务端转 opus 下发——opus 编解码与 16k 采样率由开源项目默认处理，设备端无需额外配置。

4. 首次连接设备需在 OTA 响应中完成激活流程（`activation` 字段），局域网 `auth.enabled: false` 时直接放行。

## 验证命令

```bash
# 百炼三件套真实调用（LLM/ASR/TTS）
python scripts/verify_bailian.py

# 模拟设备：OTA + WebSocket hello 握手
python scripts/device_handshake_test.py 127.0.0.1
```

## 配置说明

`data/.config.yaml` 深合并到 `config.yaml`，关键项：

| 键 | 值 | 说明 |
| --- | --- | --- |
| `server.port` | 8001 | WebSocket 服务端口 |
| `server.http_port` | 8003 | HTTP 端口（OTA / 视觉接口） |
| `prompt` | 农业专家人设 | system prompt |
| `selected_module.ASR/LLM/TTS` | 见上表 | 模块选择 |

密钥安全：`data/` 已加入 `.gitignore`；任何报告/提交中密钥一律用 `sk-***` 掩码。
