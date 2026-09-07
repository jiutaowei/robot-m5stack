# Arduino IDE 开发指南(主推)

StackChan 在 Arduino 下是一等公民:官方提供 `M5StackChan` 专用库(封装舵机 Motion 与顶部触摸传感器)+ 15+ 官方示例。本指南基于 2026-07 官方文档整理。

---

## 1. 安装 ESP32 / M5Stack 板支持包

1. 打开 Arduino IDE → `文件(File)` → `首选项(Preferences)`。
2. 在 `附加开发板管理器网址(Additional Board Manager URLs)` 填入:

   ```
   https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
   ```

   > 这是 M5Stack 自有板管理索引(非乐鑫官方 ESP32 索引),包含 M5CoreS3 板定义。
3. 侧边栏 `Board Manager` → 搜索 `M5Stack` → `Install`(建议装最新版)。
4. **手动安装(网络失败时)**:解压后放入
   - Windows:`%USERNAME%\AppData\Local\Arduino15\packages`
   - macOS:`/Users/<用户名>/Library/Arduino15/packages`
   - 解压后文件夹名必须为 `m5stack`。

### 选板
- `工具(Tools)` → `开发板(Board)` → `M5Stack` → **`M5CoreS3`**
- 芯片:ESP32-S3 / 240 MHz / Flash 16 MB / RAM 320 KB

### 板管理器版本要求(各示例)
| 示例 | 最低版本 |
|------|---------|
| 触摸传感器(TouchSensor) | ≥ 3.3.7 |
| microSD / Wakeup | ≥ 3.2.2 |

---

## 2. 安装 Arduino 库

侧边栏 `Library Manager` 搜索安装,提示依赖时点 `Install All`。

| 库名 | 最低版本 | 用途 | 仓库 |
|------|---------|------|------|
| **M5Unified** | ≥ 0.2.11 | 统一驱动(显示/电源/触摸/IMU 等) | https://github.com/m5stack/M5Unified |
| **M5GFX** | ≥ 0.2.18 | 图形绘制 | https://github.com/m5stack/M5GFX |
| **M5StackChan** | ≥ 1.0.0 | StackChan 专用(舵机 Motion / 顶部触摸) | Library Manager 搜索 `M5StackChan` |
| M5CoreS3 | — | CoreS3 底层驱动(部分示例使用,可选) | https://github.com/m5stack/M5CoreS3 |

> ⚠️ 关于 M5StackChan 库:官方文档明确引用 `<M5StackChan.h>` 头文件并要求版本 ≥ 1.0.0,可通过 Arduino Library Manager 搜索安装。但其公开 GitHub 仓库(`m5stack/M5StackChan`)调研时返回 404,可能仅通过 Library Manager 分发或随板管理包提供。社区仓库 `OXOOOOX/M5StackChan` 非官方,使用需谨慎。

---

## 3. M5StackChan 库 API

### 3.1 核心
```cpp
M5StackChan.begin();          // 初始化
M5StackChan.update();         // 主循环中调用,刷新状态
M5StackChan.Display();        // 获取显示对象(返回 M5GFX 引用)
M5StackChan.TouchSensor;      // 顶部触摸传感器对象
M5StackChan.Motion;           // 舵机运动控制对象
```

### 3.2 顶部触摸传感器(TouchSensor)
```cpp
ts.wasClicked();              // 单击
ts.wasSwipedForward();        // 向前滑动
ts.wasSwipedBackward();       // 向后滑动
ts.wasPressed();              // 按下(Servo 示例中使用)
```

### 3.3 舵机 Motion(角度单位:10 = 1°,速度 0~1000)
```cpp
Motion.move(x, y);            // 同时移动到指定角度(角度值 ×10)
Motion.moveX(angle, speed);   // X 轴(水平),范围 -1280~1280(-128°~128°)
Motion.moveY(angle, speed);   // Y 轴(垂直),范围 0~900(0°~90°),安全区间 5°~85°
Motion.rotateX(velocity);     // X 轴 360° 连续旋转,-1000~1000(负=CW顺时针,正=CCW逆时针)
Motion.stop();                // 停止连续旋转
Motion.goHome();              // 回原点
Motion.setCurrentPostionAsHome();          // 设当前为原点
Motion.setAutoAngleSyncEnabled(false);     // 关闭高频角度同步(高频更新场景)
```

