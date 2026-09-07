# 米宝一号改进计划（IMPROVEMENT_PLAN）

> 基于实习生版本（v1.4.3 已烧录验证）+ 产品定义。
> 工作目录：`robot-updating/`（基于 `robot-original/` 解压，零改动起点）
> 烧录验证：m5burner 1.4.4 屏幕 OK → 实习生 v1.4.3 屏幕 OK
> ESP-IDF：v5.5.4（实习生用版本，**不是** ~/esp/esp-idf 的 v5.5.3）

---

## 产品定义速查（米宝一号）

- **产品名**：米宝一号（Mibao One），设备 ID `mibao-01`
- **形态**：StackChan 桌面机器人（CoreS3 / ESP32-S3）
- **核心场景**：农业行业 AI 对话 + 智能会议纪要 + 个人记录 + 物联网控制
- **三大功能**（必须）：
  1. 会议录音
  2. 个人记录
  3. 物联网控制（灯/风扇/阀门/补光灯）
- **屏保**：米宝眨眼动画（你/看你形象）
- **唤醒词**："米宝米宝"（拼音 `mi bao mi bao`），熄屏可唤醒
- **唤醒后**：右下角米宝小图 + 文字"我在" + 引导语
- **AI 限制**：仅农业相关
- **顶部状态栏**：时间 / Wi-Fi / 电量
- **录音文件管理**：本地存档 + 同步 web 端，web 端标记 type=meeting/personal
- **不做**：dance 移动、Avatar 表情、企业微信对接

**阿里云百炼**（待确认 API key 是否就绪）：
- ASR：`paraformer-realtime-v2`（16kHz, pcm, zh）
- LLM：`qwen-plus`（system = 农业专家）
- TTS：`cosyvoice-v2`（longxiaochun, 22050Hz, mp3）

---

## 实习生版本现状（v1.4.3，screen-tested OK）

### ✅ 已完成（保留）
- 会议录音 app：`app_meeting/` 完整 16 个 .h/.cpp（wav_writer / segment_store / meeting_recorder / meeting_uploader / meeting_protocol / meeting_manifest / meeting_ui_model / meeting_types / audio_source / stackchan_audio_source）
- 中文显示：会议页专用 16px 中文子集字体 `meeting_zh_font.c`
- 遥控器协议：29 字节 ESP-NOW（20 字节 Espressif 组件头 + 9 字节会议载荷），已在 `hal_espnow.cpp` 验证
- 录音状态机：resetFinishedMeeting()、暂停/继续/重置逻辑
- 编译环境：ESP-IDF v5.5.4
- 分区表：assets 5.94M（已扩，容纳 Multinet 唤醒词模型）
- 米宝开会.png：3.3M 资源原图
- CMakeLists 用 GLOB_RECURSE 抓 main/hal + main/apps

### ⚠️ 现有但不适用（隐藏，不删）
- app_ai_agent - 不用
- app_app_center - 不用
- app_avatar - **不做**（米宝不用虚拟形象）
- app_dance - **不做**（米宝不做 dance 移动）
- app_ezdata - 不用
- app_espnow_ctrl - **保留源**，但**不显示在 launcher**（遥控器协议仍用 ESP-NOW 接收）

### ❌ 缺（要新建）
- 屏保：米宝眨眼动画（vs 实习生默认屏保）
- 唤醒词：米宝米宝（vs 默认 hi 乐鑫）
- 唤醒后 UI：右下角米宝图 + 文字回复
- 个人记录 app（无）
- IoT 控制 app（无）
- 农业 AI 系统提示词（无）
- 顶部状态栏（部分在 common/status_bar，但米宝要完整版）
- 阿里云百炼 ASR/LLM/TTS（无）
- 录音文件上传 web 端（占位提示，**没接通**）

---

## 改进路线（5 阶段，每阶段可独立验证）

### 阶段 0：基础（不破坏现有功能）
- [ ] 在 `apps/apps.h` 注释掉不需要的 app 注册（不删源）
- [ ] 在 `app_launcher` 里只显示米宝 3 个 app（会议/个人/IoT）+ 设置
- [ ] 验证编译 + 烧录，**屏保和 launcher 应该还在**（只是隐藏了 app）

