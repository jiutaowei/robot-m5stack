# 米宝一号 / StackChan 固件

> 基于 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) v1.4.3 二次开发。
> 工作目录：`robot-updating/`
> 原始版本：`robot-original/`

## 快速开始

```bash
cd "/Users/weijiutao/wjt/work/robot-m5stack/robot-updating"

# 增量编译
./scripts/build.sh

# 全量编译（首次或改 CMakeLists 后）
./scripts/build.sh full

# 烧录
./scripts/flash.sh
```

## 编译环境

- **ESP-IDF v5.5.3**（`~/esp/esp-idf/`，从 jihulab 国内镜像安装）
- **Python**：ESP-IDF 自带 venv（`~/.espressif/python_env/`）
- 本机项目路径无特殊字符，**直接在源码目录编译，无需镜像目录**
- **必须 macOS Terminal.app**（沙箱会屏蔽 USB 和 ninja）

## 烧录

```bash
cd "/Users/weijiutao/wjt/work/robot-m5stack/robot-updating"
./scripts/flash.sh          # 正常烧录
./scripts/flash.sh erase    # 先擦除再烧录
```

串口设备：`/dev/cu.usbmodem141301`（ESP32-S3 USB-Serial-JTAG）

## 文档

- [HANDOVER.md](./HANDOVER.md) — 完整项目交接文档
- [米宝一号交接文档_2026-08-07.md](./米宝一号交接文档_2026-08-07.md) — 近期工作交接
- [DEVELOPMENT_PLAN.md](./DEVELOPMENT_PLAN.md) — 详细开发计划
- [IMPROVEMENT_PLAN.md](./IMPROVEMENT_PLAN.md) — 改进计划
- [BUILD_LOG.md](./BUILD_LOG.md) — 编译日志
- [UI设计指导文档.md](./UI设计指导文档.md) — UI 设计规范

## 目录结构

```
robot-updating/
├── scripts/                  # 编译/烧录/工具脚本
│   ├── build.sh              # 编译脚本（增量/全量/menuconfig）
│   └── flash.sh              # 烧录脚本
├── fw/                       # 主项目（要改）
│   ├── main/                 # 主代码
│   │   ├── apps/             # app 列表
│   │   ├── hal/              # HAL 层
│   │   │   ├── hal_mcp.cpp   # MCP 协议（IoT HTTP）
│   │   │   ├── mibao_config.cpp  # 米宝配置（NVS/远程拉取）
│   │   │   └── ...
│   │   └── assets/           # 资源（图标/字体/sfx）
│   ├── CMakeLists.txt
│   └── sdkconfig.defaults
├── mibao-server/             # 统一后台（FastAPI）
│   ├── app/
│   │   ├── main.py           # 入口
│   │   ├── database.py       # SQLite 数据库
│   │   └── routers/          # API 路由
│   │       ├── recordings.py # 录音管理
│   │       ├── iot.py        # IoT 代理
│   │       ├── device_config.py  # 设备配置
│   │       └── ai_config.py      # AI 配置
│   └── README.md             # 后台启动说明
├── HANDOVER.md               # 本文件
├── README.md                 # 本文件
└── 米宝一号交接文档_2026-08-07.md  # 近期工作交接
```

## 后台服务

```bash
cd "/Users/weijiutao/wjt/work/robot-m5stack/robot-updating/mibao-server"
source .venv/bin/activate
python -m app.main
```

访问 http://localhost:8000/ 打开统一管理页面。

## 设备

- 主板：M5Stack CoreS3（ESP32-S3）
- 屏幕：2.0 寸 AMOLED 240×240
- 麦克风/扬声器：CoreS3 内置
- SD 卡：录音存到 `/sdcard/meetings/` 和 `/sdcard/personal/`
- 物联网后端：树莓派 Flask（10.51.1.205:5000）