> ⚠️ Y 轴安全区间 5°~85°(API 写作 50~850),超限可能损坏舵机。X 轴无限制。

### 3.4 电源/睡眠(复用 M5Unified)
```cpp
M5.Power.timerSleep(5);       // 定时睡眠唤醒(秒)
M5.Power.powerOff();          // 关机
```

> 扬声器 / MIC / 屏幕触摸等外设复用 CoreS3 的 M5Unified API,官方已发布 StackChan 专属示例(见第 4 节)。关键 API:
> - **扬声器**:`M5.Speaker.tone(freq, duration)` 播放音调;`M5.Speaker.playRaw(data, size, samplerate, ...)` 播放原始音频;`M5.Speaker.setVolume(255)`
> - **麦克风**:`M5.Mic.record(data, length, samplerate)` 录音;`M5.Mic.config().noise_filter_level` 噪声滤波(0~255);`M5.Mic.isEnabled()` / `M5.Mic.isRecording()`
> - **屏幕触摸**:`M5.Touch.getDetail()` 返回触摸状态(none/touch/hold/flick/drag 等)与坐标
>
> ⚠️ **全双工说明**:M5Unified 示例中麦克风和扬声器**不能同时使用**(需分时 `M5.Mic.end()` / `M5.Speaker.begin()` 切换)。但这是**软件限制,非硬件限制**——CoreS3 的 ES7210(双麦)+ AW88298(功放)是独立芯片 + 双 I2S,硬件支持全双工。如需全双工语音对话,建议用 ESP-IDF 原生 I2S 驱动(如 xiaozhi-esp32 的做法),并启用 ES7210 的 AEC 回声消除。详见 `06_生态资源与方案选型.md`。

---

## 4. 官方示例清单

官方示例位于 `docs.m5stack.com/en/arduino/stackchan/`(以下七个已核实正文内容):

| 示例 | 路径 | 头文件 | 依赖库版本 | 说明 |
|------|------|--------|----------|------|
| Servo(舵机) | `/stackchan/servo` | `<M5StackChan.h>` | M5StackChan ≥ 1.0.0 | 含两个示例:**Calibration**(屏幕双按钮设原点/回原点)+ **Control**(8 段动作演示) |
| Touch Sensor(顶部触摸) | `/stackchan/touchsensor` | `<M5StackChan.h>` | 板管理 ≥ 3.3.7 | 检测单击/前滑/后滑,屏幕打印事件 |
| Touch(屏幕触摸) | `/stackchan/touch` | `<M5Unified.h>` | 板管理 ≥ 3.2.2,M5Unified ≥ 0.2.11 | 屏幕触摸状态(touch/hold/flick/drag)+ 坐标 |
| Speaker(扬声器) | `/stackchan/speaker` | `<M5Unified.h>` | 板管理 ≥ 3.2.2,M5Unified ≥ 0.2.11 | `M5.Speaker.tone()` 播放 10kHz/4kHz 音调 |
| Mic(麦克风) | `/stackchan/mic` | `<M5Unified.h>` | 板管理 ≥ 3.2.2,M5Unified ≥ 0.2.11 | 录音+播放演示,采样率 17000Hz,噪声滤波可调;⚠️ 麦与扬声器分时切换 |
| microSD | `/stackchan/sdcard` | `<M5Unified.h>` + `<SD.h>` + `<SPI.h>` | 板管理 ≥ 3.2.2,M5Unified ≥ 0.2.11,M5GFX ≥ 0.2.18 | 文件读写 + PNG 显示,引脚见下表 |
| Wakeup(睡眠/唤醒) | `/stackchan/wakeup` | `<M5Unified.h>` | 板管理 ≥ 3.2.2,M5Unified ≥ 0.2.11 | 触摸屏幕进入睡眠,5 秒后定时唤醒 |
| Battery / Button / Camera / Display / IMU / LTR553 / NFC / RTC | 对应路径 | — | — | 复用 CoreS3 的 M5Unified API(导航有但未发布 StackChan 专属内容) |

