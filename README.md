# 米宝一号 · robot-m5stack

基于 M5Stack CoreS3 的桌面 AI 机器人「米宝一号」（StackChan 形态）。

## 目录说明

| 目录 | 内容 |
|---|---|
| `robot-updating/fw` | ESP32 固件（xiaozhi-esp32 + mooncake UI + 米宝定制应用） |
| `robot-updating/xiaozhi-server` | AI 对话服务器（xiaozhi-server，ASR/LLM/TTS 网关） |
| `robot-updating/mibao-server` | 米宝配套服务 |
| `00~03_*` | 产品定义 / 规格 / 开发指南 / 开发环境文档 |
| `米宝一号_功能架构图.html` | 功能架构图 |

## 固件功能

- AI 对话（唤醒词「米宝米宝」，支持说话时打断）
- 会议录音 / 会议录像 / 个人灵感（录音、视频录制、转会议纪要）
- 文件管理（层级目录浏览，按录音/视频/纪要分组）
- 配网与服务器地址自动迁移

## 服务器配置

密钥等敏感配置存放在 `xiaozhi-server/data/.config.yaml`（已 gitignore，不入库），
参考 `xiaozhi-server/config.yaml` 模板创建。

> ⚠️ 本仓库为公开仓库，严禁提交任何 API 密钥。
