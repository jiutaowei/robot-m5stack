# UiFlow2 开发指南

UiFlow2 是 M5Stack 的图形化编程平台(Blockly 积木),入门最快,适合快速原型与演示。StackChan 有专属 UiFlow2 固件与积木块。

> 来源:https://docs.m5stack.com/en/uiflow2/stackchan/program

---

## 1. 烧录 UiFlow2 固件(用 M5Burner)

1. 下载并打开 **M5Burner**(M5Stack 官方跨平台烧录工具)。
2. 在 M5Burner 中搜索 **StackChan** 对应的 UiFlow2 固件并下载。
3. USB-C 连接 StackChan(主机口与底座口均支持数据,**推荐底座口**避免舵机误动)。
4. **进入下载模式**:长按 `RST` 复位键约 3 秒,指示灯变绿后松开。
5. M5Burner 点击 `Burn`,配置参数:
   - **COM**:设备对应串口
   - **BaudRate**:串口波特率(M5Burner 可配置)
   - **Server**:设备连接的服务器地址
   - **WIFI SSID / WIFI Password**:Wi-Fi 凭据
   - **SNTP Server**:SNTP0 阿里云 NTP(中国)/ SNTP1 日本 / SNTP2 全球
   - **Timezone**:时区(中国填 `8` 或 `Asia/Shanghai`)
   - **Boot Option**(三选一):
     - `Run main.py directly` — 直接运行用户程序
     - `Show startup menu and network setup` — 显示启动菜单与网络配置
     - `Only network setup` — 仅网络配置
6. 显示 `Burn successfully` 即成功,复位设备。

> 烧录后可随时在 M5Burner 点 `Configure` 修改上述参数。

---

## 2. 连接设备编程

### 2.1 无线连接(Access Code)
1. 设备连网后,在 UiFlow2 启动屏查看 **Access Code**。
2. 浏览器访问 https://uiflow2.m5stack.com
3. 点 `Select Device` → `Connect Device`,输入 Access Code 即可。

### 2.2 USB 有线连接
1. Web IDE 中选 StackChan → `WebTerminal`。
2. 选择串口 → `Connect`,显示 `Connected to Serial Port!` 即成功。

---

## 3. 编程

- 拖拽 Blockly 积木编写程序。
- `Run Once`:单次测试运行(不下发到设备长期运行)。
- `Run Always`:下载到设备持久运行。

StackChan 专属积木覆盖:显示/表情、舵机运动、触摸传感器、RGB LED、扬声器等。

---

## 4. 适用场景

- 快速做出可演示的效果(表情、动作、语音)
- 非程序员参与开发
- 教学与原型验证

> ⚠️ 警告:UiFlow2 中操作舵机时同样遵守 Y 轴 5°~85° 限位,切勿手动强转舵机。
