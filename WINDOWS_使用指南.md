# Windows 电脑使用指南（米宝一号 robot-m5stack）

本指南面向 **Windows 10/11** 的开发者/使用者，从零配置到调试机器人。
与 Mac 的主要差异：脚本为 bash（Mac/Linux 语法），Windows 需用 **ESP-IDF 官方终端**代替。

---

## 准备工作（一次性，约 30-60 分钟）

### 1. 安装 ESP-IDF v5.5（编译固件必需）
1. 下载官方 **ESP-IDF Windows 安装器**：https://dl.espressif.com/dl/esp-idf/
   （选 **ESP-IDF v5.5.x**，勾选 "Install ESP-IDF"）
2. 按向导安装（建议装到默认 `C:\Espressif`）
3. 装完桌面/开始菜单会出现 **"ESP-IDF 5.5 CMD"** 快捷方式 —— 以后编译都用它

> 验证：打开 ESP-IDF CMD，输入 `idf.py --version` 能显示版本即成功。

### 2. 安装 Python 3.10（跑 AI 服务器）
- 下载安装：https://www.python.org/downloads/release/python-31011/
- **安装时务必勾选 "Add python.exe to PATH"**

### 3. 安装 ffmpeg（服务器必需）
```bat
winget install ffmpeg
```
> 若 winget 不可用，去 https://ffmpeg.org 下载 Windows 版并加入 PATH。

### 4. 安装 Git（克隆代码用）
https://git-scm.com/download/win （默认选项即可）

---

## 克隆代码

在文件夹地址栏输入 `cmd` 回车，然后：
```bat
git clone https://github.com/jiutaowei/robot-m5stack.git
cd robot-m5stack
```

---

## 第一部分：配置并启动 AI 服务器（Windows 版）

### 1. 配置密钥
```bat
cd robot-m5stack\robot-updating\xiaozhi-server
copy config.yaml data\.config.yaml
```
用记事本打开 `data\.config.yaml`，填入**你自己的**两个密钥（详见仓库主页 README）：
- `ASR.Qwen3ASRFlash.api_key` → 阿里云 DashScope 语音识别 key（`sk-` 开头）
- `LLM.HezorLLM.api_key` → Hezor 大模型 key（`hzr_` 开头）

### 2. 安装 Python 依赖
```bat
py -3.10 -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
```
> 依赖含 torch 等，首次下载较大（约 2GB），耐心等待。
> 若 `vosk` 装不上，可先 `pip install vosk` 单独重试。

### 3. 启动服务器
```bat
.venv\Scripts\python app.py
```
看到类似日志即成功：
```
Websocket地址是  ws://192.168.x.x:8001/xiaozhi/v1/
```
> ⚠️ 记下这个 **IP:8001**，配机器人要用。
> ⚠️ Windows 防火墙首次会弹窗，务必点"允许访问"（专用网络）。

---

## 第二部分：编译固件（Windows 版）

> 注意：仓库里 `scripts/build.sh` / `flash.sh` 是 **Mac 专用**，
> Windows 不要用它们，改用下面 **ESP-IDF CMD** 的方式：

### 1. 打开 ESP-IDF 终端
开始菜单 → **"ESP-IDF 5.5 CMD"**

### 2. 进入固件目录并编译
```bat
cd C:\...\robot-m5stack\robot-updating\fw
idf.py reconfigure
idf.py build
```
首次编译 5-15 分钟（自动下载组件）。看到 `stack-chan.bin` 生成即成功。

> 若报缺 `CONFIG_*` 或组件错误，先执行一次：
> ```bat
> idf.py set-target esp32s3
> ```

### 3. 连接机器人并烧录
用 USB 线连机器人 → 设备管理器看端口（形如 `COM3`）：
```bat
idf.py -p COM3 flash
```
> ESP32-S3 用 USB-Serial-JTAG，**Windows 10/11 免驱**，插上即有 COM 口。
> 若没出现 COM 口，换根**数据线**（不是充电线）。

---

## 第三部分：机器人配网联机

1. 机器人烧录后开机 → 配网界面输入：
   - WiFi 账号密码
   - 服务器地址填 **第一步记下的** `ws://192.168.x.x:8001`
2. 完成后喊「米宝米宝」即可对话

---

## 常见问题（Windows）

| 现象 | 解决 |
|---|---|
| `idf.py` 不是内部命令 | 没在 **ESP-IDF CMD** 里运行 |
| 烧录找不到端口 | 换数据线；设备管理器确认 COM 号 |
| 机器人连不上服务器 | ① 服务器是否在运行 ② Windows 防火墙是否放行 ③ 电脑和机器人是否同一 WiFi |
| pip 安装很慢/失败 | 用国内镜像：`pip install -r requirements.txt -i https://pypi.tuna.tsinghua.edu.cn/simple` |
| 服务器报 ffmpeg 找不到 | 重装 ffmpeg 并**重启终端**刷新 PATH |
| 端口 8001 被占用 | `netstat -ano \| findstr 8001` 找到进程杀掉 |

---

## 与 Mac 版差异速查

| 事项 | Mac | Windows |
|---|---|---|
| 编译环境 | `~/esp/esp-idf` + bash | ESP-IDF 安装器 + **ESP-IDF CMD** |
| 编译命令 | `./scripts/build.sh` | `idf.py build`（在 fw 目录）|
| 烧录命令 | `./scripts/flash.sh /dev/cu.usbmodemX` | `idf.py -p COM3 flash` |
| 串口号 | `/dev/cu.usbmodem*` | `COM3` 等 |
| 启动服务器 | `./start_server.sh` | `py app.py`（.venv 激活后）|
| 服务器 IP 显示 | ifconfig | 日志里直接打印 |
