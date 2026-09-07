# stack-chan 开源固件指南(Moddable / JavaScript)

除了官方 Arduino/UiFlow2 路线,StackChan 拥有活跃的开源固件社区。原项目由 Shinya Ishikawa(@meganetaaan)创建,现已迁移至 GitHub 组织 `stack-chan`。

- 仓库:https://github.com/stack-chan/stack-chan (旧地址 https://github.com/meganetaaan/stack-chan 仍可访问)
- 默认分支:`dev/v1.0`
- 协议:Apache-2.0
- 语言构成:TypeScript 58.1% / JavaScript 35.6%
- 运行时:**Moddable SDK**(XS JavaScript 引擎)+ ESP-IDF

---

## 1. 技术栈与工具链

| 项 | 说明 |
|----|------|
| 运行时 | Moddable SDK(XS JS 引擎) |
| 后端 | ESP-IDF(实测 v5.3,Python 3.12.3) |
| 主机语言 | TypeScript / JavaScript |
| Node.js | 实测 v22.9.0 |
| Moddable SDK | 实测 5.3.3 |
| 支持硬件 | M5Stack Basic/Gray/Fire、Core2、**CoreS3** |

> ✅ CoreS3 是官方明确支持的三大目标之一,构建目标 `esp32/m5stack_cores3`。

---

## 2. 与官方 Arduino 路线的区别

- 本固件是 **JavaScript/Moddable** 固件,不是 Arduino 固件。
- 仓库内置丰富的「超可爱」表情、语音、动作框架,适合做 AI 桌面机器人应用。
- ⚠️ 仓库 README 明确:**"AI Stack-chan"**(基于 Arduino 的 AI 应用,主要由 @robo8080 开发)**不在本仓库范围**,见 https://github.com/robo8080/AI_StackChan2 。
- 若熟悉 Arduino 且只用 PWM 舵机,作者推荐 @mongonta0716 的 [stack-chan-tester](https://github.com/mongonta0716/stack-chan-tester)。
- 最新 Release:**v0.2.1 "Alpha"**(2022-09-05),API 可能有破坏性变更,跟进 `dev/v1.0` 分支获取最新进展。

---

## 3. 安装与构建

```bash
# 1. 克隆仓库
git clone https://github.com/stack-chan/stack-chan.git
cd stack-chan/firmware
npm i

# 2. 安装 ModdableSDK + ESP-IDF(三选一)
#    方式 A:xs-dev 自动化(官方推荐)
npm run setup
npm run setup -- --device=esp32

#    方式 B:Docker 镜像(仅 Linux 推荐;WSL/macOS 有设备连接问题)
./docker/build-container.sh
./docker/launch-container.sh
npm install

#    方式 C:手动按 Moddable 官方文档安装

# 3. 验证环境
npm run doctor
# 成功输出应显示 Moddable SDK Version 与 Supported target devices: lin, esp32

# 4. 构建并烧录 host(基础程序)— CoreS3
npm run build  --target=esp32/m5stack_cores3
npm run deploy --target=esp32/m5stack_cores3

# 5. (可选)调试
npm run debug --target=esp32/m5stack_cores3   # 打开 xsbug 调试器

# 6. (可选)烧录用户应用 MOD(秒级迭代)
npm run mod --target=esp32/m5stack_cores3 ./mods/look_around/manifest.json

# 7. (可选)擦除整个 flash
npm run erase-flash
```

---

## 4. 固件架构:Host + MOD 分离

- **Host(宿主程序)**:烧录一次即可,提供运行时与基础框架。
- **MOD(用户应用)**:Host 烧录后,仅刷写 MOD 可实现秒级迭代开发。

### 配置文件:`firmware/stackchan/manifest_local.json`

在 `"config"` 下配置:

**舵机驱动类型** `driver.type`:
- `"scservo"` / `"rs30x"` / `"pwm"` / `"none"` / `"dynamixel"`
- `driver.panId` / `driver.tiltId`:串行舵机 ID(1~254)
- `driver.offsetPan` / `driver.offsetTilt`:轴偏移(-90~90)
- PWM 舵机引脚示例(Core2 Port.A):`"pwmPan": 33, "pwmTilt": 32`

**TTS 语音合成** `tts.type`:
- `"local"` / `"voicevox"` / `"remote"` / `"voicevox-web"` / `"elevenlabs"` / `"openai"`

---

## 5. 功能特性(README 核实)

- 可用 JavaScript 编程
- 支持多种舵机:**Feetech / FUTABA / DYNAMIXEL / PWM 舵机**
- 支持云端文本转语音(TTS):**VOICEVOX / ElevenLabs**
- Host + MOD 分离设计,仅刷 MOD 可秒级迭代,开发高效
- 支持**从 Web 浏览器烧录固件**(`docs/flashing-firmware-web_ja.md`)
- 表情:😐 显示可爱表情 / 😄 表情切换(Happy / Angry / Sad 等)/ 😺 自定义表情
- 👀 视线(瞥/凝视/注视)/ 💬 说话 / 💡 扩展 M5Units / 🎲 自定义应用

---

## 6. 仓库结构

```
stack-chan/
├── firmware/                固件源码(主开发目录)
│   ├── stackchan/           固件源码
│   ├── mods/                用户应用(MOD)源码
│   ├── scripts/             语音合成等脚本
│   ├── typings/             TypeScript 类型定义(d.ts)
│   └── docs/                文档(getting-started / flashing-firmware / api)
├── case/                    外壳 STL(3D 打印)
├── schematics/              原理图与 PCB 布局数据
└── web/                     Web 相关
```

---

## 7. 参考链接

- 仓库:https://github.com/stack-chan/stack-chan
- firmware README:https://github.com/meganetaaan/stack-chan/blob/dev/v1.0/firmware/README.md
- 构建环境:getting-started.md
- 构建与烧录:flashing-firmware.md
- API 文档:api.md
- MOD 开发:mods/README.md
- Web 浏览器烧录:flashing-firmware-web_ja.md
- AI_StackChan2(Arduino 版 AI 应用,第三方):https://github.com/robo8080/AI_StackChan2
- stack-chan-tester(PWM 舵机测试,Arduino):https://github.com/mongonta0716/stack-chan-tester

> ⚠️ 警告:Moddable 固件配置舵机时同样遵守机械限位,Y 轴 5°~85°,切勿手动强转舵机。该固件为 Alpha 阶段,API 可能变更。
