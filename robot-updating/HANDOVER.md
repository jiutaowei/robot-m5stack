# 米宝一号 / StackChan — 项目交接与详细说明

> **目的**：把从开工到现在所有的工作、决策、产出、参考资料、阶段性文档汇总到这一份文档里，**让任何接手的同事（或将来的我自己）能 30 秒内知道我们做什么、做到哪、缺什么**。
>
> ⚠️ **重要说明**：本文档是历史记录（阶段 0~1.2），部分内容已过时。当前最新状态请见 [米宝一号交接文档_2026-08-07.md](./米宝一号交接文档_2026-08-07.md)。所有旧版 build 脚本（`build_robot.sh` / `build_stage*`）已被 `scripts/build.sh` 取代，请勿使用。
>
> **最后更新**：2026-08-09（文档更新：移除已清理目录引用，更新编译指南为 scripts/build.sh）

---

## 目录

- [1. 30 秒速读（TL;DR）](#1-30-秒速读tldr)
- [2. 我们在做什么](#2-我们在做什么)
- [3. 关键概念与产品定义](#3-关键概念与产品定义)
- [4. 工作目录与文件结构](#4-工作目录与文件结构)
- [5. 阶段路线总览（5 阶段）](#5-阶段路线总览5-阶段)
- [6. 当前进度（细化到每个阶段）](#6-当前进度细化到每个阶段)
- [7. 参考资料清单（所有文档、脚本、Skill）](#7-参考资料清单所有文档脚本skill)
- [8. 编译/烧录环境（踩坑后的稳定版）](#8-编译烧录环境踩坑后的稳定版)
- [9. 关键设计决策（不要重做决定）](#9-关键设计决策不要重做决定)
- [10. 阶段性文档（按时间线）](#10-阶段性文档按时间线)
- [11. 完整工作历史（从开工到今天）](#11-完整工作历史从开工到今天)
- [12. 已踩过的坑（避免重犯）](#12-已踩过的坑避免重犯)
- [13. 下一步具体动作](#13-下一步具体动作)
- [14. 警告与注意（**置底**）](#14-警告与注意置底)

---

## 1. 30 秒速读（TL;DR）

| 项 | 内容 |
|---|---|
| **产品** | 米宝一号（Mibao One）— 桌面 AI 机器人 |
| **硬件** | M5Stack CoreS3（ESP32-S3，320×240 IPS LCD） |
| **基础** | 基于实习生交付版本 v1.4.3（xiaozhi-esp32 框架） |
| **三大功能** | 会议录音 / 个人记录 / IoT 控制 |
| **唤醒词** | `mi bao mi bao`（MultiNet6） |
| **屏保** | 米宝眨眼 4s 睁 + 200ms 闭（**代码已修，待重跑验证**） |
| **工作目录** | `robot-updating/`（基于 `robot-original/` 解压） |
| **编译环境** | ESP-IDF v5.5.3（`~/esp/esp-idf`）+ macOS Terminal.app |
| **当前阶段** | **阶段 1.2 屏保改米宝眨眼，编译错误已修，等重跑** |
| **下一步** | 重跑 `build_stage1_2.sh` → 烧录 → 目视验证屏保 |

---

## 2. 我们在做什么

### 2.1 业务背景

公司（**金禾天成 / JH**）在做一个桌面 AI 机器人，**设备 ID = `mibao-01`**。形态参考 StackChan 桌面机器人（Cohere 合作的开源项目），但我们的吉祥物是**米宝**（已有完整 PNG 资源库）。

### 2.2 核心使用场景

老板放在桌面上，主要用：
1. **农业领域 AI 对话**（限制 system prompt 为农业专家）— `qwen-plus`
2. **会议录音**（智能会议纪要，自动转写 + 待办提取）
3. **个人记录**（随手语音备忘录）
4. **物联网控制**（对接"硅基一号"系统，灯/风扇/水泵/加热垫）

### 2.3 关键约束

- **AI 范围限制**：只回答农业相关问题（避免 hallucination 和合规风险）
- **离线功能**：录音本地存档 + 自动同步 web 端（断网也能用）
- **品牌差异化**：和原 StackChan 的区别就是米宝形象 + 农业领域
- **不做**：dance 移动、Avatar 表情、企业微信、视频推流

### 2.4 技术栈

```
设备端：ESP32-S3 + LVGL 9 + FreeRTOS + xiaozhi-esp32 框架
AI：阿里云百炼（ASR=paraformer-realtime-v2 / LLM=qwen-plus / TTS=cosyvoice-v2）
后端：FastAPI + Python 3.14 + sqlite（部署在 MacBook 本地）
物联网：HTTP POST 到树莓派 Flask（10.51.1.205:5000）
Web 端：Flutter app（实习生交付）
遥控器：M5Stack CardKB（ESP-NOW 协议，29 字节帧）
```

---

## 3. 关键概念与产品定义

### 3.1 米宝一号 = Mibao One = device_id `mibao-01`

> **项目代号清单**（用户档案已沉淀）：
> - **米宝一号 / Mibao One**：本产品，device_id=`mibao-01`
> - **金禾天成 / JH**：公司
> - **硅基一号 / Guiji-01**：物联网后端代号
> - **小智 / XiaoZhi**：xiaozhi-esp32 智能对话框架
> - **小鹰视界**：CB79 camera project

### 3.2 三大功能矩阵

| 功能 | 现状 | 实现位置 | 状态 |
|---|---|---|---|
| 会议录音 | `app_meeting/` 实习生已完整实现 16 文件 | `fw/main/apps/app_meeting/` | ✅ 可工作，**需品牌化改造** |
| 个人记录 | 不存在，需新建 | `fw/main/apps/app_mibao_personal/`（**空壳已建**） | ⏳ 阶段 4 实施 |
| IoT 控制 | 不存在，需新建 | `fw/main/apps/app_mibao_iot/`（**空壳已建**） | ⏳ 阶段 5 实施 |
| 屏保 | DVD 弹跳方块，**已改写为米宝眨眼** | `fw/main/apps/app_launcher/view/screensaver.cpp` | 🔄 阶段 1.2 代码完成，待编译 |
| 唤醒词 | 默认 `hi 乐鑫`（英文） | `main/audio/wake_words/afe_wake_word.cc` | ⏳ 阶段 6 改 `mi bao mi bao` |
| 唤醒后 UI | 无 | 待 `mibao_wake_ui.{h,cpp}` | ⏳ 阶段 6 实施 |
| 状态栏 | 基础版（时间 + Wi-Fi） | `fw/main/apps/common/status_bar/` | ⏳ 阶段 3 加电量百分比 |
| AI 对话 | 框架有（xiaozhi 默认） | xiaozhi-esp32 | ⏳ 阶段 8 接阿里云百炼 + 农业 prompt |

### 3.3 已确认用户决策

| # | 决策点 | 拍板 | 备注 |
|---|---|---|---|
| 1 | 屏保动画风格 | **B 固定位置 + 4s 睁 + 200ms 闭** | 简单：两个 timer 驱动图片切换 |
| 2 | IoT 设备类型 | **A 按实际对接**（fan/pump/light/heat） | 与后端一致，不改后端 |
| 3 | IoT 协议 | **HTTP**（`hal_mcp.cpp` 现有实现） | 树莓派 Flask `10.51.1.205:5000` |
| 4 | IoT app 与 MCP 关系 | **A 复用 MCP**（按键和语音都能控制） | 同一个 `controlGreenhouseActuator` 函数 |
| 5 | 唤醒后是否自动开 AI 对话 | **B 直接进入 AI 对话** | 不等用户点 |
| 6 | 录音上传策略 | **A 默认不上传，本地存档** | 后续可加手动同步 |

---

## 4. 工作目录与文件结构

### 4.1 项目根目录

```
/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/
├── README.md                              ← 根目录总索引（产品/规格/指南/开发环境）
├── 产品定义与优化方向.md                  ← 米宝一号产品定义（**长期记忆**）
├── robot/                                 ← 实习生原始交付（1.4GB 备份，**不要动**）
├── robot.zip                              ← 1.4GB 全量压缩包备份
├── robot-original/                        ← 解压后未改，参考用
│
├── robot-updating/                        ← **当前工作目录（活跃）**
│   ├── HANDOVER.md                        ← 本文件
│   ├── README.md                          ← 烧录/编译命令速查
│   ├── IMPROVEMENT_PLAN.md                ← 5 阶段战略路线
│   ├── DEVELOPMENT_PLAN.md                ← 详细规格（屏幕/UI 坐标/按钮）
│   ├── BUILD_LOG.md                       ← 完整编译日志（追加，不删）
│   ├── scripts/                           ← 编译/烧录/工具脚本
│   ├── fw/                                ← ESP-IDF 工程（要改）
│   ├── esp-idf-env/v5.5.4/                ← 实习生 ESP-IDF（**当前不用**）
│   └── .agents/                           ← 实习生 agent 配置
│
├── 01_产品规格/                           ← StackChan 硬件规格
├── 02_开发指南/                           ← 四种开发平台指南
├── 03_开发环境/                           ← 各种工程骨架
├── 04_API KEY/                            ← 默认业务空间 apiKey
├── UI参考图/                              ← 米宝 PNG 原图
├── archive/                               ← 历史废弃脚本（30 个）
└── .trae/skills/                          ← 5 个 mibao skill
    ├── mibao-product/
    ├── mibao-firmware-dev/
    ├── mibao-app-architecture/
    ├── mibao-recorder-pattern/
    └── png-to-lvgl-bin/
```

### 4.2 固件核心目录（`fw/main/`）

```
fw/main/
├── apps/
│   ├── apps.h                              ← app 注册中心（已注释隐藏 6 个）
│   ├── app_meeting/                        ← 会议录音（实习生完整 16 文件）
│   ├── app_launcher/
│   │   ├── view/
│   │   │   ├── view.h                      ← 已改：Screensaver 不再继承 DvdScreensaver
│   │   │   ├── view.cpp                    ← launcher 9 app 滚动逻辑
│   │   │   └── screensaver.cpp             ← ✅ 阶段 1.2 改写：米宝眨眼
│   │   └── app_launcher.cpp
│   ├── app_mibao_personal/                 ← 新建空壳（阶段 4 实施）
│   ├── app_mibao_iot/                      ← 新建空壳（阶段 5 实施）
│   ├── app_setup/                          ← 设置
│   ├── app_template/                       ← app 模板
│   ├── common/                             ← 通用组件（status_bar / toast / reminder）
│   └── （隐藏但保留源）app_ai_agent/ app_avatar/ app_dance/ app_ezdata/ app_app_center/ app_espnow_ctrl/
│
├── assets/
│   ├── mibao/                              ← 米宝 PNG 源图（已就位）
│   ├── mibao_bin/                          ← PNG→LVGL bin 转换产物（7 个）
│   ├── assets_bin/                         ← 编译时打包进 flash（自动包含 mibao_bin/）
│   ├── png_to_mibao_bin.py                 ← PNG→bin 转换脚本
│   ├── fonts/                              ← 字体
│   └── sfx/                                ← 音效
│
├── hal/
│   ├── hal.cpp / hal.h                     ← HAL 主入口
│   ├── hal_audio.cpp / audio.cpp           ← 音频 codec
│   ├── hal_servo.cpp / imu.cpp / rtc.cpp   ← 硬件抽象
│   ├── hal_mcp.cpp                         ← MCP 协议（IoT HTTP）
│   ├── hal_espnow.cpp                      ← 遥控器协议（29 字节 ESP-NOW）
│   ├── hal_ble.cpp                         ← BLE 配网
│   └── mibao_*.{h,cpp}                     ← 米宝 HAL 层（待新建：config/recorder/iot_client/wake_word/wake_ui）
│
├── CMakeLists.txt                          ← 已加 mibao bin 同步逻辑（line 409-415）
└── Kconfig.projbuild
```

### 4.3 外部构建路径

```
/Users/cobain/robot-build/fw/               ← 镜像构建目录（**无方括号**，CMake 不会 glob 错）
├── main/assets/assets_bin/                 ← 米宝 bin 已自动同步到此
├── build/stack-chan.bin                    ← 编译产物
└── build/generated_assets.bin              ← 资源打包产物

/Users/cobain/esp/esp-idf/                  ← ESP-IDF v5.5.3（**当前用**这个）

/Users/cobain/.espressif/                   ← 工具链根目录
├── tools/xtensa-esp-elf/esp-14.2.0_20251107/
├── tools/riscv32-esp-elf/...
└── python_env/idf5.5_py3.14_env/           ← Python 3.14 虚拟环境
```

---

## 5. 阶段路线总览（5 阶段）

> 详细规划见 [IMPROVEMENT_PLAN.md](file:///Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/IMPROVEMENT_PLAN.md)
> 详细规格见 [DEVELOPMENT_PLAN.md](file:///Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/DEVELOPMENT_PLAN.md)

### 5.1 阶段 0：基础整理 ✅ 已完成

- 注释不需要的 app（`ai_agent` / `avatar` / `app_center` / `ezdata` / `dance` / `espnow_ctrl`）
- 新建 `app_mibao_personal/` + `app_mibao_iot/` 空壳
- 返回键统一左上角 64×32 浅灰
- **验证**：编译 + 烧录 OK

### 5.2 阶段 1：米宝主题化（视觉）🔄 进行中

- ✅ **1.1** 米宝 PNG 资源收集 + 转 bin（7 个 bin 已生成）
- 🔄 **1.2** 屏保改米宝眨眼（**代码完成，待重跑编译**）
- ⏳ **1.3** launcher home icon + app icon 换米宝
- ⏳ **1.4** 状态栏补全时间/电量/Wi-Fi

### 5.3 阶段 2：唤醒词 + 唤醒后 UI ⏳

- 替换 `afe_wake_word.cc` 命令词为 `["mi bao mi bao"]`
- 加 `multinet6_zh.bin` 模型
- 实现 `mibao_wake_word` + `mibao_wake_ui`
- 唤醒后右下角米宝小图 + "我在" 文字

### 5.4 阶段 3：三大功能 app ⏳

- **会议录音**（基于 app_meeting 改品牌 + 加 5 分钟自动分段）
- **个人记录 app**（新建 `app_mibao_personal/`，复用录音架构）
- **IoT 控制 app**（新建 `app_mibao_iot/`，复用 MCP 工具）
- **米宝 HAL 层**（`mibao_config` / `mibao_recorder` / `mibao_iot_client`）

### 5.5 阶段 4：Web 端对接 ⏳

- 后端 FastAPI 接收 `/api/recording/upload` + `/api/iot/devices` + `/api/iot/control`
- 前端录音管理页面 + IoT 设备管理
- 米宝固件录音结束 POST 到 backend

### 5.6 阶段 5：AI 农业对话 ⏳

- 阿里云百炼 API key 配进 NVS
- 唤醒后自动开启对话
- ASR → LLM → TTS 闭环
- LLM system prompt = 农业专家

---

## 6. 当前进度（细化到每个阶段）

### 6.1 阶段 0 ✅ 全部完成

| 任务 | 状态 | 证据 |
|---|---|---|
| 注释隐藏 6 个 app | ✅ | `fw/main/apps/apps.h` |
| 新建 personal 空壳 | ✅ | `fw/main/apps/app_mibao_personal/` |
| 新建 IoT 空壳 | ✅ | `fw/main/apps/app_mibao_iot/` |
| 返回键统一 | ✅ | launcher 子页统一 64×32 左上角 |

### 6.2 阶段 1.1 ✅ 完成

| 任务 | 状态 | 证据 |
|---|---|---|
| 米宝 PNG 源图就位 | ✅ | `fw/main/assets/mibao/` |
| PNG→bin 转换脚本 | ✅ | `fw/main/assets/png_to_mibao_bin.py`（手写 12 字节 header） |
| 7 个 bin 已生成 | ✅ | `fw/main/assets/mibao_bin/`：chat_60 / iot_150 / look_open_150 / look_close_150 / meeting_150 / personal_150 / work_150 |
| CMakeLists 同步逻辑 | ✅ | `fw/main/CMakeLists.txt` line 409-415：`file(COPY)` 把 mibao_bin/*.bin 复制到 assets_bin/ |
| 资源编译进 flash | ✅ | `generated_assets.bin` = 5.4M（含米宝资源） |

### 6.3 阶段 1.2 🔄 代码完成，待编译验证

| 任务 | 状态 | 证据 |
|---|---|---|
| 重写 screensaver.cpp | ✅ | `fw/main/apps/app_launcher/view/screensaver.cpp` |
| 去掉 DvdScreensaver 继承 | ✅ | `view.h` Screensaver 类独立 |
| 4s 睁 + 200ms 闭眨眼 | ✅ | lv_timer 周期 + 单次 close_timer |
| 修复编译错误 | ✅ | 移除 `LV_TIMER_REPEAT_INFINITE`（LVGL 9 中不存在） |
| **重跑 build_stage1_2.sh 验证** | ⏳ | 待执行 |

### 6.4 阶段 1.3 ⏳ 未开始

- launcher 9 app → 4 app（米宝会议/个人/IoT/设置）
- home icon → `mibao_home_150.bin`
- app icon → `mibao_meeting_150.bin` / `mibao_personal_150.bin` / `mibao_iot_150.bin`

### 6.5 阶段 1.4 ⏳ 未开始

- 状态栏：左 = 时间（HH:MM 24h），中 = Wi-Fi，右 = 电量 + 百分比

---

## 7. 参考资料清单（所有文档、脚本、Skill）

### 7.1 项目内文档（**权威，按使用频率排序**）

| 文档 | 路径 | 用途 | 何时看 |
|---|---|---|---|
| **HANDOVER.md** | [robot-updating/HANDOVER.md](file:///Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/HANDOVER.md) | 本文件（30 秒速读 + 完整索引） | 接手时第一份 |
| **README.md**（项目） | [robot-updating/README.md](file:///Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/README.md) | 编译/烧录命令速查（`./scripts/build.sh`） | 每次编译烧录前 |
| **IMPROVEMENT_PLAN.md** | [robot-updating/IMPROVEMENT_PLAN.md](file:///Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/IMPROVEMENT_PLAN.md) | 5 阶段战略 + 产品定义 | 规划下一阶段时 |
| **DEVELOPMENT_PLAN.md** | [robot-updating/DEVELOPMENT_PLAN.md](file:///Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/DEVELOPMENT_PLAN.md) | 详细规格（屏幕/UI 坐标/按钮/数据流） | 改 UI / 写新 app 时 |
| **BUILD_LOG.md** | [robot-updating/BUILD_LOG.md](file:///Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/BUILD_LOG.md) | 完整编译日志（追加，不删） | 排查编译问题时 |
| 实习生交接（3 份） | `fw/StackChan任务进度交接.md`<br>`fw/STACKCHAN_修复后安装烧录步骤.md`<br>`fw/StackChan录音设备升级方案.md` | 实习生原文交付 | 了解实习生做了什么 |
| 遥控器联调 | `fw/遥控器开发现状与录音控制联调说明.md` | 遥控器协议细节 | 改 ESP-NOW 时 |
| 产品定义（长期记忆） | [产品定义与优化方向.md](file:///Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/产品定义与优化方向.md) | 公司产品定义 + 关键 bug 清单 | 战略层讨论时 |

### 7.2 实习生交付文档（不修改，仅参考）

- `fw/StackChan任务进度交接.md` — 实习生做了什么、怎么做的
- `fw/STACKCHAN_修复后安装烧录步骤.md` — 烧录参数（bootloader offset 0x0 等）
- `fw/StackChan录音设备升级方案.md` — 录音架构设计
- `fw/遥控器开发现状与录音控制联调说明.md` — 29 字节 ESP-NOW 协议

### 7.3 项目级 Skill（5 个，在 `.trae/skills/`）

| Skill | 路径 | 用途 |
|---|---|---|
| `mibao-product` | [SKILL.md](file:///Users/cobain/.trae-cn/memory/projects/-Users-cobain-Documents--0-project-JHTech--2---------2-------15-Stackstan--p2-2bfa0bcd14f2a7d346a8/20260304) | 米宝产品定义总览，**对话压缩后第一份读** |
| `mibao-firmware-dev` | [.trae/skills/mibao-firmware-dev/SKILL.md](file:///Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/.trae/skills/mibao-firmware-dev/SKILL.md) | 固件开发核心模式 |
| `mibao-app-architecture` | [.trae/skills/mibao-app-architecture/SKILL.md](file:///Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/.trae/skills/mibao-app-architecture/SKILL.md) | App 完整架构模式 |
| `mibao-recorder-pattern` | [.trae/skills/mibao-recorder-pattern/SKILL.md](file:///Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/.trae/skills/mibao-recorder-pattern/SKILL.md) | 录音服务标准架构 |
| `png-to-lvgl-bin` | [.trae/skills/png-to-lvgl-bin/SKILL.md](file:///Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/.trae/skills/png-to-lvgl-bin/SKILL.md) | PNG 转 LVGL 9 bin 资源 |

### 7.4 全局记忆（AI 跨会话记忆）

- [user_profile.md](file:///Users/cobain/.trae-cn/memory/user_profile.md) — 用户档案（个人偏好 + 跨项目经验 + 项目代号）
- project_memory.md（在 `/Users/cobain/.trae-cn/memory/projects/.../20260304/` 下）— 项目长期记忆

### 7.5 关键脚本（构建用）

| 脚本 | 路径 | 用途 |
|---|---|---|
| `scripts/build.sh` | [robot-updating/scripts/build.sh](file:///Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/scripts/build.sh) | 编译脚本（增量 `./scripts/build.sh` / 全量 `./scripts/build.sh full`） |
| `scripts/flash.sh` | [robot-updating/scripts/flash.sh](file:///Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/scripts/flash.sh) | 烧录脚本（`./scripts/flash.sh` / `./scripts/flash.sh erase`） |
| `png_to_mibao_bin.py` | `fw/main/assets/png_to_mibao_bin.py` | PNG → LVGL 9 ARGB8888 bin 转换 |

### 7.6 历史废弃脚本（30 个，已移到 `archive/`）

> 不再用，仅供回溯参考。**不要回到这些脚本**，都已被 `scripts/build.sh` / `scripts/flash.sh` 取代。

---

## 8. 编译/烧录环境（踩坑后的稳定版）

### 8.1 当前可用环境

- **ESP-IDF**：`~/esp/esp-idf/`（**v5.5.3**）
  - **不用** `idf_tools.py install`（卡死）
- **构建路径**：`/Users/cobain/robot-build/fw/`（**必须**用无方括号镜像目录，脚本自动同步）
  - 原路径 `/Users/cobain/Documents/[0]project-JHTech/...` 含方括号 `[]`，CMake `file(GLOB)` 会解析为字符类通配符，导致 components 扫描失败
- **执行终端**：macOS Terminal.app
  - Trae 沙盒阻止 build 目录写入 + 阻止 USB + 阻止 ESP-IDF `.git` 写入
- **Python 环境**：`~/.espressif/python_env/idf5.5_py3.14_env/bin/python`
- **推荐编译方式**：`./scripts/build.sh`（自动处理环境激活、镜像同步、增量/全量编译）

### 8.2 编译命令（**当前可用**）

```bash
cd "/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating"

# 方法 1：用脚本（**推荐，自动处理环境同步**）
./scripts/build.sh              # 增量编译
./scripts/build.sh full         # 全量清理后编译
./scripts/build.sh menuconfig   # 打开 menuconfig 配置

# 方法 2：手动（在 Terminal.app）
cd /Users/cobain/robot-build/fw
source ~/esp/esp-idf/export.sh

# 首次或 CMakeLists 改了：fullclean + build
idf.py fullclean
idf.py build 2>&1 | tee /tmp/robot_build.log

# 增量编译（只改了 .h/.cpp）
idf.py build 2>&1 | tee /tmp/robot_build.log
```

### 8.3 烧录命令（5 个文件，**bootloader offset 必须 0x0**）

```bash
cd "/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating"

# 推荐：用脚本（自动查找镜像目录的 build 产物）
./scripts/flash.sh         # 正常烧录
./scripts/flash.sh erase   # 先擦除再烧录

# 手动：
cd /Users/cobain/robot-build/fw

# 1) 先擦
esptool.py --chip esp32s3 -b 460800 erase_flash

# 2) 写 5 个 bin
esptool.py --chip esp32s3 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0       build/bootloader/bootloader.bin \
  0x8000    build/partition_table/partition-table.bin \
  0xd000    build/ota_data_initial.bin \
  0x20000   build/stack-chan.bin \
  0xA00000  build/generated_assets.bin
```

### 8.4 烧录到验证的产物路径

```
/Users/cobain/robot-build/fw/build/
├── bootloader/bootloader.bin           (0x0)
├── partition_table/partition-table.bin (0x8000)
├── ota_data_initial.bin                (0xd000)
├── stack-chan.bin                      (0x20000)  ← 固件
└── generated_assets.bin                (0xA00000) ← 资源
```

---

## 9. 关键设计决策（不要重做决定）

| 项 | 决策 | 原因 |
|---|---|---|
| 屏保节奏 | 4s 睁 + 200ms 闭 | 用户测试"最自然"，不费资源（不动画） |
| 屏保位置 | 居中 150×150 | 屏幕 320×240，220×220 会被裁（已踩坑） |
| 屏保背景 | 纯黑 | 黑底反衬米宝图，不需要额外设计 |
| 唤醒后行为 | 直接进 AI 对话 | 用户决策 B，不停在 launcher |
| 资源尺寸 | 屏保/大图 ≤ 150×150 | 240×240 屏 + 240×240 屏内容 |
| 资源格式 | LVGL 9 ARGB8888 | 12 字节 header，保留 alpha，黑底透出米宝图 |
| 录音存储 | 会议 `/sdcard/meetings/`，个人 `/sdcard/personal/` | 与 web 端 type 字段对应 |
| IoT 控制 | 复用 MCP 工具，独立 app 也能点 | 兼容原语音控制（远程树莓派后端） |
| 编译路径 | 用镜像目录 `/Users/cobain/robot-build/fw/` | 路径含方括号 CMake 必挂 |
| 工具链 | 用 `~/esp/esp-idf` v5.5.3 | 实习生 v5.5.4 工具链不完整 |
| ESP-IDF 版本 | v5.5.3（v5.5.4 工具链不全） | 已验证可编译通过 |
| 米宝 app 命名 | `app_mibao_<name>/` + 类名 `AppMibao<Name>` | 与原 app 区分明确 |
| 录音架构 | **FreeRTOS task 主动轮询 codec** | 不能用 `onMicData` 被动回调（智能手表方案） |
| 返回键 | 左上角 64×32 浅灰 `#E0E0E0` + 黑字 | 不与主控件冲突 |

---

## 10. 阶段性文档（按时间线）

### 10.1 阶段 0 输出

- [DEVELOPMENT_PLAN.md](file:///Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/DEVELOPMENT_PLAN.md) — 详细开发计划（屏幕坐标系、屏保、启动器、3 个 app、HAL 层）
- [IMPROVEMENT_PLAN.md](file:///Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/IMPROVEMENT_PLAN.md) — 5 阶段改进路线
- `fw/main/apps/apps.h` — app 注册中心（已注释隐藏）
- `fw/main/apps/app_mibao_personal/` + `app_mibao_iot/` — 空壳

### 10.2 阶段 1 输出

- `fw/main/assets/mibao/` — 米宝 PNG 源图（从 `UI参考图/` 复制）
- `fw/main/assets/mibao_bin/` — 7 个转换后的 bin
- `fw/main/assets/png_to_mibao_bin.py` — 转换脚本（手写 12 字节 header）
- `fw/main/CMakeLists.txt` line 409-415 — 资源同步逻辑
- `fw/main/apps/app_launcher/view/screensaver.cpp` — 米宝眨眼屏保（重写）
- `fw/main/apps/app_launcher/view/view.h` — Screensaver 类改造
- `build_robot.sh` + `build_stage1_2.sh` — 编译脚本
- `BUILD_LOG.md` — 完整编译日志

### 10.3 阶段 2~5 输出（待）

- 唤醒词替换
- 三大功能 app
- Web 后端对接
- AI 农业对话

---

## 11. 完整工作历史（从开工到今天）

### 11.1 第一阶段：环境与版本确认（**烧录验证**）

1. 收到实习生交付版本（`robot/` 1.4GB zip + `robot-original/` 解压）
2. 用户决定："**基于实习生版本做二次开发**"，不重写
3. 烧录验证：m5burner 1.4.4 官方版 → 屏幕 OK → 实习生 v1.4.3 → **屏幕 OK**
4. **根因教训**：之前烧录不亮是因为 bootloader offset 错用 0x1000（v5.5+ 必须用 0x0）

### 11.2 第二阶段：整理工作目录

1. 实习生原始版本 `robot/` 标记为只读备份
2. 新建 `robot-updating/` 作为工作目录
3. 清理根目录 30 个废弃脚本到 `archive/`
4. 路径策略：源在 `robot-updating/fw/`，**构建在 `/Users/cobain/robot-build/fw/`（无方括号）**

### 11.3 第三阶段：环境踩坑（**最曲折**）

1. 实习生用 ESP-IDF v5.5.4，工具链不完整（缺 riscv32-esp-elf-gdb）
2. 试 `idf_tools.py install` → 卡死
3. **回退到 `~/esp/esp-idf` v5.5.3**（已装完整工具链）
4. 报 `Failed to resolve component 'ArduinoJson'`
5. **根因 1**：ESP-IDF v5.5.3 不扫描本地 `components/` → 在 `fw/CMakeLists.txt` 加 `set(EXTRA_COMPONENT_DIRS ...)`
6. **根因 2**：路径含方括号 `[0]project` CMake `file(GLOB)` 把 `[0]` 当字符类 → 创建镜像目录 `/Users/cobain/robot-build/fw/`
7. 解决后编译成功，资源包 `generated_assets.bin` 5.4M
8. 烧录验证：屏幕亮 + 启动正常 + 屏保（DVD 弹跳方块）确认

### 11.4 第四阶段：阶段 0 基础整理

1. `apps/apps.h` 注释隐藏 6 个 app（ai_agent / avatar / app_center / ezdata / dance / espnow_ctrl）
2. 新建 `app_mibao_personal/` + `app_mibao_iot/` 空壳
3. 返回键统一左上角 64×32 浅灰
4. 编译 + 烧录验证：launcher 显示 4 app（会议/个人/IoT/设置）

### 11.5 第五阶段：阶段 1.1 资源准备

1. 从 `UI参考图/` 复制 7 个米宝 PNG 到 `fw/main/assets/mibao/`
2. 写 `png_to_mibao_bin.py`（PIL 读 PNG 拿 raw ARGB，手写 12 字节 LVGL 9 header）
3. 转 7 个 bin 到 `fw/main/assets/mibao_bin/`
4. 在 `fw/main/CMakeLists.txt` 加 `file(COPY)` 同步 mibao_bin → assets_bin
5. 验证编译：米宝资源进 `generated_assets.bin`

### 11.6 第六阶段：阶段 1.2 屏保改写

1. 重写 `screensaver.cpp`：去掉 DvdScreensaver 继承，纯 LVGL Image + lv_timer
2. 节奏：4s 睁 + 200ms 闭
3. 写 `build_stage1_2.sh` 脚本
4. **踩坑**：`LV_TIMER_REPEAT_INFINITE` was not declared → 移除（LVGL 9 默认无限循环）
5. 修复后代码完成，**等重跑编译验证**

### 11.7 当前待办（阶段 1.2 验证 + 阶段 1.3/1.4）

1. **立即**：重跑 `build_stage1_2.sh` 验证屏保编译
2. **之后**：烧录 + 目视屏保
3. **然后**：阶段 1.3 launcher 改米宝主题 + 阶段 1.4 状态栏完善

---

## 12. 已踩过的坑（避免重犯）

### 12.1 编译相关

| 坑 | 现象 | 根因 | 解决 |
|---|---|---|---|
| **路径方括号** | `Failed to resolve component 'ArduinoJson'` | CMake `file(GLOB)` 把 `[0]` 当字符类 | 构建用 `/Users/cobain/robot-build/fw/` 镜像 |
| **ArduinoJson 找不到** | `Failed to resolve component 'ArduinoJson' required by component 'main': unknown name` | v5.5.3 不扫描本地 `components/` | `fw/CMakeLists.txt` 加 `EXTRA_COMPONENT_DIRS` |
| **v5.5.4 工具链不全** | `ERROR: tool riscv32-esp-elf-gdb has no installed versions` | 期望 20260121，已装 20251107 | 用 `~/esp/esp-idf` v5.5.3 |
| **idf_tools.py install 卡死** | 17% 几分钟后无进度 | 疑似网络问题 | 不安装新工具链 |
| **Python 路径不一致** | `is currently active in the environment while the project was configured with` | source ESP-IDF 后 python 路径变 | `idf.py fullclean` 后重 build |
| **Trae 沙盒** | `hit restricted` / `Operation not permitted` | 阻止 build 目录写入 + USB | 用 macOS Terminal.app |
| **LVGL 9 宏不存在** | `'LV_TIMER_REPEAT_INFINITE' was not declared` | LVGL 9 中没这个宏 | `lv_timer_create` 默认无限循环 |
| **Windows CRLF** | 编译可能异常 | 行尾符差异 | Python 脚本批量转 LF |

### 12.2 烧录相关

| 坑 | 现象 | 根因 | 解决 |
|---|---|---|---|
| **bootloader offset 错** | 烧录后屏幕不亮 | v5.5+ 必须 0x0，不是 0x1000 | 烧录命令 offset 用 0x0 |
| **erase_flash 忘** | 启动混乱 | 残留 bootloader 冲突 | 每次烧新版本前先擦 |
| **stack-chan.bin 没生成** | esptool 报错 | Trae 沙盒阻止 esptool | Terminal.app 跑 elf2image |
| **assets partition 不够** | `will not fit in 16777216 bytes of flash` | generated_assets.bin 太大 | 改 `partitions.csv` assets=6M |

### 12.3 资源相关

| 坑 | 现象 | 根因 | 解决 |
|---|---|---|---|
| **屏保 220×220 被裁** | 屏保图显示不全 | 220×220 在 240×240 屏外 | 统一 ≤ 150×150 |
| **PNG→bin cf 字段错** | 显示花屏 | cf=0x06 但实际 RGB888 | 用 PIL + 手写 ARGB8888 header |
| **覆盖原生 icon** | 实习生的 dance 图标消失 | 批次6脚本用新 bin 覆盖老 bin | 用新名字如 `icon_meeting_150.bin` |

### 12.4 已沉淀的跨项目经验

> 这些在 `user_profile.md` 里也沉淀了：

- **录音架构**：必须 FreeRTOS task 主动轮询 codec（**不要**被动回调 `onMicData`）
- **WAV 文件头**：开始时占位，结束时回填 data_size
- **文件命名**：`rec_YYYYMMDD_HHMMSS.wav`
- **返回键标准**：左上角 64×32 浅灰，位置 `LV_ALIGN_TOP_LEFT, 4, 4`
- **BLE 配网兼容**：v1.2.2 与 v1.4.x 不兼容，**v1.4.4 有死机缺陷，回退 v1.4.2 最稳**
- **ESP-IDF 工具链**：用 `~/esp/esp-idf` 已装版本，不要用项目自带的
- **资源路径含特殊字符**：`[ ] ( ) { } * ?` 在 `file(GLOB)` 中会被解析，绕道用镜像目录

---

## 13. 下一步具体动作

### 13.1 立即（编译验证）

```bash
bash "/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/build_stage1_2.sh"
```

**期望输出**：
- `BUILD_LOG.md` 追加新段落
- `/Users/cobain/robot-build/fw/build/stack-chan.bin` 更新 mtime
- 退出码 0

### 13.2 烧录验证（compile OK 后）

```bash
cd /Users/cobain/robot-build/fw
esptool.py --chip esp32s3 -b 460800 erase_flash
esptool.py --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0xd000 build/ota_data_initial.bin 0x20000 build/stack-chan.bin 0xA00000 build/generated_assets.bin
```

### 13.3 目视验证

- 启动 launcher → 看米宝主题（**目前还没改 launcher**，仍是原版）
- 30s 无操作 → 屏保出现 → **黑底 + 米宝睁眼图（150×150 居中）**
- 4s 后看到闭眼图 200ms → 立刻切回睁眼
- 触屏 → 退出屏保

### 13.4 阶段 1.3 启动

把 launcher 9 app 砍到 4（米宝会议/个人/IoT/设置），home icon + app icon 换米宝图。

---

## 14. 警告与注意（**置底**）

> 严格遵守以下约束，否则可能重新踩坑：

- **不要在 Trae 内置终端跑 build/usb 操作**（沙盒限制）
- **不要改 `~/esp/esp-idf/` 工具链**（v5.5.3 已稳定，不要安装新版本）
- **不要用裸路径含方括号跑 cmake**（必须用 `/Users/cobain/robot-build/fw/` 镜像）
- **不要在 path 含特殊字符的目录 build**（`[ ] ( ) { } * ?` 会让 `file(GLOB)` 解析错）
- **不要实现 `onMicData` 当被动回调**（必须 FreeRTOS task 主动轮询 codec）
- **烧录前必须 `erase_flash`**（v5.5+ bootloader offset 0x0 与旧版不兼容）
- **bootloader offset 必须 0x0**（不是 0x1000）
- **assets partition 5.94M**（已扩好，唤醒词模型 + 屏保图够装）
- **改 CMakeLists 后必须 fullclean**（esp-idf v5.5 python 路径变化会要求）
- **首次接 ESP-SR 唤醒词前**确认模型 `multinet6_zh.bin` 已在 `assets_bin/`（待阶段 2 加）
- **不要覆盖原生 app 图标**（用新名字如 `mibao_meeting_150.bin`）
- **不要用 220×220 屏保图**（会被 240×240 屏裁）
- **不要用 `ESP_LOGI`**（用 `mclog::tagInfo`）
- **不要在 src 路径直接 build**（必须在 `/Users/cobain/robot-build/fw/` 镜像）

---

## 附录 A：完整文件改动清单

### 已新建（阶段 0~1.2）

- `fw/main/apps/app_mibao_personal/{app_mibao_personal.h, app_mibao_personal.cpp}`（空壳）
- `fw/main/apps/app_mibao_iot/{app_mibao_iot.h, app_mibao_iot.cpp}`（空壳）
- `fw/main/assets/mibao/*.png`（7 个）
- `fw/main/assets/mibao_bin/*.bin`（7 个）
- `fw/main/assets/png_to_mibao_bin.py`
- `HANDOVER.md`（本文件）
- `BUILD_LOG.md`
- `DEVELOPMENT_PLAN.md`
- `IMPROVEMENT_PLAN.md`
- `build_robot.sh`
- `build_stage1_2.sh`

### 已修改（阶段 0~1.2）

- `fw/main/CMakeLists.txt` — 加 `EXTRA_COMPONENT_DIRS` + mibao bin 同步
- `fw/main/apps/apps.h` — 注释隐藏 6 个 app
- `fw/main/apps/app_launcher/view/screensaver.cpp` — 重写为米宝眨眼
- `fw/main/apps/app_launcher/view/view.h` — Screensaver 类不再继承
- `fw/StackChan任务进度交接.md` 等 3 份实习生文档（**未修改**）
- `README.md`（根目录）— 路径更新
- `产品定义与优化方向.md` — 路径更新 + 关键 bug 清单

### 待新建（阶段 2~5）

- `fw/main/hal/mibao_config.{h,cpp}`
- `fw/main/hal/mibao_recorder.{h,cpp}`
- `fw/main/hal/mibao_iot_client.{h,cpp}`
- `fw/main/hal/mibao_wake_word.{h,cpp}`
- `fw/main/hal/mibao_wake_ui.{h,cpp}`
- `fw/main/hal/mibao_asr.{h,cpp}`
- `fw/main/hal/mibao_tts.{h,cpp}`
- `fw/main/hal/mibao_llm.{h,cpp}`
- `fw/main/apps/app_mibao_personal/view/{view.h, view.cpp}`
- `fw/main/apps/app_mibao_iot/view/{view.h, view.cpp}`

### 待修改（阶段 2~5）

- `fw/main/main.cpp` — install 顺序改 4 个
- `fw/main/apps/app_launcher/view/view.cpp` — home icon 换米宝
- `fw/main/apps/app_launcher/app_launcher.cpp` — 加 WakeUI 轮询
- `fw/main/apps/app_meeting/app_meeting.cpp` — 5 分钟自动分段
- `fw/main/assets/assets.cpp` — 注册新 bin
- `fw/main/common/status_bar/status_bar.cpp` — 加电量百分比
- `fw/xiaozhi-esp32/main/audio/wake_words/afe_wake_word.cc` — 命令词改 `["mi bao mi bao"]`
- `fw/sdkconfig.defaults` — 关 WakeNet + 开 MultiNet6

---

## 附录 B：联系人 / 上下文恢复

- **用户档案**：[user_profile.md](file:///Users/cobain/.trae-cn/memory/user_profile.md)
- **项目记忆**：[project_memory.md](file:///Users/cobain/.trae-cn/memory/projects/-Users-cobain-Documents--0-project-JHTech--2---------2-------15-Stackstan--p2-2bfa0bcd14f2a7d346a8/)
- **会话恢复优先级**：先读本 HANDOVER.md → 再读 DEVELOPMENT_PLAN.md → 再读 BUILD_LOG.md（最近 200 行）

---

**更新日志**：
- 2026-08-05：阶段 0~1.1 完成，编译链路打通
- 2026-08-06：阶段 1.2 屏保代码完成，编译错误已修，等重跑验证
