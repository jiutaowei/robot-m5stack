# 米宝一号 · robot-m5stack

基于 **M5Stack CoreS3** 的桌面 AI 机器人「米宝一号」（StackChan 形态）。
含完整固件源码、AI 对话服务器、产品文档。

## 🖥️ 平台使用指南
- **Windows 电脑**（编译固件 + 跑服务器）：见 [WINDOWS_使用指南.md](WINDOWS_使用指南.md)
- **macOS 电脑**：见下方各节（脚本为 bash，适配 Mac）

---

## 📦 克隆后目录结构

```
robot-m5stack/
├── robot-updating/
│   ├── fw/                  # ESP32 固件（含 xiaozhi-esp32 框架 + 米宝定制应用）
│   │   ├── main/            #   米宝定制层：会议录音/录像、文件管理、唤醒词…
│   │   ├── xiaozhi-esp32/   #   固件框架（mooncake UI / audio / protocol）
│   │   ├── components/      #   定制 LVGL 组件
│   │   └── scripts/         #   build.sh / flash.sh
│   ├── xiaozhi-server/      # AI 对话服务器（ASR / LLM / TTS 网关）
│   ├── mibao-server/        # 米宝配套服务
│   └── start_server.sh      # 一键启动 AI 服务器
├── 00_产品定义与优化方向/     # 产品文档
├── 01_产品规格/
├── 02_开发指南/
├── 03_开发环境/
└── 米宝一号_功能架构图.html
```

## 🔑 首次使用：必须自己配置的（不入库）

以下内容涉及你的密钥/机器环境，**gitignore 已排除，需自行创建**：

### 1. AI 服务器密钥 `xiaozhi-server/data/.config.yaml`
克隆后没有此文件。复制模板创建，并填入**你自己的** ASR/LLM/TTS 密钥：
```bash
cp xiaozhi-server/config.yaml xiaozhi-server/data/.config.yaml
```
再参考本项目用到的模型改关键项（示例，换成你的 key）：
```yaml
ASR:
  Qwen3ASRFlash:
    type: qwen3_asr_flash
    api_key: sk-你的key
    model_name: qwen3-asr-flash
    ...
LLM:
  HezorLLM:
    type: openai
    base_url: https://hezor.com/api/v1/openai/latest
    model_name: hezor-trusted-4
    api_key: hzr_你的key
TTS:
  EdgeTTS:
    type: edge
    voice: zh-CN-XiaoxiaoNeural
```

### 2. Python 依赖
```bash
cd xiaozhi-server
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
```

### 3. 固件编译环境
需安装 [ESP-IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/get-started/)。
首次构建会自动下载组件并生成构建产物（`fw/managed_components/`、`fw/build/` 均不入库）：
```bash
cd robot-updating
./scripts/build.sh          # 编译
./scripts/flash.sh /dev/cu.usbmodemXXXX   # 烧录（换成你的串口）
```

## 🚀 运行

1. **启动 AI 服务器**（机器人对话依赖它）：
   ```bash
   ./robot-updating/start_server.sh
   ```
   会输出服务器地址（形如 `ws://192.168.x.x:8001`）。

2. **给机器人配网 / 填服务器地址**：
   机器人开机 → 配网界面填入 WiFi 与服务器地址。

3. **唤醒词**：对机器人喊「米宝米宝」。

## ⚠️ 注意事项
- **严禁提交任何 API 密钥**到本公开仓库（`.config.yaml`、`04_API KEY/` 已 gitignore）。
- `fw/build/`、`fw/managed_components/` 是构建产物，clone 后由 build 自动生成，无需也不应入库。
- 服务器地址变更时，机器人端固件有自动迁移逻辑（见 `fw/main/hal/mibao_config.cpp`）。

## 📋 克隆后"缺的东西"对照表

| 克隆后缺失 | 为什么缺 | 怎么恢复（自动/手动） |
|---|---|---|
| `xiaozhi-server/data/.config.yaml` | 含你的密钥，gitignore | **手动**：`cp config.yaml data/.config.yaml` 后填自己的 key |
| `04_API KEY/` | 密钥汇总，gitignore | 不属于代码，不公开 |
| `.venv/` | Python 环境 | **手动**：`pip install -r requirements.txt` |
| `fw/managed_components/` | ESP-IDF 自动下载的组件 | **自动**：`./scripts/build.sh` 时自动拉取 |
| `fw/build/` | 编译产物 | **自动**：build 生成 |
| `xiaozhi-server/models/` | 语音模型（SileroVAD 等） | 首次运行自动下载或按 server 提示放置 |
| 服务器 IP 地址 | 每台机器不同 | 运行时配网填入 |

**结论**：仓库包含**全部源码**（固件、框架、组件、服务器、文档），
缺的只是「每台机器各自的密钥/环境/产物」，均可按上表恢复，不影响从零编译运行。


## 固件功能一览
- AI 对话：唤醒词 + 说话时打断（服务端 AEC）
- 会议录音 / 会议录像 / 个人灵感：录制 + 一键转会议纪要
- 文件管理：层级目录、按录音/视频/纪要分组、删除二次确认
- 边缘滑动返回、自动 OTA 配置
