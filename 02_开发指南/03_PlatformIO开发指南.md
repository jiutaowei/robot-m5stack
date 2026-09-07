# PlatformIO 开发指南

PlatformIO 适合工程化、Git 版本管理与团队协作。StackChan/CoreS3 在 PlatformIO 中有官方板卡定义。

---

## 1. 板卡标识

- **platform**:`espressif32`
- **board ID**:`m5stack-cores3`
- 硬件参数:ESP32-S3 / 240 MHz / Flash 16 MB / RAM 320 KB
- 默认上传协议:`esptool`
- **支持框架**:Arduino、ESP-IDF
- 板卡 manifest:https://github.com/platformio/platform-espressif32/blob/master/boards/m5stack-cores3.json
- 官方说明:https://docs.platformio.org/en/latest/boards/espressif32/m5stack-cores3.html

---

## 2. platformio.ini 配置

### 2.1 Arduino 框架(推荐,与 Arduino IDE 示例一致)
```ini
[env:m5stack-cores3]
platform = espressif32
board = m5stack-cores3
framework = arduino
monitor_speed = 115200
upload_protocol = esptool
lib_deps =
    m5stack/M5Unified
    m5stack/M5GFX
    ; M5StackChan 库:若 PlatformIO Registry 可用则用下面写法,
    ; 若不可用则需手动安装(zip 引入或 lib_extra_dirs 指向本地副本)
    ; m5stack/M5StackChan
build_flags =
    -DARDUINO_M5STACK_CORES3
```

### 2.2 ESP-IDF 框架(底层定制)
```ini
[env:m5stack-cores3]
platform = espressif32
board = m5stack-cores3
framework = espidf
```

### 2.3 覆盖默认参数
```ini
[env:m5stack-cores3]
platform = espressif32
board = m5stack-cores3
framework = arduino
board_build.mcu = esp32s3
board_build.f_cpu = 240000000L
upload_protocol = esptool
monitor_speed = 115200
```

---

## 3. 工程骨架

可用的工程骨架已放在 `03_开发环境/platformio/`,包含:
- `platformio.ini`
- `src/main.cpp`(基于 M5StackChan 的最小示例)

---

## 4. 常用命令

```bash
pio run              # 编译
pio run -t upload    # 编译并烧录(烧录前需长按 RST 3 秒进下载模式)
pio device monitor   # 串口监视器(115200)
pio run -t clean     # 清理
```

---

## 5. 注意事项

- ⚠️ **M5StackChan 库**:PlatformIO Registry 中 `m5stack/M5StackChan` 是否可用未经验证(该库公开 GitHub 仓库调研时返回 404)。若 `lib_deps` 解析失败,改用本地引入:`lib_extra_dirs = <本地库路径>`,或将库放入 `lib/` 目录。
- PlatformIO 使用自带的 ESP32 工具链,与 Arduino IDE 的 M5Stack 板管理包相互独立,板定义通过 `espressif32` 平台提供,无需单独配置板管理 URL。
- 烧录前必须进入下载模式:长按 RST 键约 3 秒,绿灯亮后松开。

> ⚠️ 警告:Y 轴舵机限位 5°~85°,X 轴无限制;切勿手动强转舵机。