### 阶段 1：米宝主题化（视觉）
- [ ] 新增 `main/assets/mibao/` 目录：放米宝 PNG（眨眼、开会、工作、IoT 图标等）
- [ ] 新增 `main/assets/mibao_bin/` 目录：PNG 转 LVGL 9 bin
- [ ] `main/assets/assets.cpp` 注册新 bin 文件
- [ ] 替换 `app_launcher` 的 home icon / 背景
- [ ] 替换屏保（`app_launcher/view/screensaver.cpp`）为米宝眨眼
- [ ] `common/status_bar/` 完善：时间（24h）/ Wi-Fi 强度 / 电量百分比

### 阶段 2：唤醒词 + 唤醒后 UI
- [ ] `main/hal/mibao_wake_word.{h,cpp}`：包装 `mibao::WakeWord` + 集成 ESP-SR MultiNet6
- [ ] 替换 `xiaozhi-esp32` 默认唤醒词为 `["mi bao mi bao"]`（参考 xiaozhi `afe_wake_word.cc`）
- [ ] 加 multinet6_zh.bin 模型到 `main/assets/assets_bin/`（需 3MB+ 空间，assets=5.94M 已够）
- [ ] 唤醒后：在 `app_launcher` 上 overlay 一个 150×150 米宝小图（认真工作.png） + 文字"我在" + 引导语
- [ ] 验证：叫"米宝米宝"→ 屏幕唤醒 + 出现米宝 + 文字

### 阶段 3：米宝三大功能
- [ ] **会议录音**（基于 app_meeting，改品牌）
  - 改 launcher icon 为 `icon_meeting_150.bin`（150×150 ARGB8888，别用 220×220）
  - 加米宝主题色（背景 + 文字）
  - 增加 5 分钟自动分段（已部分支持，需补全）
- [ ] **个人记录 app**（新建 `app_mibao_personal/`）
  - 复用 meeting 录音架构（wav_writer / segment_store / meeting_recorder）
  - 但 type=personal，文件存到 `/sdcard/personal/`
  - UI：单按钮"开始/停止"，跟米宝主题色一致
- [ ] **IoT 控制 app**（新建 `app_mibao_iot/`）
  - 启动时从后端拉取设备列表（fallback 4 个默认：灯/风扇/阀门/补光灯）
  - 2×2 设备网格按钮
  - 点击立即禁用 + 显示"控制中..."
  - 调 `mibao::IoTClient::control(device, "on"|"off")`
  - 成功更新 UI / 失败 toast
- [ ] 米宝 HAL 层（新增 `main/hal/mibao_*.{h,cpp}`）
  - `mibao_config.{h,cpp}` - NVS 配置（iot_url, llm_api_key, etc）
  - `mibao_recorder.{h,cpp}` - 录音基类（**用 FreeRTOS task 主动轮询 codec**，不要被动回调）
  - `mibao_iot_client.{h,cpp}` - HTTP 调 IoT 后端
  - `mibao_wake_word.{h,cpp}` - 见阶段 2
  - `mibao_ai_dialog.{h,cpp}` - 阿里云百炼 LLM 对话（可选，阶段 5）
  - `mibao_asr.{h,cpp}` - 阿里云 ASR（可选，阶段 5）
  - `mibao_tts.{h,cpp}` - 阿里云 TTS（可选，阶段 5）

### 阶段 4：Web 端对接
- [ ] 后端：`stackchan-fixed-blinkfix/server/` 或新建 `backend/`
  - POST `/api/recording/upload` 接收 WAV 文件 + type 字段
  - GET `/api/recording/list?type=meeting` 列出文件
  - DELETE `/api/recording/:id` 删除文件
  - GET `/api/iot/devices` 拉取设备列表
  - POST `/api/iot/control` 控制设备
- [ ] 前端：录音管理页面（按 type 过滤）+ IoT 设备管理
- [ ] 米宝固件：录音结束后 POST 到 backend，上传成功才标记 "已上传"
- [ ] IoT 设备：启动时 GET 拉列表，控制时 POST

### 阶段 5：AI 对话（农业专家）
- [ ] 阿里云百炼 API key 配进 NVS
- [ ] 唤醒后自动开启对话（"我在" 说完后听用户说）
- [ ] ASR → LLM → TTS 闭环
- [ ] LLM system prompt：农业专家（限制范围）
- [ ] 录音类 app 不受影响（保持本地录音独立）

---

## 米宝 HAL 标准模式（必须遵守，避免重犯旧错）

### Recorder（**最关键**）
- **必须**有 FreeRTOS task 持续读 audio codec
- ❌ **不要**只实现 `onMicData` 接口（被动回调，没人调）
- ✅ 智能手表方案：在 `start()` 里 `xTaskCreatePinnedToCore` 跑 task
- 任务循环：`while(active) { codec->Read(buf, n); onMicData(buf, n); vTaskDelay(10); }`
- WAV 文件头：开始时占位，结束时回填 data_size
- 文件命名：`rec_YYYYMMDD_HHMMSS.wav`

