# StackChan 产品规格书

> 来源:M5Stack 官方产品页与文档(2026-07 调研整理)
> SKU:**K151**(StackChan 单机)/ **K151-R**(含遥控器套装)
> 定位:与社区共创的开源 AI 桌面机器人,基于 M5Stack 旗舰主控 CoreS3。

---

## 1. 产品概述

StackChan 是 M5Stack 与用户社区共创的「超可爱」AI 桌面机器人。主控采用 M5Stack 旗舰 IoT 开发套件 **CoreS3**(ESP32-S3),机身集成双反馈舵机、RGB LED、红外收发、三区触摸面板与全功能 NFC。出厂固件含 AI Agent、表情动画、ESP-NOW 无线遥控、在线 App 下载,并支持 OTA 升级。

**出厂固件能力**:AI 对话 / 表情动画 / ESP-NOW 遥控 / 在线 App 下载 / 手机 App 视频与远程头像控制 / OTA。

**主要贡献者**:
- Shinya Ishikawa(@meganetaaan / @stack_chan)— 原作者
- Takao Akaki(@mongonta555)— 社区贡献

---

## 2. 硬件规格

### 2.1 主控(CoreS3,ESP32-S3)

| 项目 | 参数 |
|------|------|
| 主控芯片 | ESP32-S3,Xtensa® 双核 32 位 LX7,240 MHz |
| Flash | 16 MB |
| PSRAM | 8 MB Quad |
| 无线 | 2.4 GHz Wi-Fi(IEEE 802.11 b/g/n);Bluetooth® 5 LE |
| 有线 | USB CDC & 全速 USB OTG;GPIO / UART / I2C |
| 显示 | 2.0" IPS LCD,320×240,65536 色,驱动 ILI9342C;电容多点触摸,驱动 FT6336U |
| 摄像头 | GC0308,640×480,0.3 MP |
| 麦克风 | 双麦克风,音频编解码 ES7210 |
| 接近/环境光 | LTR-553ALS-WA |
| IMU | 9 轴:BMI270(加速度+陀螺仪)+ BMM150(磁力计) |
| NFC | 全功能 NFC,ST25R3916 |
| 扬声器 | 1W,AW88298 16 位 I2S 功放 |
| 电源管理 | AXP2101 PMU + AW9523B IO 扩展;RTC BM8563 |
| 扩展 | microSD 卡槽(SPI:CS=4/SCK=36/MISO=35/MOSI=37,25MHz,FAT32);Grove 接口 ×3;LEGO® 兼容安装孔 |
| 按键 | POWER 键 + RESET(RST)键;电源指示 LED |

### 2.2 机身(Robot Body)

| 项目 | 参数 |
|------|------|
| 数据/供电接口 | USB-C |
| 电池 | 550 mAh |
| 舵机 | 双反馈舵机:X 轴 360° 连续旋转(水平);Y 轴 90° 行程(垂直) |
| RGB LED | WS2812C ×12(两排) |
| 红外 | 发射器 + 接收器(IRM56384) |
| 触摸面板 | 顶部三区触摸,Si12T 驱动 |
| NFC | 全功能 NFC,ST25R3916 |

### 2.3 机械尺寸与重量

| 项目 | 参数 |
|------|------|
| 成品尺寸 | 54.0 × 70.5 × 61.5 mm |
| 成品重量 | StackChan:187.0 g;遥控器:37.6 g |
| 包装尺寸 | StackChan:142.0 × 101.0 × 58.0 mm;遥控器套装:155.0 × 109.0 × 65.0 mm |
| 毛重 | StackChan:272.4 g;遥控器套装:372.9 g |

---

## 3. 包装清单

### StackChan(SKU: K151)
- StackChan 主机一台(CoreS3 已预装)
- USB-A 转 USB-C 数据线(50 cm)
- 表情贴纸一张
- 印刷用户手册一份
- 收纳袋/盒(依套装而定)

