# StackChan 任务进度交接

更新时间：2026-07-16  
工程目录：`C:\esp\robot\fw`  
机器人串口：`COM3`（ESP32-S3，MAC：`68:ee:8f:d7:3b:68`）

## 当前目标与完成状态

本轮完成了 StackChan 会议录音 App 的中文显示、遥控录音控制和录音结束后的状态恢复。当前固件已构建并烧录到机器人，所有写入均已通过 esptool 哈希校验。

| 项目 | 状态 | 说明 |
|---|---|---|
| 会议 App 中文化 | 已完成 | 界面可见文本已改为中文。 |
| 中文字体显示 | 已完成 | 使用会议页专用的非压缩中文子集字体，覆盖会议源码中的全部中文字符。 |
| 遥控器短按控制录音 | 已完成 | 短按 B 可开始、暂停、继续。 |
| 遥控器长按结束录音 | 已完成 | 长按 B（≥ 1 秒）结束并保存 WAV。 |
| 结束后重新开始 | 已完成 | 已完成/错误状态下短按 B 直接开始新录音；长按 B 回到就绪。 |
| 服务器上传/转写 | 未接入 | 当前仅保存本地 WAV；“等待服务器接口”为占位提示。 |

## 关键结论：遥控器协议

最初按工作区中的遥控器源码推断其发送原生 9 字节 ESP-NOW 数据，但真实硬件串口日志显示机器人收到的帧长度为 **29 字节**。

实际遥控器运行的是 Espressif `esp-now` 组件封装协议：

```text
20 字节 Espressif ESP-NOW 组件头 + 9 字节会议控制载荷 = 29 字节
```

9 字节会议载荷：

| 字节 | 含义 |
|---:|---|
| `[0]` | 目标 ID，0 为广播，1 为机器人 |
| `[1..2]` | yaw（会议模式为 0） |
| `[3..4]` | pitch（会议模式为 0） |
| `[5..6]` | speed（会议模式为 600） |
| `[7]` | 命令：1 主操作，2 结束 |
| `[8]` | 命令序号，用于去重 |

因此机器人端已改为使用 Espressif `esp-now` 组件接收。组件会先校验并剥离 20 字节头，再把正确的 9 字节载荷交给会议 App。

遥控器操作约定：

- 进入遥控器会议模式。
- 短按 BtnB：开始 / 暂停 / 继续；会议已完成时，直接开始一场新录音。
- 长按 BtnB（≥ 1 秒）：录音中时结束并保存；已完成时清空当前会议并回到就绪。

机器人串口验证成功时应出现：

```text
EspNow: meeting command accepted: command=1 sequence=...
```

或：

```text
EspNow: meeting command accepted: command=2 sequence=...
```

## 本轮主要代码变更

### 1. ESP-NOW 协议兼容

文件：[main/hal/hal_espnow.cpp](main/hal/hal_espnow.cpp)

- 改为 `espnow_init()` 和 `espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_DATA, ...)`。
- 接收回调只处理组件已解封的 payload。
- 接受目标 ID 为 0 或 1，命令为 1 或 2 的帧。
- 通过命令序号去重，避免组件重传导致重复开始/结束。
- 保留 Wi-Fi/ESP-NOW 信道检查；会议 App 使用信道 1。

### 2. 会议 App 录音状态恢复

文件：[main/apps/app_meeting/app_meeting.cpp](main/apps/app_meeting/app_meeting.cpp)、[main/apps/app_meeting/app_meeting.h](main/apps/app_meeting/app_meeting.h)

- 新增 `resetFinishedMeeting()`，统一清除已完成/错误会议的状态、路径、录音计数和提示行。
- 已完成或错误状态下：
  - 主操作（短按 B / 开始按钮）会重置并立即开始新录音。
  - 结束操作（长按 B / 结束按钮）只重置到就绪。
- 首页返回通过延迟关闭处理，避免在 LVGL 锁内重入关闭导致桌面菜单卡住。

### 3. 中文字体

文件：[main/assets/fonts/meeting_zh_font.c](main/assets/fonts/meeting_zh_font.c)、[main/CMakeLists.txt](main/CMakeLists.txt)

- 使用普惠字体生成了会议页专用、16px、4bpp、非压缩字库。
- `meeting_zh_font.c` 被显式加入 CMake 源文件，避免 `file(GLOB_RECURSE)` 不能自动发现新增文件的问题。
- 字体覆盖会议页面源码中使用的所有汉字。

## 当前录音与服务器状态

录音结束后界面显示：

```text
已保存：/sdcard/meetings/...
上传：等待服务器接口
```

这不是网络阻塞。当前代码的行为是保存本地 WAV 后直接标记会议完成；上传类仅有协议骨架，未创建 WebSocket 传输实例，也未配置服务器地址。

如需接入音频上传和实时/离线转写，服务端需提供：

- WebSocket 地址（推荐 `wss://...`）。
- 鉴权方案与令牌获取方式。
- `device_id`、`client_id` 的填写规则。
- 接收 PCM16 音频二进制帧的能力。
- ACK 消息格式：`{"type":"ack","sequence":123}`。
- 转写消息格式：`{"type":"transcript","sentenceId":...,"speakerId":...,"final":true,"startMs":...,"endMs":...,"text":"..."}`。

音频协议定义在 `main/apps/app_meeting/meeting_protocol.h`：SCMT v1，固定 24 字节头加 PCM16 载荷。

## 构建与烧录

已验证的构建环境：

```powershell
$env:IDF_PATH='C:\esp\robot\esp-idf-env\v5.5.4\esp-idf'
$env:ESP_ROM_ELF_DIR='C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\lib\esp32s3\rom'
& 'C:\Espressif\tools\cmake\3.30.2\bin\cmake.exe' --build build -j 8
```

最后一次构建结果：

```text
stack-chan.bin binary size 0x3ba560
最小应用分区 0x4f0000
剩余约 1.3 MiB
```

串口监视：

```powershell
C:\Espressif\tools\python\v5.5.4\venv\Scripts\python.exe -m serial.tools.miniterm COM3 115200
```

退出串口监视：`Ctrl+]`。

## 下一阶段：家用 AI 设备智能控制 Agent

下一次对话的目标是将 StackChan 用作家用 AI 设备控制 Agent。建议按以下顺序推进：

1. 明确待控设备及其现有接入方式：Home Assistant、米家、涂鸦、MQTT、局域网 HTTP、红外或蓝牙等。
2. 优先采用一个统一控制中枢（推荐 Home Assistant 或 MQTT），避免机器人分别对接多个厂商私有协议。
3. 在机器人端定义受限的设备控制工具，例如：查询设备状态、开关灯、调节亮度、控制空调模式与温度、执行预定义场景。
4. 加入设备白名单、参数范围校验和危险操作确认；门锁、燃气、窗帘等设备应使用更严格的授权策略。
5. 接入语音/LLM 指令解析后，先执行“识别意图 → 显示/语音确认 → 调用设备工具 → 回报结果”的闭环。
6. 记录命令、设备响应和失败原因，方便排查网络、鉴权和设备离线问题。

开始下一阶段前，建议准备：

- 家中 AI 设备清单与品牌/型号；
- 是否已有 Home Assistant、MQTT Broker 或可用的本地服务器；
- 希望控制的首批动作；
- 网络拓扑，以及可提供的接口文档或测试账号（不要在对话中直接粘贴长期有效密钥）。