### 返回键
- 左上角独立按钮 `LV_SYMBOL_LEFT " 返回"`，64×32 size
- 背景色对比强（如浅灰 #E0E0E0 + 黑字）
- 位置 `LV_ALIGN_TOP_LEFT, 4, 4`

### LVGL 9 资源格式
- bin 文件 12 字节 header：`magic(8) cf(8) flags(16) w(16) h(16) stride(16) reserved(16)`
- 合法 cf：ARGB8888=0x06
- PNG→bin：用 PIL 读 PNG 拿 raw ARGB，手写 header
- 屏保/大图 ≤ 150×150（240×240 屏幕），别用 220×220

### 米宝 app 命名
- 头文件：`main/apps/app_mibao_<name>/{app_mibao_<name>.h, app_mibao_<name>.cpp, view/view.h, view/view.cpp}`
- 类名：`AppMibao<Name>` 继承 `mooncake::AppAbility`
- View 命名空间：`namespace Mibao<Name>View { void build(); void destroy(); }`

### 资源大小约束
- assets 分区 5.94M（已经扩好）
- 屏保/大图 ≤ 150×150
- generated_assets.bin 控制在 5.5M 以内

---

## 编译/烧录命令（macOS Terminal.app）

```bash
cd "/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw"

# 1) 编译
source ../esp-idf-env/v5.5.4/esp-idf/export.sh
export IDF_PATH="$(pwd)/../esp-idf-env/v5.5.4/esp-idf"
idf.py set-target esp32s3
idf.py reconfigure
idf.py build

# 2) 烧录（5 个文件，bootloader offset 用 0x0）
esptool.py --chip esp32s3 -b 460800 erase_flash   # 先擦
esptool.py --chip esp32s3 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0       build/bootloader/bootloader.bin \
  0x8000    build/partition_table/partition-table.bin \
  0xd000    build/ota_data_initial.bin \
  0x20000   build/stack-chan.bin \
  0xA00000  build/generated_assets.bin
```

**关键**：bootloader offset **必须 0x0**（不是 0x1000）——这是之前烧录后屏幕不亮的根因。

---

## 验证清单（每个阶段完成后跑）

- [ ] 阶段 0：编译 OK + 烧录 OK + 启动 launcher + 会议 app 还在
- [ ] 阶段 1：屏保是米宝眨眼 + launcher 用米宝图标 + 状态栏显示时间/Wi-Fi/电量
- [ ] 阶段 2：叫"米宝米宝" → 屏幕亮 + 米宝小图 + "我在" 文字
- [ ] 阶段 3：3 个 app 都能进 + 录音能录 + IoT 能控制
- [ ] 阶段 4：web 端能看到录音文件 + IoT 设备列表能拉
- [ ] 阶段 5：对话能听懂"番茄叶子发黄怎么办" + 农业相关回答

---

## 不要做的事（learned from stackchan-fixed-blinkfix 大坑）

- ❌ 不要改 `esp-idf-env/` 里任何东西
- ❌ 不要覆盖 `assets_bin/icon_*.bin` 中**已存在**的原生 app 图标（用新名字如 `icon_meeting_150.bin`）
- ❌ 不要把 sdk 路径写死成 `~/esp/esp-idf`（实习生用 v5.5.4，不是 v5.5.3）
- ❌ 不要改 bootloader offset 0x0 为别的（这是 v5.5+ 格式）
- ❌ 不要在 `main/CMakeLists.txt` 加 `mibao_*.cpp` 后不跑 `idf.py reconfigure`
- ❌ 不要用 `ESP_LOGI`（用 `mclog::tagInfo`）
- ❌ 不要实现 `Recorder::onMicData` 当成被动回调（必须 FreeRTOS task 主动轮询）
- ❌ 不要烧录前忘了 `erase_flash`（残留会导致 OTA 启动混乱）

---

## 参考文档

- 实习生交接：`fw/StackChan任务进度交接.md`
- 烧录步骤：`fw/STACKCHAN_修复后安装烧录步骤.md`
- 录音方案：`fw/StackChan录音设备升级方案.md`
- 遥控器：`fw/遥控器开发现状与录音控制联调说明.md`
- 产品定义：`产品定义与优化方向.md`（项目根目录）
- 跨项目经验：`智能手表 voice_recorder`（录音架构参考）
