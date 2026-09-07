# 米宝一号后端（mibao-server）

Stackstan 桌面机器人的 FastAPI 后端骨架：录音文件管理 + 百炼转写/纪要总结 + IoT 执行器代理 + Web 管理页。

## 环境要求

- Python 3.10+（已在 Python 3.14 验证）
- 依赖：`fastapi` / `uvicorn` / `httpx` / `python-multipart` / `dashscope`（sqlite 用标准库）

## 启动方式

```bash
cd mibao-server
python3 -m venv .venv && source .venv/bin/activate   # 首次
pip install -r requirements.txt                        # 首次

# 启动（默认 0.0.0.0:8000）
python -m app.main
# 或
uvicorn app.main:app --host 0.0.0.0 --port 8000
```

- Web 管理页：`http://localhost:8000/`（录音列表每行含「转写」「纪要」按钮，转写中实时刷新状态徽章，纪要弹层展示转写全文 + Markdown 总结）
- Swagger 文档：`http://localhost:8000/docs`
- 设备端 `upload_url` 指向 `http://<电脑IP>:8000/api/recording/upload`

## 配置（环境变量，可选 `.env`，全部有默认值）

| 变量 | 默认值 | 说明 |
| --- | --- | --- |
| `MIBAO_PORT` / `PORT` | `8000` | 服务端口 |
| `MIBAO_HOST` | `0.0.0.0` | 监听地址 |
| `MIBAO_DATA_DIR` | `./data` | 数据目录（sqlite + 录音） |
| `MIBAO_IOT_TARGET_URL` | `http://10.51.1.205:5000/actuator_control` | 树莓派 Flask 代理目标 |
| `MIBAO_IOT_TIMEOUT` | `5` | IoT 转发超时（秒） |
| `MIBAO_DASHSCOPE_KEY` | 空 | 百炼 API Key（转写/纪要总结必填，从业务空间 xlsx 导出） |
| `MIBAO_DASHSCOPE_BASE_URL` | `https://dashscope.aliyuncs.com/api/v1` | paraformer 转写端点（专属空间指向自定义域名） |
| `MIBAO_DASHSCOPE_OPENAI_BASE_URL` | `https://dashscope.aliyuncs.com/compatible-mode/v1` | qwen 纪要总结的 OpenAI 兼容端点 |
| `MIBAO_QWEN_MODEL` | `qwen-plus` | 纪要总结模型 |
| `MIBAO_ASR_MODEL` | `paraformer-realtime-v2` | 语音转写模型 |
| `MIBAO_LLM_TIMEOUT` | `120` | 总结调用超时（秒） |

> 🔒 **密钥安全**：`MIBAO_DASHSCOPE_KEY` 只写入 `.env`（已 gitignore），不入库、不入 git；`/api/health` 及日志中一律以 `sk-***` 掩码展示。

数据落盘：`data/mibao.db`（元数据 + 转写/总结）、`data/recordings/YYYY-MM-DD/*.wav`（文件），均被 `.gitignore` 排除。

## 接口清单

### 录音模块

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| POST | `/api/recording/upload` | multipart 上传：`file`（必填，.wav）+ `type`（可选）+ `device_id`（默认 `mibao-01`）。type 未提供时按文件名前缀 `meeting_` / `personal_` 解析 |
| GET | `/api/recording/list?type=&page=&page_size=` | 分页列表（默认每页 20） |
| GET | `/api/recording/{id}` | 下载 WAV 文件 |
| DELETE | `/api/recording/{id}` | 删除（文件 + 记录，同时清理转写/总结） |

### 转写 / 纪要总结模块（百炼）

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| POST | `/api/recording/{id}/transcribe` | 异步转写：WAV 去头取裸 PCM → `paraformer-realtime-v2`，结果存 `transcripts` 表；立即返回 `processing`，前端轮询查状态 |
| POST | `/api/recording/{id}/summarize` | 基于转写文本调 `qwen-plus`：`type=meeting` 生成会议纪要（议题/结论/待办/责任人），`type=personal` 生成灵感整理（要点/可行动项）；存 `summaries` 表 |
| GET | `/api/recording/{id}/transcript` | 返回转写全文（含 status/speakers）+ 总结内容 |

转写状态机：`processing → done | error`（存在 `transcripts.status`，列表接口附 `transcript_status`/`has_summary` 供前端徽章展示）。

### IoT 代理模块

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/api/iot/devices` | 4 个执行器静态清单（fan/pump/light/heat） |
| POST | `/api/iot/control` | body 与设备端直连格式完全一致，转发树莓派并透传响应；不可达时返回 `{"success":false,"error":...}` |

### 其他

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/api/health` | 健康检查 |

## 协议契约（与设备端固件对齐）

- 录音文件：16kHz 单声道 16bit WAV；文件名 `meeting_YYYYMMDD_HHMMSS.wav`（会议）/ `personal_YYYYMMDD_HHMMSS.wav`（个人灵感）
- IoT body：`{"actuator_id":"fan|pump|light|heat","action":"on|off","duration":0..3600}`，5s 超时，响应含 `success`

## curl 示例

```bash
# 生成一个 16kHz 单声道测试 WAV（约 2 秒）
python3 -c "import wave,struct;w=wave.open('meeting_20260807_120000.wav','wb');w.setnchannels(1);w.setsampwidth(2);w.setframerate(16000);w.writeframes(struct.pack('<32000h',*[0]*32000));w.close()"

# 上传（type 省略时按文件名前缀解析为 meeting）
curl -F "file=@meeting_20260807_120000.wav" -F "device_id=mibao-01" \
  http://localhost:8000/api/recording/upload

# 列表（筛选 + 分页）
curl "http://localhost:8000/api/recording/list?type=meeting&page=1"

# 下载
curl -OJ http://localhost:8000/api/recording/1

# 删除
curl -X DELETE http://localhost:8000/api/recording/1

# 发起转写（异步，立即返回 processing）
curl -X POST http://localhost:8000/api/recording/1/transcribe

# 轮询转写状态（status: processing / done / error）
curl http://localhost:8000/api/recording/1/transcript

# 生成纪要 / 灵感总结（需先转写完成）
curl -X POST http://localhost:8000/api/recording/1/summarize

# 再次查看转写全文 + 总结
curl http://localhost:8000/api/recording/1/transcript

# IoT 设备清单
curl http://localhost:8000/api/iot/devices

# IoT 控制（与设备端 body 格式一致）
curl -X POST http://localhost:8000/api/iot/control \
  -H "Content-Type: application/json" \
  -d '{"actuator_id":"fan","action":"on","duration":60}'
```