> 主入口 `/stackchan/program` 官方仍标注 "coming soon"。

### microSD 引脚定义(官方示例核实)
| 信号 | 引脚 |
|------|------|
| CS | 4 |
| SCK | 36 |
| MISO | 35 |
| MOSI | 37 |
| SPI 频率 | 25 MHz(25000000) |
| 文件系统 | FAT32 |

> SD 卡插入方向:**触点面朝向屏幕同一侧**。图片建议分辨率 320×240,否则显示异常。

---

## 5. 最小可运行示例

### 示例 A:显示 + 顶部触摸
```cpp
#include <M5StackChan.h>

void setup() {
    M5StackChan.begin();
    M5StackChan.Display().setTextSize(2);
    M5StackChan.Display().setTextScroll(true);
    M5StackChan.Display().setTextColor(TFT_ORANGE);
    M5StackChan.Display().printf("> Touch or swipe the top\n");
    M5StackChan.Display().setTextColor(TFT_GREEN);
}

void loop() {
    M5StackChan.update();
    auto& ts = M5StackChan.TouchSensor;
    if (ts.wasClicked())           M5StackChan.Display().printf("> Was clicked\n");
    if (ts.wasSwipedForward())     M5StackChan.Display().printf("> Was swiped forward\n");
    if (ts.wasSwipedBackward())    M5StackChan.Display().printf("> Was swiped backward\n");
    delay(50);
}
```

### 示例 B:舵机运动控制
```cpp
#include <M5StackChan.h>

int state = 1;
const int MAX_STATE = 8;

void setup() {
    M5StackChan.begin();
    M5StackChan.Motion.goHome();
    M5StackChan.Display().setTextSize(2);
    M5StackChan.Display().setTextScroll(true);
    M5StackChan.Display().setTextColor(TFT_ORANGE);
    M5StackChan.Display().printf("> Touch the top to start\n");
    M5StackChan.Display().setTextColor(TFT_GREEN);
}

void loop() {
    M5StackChan.update();
    if (M5StackChan.TouchSensor.wasPressed()) {
        // 角度单位 10 = 1°;速度 0~1000
        // X 范围 -1280~1280,Y 范围 0~900(安全 50~850)
        switch (state) {
            case 1: M5StackChan.Motion.move(0, 450);    // X=0°, Y=45°
                    M5StackChan.Display().printf("> Turn Y to 45\n"); break;
            case 2: M5StackChan.Motion.moveX(900, 500); // X=90° 向左
                    M5StackChan.Display().printf("> Turn Left\n"); break;
            case 3: M5StackChan.Motion.moveX(-900, 500);// X=-90° 向右
                    M5StackChan.Display().printf("> Turn Right\n"); break;
            case 4: M5StackChan.Motion.moveY(900, 300); // Y=90° 抬头(谨慎,接近上限)
                    M5StackChan.Display().printf("> Look Up\n"); break;
            case 5: M5StackChan.Motion.moveY(0, 300);   // Y=0° 低头(谨慎,接近下限)
                    M5StackChan.Display().printf("> Look Down\n"); break;
            case 6: M5StackChan.Motion.rotateX(-800);   // 顺时针
                    M5StackChan.Display().printf("> Rotate clockwise\n");
                    delay(2000); M5StackChan.Motion.stop(); break;
            case 7: M5StackChan.Motion.rotateX(800);    // 逆时针
                    M5StackChan.Display().printf("> Rotate counter-clockwise\n");
                    delay(2000); M5StackChan.Motion.stop(); break;
            default: M5StackChan.Motion.goHome();
                     M5StackChan.Display().printf("> Go home\n"); break;
        }
        state++;
        if (state > MAX_STATE) state = 1;
    }
    delay(10);
}
```