### StackChan 遥控器套装(SKU: K151-R)
- StackChan 主机一台(CoreS3 已预装)
- 遥控器一台(由 Hat Mini JoyC + StickC-Plus 组装)
- USB Type-C 数据线
- 表情贴纸 / 印刷用户手册

---

## 4. 开发平台支持

| 平台 | 说明 |
|------|------|
| UiFlow2 | M5Stack 图形化编程(Blockly),入门最快 |
| Arduino IDE | C/C++,产品固件主推,有 M5StackChan 专用库 |
| PlatformIO | board ID `m5stack-cores3`,支持 Arduino / ESP-IDF 框架 |
| ESP-IDF | 通过 PlatformIO 或作为 Moddable 固件后端使用 |

详见 `02_开发指南/` 各文档。

---

## 5. 典型应用场景

- 桌面陪伴机器人
- AI Agent / 语音问答
- 智能家居控制
- IoT 控制

---

## 6. 舵机安全注意事项(重要)

> ⚠️ 操作不当可能造成永久性硬件损坏,开发前务必阅读。

| 轴 | 范围说明 | 安全使用 | 连续旋转 |
|----|---------|---------|---------|
| **X 轴(水平 Pan)** | API 支持 -128°~128° | 无角度限制 | ✅ 支持 360° 连续旋转 |
| **Y 轴(垂直 Tilt)** | API 支持 0°~90° | **建议 5°~85°** | ❌ 不支持 |

**安全规则:**
1. **Y 轴(垂直)严禁超范围**:实际安全区间为 5°~85°,在极限角度运行可能导致舵机堵转并造成永久损坏。
2. **X 轴(水平)无角度限制**,可 360° 连续旋转。
3. **切勿手动强转舵机**:在不确定舵机是否通电受控时,不要用手强行转动任何运动部件,可能损坏齿轮/舵机。
4. **API 角度单位换算**:M5StackChan 库中角度值需 ×10(如 45° 写作 `450`),速度范围 0~1000,旋转速度 -1000~1000(负=顺时针 CW,正=逆时针 CCW)。
5. **首次使用先校准原点**:`Motion.setCurrentPostionAsHome()` 设原点 → `Motion.goHome()` 归位。

---

## 7. 电源与按键操作(CoreS3)

| 操作 | 方法 |
|------|------|
| 开机 | 单击左侧 POWER 键 |
| 关机 | 长按左侧 POWER 键 6 秒 |
| 复位 | 单击底部 RST 键 |
| 进入下载模式 | **长按 RST 键至绿色 LED 亮起后松开**(CoreS3 文档约 2 秒,StackChan/UiFlow2 文档约 3 秒,以绿灯亮起为准,建议按足 3 秒;烧录固件前必须进入此模式) |

- 供电:USB-C(数据+供电);机身口与底座口均支持数据传输,**推荐使用底座口**(避免烧录时舵机误动)。
- USB 串口芯片:CH343 等;Windows 通常需装驱动,macOS/Linux 一般免驱。
- BMM150 磁力计注意:含磁铁的产品会干扰磁力计读数,使用时移除磁铁并远离强磁场。

---

## 8. 关键文档与资源链接

| 资源 | 地址 |
|------|------|
| 官方产品页 | https://shop.m5stack.com/products/stackchan-kawaii-co-created-open-source-ai-desktop-robot |
| 官方文档首页 | https://docs.m5stack.com/en/StackChan |
| CoreS3 文档 | https://docs.m5stack.com/en/core/CoreS3 |
| CoreS3 原理图 PDF | https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/490/Sch_M5_CoreS3_v1.0.pdf |
| DinBase 原理图 PDF | https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/559/SCH_DinBase_V1.1.pdf |
| 开源固件仓库 | https://github.com/stack-chan/stack-chan |
| 认证 | CE / MIC / FCC / SAR |

> ⚠️ 警告:出厂 StackChan 的 CoreS3 已预装,自行拆装或刷写非官方固件可能影响保修,操作前请评估风险。
