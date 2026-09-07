# 米宝一号详细开发计划（DEVELOPMENT_PLAN）

> **本文档定位**：具体到每个像素、每个按钮、每个数据流的开发规格。**先想清楚再动代码**。
>
> 战略层见 [IMPROVEMENT_PLAN.md](file:///Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/IMPROVEMENT_PLAN.md)
> 产品定义见 [产品定义与优化方向.md](file:///Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/产品定义与优化方向.md)
> 工作目录：`robot-updating/`

---

## 0. 关键事实修正（2026-08-05 调研结果）

| 之前假设 | 实际现状 | 影响 |
|---------|---------|------|
| 屏幕 240×240 | **320×240**（CoreS3 2.0 寸 IPS LCD） | 所有 UI 坐标需按 320×240 设计 |
| 屏保是变色方块 | **DvdScreensaver + 米宝脸轮廓**（黑底 + 64×48 彩色方块 + 黑白眼睛/嘴） | 屏保基础已有，需替换为真米宝眨眼 |
| mibao_* 代码已存在 | **完全不存在** | 所有 mibao_* 文件需从零创建 |
| 米宝 PNG 资源已存在 | **不存在**（在 `UI参考图/` 找到，需复制） | 资源从 `UI参考图/` 复制并转 bin |
| 个人记录 app 已存在 | **不存在** | 需新建 |
| IoT app 已存在 | **不存在** | 需新建 |
| 唤醒词已生效 | **没集成 ESP-SR** | 需 MultiNet6 替换命令词 |
| 状态栏完整 | `common/status_bar/` 已存在 | 需复用并扩展电量/时间 |

**已有可用资源**（在 `UI参考图/`，**待复制**到 `fw/main/assets/mibao/`）：
- `mibao_chat_60.png` - 60×60 米宝小图（唤醒后右下角用）
- `mibao_meeting_150.png` - 150×150 会议图标
- `mibao_personal_150.png` - 150×150 个人记录图标
- `mibao_iot_150.png` - 150×150 IoT 图标
- `看你_睁眼.png` / `看你_闭眼.png` - 屏保米宝睁闭眼（**注意：别用 _220 版本，会被裁剪**）
- `认真工作.png` - 米宝工作图（替代 xiaozhi 默认图）
- `米宝_开会.png` / `米宝_物联网.png` / `米宝_对话.png` / `米宝_个人灵感.png` - 大主题图

**实习生可复用资产**（已编译进 `assets_bin/`）：
- `icon_home.bin`、`icon_setup.bin`、`icon_wifi_*.bin`、`icon_bell.bin`、`icon_bat_lightning.bin` - 通用 UI 元素
- `icon_indicator_left.bin` / `icon_indicator_right.bin` - 翻页箭头
- `meeting_bg.png` - 会议背景（米宝开会图）

**实习生完整可用的 app**（已实现中文 + 录音 + 遥控器）：
- `app_meeting` - 会议录音（**16 个文件全齐**，可改成"米宝会议"主题）

---

## 1. 屏幕与坐标系约定（**已确认**）

```
屏幕：320 × 240 像素（CoreS3 2.0" IPS LCD）
状态栏：顶部 0~28（高 28px）
内容区：28~240（高 212px）
返回键：左上角 (4, 32)，紧贴状态栏下方
主内容：居中布局，留 8px 边距
```

**颜色**（米宝主题）：
- 主色（绿）：`#2DBE8D`（与 app_meeting 一致，延续实习生品牌色）
- 主色深（按钮按下）：`#155D4A`
- 危险色（红）：`#E64B4B`
- 文字主：`#273238`（深灰）
- 文字次：`#9FAAA5`（中灰）
- 背景：`#101417`（深色）/ `#F7EFE3`（米色卡片）
- 卡片：`#1C2428`

**字体**：
- 中文：`meeting_zh_font`（**复用实习生**，4bpp, 16px 精简子集，仅覆盖会议页字符）
- 后续如需扩展中文字符：使用 `lvgl-font-tool` 重新生成
- 西文/数字：`lv_font_montserrat_20` / `lv_font_montserrat_14`
- 图标：`lv_font_awesome_14` / `lv_font_awesome_20`

---

## 2. 屏保（`app_launcher/view/screensaver.cpp` 改造）

**触发条件**：30s 无触摸操作（`SCREENSAVER_TIMEOUT_MS = 30000`，沿用现状）

**触屏退出**：任何触摸事件立即退出屏保

**屏保动画**（**用户决策 #1 = B：固定位置 + 4s 睁 + 200ms 闭**）：

```
┌──────────────────────────────────────┐
│  320 × 240 屏保区                    │
│                                      │
│                                      │
│                                      │
│                                      │
│                                      │
│                                      │
│                                      │
│                            ┌────┐  │  ← 固定右下角
│                            │米宝│  │    位置 (220, 100)
│                            │睁眼│  │    100×100
│                            └────┘  │
│                                      │  ← 4s 后切闭眼图 200ms
│                              ┌────┐  │    然后立刻切回睁眼
│                              │米宝│  │
│                              │闭眼│  │
│                              └────┘  │
│                                      │
│  背景：纯黑 #000000                  │
└──────────────────────────────────────┘
```

**眨眼节奏**（**最自然**）：
- 4 秒睁眼（看你_睁眼.png）
- 200 毫秒闭眼（看你_闭眼.png）
- 循环：睁 → 闭 → 睁 → 4s → 闭 → 睁
- 用 `lv_timer_create` 周期 4s，闭眼再起 200ms 单次 timer 切回睁眼

**实现方式（最简，不增加负担）**：
```cpp
// 每 4s 触发眨眼
static void _blink_timer_cb(lv_timer_t* t) {
    auto* self = static_cast<Screensaver*>(lv_timer_get_user_data(t));
    self->blink();  // 切到闭眼图，启动 200ms 单次 timer 切回睁眼
}

// 在 Screensaver::open() 中
_blink_timer = lv_timer_create(_blink_timer_cb, 4000, this);

// Screensaver::blink() 内部
void Screensaver::blink() {
    lv_img_set_src(_mibao_img, &mibao_close);  // 切闭眼
    lv_timer_t* close_timer = lv_timer_create([](lv_timer_t* t){
        auto* self = static_cast<Screensaver*>(lv_timer_get_user_data(t));
        lv_img_set_src(self->_mibao_img, &mibao_open);  // 200ms 后切回睁眼
    }, 200, this);
    lv_timer_set_repeat_count(close_timer, 1);  // 只跑一次
}
```

**具体规格**：
- 资源：`看你_睁眼.png`（100×100）和 `看你_闭眼.png`（100×100 PNG）
- 位置：固定在屏幕右下角 `(220, 100)`，不移动
- 背景：纯黑（黑底反衬米宝）
- **不显示时间/日期**（避免屏保过密）
- **触屏立即退出**（任何 `lv_obj_has_state` 检测 pressed → 销毁屏保）
- **不阻塞** 录音/遥控器（屏保只是一个 LVGL 浮动层，HAL 仍正常工作）

**代码改动**：
- `screensaver.cpp` 改写：去掉 DvdScreensaver 继承，自定义 LVGL 容器 + 2 个 PNG image + `lv_timer_create` 驱动眨眼中断
- `assets.cpp` 注册 `mibao_screensaver_open.bin` 和 `mibao_screensaver_close.bin`

---

## 3. 启动器（`app_launcher/view/view.cpp` 改造）

**目标**：把 9 个 app 砍到 4 个（会议/个人/IoT/设置），只显示米宝主题 icon。

**当前显示的 9 个 app**（`main.cpp` 中 install 顺序）：
1. AppMeeting
2. AppLauncher（home，本身不显示）
3. AppAiAgent ← 砍
4. AppAvatar ← 砍
5. AppEspnowControl ← 砍（保留源但 hidden）
6. AppAppCenter ← 砍
7. AppEzdata ← 砍
8. AppDance ← 砍
9. AppSetup ← 保留

**阶段 0 后只显示 4 个**：`AppMeeting` / `AppPersonal`（新建）/ `AppIot`（新建）/ `AppSetup`

**Launcher 屏幕**（320×240）：
```
┌──────────────────────────────────────┐
│  [状态栏 28px - 时间/Wi-Fi/电量]    │  ← 用 common/status_bar
├──────────────────────────────────────┤
│   [Quick AI 按钮 58×34 右上]         │  ← 调 xiaozhi 对话（保留）
│                                      │
│  ┌──────┐                            │
│  │ 会议 │  ← 150×150 米宝开会图      │  ← 居中可滚动
│  │ 录音 │                            │
│  └──────┘                            │
│                                      │
│  [左右翻页箭头 52×160 浮动]          │  ← 沿用现状
│  [Page Indicator 底部 24px 居中]     │
└──────────────────────────────────────┘
```

**按钮**：
- **Quick AI**（58×34 右上，标签 "AI"）→ 调起 xiaozhi 智能对话
- **4 个 app icon**（150×150 居中可滚动）→ 调对应 app
- **左右翻页箭头**（52×160 浮动两侧）→ 翻页

**状态显示**：
- 顶部状态栏：`common/status_bar` 已有
- 选中 app 名称：上方 `MontserratSemiBold26` 字体（沿用现状 `DynamicIconLabel`）

**改动**：
- `apps/apps.h` 注释掉 4 个不需要的 include
- `apps/apps.h` 加 `app_mibao_personal/app_mibao_personal.h` 和 `app_mibao_iot/app_mibao_iot.h`
- `main.cpp` install 改成 4 个
- `view.cpp` 中 home icon 改为 `mibao_home_150.bin`

---

## 4. 会议录音（`app_meeting` 改造，**基于已有**）

**状态**：已完整可工作，**只需改品牌** + 加 5 分钟自动分段

**屏幕布局**（320×240，沿用现状）：
```
┌──────────────────────────────────────┐
│  [状态栏 28px]                       │
├──────────────────────────────────────┤
│  ┌─ 信息面板 296×56 ──────────────┐ │  ← 标题 + 状态点 + 时长
│  │ ● 会议录音      00:00          │ │
│  │   就绪   无线：强  存储：就绪  │ │
│  └────────────────────────────────┘ │
│  ┌─ 转写面板 296×84 ──────────────┐ │  ← 4 行滚动显示
│  │                                  │ │
│  │ (4 行转写占位)                  │ │
│  │                                  │ │
│  └────────────────────────────────┘ │
│  [开始 76×32]  [结束 76×32]   底部  │  ← 控制面板
└──────────────────────────────────────┘
```

**按钮**：
- **开始/暂停按钮**（76×32，绿色 `#2DBE8D`，左下 (10, -10)）→ 切换录音状态
- **结束按钮**（76×32，红色 `#E64B4B`，右下 (94, -10)）→ 两次点击确认（第一次变"确认"）
- **返回键**（沿用 `view::create_home_indicator` 的 home indicator，不另外加）

**状态显示**：
- 标题："会议录音"
- 录音状态点（10×10 圆点，红色脉冲动画，录音中显示）
- 时长：`MM:SS` 格式
- 状态文字：就绪 / 录音中 / 暂停 / 已完成 / 错误
- 网络：无线：强/中/弱/关闭
- 存储：就绪 / 未挂载 / 录音中 / 已保存
- 转写：4 行滚动（demo 模式，每 5s 一行）

**改动**：
- ✅ 已完成所有 UI，**无需重写**
- ⚠️ **需补**：5 分钟自动分段（当前 `_audio_source` 持续录音，**没有切段**）
  - 在 `app_meeting.cpp::captureRecordingFrame()` 中加：累计 5×60×16000=4.8M samples 时调 `finalizeRecordingFile()` + `beginRecording()` 切段
  - 分段命名：`meeting_<timestamp>_<seq>.wav`
  - 分段列表存在 `_model.recentTranscripts()` 里显示

**资源**：无新增（沿用 `icon_meeting.png` 和 `meeting_bg.png`）

---

## 5. 个人记录 app（`app_mibao_personal/` 新建）

**目的**：个人随手记录，不上传服务器（只本地存档），与会议 app 共享 WAV 写入逻辑

**屏幕布局**（320×240）：
```
┌──────────────────────────────────────┐
│  [状态栏 28px]                       │
├──────────────────────────────────────┤
│  [← 返回] [标题: 个人记录]   [i 帮助] │  ← 顶栏 40px 高
│  ┌─ 信息面板 296×60 ──────────────┐ │
│  │ ● 个人记录      00:00          │ │
│  │   就绪   存储：就绪            │ │
│  └────────────────────────────────┘ │
│  ┌─ 文件列表 296×80 ──────────────┐ │  ← 滚动显示最近 3 个文件
│  │ ▸ 2026-08-05_1830.wav  3:42   │ │  ← 点击可播放/删除（v2 阶段）
│  │ ▸ 2026-08-05_1755.wav  1:20   │ │
│  │ ▸ 2026-08-04_2201.wav  0:15   │ │
│  └────────────────────────────────┘ │
│  ┌──────────────────────────────┐    │  ← 中央大圆按钮
│  │  ● ● ● ● ●  ●  ●            │    │    直径 120px
│  │                              │    │    红色 #E64B4B（未录音）
│  │  [开始录音]                  │    │    录音中：绿色 + 脉冲
│  │                              │    │
│  │  ● ● ● ● ●  ●  ●            │    │
│  └──────────────────────────────┘    │
│  [提示: 短按开始/停止, 长按切分段]   │  ← 底部 12px
└──────────────────────────────────────┘
```

**按钮**（**全部**需要生效）：
- **← 返回**（左上 (4, 32)，64×32，浅灰 `#E0E0E0` + 黑字）→ 关闭 app 回 launcher
- **i 帮助**（右上 32×32，圆圈 ⓘ）→ 弹出 toast"短按开始/停止"
- **中央大圆按钮**（120×120 居中 (160, 140)）
  - 短按：开始 / 停止 录音
  - 长按：保存当前段并开始新段（5 分钟限制）
- **文件列表行**（每行 30px 高）→ v2 阶段支持点击删除

**状态显示**：
- 标题："个人记录"
- 录音点：10×10 圆点
- 时长：`MM:SS`
- 状态：就绪 / 录音中 / 已保存
- 存储：就绪 / 未挂载 / 录音中 / 已保存
- 文件列表：3 行（最新 3 个）

**数据流**：
```
用户短按大圆按钮
    ↓
AppMibaoPersonal::handlePrimaryAction()
    ↓
mibao::Recorder::start(type=personal, deviceId="mibao-01")
    ↓
[FreeRTOS task 持续读 audio codec]    ← 关键！必须主动轮询
    ↓
mibao::Recorder::onMicData(pcm, samples)  ← 写入 SDCard
    ↓
WAV 文件：/sdcard/personal/rec_YYYYMMDD_HHMMSS.wav
    ↓
用户再次按大圆按钮 → stopMeetingTasks() → 关闭文件
```

**5 分钟自动分段**：
- 累计时长 5 分钟 → 关闭当前文件，开始新文件
- 文件名后缀 `_part<N>`：`rec_20260805_183000_part1.wav`、`rec_20260805_183000_part2.wav`

**返回键（最关键）**：
- 左上角独立按钮：`[← 返回]`，size 64×32，背景 `#E0E0E0`，文字 `#273238`，位置 `(4, 32)`
- 触屏反馈：按下时变深色 `#C0C0C0`
- 点击动作：先 stop 录音（如果有） → 然后 `this->close()` 回 launcher

**资源**：
- `mibao_personal_150.png`（launcher icon）→ 复制到 `assets/mibao/` → 转 bin
- 中央大圆按钮用 `lv_arc` 或简单 `lv_btn`，**不需要 PNG**
- 录音中的米宝动画：复用 `mibao_chat_60.png`（60×60 显示在按钮上方）

**HAL 依赖**：
- `mibao::Recorder`（基于智能手表方案，**新建**）
- `mibao::ConfigManager`（存个人记录相关配置）
- `mibao::FileLister`（扫 SD card 列出 wav 文件）

---

## 6. 物联网控制 app（`app_mibao_iot/` 新建，**复用 MCP 工具**）

**目的**：把原来 xiaozhi AI 对话里"打开硅基一号的风扇"这类 MCP 工具调用，包装成独立触屏 app 入口，**按键和语音都能控制**

**设备类型**（**用户决策 = A：按实际对接**）：
- `fan`（硅基一号的风扇）
- `pump`（硅基一号的水泵）
- `light`（硅基一号的生长灯）
- `heat`（硅基一号的加热垫）

**屏幕布局**（320×240）：
```
┌──────────────────────────────────────┐
│  [状态栏 28px]                       │
├──────────────────────────────────────┤
│  [← 返回] 物联网控制    [↻ 刷新]     │  ← 顶栏 40px
│  ┌────────┐  ┌────────┐              │
│  │ 风扇   │  │ 水泵   │  2×2 网格     │  ← 每格 140×88
│  │  [图]  │  │  [图]  │              │  间距 8px
│  │  ●关  │  │  ●关  │  状态点 + 文字 │
│  └────────┘  └────────┘              │
│  ┌────────┐  ┌────────┐              │
│  │ 生长灯 │  │ 加热垫 │              │
│  │  [图]  │  │  [图]  │              │
│  │  ●关  │  │  ●关  │              │
│  └────────┘  └────────┘              │
│  提示: 点击控制设备 (与语音可同时)   │  ← 底部 12px 灰字
└──────────────────────────────────────┘
```

**按钮**（**全部**需要生效）：
- **← 返回**（左上 (4, 32)，64×32，浅灰底+黑字）→ 关 app 回 launcher
- **↻ 刷新**（右上 64×32）→ 重新 GET 设备列表
- **4 个设备按钮**（每格 140×88）
  - 短按：toggle 开关（on ↔ off）
  - 触屏反馈：按下时变深色（`#155D4A`），状态点变黄 = "控制中"

**状态显示**：
- 设备图标：60×60 PNG（fan/pump/light/heat 风格）
- 设备名称：12px 中文（"风扇" / "水泵" / "生长灯" / "加热垫"）
- 状态点：6×6 圆点（绿色=开、灰色=关、黄色=控制中、红色=失败）
- 状态文字："开" / "关" / "控制中..." / "失败"
- 设备状态 **本地缓存**到 NVS（断电恢复）

**数据流（复用 [hal_mcp.cpp](file:///Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/main/hal/hal_mcp.cpp) 的 `controlGreenhouseActuator`）**：

```
启动时
  ↓
mibao::IoTClient::loadStates()  ← 从 NVS 读上次状态
  ↓
显示在 2×2 网格（4 个设备 + 缓存状态）

用户点设备按钮
  ↓
AppMibaoIot::handleDeviceClick(actuator)
  ↓
**立即**禁用按钮 + 灰色 + 状态点变黄 + 显示"控制中..."
  ↓
mibao::IoTClient::control(actuator_id, "on"|"off")
  ↓
[内部] 调用 controlGreenhouseActuator(actuator, action, 0)
  ↓
POST http://10.51.1.205:5000/actuator_control
  ↓
响应 200 → 更新状态点 + 文字 + 缓存到 NVS
响应非200 → 恢复旧状态 + toast"控制失败"
超时（5s）→ 恢复旧状态 + toast"控制超时"
```

**关键 UX 规则**（**避免 P7 蓝色方块混乱**）：
- **点击后立即禁用按钮**（`lv_obj_add_state(_btn, LV_STATE_DISABLED)`）
- **按钮变灰 + 状态点变黄 + 文字"控制中..."**
- **响应回来再更新状态**（无论成功失败）
- **失败用 toast 提示**（用 `common/toast/` 已有）
- **绝不卡 UI**（HTTP 请求走 `Board::GetInstance().GetNetwork()->CreateHttp()` 同步调用，但单次 5s 内必须返回，UI 线程先 disable 按钮让用户看到反馈）

**按键 + 语音同时生效**（**用户决策 = A：复用 MCP**）：
- 触屏点击：走 `mibao_iot_client` → 调 `controlGreenhouseActuator`（即复用 MCP 的 HTTP 实现）
- 语音："打开硅基一号的风扇" → xiaozhi 自动调 MCP 工具 → 也调 `controlGreenhouseActuator`
- **同一个函数，两条入口**，状态共享 NVS
- v2 阶段：app 内增加"按住说话"按钮，对接 xiaozhi 流式 ASR

**返回键**：
- 左上角独立按钮：`[← 返回]`，64×32，与个人记录 app 一致

**资源**：
- `mibao_iot_150.png`（launcher icon）
- `device_fan_60.png` / `device_pump_60.png` / `device_light_60.png` / `device_heat_60.png`（**用 Seedream 生成**）

**HAL 依赖**（**新建**）：
- `mibao_iot_client.{h,cpp}`：包装 `controlGreenhouseActuator` + NVS 状态缓存
- `mibao::ConfigManager`（存 backend URL，可选）

**后端 URL**（从 `hal_mcp.cpp` 读出）：
```
http://10.51.1.205:5000/actuator_control
```
- 这是树莓派 Flask 服务，**已存在**，不用新建
- 暂不暴露给用户配置（hard-code 进 `mibao_iot_client`）

---

## 7. 设置 app（`app_setup/` 沿用，**只改入口**）

**状态**：实习生已实现 8 个 worker（about/account/ai_agent/audio/connectivity/display/servo/startup/system）

**米宝定制改动**：
- 移除 "AI Agent"、"Servo"（米宝不要舵机）
- 保留 "About"、"Audio"、"Connectivity"、"Display"、"Startup"
- 新增 "IOT Backend"（配置 backend URL）
- 新增 "Wake Word"（显示当前唤醒词 + 测试按钮）

**入口**（launcher icon 用 `mibao_setup_150.png`，**需生成**）

---

## 8. 唤醒词 + 唤醒后 UI（**直接进 AI 对话**）

**唤醒词方案 C**（沿用之前决策）：
- 替换 `xiaozhi-esp32/audio/wake_words/afe_wake_word.cc` 默认命令词为 `["mi bao mi bao"]`
- 集成 `multinet6_zh.bin`（3MB，assets 分区已扩到 5.94M 够用）
- 关闭 WakeNet（默认"hi 乐鑫"）

**唤醒后行为**（**用户决策 = B：直接进入 AI 对话**）：
- 叫"米宝米宝" → xiaozhi 立即进入 listening 状态（**不等用户点**）
- 屏幕右下角显示米宝聊天小图（60×60）+ 中央浮层"我在" 1s 后消失
- 背光从 50% 升到 100%
- 用户直接说话 → ASR → LLM → TTS 回答

**触发方式**（不直接 hook xiaozhi application.cc）：
- 在 `app_launcher::onLauncherRunning()` 中每 200ms 读 `Board::GetInstance().GetDeviceState()`
- 检测到 `idle → listening` 转变时，调用 `mibao::WakeUI::show()`
- 浮层显示 1s 后自动隐藏
- 浮层期间，背光从 50% 升到 100%（屏幕被唤醒）

**资源**：
- `mibao_chat_60.png` 复制到 `assets/mibao/`
- 浮层用代码绘制（文字 + 半透明背景），**不需要 PNG**

**HAL 依赖**：
- `mibao::WakeUI`（**新建**，包装浮层显示/隐藏 + 背光控制）
- 复用 xiaozhi 框架的 `Board::GetInstance().GetDeviceState()`

---

## 9. 状态栏（`common/status_bar/` 扩展）

**当前**：`view::create_status_bar(accent, dark)` 已实现基础（时间 + Wi-Fi）

**扩展**：
- 左：时间（`HH:MM` 24h，20px Montserrat）
- 中：网络（Wi-Fi icon：高中低/关闭）
- 右：电量（`battery_lightning.bin` icon + 百分比文字）

**米宝主题色**：`#2DBE8D` 主色 / `#155D4A` 深色（沿用 app_meeting）

**HAL 依赖**：
- `GetHAL().getBatteryLevel()`（已有）
- `GetHAL().getWifiStatus()`（已有）
- `GetHAL().syncRtcTimeToSystem()`（已有）

---

## 10. 资源清单

### 10.1 需复制到 `fw/main/assets/mibao/`（从 `UI参考图/`）

| 源文件 | 目标文件 | 用途 | 尺寸 |
|--------|---------|------|------|
| `mibao_meeting_150.png` | `mibao_meeting_150.png` | 会议 app icon | 150×150 |
| `mibao_personal_150.png` | `mibao_personal_150.png` | 个人记录 app icon | 150×150 |
| `mibao_iot_150.png` | `mibao_iot_150.png` | IoT app icon | 150×150 |
| `mibao_chat_60.png` | `mibao_chat_60.png` | 唤醒后右下角小图 | 60×60 |
| `看你_睁眼.png` | `mibao_screensaver_open_100.png` | 屏保米宝睁眼 | 100×100 |
| `看你_闭眼.png` | `mibao_screensaver_close_100.png` | 屏保米宝闭眼 | 100×100 |
| `认真工作.png` | `mibao_working.png` | 唤醒后主图（可选） | 150×150 |
| `米宝_开会.png` | （已有 meeting_bg.png） | 会议背景 | 沿用 |

**警告**：**不要用** `看你_睁眼_220.png` / `看你_闭眼_220.png`（220×220 在 320×240 屏上会超界）

### 10.2 需 Seedream 生成（**决策点 #2**）

| 资源 | 用途 | 风格 |
|------|------|------|
| `mibao_setup_150.png` | 设置 app icon | 米宝手持扳手 |
| `device_light_60.png` | IoT 设备-灯 | 灯泡图标 |
| `device_fan_60.png` | IoT 设备-风扇 | 风扇图标 |
| `device_valve_60.png` | IoT 设备-阀门 | 水阀图标 |
| `device_growlight_60.png` | IoT 设备-补光灯 | 植物灯图标 |
| `mibao_home_150.png` | launcher home icon（替换现有） | 米宝微笑脸 |

### 10.3 转 bin

`fw/main/assets/mibao_bin/`（或直接放 `assets_bin/`）：

```python
# 用 archive/convert_mibao_pngs_to_bin.py 工具（已存在但 cf 注释有错，需修正）
# 正确格式：
# header (12 bytes): magic(4) + cf(1) + flags(2) + w(2) + h(2) + stride(2)
# cf=0x06 (ARGB8888)  magic=0x19E70019  stride = w*4
```

`main/assets/assets.cpp` 中加 `get_image("mibao_xxx.bin")` 引用声明

### 10.4 资源大小约束

- assets 分区 5.94M（已扩）
- 所有 mibao PNG 合计应 ≤ 500KB
- 所有 mibao bin 合计应 ≤ 500KB
- `generated_assets.bin` 总大小 ≤ 5.5M

---

## 11. 米宝 HAL 层（`fw/main/hal/mibao_*.{h,cpp}` 新建）

### 11.1 `mibao_config.{h,cpp}`
```cpp
namespace mibao {
struct Config {
    std::string iot_backend_url;    // http://192.168.1.100:8000
    std::string device_id;          // "mibao-01"
    std::string wake_word_pinyin;   // "mi bao mi bao"
};
class ConfigManager {
public:
    static ConfigManager& instance();
    void init();                                   // 在 main.cpp 调一次
    void load(Config& out);
    void save(const Config& in);                   // 写 NVS
};
}
```

### 11.2 `mibao_recorder.{h,cpp}`（**最关键，P0 bug 防御**）
```cpp
namespace mibao {
enum class RecorderType { Meeting, Personal };

class Recorder {
public:
    static Recorder& instance();
    
    // **必须**有 FreeRTOS task 持续读 audio codec
    bool start(RecorderType type, const std::string& device_id);
    void stop();
    bool isRecording() const;
    
    // 5 分钟自动分段：内部计时，到 5min 切段
    // 文件命名：rec_YYYYMMDD_HHMMSS_partN.wav
    
private:
    void recorderTask();     // FreeRTOS task 函数
    void onMicData(const int16_t* pcm, size_t samples);
    void finalizeCurrentSegment();
    void beginNewSegment();
    
    // 内部状态
    FILE* _file = nullptr;
    std::string _current_path;
    int _segment_seq = 0;
    uint32_t _segment_start_ms = 0;
    TaskHandle_t _task_handle = nullptr;
    bool _active = false;
};
}
```

**关键代码模板**（从智能手表方案移植）：
```cpp
bool Recorder::start(RecorderType type, const std::string& device_id) {
    if (_active) return false;
    
    // 1. 启用 audio codec 输入
    GetHAL().enableMicInput(true);
    
    // 2. 创建 FreeRTOS task 持续读 codec
    xTaskCreatePinnedToCore(
        [](void* arg) { static_cast<Recorder*>(arg)->recorderTask(); },
        "mibao_rec", 4096, this, 5, &_task_handle, 1);
    
    _active = true;
    beginNewSegment();
    return true;
}

void Recorder::recorderTask() {
    const size_t BUF_SAMPLES = 1024;
    int16_t buf[BUF_SAMPLES];
    while (_active) {
        size_t n = GetHAL().readMic(buf, BUF_SAMPLES);
        if (n > 0) onMicData(buf, n);
        
        // 5 分钟自动分段
        if (GetHAL().millis() - _segment_start_ms >= 5 * 60 * 1000) {
            finalizeCurrentSegment();
            beginNewSegment();
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(nullptr);
}
```

### 11.3 `mibao_iot_client.{h,cpp}`
```cpp
namespace mibao {
struct IotDevice {
    std::string id;
    std::string name;
    std::string type;      // "light" | "fan" | "valve" | "growlight"
    bool state;            // true = on
};

class IoTClient {
public:
    static IoTClient& instance();
    void init();
    
    // 异步，不阻塞调用方
    void fetchDeviceList(std::function<void(bool ok, std::vector<IotDevice>)> on_done);
    void control(const std::string& device_id, const std::string& action,
                 std::function<void(bool ok, bool new_state)> on_done);
};
}
```

### 11.4 `mibao_wake_word.{h,cpp}`
- 包装 NVS 存储的"米宝米宝"拼音字符串
- 初始化时设置到 xiaozhi 框架
- 提供 `set("mi bao mi bao")` API

### 11.5 `mibao_wake_ui.{h,cpp}`
- 在 launcher 之上显示浮层
- 200ms 轮询 `Board::GetInstance().GetDeviceState()`
- `idle → listening` 转变时显示浮层，10s 后自动隐藏

---

## 12. 阶段开发顺序（**先想清楚再开发**）

### 阶段 0：基础整理（**0 风险**，仅删/移代码）
1. 在 `apps/apps.h` 注释掉 4 个不需要的 app（ai_agent / avatar / espnow_ctrl / app_center / ezdata / dance）
2. **新建** `app_mibao_personal/{app_mibao_personal.h, app_mibao_personal.cpp}`（**仅空壳**，先注册到 launcher 显示空页面）
3. **新建** `app_mibao_iot/{app_mibao_iot.h, app_mibao_iot.cpp}`（**仅空壳**）
4. `main.cpp` install 顺序改为 4 个
5. 编译 + 烧录 → 验证：launcher 显示 4 个 app，3 个原有（会议/遥控/设置）+ 1 个空（个人/IoT）

**验证**：launcher 滚到 4 个 app 都能点开（空 app 显示空白 + 返回键可关）

### 阶段 1：资源准备
1. 复制 7 个米宝 PNG 到 `fw/main/assets/mibao/`
2. 用修正版 `convert_mibao_pngs_to_bin.py` 转 bin 到 `fw/main/assets/mibao_bin/`
3. 在 `main/assets/assets.cpp` 中声明所有新 bin
4. **重编译**验证资源加载不报错

**验证**：`main.cpp` 加临时代码 `auto img = assets::get_image("mibao_screensaver_open_100.bin");` 看是否能成功获取 descriptor

### 阶段 2：屏保改造
1. 改写 `app_launcher/view/screensaver.cpp`：
   - 去掉 DvdScreensaver 继承
   - 自定义 LVGL 容器 + 2 个 image
   - 移动 + 眨眼定时器
2. 编译 + 烧录 → 验证：30s 无操作 → 屏保米宝眨眼 + 移动

**验证**：触屏任意位置 → 屏保立即消失

### 阶段 3：状态栏扩展
1. 在 `common/status_bar/` 加电量 + 百分比显示
2. 编译 + 烧录 → 验证：所有 app 顶部状态栏都有电量

### 阶段 4：个人记录 app
1. 实现 `main/hal/mibao_config.{h,cpp}` + `mibao_recorder.{h,cpp}`
2. 实现 `app_mibao_personal/{h,cpp, view/{h,cpp}}`
3. UI：返回键 + 中央大圆按钮 + 状态显示
4. 编译 + 烧录 → 验证：进入 app → 短按大圆 → 录音开始 → 再按停止 → SDCard 出现 wav 文件

### 阶段 5：IoT app
1. 实现 `main/hal/mibao_iot_client.{h,cpp}`
2. 实现 `app_mibao_iot/{h,cpp, view/{h,cpp}}`
3. UI：返回键 + 2×2 网格 + 状态点
4. 编译 + 烧录 → 验证：4 个设备按钮可点（即使后端没启动，也能正常显示 + toast 失败提示）

### 阶段 6：唤醒词 + 唤醒后 UI
1. 改 `xiaozhi-esp32/audio/wake_words/afe_wake_word.cc` 命令词列表为 `["mi bao mi bao"]`
2. 复制 `multinet6_zh.bin` 到 `main/assets/assets_bin/`
3. 实现 `main/hal/mibao_wake_word.{h,cpp}` + `mibao_wake_ui.{h,cpp}`
4. 在 `app_launcher::onLauncherRunning()` 加 200ms 轮询
5. 编译 + 烧录 → 验证：叫"米宝米宝" → 浮层出现 → 10s 后消失

### 阶段 7：Web 后端对接
1. 后端：FastAPI 接收 `/api/recording/upload` + `/api/iot/devices` + `/api/iot/control`
2. 前端：录音管理页面（按 type 过滤）
3. 固件：录音结束 POST 到 backend
4. IoT：启动 GET 拉列表

### 阶段 8：AI 农业对话（**最后**，需 API key）
1. 阿里云百炼 ASR/LLM/TTS 集成
2. 唤醒后自动开启对话
3. LLM system prompt = 农业专家

---

## 13. 关键决策点（**已拍板**）

| # | 决策 | 用户拍板 | 备注 |
|---|------|----------|------|
| 1 | 屏保动画风格 | **B 固定位置 + 4s 睁 + 200ms 闭** | 简单：两个 timer 驱动图片切换 |
| 2 | IoT 设备类型 | **A 按实际对接（fan/pump/light/heat）** | 与后端一致，不改后端 |
| 3 | IoT 协议 | **HTTP（hal_mcp.cpp 现有实现）** | 树莓派 Flask `10.51.1.205:5000` |
| 4 | IoT app 与 MCP 关系 | **A 复用 MCP，按键和语音都能控制** | 同一个 `controlGreenhouseActuator` 函数 |
| 5 | 唤醒后是否自动开 AI 对话 | **B 直接进入 AI 对话** | 不等用户点 |
| 6 | 录音上传策略 | **A 默认不上传，本地存档** | 后续可加手动同步 |

---

## 14. 验证清单（每阶段跑）

| 阶段 | 验证项 | 通过标准 |
|------|--------|----------|
| 0 | 编译 + 烧录 | 屏幕亮 + launcher 显示 4 个 app + 3 个原有 app 可进 |
| 1 | 资源加载 | 无 build error + 无 asset 找不到 |
| 2 | 屏保 | 30s 后出现米宝 + 眨眼 + 移动 + 触屏退出 |
| 3 | 状态栏 | 所有 app 顶部显示电量百分比 |
| 4 | 个人记录 | 录音 → SDCard 有 wav 文件 + 时长正确 + 返回键关 app |
| 5 | IoT | 4 设备按钮可点 + toast 失败提示（无后端）+ 状态点正确 |
| 6 | 唤醒词 | 叫"米宝米宝" → 浮层出现 + 10s 后消失 |
| 7 | Web 端 | 上传录音 → 列表显示 + IoT 控制请求成功 |
| 8 | AI 对话 | 问"番茄叶子发黄" → 听到农业相关回答 |

---

## 15. 风险与防御

| 风险 | 防御 |
|------|------|
| mibao_recorder 录音 0 字节 | 智能手表方案：FreeRTOS task + codec->Read + 持续轮询 |
| 资源太大烧不进 flash | 屏保/图标 ≤ 150×150，bin 总和 ≤ 500KB |
| 屏保遮挡其他 app | 触屏事件立即销毁屏保，30s 阈值 |
| IoT 无后端点击卡住 | 立即禁用按钮 + 5s 超时回滚 + toast |
| 唤醒词误触发 | 浮层显示而非直接进 xiaozhi，10s 自动消失 |
| 烧录后屏幕不亮 | 沿用烧录步骤：bootloader offset 0x0 + erase_flash + ESP-IDF v5.5.4 |
| 中文显示乱码 | 沿用 `meeting_zh_font.c`，新增字符需重新生成 |

---

## 16. 文件改动清单（**总览**）

### 新建（**约 25 个**）
- `fw/main/apps/app_mibao_personal/{app_mibao_personal.h, app_mibao_personal.cpp, view/{view.h, view.cpp}}`
- `fw/main/apps/app_mibao_iot/{app_mibao_iot.h, app_mibao_iot.cpp, view/{view.h, view.cpp}}`
- `fw/main/hal/mibao_config.{h,cpp}`
- `fw/main/hal/mibao_recorder.{h,cpp}`
- `fw/main/hal/mibao_iot_client.{h,cpp}`
- `fw/main/hal/mibao_wake_word.{h,cpp}`
- `fw/main/hal/mibao_wake_ui.{h,cpp}`
- `fw/main/assets/mibao/*.png`（从 `UI参考图/` 复制，**约 8 个**）
- `fw/main/assets/mibao_bin/*.bin`（PNG 转 bin，**约 8 个**）
- `fw/tools/png2bin.py`（修正版 PNG→bin 工具，cf 字段修正）

### 修改（**约 10 个**）
- `fw/main/apps/apps.h`（注释 6 个 include + 加 2 个新 include）
- `fw/main/main.cpp`（install 顺序改 4 个）
- `fw/main/apps/app_launcher/view/screensaver.cpp`（屏保改造）
- `fw/main/apps/app_launcher/view/view.cpp`（home icon 换米宝）
- `fw/main/apps/app_launcher/app_launcher.cpp`（加 WakeUI 轮询）
- `fw/main/apps/app_meeting/app_meeting.cpp`（5 分钟自动分段）
- `fw/main/assets/assets.cpp`（注册新 bin）
- `fw/main/common/status_bar/status_bar.cpp`（加电量百分比）
- `fw/StackChan/audio/wake_words/afe_wake_word.cc`（命令词改 `["mi bao mi bao"]`）
- `fw/sdkconfig.defaults`（关 WakeNet + 开 MultiNet6）

### 注释不改源（**约 6 个 app 目录**）
- `fw/main/apps/app_ai_agent/`
- `fw/main/apps/app_avatar/`
- `fw/main/apps/app_dance/`
- `fw/main/apps/app_ezdata/`
- `fw/main/apps/app_app_center/`
- `fw/main/apps/app_espnow_ctrl/`（保留源，ESP-NOW 仍用）

---

**下一步**：等用户 review 完本计划 + 拍板 5 个决策点后，再开始动代码。