### 示例 C:舵机校准(Calibration,官方示例)
屏幕显示两个按钮:**上半屏 "set current position as home"** / **下半屏 "move to home"**。触摸设当前舵机位置为原点,再次触摸让舵机回原点。首次使用建议先跑此示例校准。
```cpp
#include <M5StackChan.h>

// 关键 API:
//   M5StackChan.Motion.setCurrentPostionAsHome();  // 设当前为原点
//   M5StackChan.Motion.goHome();                   // 回原点
// 完整 UI 代码见官方示例 /stackchan/servo
```

### 示例 D:microSD 读写 + PNG 显示(官方示例)
```cpp
#include <SPI.h>
#include <SD.h>
#include <M5Unified.h>

#define SD_SPI_CS_PIN    4
#define SD_SPI_SCK_PIN  36
#define SD_SPI_MISO_PIN 35
#define SD_SPI_MOSI_PIN 37

void setup() {
    M5.begin();
    M5.Display.setFont(&fonts::FreeMono12pt7b);
    M5.Display.clear();

    SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);

    if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {   // 25 MHz
        M5.Display.println("SD card not detected");
        while (1);
    }
    M5.Display.println("SD card detected");

    // 写测试
    auto file = SD.open("/WriteTest.txt", FILE_WRITE, true);
    if (file) {
        file.print("Hello, world!\nSD card write success!\n");
        file.close();
        M5.Display.println("SD card write success");
    }

    // 读 PNG(需预先放 320x240 PNG 到根目录)
    // M5.Display.drawPngFile(SD, "/cores3_test_picture01.png");
}

void loop() {
    // M5.Display.drawPngFile(SD, "/cores3_test_picture01.png");
    // delay(500);
}
```
> SD 卡格式化为 FAT32;图片分辨率建议 320×240;插入时触点朝屏幕同侧。

### 示例 E:麦克风录音 + 播放(官方示例,AI 对话关键)
```cpp
#include <M5Unified.h>

// 关键 API(完整波形绘制 UI 见官方 /stackchan/mic):
//   M5.Mic.record(data, length, samplerate);   // 录音到 buffer
//   M5.Mic.config().noise_filter_level = N;    // 噪声滤波 0~255(按 BtnPWR 调整)
//   M5.Speaker.playRaw(data, size, samplerate, ...);  // 播放原始音频
//
// ⚠️ M5Unified 中麦与扬声器不能同时用,需分时切换:
//    录音时: M5.Speaker.end(); M5.Mic.begin();
//    播放时: M5.Mic.end();     M5.Speaker.begin();
// 硬件实际支持全双工(见上方"全双工说明"),此限制仅为 M5Unified 示例设计。
```
> 采样率 17000Hz;按电源键(BtnPWR)调噪声滤波;触摸屏幕切换录音/播放。

> 示例 A、B 已放入 `03_开发环境/arduino/stackchan_quickstart/`,可直接编译烧录。

---

## 6. 烧录步骤

1. USB-C 连接 StackChan 底座口。
2. **长按 RST 键,绿灯亮后松开**进入下载模式。CoreS3 文档标注约 2 秒,StackChan/UiFlow2 文档标注约 3 秒,以**绿灯亮起**为成功判据,建议按足 3 秒保守操作。
3. Arduino IDE 选对端口(`工具` → `端口`)。
4. 选板 `M5CoreS3`。
5. 点 `上传`。串口监视器波特率设 `115200` 查看输出。

---

## 7. 参考链接

- CoreS3 Arduino 编程:https://docs.m5stack.com/en/arduino/m5cores3/program
- StackChan 触摸示例:https://docs.m5stack.com/en/arduino/stackchan/touchsensor
- StackChan 舵机示例:https://docs.m5stack.com/en/arduino/stackchan/servo
- StackChan microSD:https://docs.m5stack.com/en/arduino/stackchan/sdcard
- StackChan Wakeup:https://docs.m5stack.com/en/arduino/stackchan/wakeup
- Arduino 板管理说明:https://docs.m5stack.com/en/arduino/arduino_board

> ⚠️ 警告:Y 轴舵机限位 5°~85°,切勿手动强转舵机,否则可能造成永久损坏。
