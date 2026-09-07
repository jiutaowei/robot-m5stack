# 编译过程日志

> 目的：把每次 `idf.py` 操作的完整日志保存在这里，AI 直接读这个文件，不用用户复制粘贴。
> 用户操作：跑完命令后 `cat /tmp/build.log >> /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/BUILD_LOG.md` 追加到本文档。

---

## 2026-08-05 阶段 0 编译验证

### 背景
- 目标 ESP-IDF：v5.5.4（实习生配的版本，在 `robot-updating/esp-idf-env/v5.5.4/esp-idf`）
- 已装工具链（`~/.espressif/tools/`）：`xtensa-esp-elf/esp-14.2.0_20251107`、`riscv32-esp-elf/esp-14.2.0_20251107`、`xtensa-esp-elf-gdb/16.3_20250913`
- v5.5.4 期望工具链版本：`esp-14.2.0_20260121`（**与已装不一致**）
- v5.5 (~/esp/esp-idf) 用已装工具链（20251107）能识别

### 第 1 次尝试：用 v5.5.4
```
source ../esp-idf-env/v5.5.4/esp-idf/export.sh
```
**失败**：
```
ERROR: tool riscv32-esp-elf-gdb has no installed versions.
ERROR: tool xtensa-esp-elf has no installed versions.
ERROR: tool riscv32-esp-elf has no installed versions.
```
**根因**：v5.5.4 期望 `20260121`，已装 `20251107`。

### 第 2 次尝试：idf_tools.py install
```
/Users/cobain/.espressif/python_env/idf5.5_py3.14_env/bin/python ./tools/idf_tools.py install
```
**现象**：卡在 17% 几分钟后无进度，疑似网络问题。
**用户反馈**："还是很慢，甚至没有百分比了"

### 第 3 次尝试：用 ~/esp/esp-idf (v5.5)
```
source ~/esp/esp-idf/export.sh
export IDF_PATH=~/esp/esp-idf
idf.py set-target esp32s3
```
**失败**：
```
Failed to resolve component 'ArduinoJson' required by component 'main': unknown name.
```
**根因**：`components/ArduinoJson/idf_component.yml` 缺 `name` 字段。

### 第 4 次尝试：修复 ArduinoJson idf_component.yml
**修改**：
```yaml
# /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/components/ArduinoJson/idf_component.yml
name: "bblanchon/arduinojson"   # 加这一行
version: "7.4.2"
```
**结果**：通过 reconfigure，ArduinoJson 错误消失，但出现**新错误**：
```
xtensa-esp-elf-g++ is not a full path and was not found in the PATH.
xtensa-esp-elf-gcc is not a full path and was not found in the PATH.
```

### 第 5 次尝试：Trae 跑 idf.py（独立子进程）
**问题**：Trae 触发的是独立子进程，**继承不到 Terminal.app 的 PATH 环境变量**。
- 用户在 Terminal 看到 `~/.espressif/tools/xtensa-esp-elf/.../bin` 在 PATH 里
- 但 Trae 的子进程看不到这个 PATH
- 即使前面 reconfigure 走通了 ArduinoJson 错误，新的工具链错误又出现

### 第 6 次尝试：用户直接在 Terminal.app 跑
```
cd "/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw"
source ~/esp/esp-idf/export.sh
export IDF_PATH=~/esp/esp-idf
idf.py reconfigure
```
**结果**：
```
Failed to resolve component 'ArduinoJson' required by component 'main': unknown name.
```
**根因**：
- v5.5 (~/esp/esp-idf) 的 `idf_tools.py` 和 v5.5.4 的不一样
- 之前在 Trae 里跑的 `idf.py reconfigure` 通过了 ArduinoJson 错误，说明 Trae 跑有 PATH 但用的不是 ~/esp/esp-idf
- 用户在 Terminal.app 跑的是 ~/esp/esp-idf，**这个版本的 component manager 对 `name: "bblanchon/arduinojson"` 这种格式不识别**

**用户反馈**："感觉你用了ESP-IDF5.5有点水土不服啊，提高效率解决问题"

### 第 7 次：调查 v5.5.3 的 idf_component_manager 行为

**根因定位**（用户要求"提高效率"，所以一次性深挖）：

1. v5.5.3 的 `tools/idf.py` 第 52-53 行：
   ```python
   if os.getenv('IDF_COMPONENT_MANAGER') != '0':
       from idf_component_manager import idf_extensions
   ```
2. 用户 Python 虚拟环境 `~/.espressif/python_env/idf5.5_py3.14_env/lib/.../site-packages/` 已安装 `idf_component_manager-2.4.11`
3. **idf_component_manager 看到 `idf_component.yml` 里的 `name: "bblanchon/arduinojson"` 字段后，会把它当作"来自 component registry 的依赖"，去下载到 `managed_components/` 目录，而不是用本地 `components/ArduinoJson/`**
4. **没有 name 字段的本地 component（如 `components/esp-now/`、`components/mooncake/`）就被正常识别为本地 component**

**对比**：
- `components/esp-now/idf_component.yml` → 只有 `version` 和 `dependencies`，**没有 name** → 本地 component 正常用
- `components/ArduinoJson/idf_component.yml` → 我加的 `name: "bblanchon/arduinojson"` → 被 idf_component_manager 接管

**修复**：删除 `components/ArduinoJson/idf_component.yml` 的 `name` 字段，保留 `version/description/url/files`。

```yaml
# 修复前（错）
name: "bblanchon/arduinojson"
version: "7.4.2"
description: ...
```

```yaml
# 修复后（对）
## IDF Component Manager Manifest File
version: "7.4.2"
description: ...
url: https://arduinojson.org/
files:
  exclude: ...
```

**用户重新跑**：
```bash
cd "/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw"
source ~/esp/esp-idf/export.sh
export IDF_PATH=~/esp/esp-idf
idf.py reconfigure
```

**仍然失败**：
```
CMake Error at /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:330 (message):
  Failed to resolve component 'ArduinoJson' required by component 'main':
  unknown name.
```

### 第 8 次：深挖根因 - idf_component_manager 接管了 ArduinoJson

**关键证据**（用户跑 stdout 里出现）：
```
NOTICE: Processing 60 dependencies:
NOTICE: [1/60] 78/esp-ml307 (3.6.5)
...
NOTICE: [60/60] idf (5.5.3)
```
**60 个依赖全部从 registry 下载并处理**（managed_components/ 下有 59 个），但 **ArduinoJson 不在其中**。

**核心代码**（`idf_component_manager/core.py:729-734`）：
```python
# Check that CMakeLists.txt and idf_component.yml exists for all component dirs
local_components = [
    component
    for component in local_components
    if os.path.isfile(os.path.join(component['path'], 'CMakeLists.txt'))
    and os.path.isfile(os.path.join(component['path'], MANIFEST_FILENAME))
]
```
**`idf_component_manager` 接管了 `components/ArduinoJson/`（因为同时有 CMakeLists.txt 和 idf_component.yml）**。

**对比**（同样作为本地 component）：
- `components/mooncake/` 只有 `CMakeLists.txt` + `library.json`（PlatformIO 用）→ **idf_component_manager 不接管**
- `components/mooncake_log/` 同上 → **idf_component_manager 不接管**
- `components/smooth_ui_toolkit/` 同上 → **idf_component_manager 不接管**
- `components/esp-now/` 有 `idf_component.yml` 带 `dependencies: cmake_utilities` → idf_component_manager 接管下载 `espressif__cmake_utilities`，但 `esp-now` 自己作为本地 component 仍被 `__project_component_dir` 加进去
- `components/ArduinoJson/` 有 `idf_component.yml` 但**只**有 `version/description/url/files`（没 dependencies）→ idf_component_manager "接管"但没正确处理 component target 注册，**导致 CMake 找不到 ArduinoJson**

**修复**：**直接删除 `components/ArduinoJson/idf_component.yml`**（跟 mooncake 一致）。

```bash
rm /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/components/ArduinoJson/idf_component.yml
```

**用户重新跑**：
```bash
cd "/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw"
source ~/esp/esp-idf/export.sh
export IDF_PATH=~/esp/esp-idf
idf.py reconfigure
```

---

## 待验证：2026-08-05 阶段 0 重试（等用户输入）


### 第 10 次：删除 name 字段（v5.5.3 下的正确配置）

**用户反馈**："还是失败，返回后面的一部分是：... Failed to resolve component 'ArduinoJson' required by component 'main': unknown name."

**错误日志关键信息**：
```
NOTICE: [60/60] idf (5.5.3)
```
说明用户这次跑的还是 `~/esp/esp-idf`（v5.5.3），但 `idf_component.yml` 里**仍带着** `name: "bblanchon/arduinojson"` 字段（之前第 4 次尝试加上的）。

**修复**：直接删除 `name` 字段：
```yaml
# /Users/cobain/Documents/.../fw/components/ArduinoJson/idf_component.yml
- name: "bblanchon/arduinojson"   # 删除这行
  version: "7.4.2"
```

**关键结论**（重要，记住）：
- **v5.5.3 (~/esp/esp-idf)**：`idf_component.yml` **不能有** `name` 字段，否则 component manager 会去 registry 下载
- **v5.5.4 (esp-idf-env/v5.5.4)**：`idf_component.yml` **必须**有 `name` 字段（不带就报 unknown name）
- 切版本时要**同步修改**这个 yml

**操作**（用户在 Terminal.app 执行）：
```bash
cd "/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw"
rm -rf build/ managed_components/  # 清掉之前失败的缓存
source ~/esp/esp-idf/export.sh
export IDF_PATH=~/esp/esp-idf
idf.py set-target esp32s3
idf.py reconfigure 2>&1 | tee /tmp/reconfigure.log
```

**待验证**：用户跑完反馈结果。
Executing action: reconfigure
Running cmake in directory /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build
Executing "cmake -G Ninja -DPYTHON_DEPS_CHECKED=1 -DPYTHON=/Users/cobain/.espressif/python_env/idf5.5_py3.14_env/bin/python -DESP_PLATFORM=1 -DCCACHE_ENABLE=0 /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw"...
-- IDF_TARGET is not set, guessed 'esp32s3' from sdkconfig '/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/sdkconfig.defaults'
-- Found Git: /usr/bin/git (found version "2.50.1 (Apple Git-155)")
-- Minimal build - OFF
-- The C compiler identification is GNU 14.2.0
-- The CXX compiler identification is GNU 14.2.0
-- The ASM compiler identification is GNU
-- Found assembler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Building ESP-IDF components for target esp32s3
NOTICE: Skipping optional dependency: espressif/esp_h264
NOTICE: Skipping optional dependency: espressif/esp_ipa
NOTICE: Skipping optional dependency: espressif/usb
NOTICE: Skipping optional dependency: espressif/esp_lcd_jd9365
NOTICE: Skipping optional dependency: waveshare/esp_lcd_st7703
NOTICE: Skipping optional dependency: espressif/esp32_p4_function_ev_board
NOTICE: Skipping optional dependency: espressif/esp_lcd_ili9881c
NOTICE: Skipping optional dependency: espressif/esp_lcd_ek79007
NOTICE: Skipping optional dependency: espressif/esp_hosted
NOTICE: Skipping optional dependency: espressif/esp_wifi_remote
NOTICE: Skipping optional dependency: espfriends/servo_dog_ctrl
NOTICE: Skipping optional dependency: llgok/cpp_bus_driver
NOTICE: Skipping optional dependency: espressif/mqtt
NOTICE: Skipping optional dependency: espressif/cjson
NOTICE: 
Following dependencies have new versions available:
Dependency "78/esp-wifi-connect": "3.1.2" -> "3.1.5"
Dependency "78/esp_lcd_nv3023": "1.0.0" -> "1.0.1"
Dependency "78/uart-eth-modem": "0.3.4" -> "0.3.5"
Dependency "espressif/adc_battery_estimation": "0.2.1" -> "0.2.2"
Dependency "espressif/adc_mic": "0.2.2" -> "0.2.3"
Dependency "espressif/bmi270_sensor": "0.1.1" -> "0.1.2"
Dependency "espressif/button": "4.1.6" -> "4.1.7"
Dependency "espressif/esp32-camera": "2.1.5" -> "2.1.7"
Dependency "espressif/esp_codec_dev": "1.5.11" -> "1.5.4"
Dependency "espressif/esp_image_effects": "1.0.1" -> "1.1.0"
Dependency "espressif/esp_lcd_co5300": "2.0.3" -> "2.1.0"
Dependency "espressif/esp_lcd_panel_io_additions": "1.0.1" -> "1.0.1~1"
Dependency "espressif/esp_lcd_touch_cst816s": "1.1.1" -> "1.1.1~2"
Dependency "espressif/esp_lcd_touch_gt1151": "1.1.0" -> "1.1.0~2"
Dependency "espressif/esp_lcd_touch_gt911": "1.2.0~1" -> "1.2.0~3"
Dependency "espressif/esp_lcd_touch_st7123": "1.0.1" -> "1.0.2"
Dependency "espressif/esp_mmap_assets": "1.4.0" -> "2.0.0"
Dependency "espressif/knob": "1.0.2" -> "1.1.0"
Dependency "tny-robotics/sh1106-esp-idf": "1.0.0" -> "1.0.1"
Dependency "txp666/otto-emoji-gif-component": "1.1.1" -> "1.3.0"
Dependency "waveshare/custom_io_expander_ch32v003": "1.0.1" -> "1.0.2"
Consider running "idf.py update-dependencies" to update your lock file.
NOTICE: Processing 60 dependencies:
NOTICE: [1/60] 78/esp-ml307 (3.6.5)
NOTICE: [2/60] 78/esp-wifi-connect (3.1.2)
NOTICE: [3/60] 78/esp_lcd_nv3023 (1.0.0)
NOTICE: [4/60] 78/uart-eth-modem (0.3.4)
NOTICE: [5/60] 78/uart-uhci (0.2.2)
NOTICE: [6/60] 78/xiaozhi-fonts (1.6.0)
NOTICE: [7/60] espressif/adc_battery_estimation (0.2.1)
NOTICE: [8/60] espressif/adc_mic (0.2.2)
NOTICE: [9/60] espressif/bmi270_sensor (0.1.1)
NOTICE: [10/60] espressif/button (4.1.6)
NOTICE: [11/60] espressif/cmake_utilities (0.5.3)
NOTICE: [12/60] espressif/dl_fft (0.6.0)
NOTICE: [13/60] espressif/esp-dsp (1.7.0)
NOTICE: [14/60] espressif/esp-sr (2.3.1)
NOTICE: [15/60] espressif/esp32-camera (2.1.5)
NOTICE: [16/60] espressif/esp_audio_codec (2.4.1)
NOTICE: [17/60] espressif/esp_audio_effects (1.2.1)
NOTICE: [18/60] espressif/esp_cam_sensor (1.5.2)
NOTICE: [19/60] espressif/esp_codec_dev (1.5.11)
NOTICE: [20/60] espressif/esp_image_effects (1.0.1)
NOTICE: [21/60] espressif/esp_io_expander (1.2.1)
NOTICE: [22/60] espressif/esp_io_expander_tca9554 (2.0.0)
NOTICE: [23/60] espressif/esp_io_expander_tca95xx_16bit (2.0.2)
NOTICE: [24/60] espressif/esp_jpeg (1.3.1)
NOTICE: [25/60] espressif/esp_lcd_axs15231b (1.0.1~1)
NOTICE: [26/60] espressif/esp_lcd_co5300 (2.0.3)
NOTICE: [27/60] espressif/esp_lcd_gc9a01 (2.0.1)
NOTICE: [28/60] espressif/esp_lcd_ili9341 (1.2.0)
NOTICE: [29/60] espressif/esp_lcd_panel_io_additions (1.0.1)
NOTICE: [30/60] espressif/esp_lcd_spd2010 (1.0.2)
NOTICE: [31/60] espressif/esp_lcd_st7701 (1.1.5)
NOTICE: [32/60] espressif/esp_lcd_st77916 (1.0.1)
NOTICE: [33/60] espressif/esp_lcd_st7796 (1.3.5)
NOTICE: [34/60] espressif/esp_lcd_touch (1.2.1)
NOTICE: [35/60] espressif/esp_lcd_touch_cst816s (1.1.1)
NOTICE: [36/60] espressif/esp_lcd_touch_ft5x06 (1.0.7)
NOTICE: [37/60] espressif/esp_lcd_touch_gt1151 (1.1.0)
NOTICE: [38/60] espressif/esp_lcd_touch_gt911 (1.2.0~1)
NOTICE: [39/60] espressif/esp_lcd_touch_st7123 (1.0.1)
NOTICE: [40/60] espressif/esp_lvgl_port (2.7.2)
NOTICE: [41/60] espressif/esp_mmap_assets (1.4.0)
NOTICE: [42/60] espressif/esp_new_jpeg (0.6.1)
NOTICE: [43/60] espressif/esp_sccb_intf (0.0.8)
NOTICE: [44/60] espressif/esp_video (1.3.1)
NOTICE: [45/60] espressif/i2c_bus (1.5.2)
NOTICE: [46/60] espressif/iot_eth (0.1.0)
NOTICE: [47/60] espressif/iot_usbh_cdc (3.1.0)
NOTICE: [48/60] espressif/iot_usbh_rndis (0.3.1)
NOTICE: [49/60] espressif/knob (1.0.2)
NOTICE: [50/60] espressif/led_strip (3.0.3)
NOTICE: [51/60] espressif/usb_host_uvc (2.3.1)
NOTICE: [52/60] espressif2022/image_player (1.1.1)
NOTICE: [53/60] lvgl/lvgl (9.4.0)
NOTICE: [54/60] tny-robotics/sh1106-esp-idf (1.0.0)
NOTICE: [55/60] txp666/otto-emoji-gif-component (1.1.1)
NOTICE: [56/60] waveshare/custom_io_expander_ch32v003 (1.0.1)
NOTICE: [57/60] waveshare/esp_lcd_sh8601 (1.0.2)
NOTICE: [58/60] waveshare/esp_lcd_touch_cst9217 (1.0.4)
NOTICE: [59/60] wvirgil123/sscma_client (1.0.2)
NOTICE: [60/60] idf (5.5.3)
-- ESP-TEE is currently supported only on the esp32c6;esp32h2;esp32c5 SoCs
NOTICE: Skipping optional dependency: espressif/esp_lcd_jd9365
NOTICE: Skipping optional dependency: waveshare/esp_lcd_st7703
NOTICE: Skipping optional dependency: espressif/esp32_p4_function_ev_board
NOTICE: Skipping optional dependency: espressif/esp_lcd_ili9881c
NOTICE: Skipping optional dependency: espressif/esp_lcd_ek79007
NOTICE: Skipping optional dependency: espressif/esp_hosted
NOTICE: Skipping optional dependency: espressif/esp_wifi_remote
NOTICE: Skipping optional dependency: espfriends/servo_dog_ctrl
NOTICE: Skipping optional dependency: llgok/cpp_bus_driver
NOTICE: Skipping optional dependency: espressif/esp_h264
NOTICE: Skipping optional dependency: espressif/esp_ipa
NOTICE: Skipping optional dependency: espressif/usb
CMake Error at /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:330 (message):
  Failed to resolve component 'ArduinoJson' required by component 'main':
  unknown name.
Call Stack (most recent call first):
  /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:379 (__build_resolve_and_add_req)
  /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:687 (__build_expand_requirements)
  /Users/cobain/esp/esp-idf/tools/cmake/project.cmake:741 (idf_build_process)
  CMakeLists.txt:23 (project)


-- Configuring incomplete, errors occurred!
[0;33mHINT: The component 'ArduinoJson' could not be found. This could be because: component name was misspelled, the component was not added to the build, the component has been moved to the IDF component manager, the component has been removed and refactored into some other component or the component may not be supported by the selected target.
Please look out for component in 'https://components.espressif.com' and add using 'idf.py add-dependency' command.
Refer to the migration guide for more details about moved components.
Refer to the build-system guide for more details about how components are found and included in the build.[0m
cmake failed with exit code 1, output of the command is in the /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build/log/idf_py_stderr_output_49858 and /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build/log/idf_py_stdout_output_49858

## 2026-08-05 22:39:02 reconfigure 完成

### 第 11 次：恢复 idf_component.yml（删除 yml 反而触发过滤）

**用户反馈**："还是失败，错误还是 unknown name"

**关键发现**（深挖 idf_component_manager 源码）：

`idf_component_manager/core.py:715-734`：
```python
local_components = [
    component
    for component in local_components
    if os.path.isfile(os.path.join(component['path'], 'CMakeLists.txt'))
    and os.path.isfile(os.path.join(component['path'], MANIFEST_FILENAME))  # ← 强制要求 yml
]
```

**结论**：删除 yml 反而**让 ArduinoJson 整个被过滤掉**，CMake 从未加载它的 CMakeLists.txt，target "ArduinoJson" 从未注册 → 报 `unknown name`。

**之前第 4 次"加 name 字段"的修复方向其实是对的**，只是当时在 Trae 跑时缺 toolchain path 走到了另一个错误。

**最终正确配置**（v5.5.3 和 v5.5.4 一致）：
```yaml
name: "bblanchon/arduinojson"
version: "7.4.2"
```

- v5.5.3：从 registry 下载 `bblanchon/arduinojson` 到 `managed_components/`
- v5.5.4：同样下载
- 两种版本都正确识别本地 `components/ArduinoJson/`

**待验证**：用户重跑确认。
Executing action: reconfigure
Running cmake in directory /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build
Executing "cmake -G Ninja -DPYTHON_DEPS_CHECKED=1 -DPYTHON=/Users/cobain/.espressif/python_env/idf5.5_py3.14_env/bin/python -DESP_PLATFORM=1 -DCCACHE_ENABLE=0 /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw"...
-- IDF_TARGET is not set, guessed 'esp32s3' from sdkconfig '/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/sdkconfig.defaults'
-- Found Git: /usr/bin/git (found version "2.50.1 (Apple Git-155)")
-- Minimal build - OFF
-- The C compiler identification is GNU 14.2.0
-- The CXX compiler identification is GNU 14.2.0
-- The ASM compiler identification is GNU
-- Found assembler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Building ESP-IDF components for target esp32s3
NOTICE: Skipping optional dependency: espressif/esp_h264
NOTICE: Skipping optional dependency: espressif/esp_ipa
NOTICE: Skipping optional dependency: espressif/usb
NOTICE: Skipping optional dependency: espressif/esp_lcd_jd9365
NOTICE: Skipping optional dependency: waveshare/esp_lcd_st7703
NOTICE: Skipping optional dependency: espressif/esp32_p4_function_ev_board
NOTICE: Skipping optional dependency: espressif/esp_lcd_ili9881c
NOTICE: Skipping optional dependency: espressif/esp_lcd_ek79007
NOTICE: Skipping optional dependency: espressif/esp_hosted
NOTICE: Skipping optional dependency: espressif/esp_wifi_remote
NOTICE: Skipping optional dependency: espfriends/servo_dog_ctrl
NOTICE: Skipping optional dependency: llgok/cpp_bus_driver
NOTICE: Skipping optional dependency: espressif/mqtt
NOTICE: Skipping optional dependency: espressif/cjson
NOTICE: 
Following dependencies have new versions available:
Dependency "78/esp-wifi-connect": "3.1.2" -> "3.1.5"
Dependency "78/esp_lcd_nv3023": "1.0.0" -> "1.0.1"
Dependency "78/uart-eth-modem": "0.3.4" -> "0.3.5"
Dependency "espressif/adc_battery_estimation": "0.2.1" -> "0.2.2"
Dependency "espressif/adc_mic": "0.2.2" -> "0.2.3"
Dependency "espressif/bmi270_sensor": "0.1.1" -> "0.1.2"
Dependency "espressif/button": "4.1.6" -> "4.1.7"
Dependency "espressif/esp32-camera": "2.1.5" -> "2.1.7"
Dependency "espressif/esp_codec_dev": "1.5.11" -> "1.5.4"
Dependency "espressif/esp_image_effects": "1.0.1" -> "1.1.0"
Dependency "espressif/esp_lcd_co5300": "2.0.3" -> "2.1.0"
Dependency "espressif/esp_lcd_panel_io_additions": "1.0.1" -> "1.0.1~1"
Dependency "espressif/esp_lcd_touch_cst816s": "1.1.1" -> "1.1.1~2"
Dependency "espressif/esp_lcd_touch_gt1151": "1.1.0" -> "1.1.0~2"
Dependency "espressif/esp_lcd_touch_gt911": "1.2.0~1" -> "1.2.0~3"
Dependency "espressif/esp_lcd_touch_st7123": "1.0.1" -> "1.0.2"
Dependency "espressif/esp_mmap_assets": "1.4.0" -> "2.0.0"
Dependency "espressif/knob": "1.0.2" -> "1.1.0"
Dependency "tny-robotics/sh1106-esp-idf": "1.0.0" -> "1.0.1"
Dependency "txp666/otto-emoji-gif-component": "1.1.1" -> "1.3.0"
Dependency "waveshare/custom_io_expander_ch32v003": "1.0.1" -> "1.0.2"
Consider running "idf.py update-dependencies" to update your lock file.
NOTICE: Processing 60 dependencies:
NOTICE: [1/60] 78/esp-ml307 (3.6.5)
NOTICE: [2/60] 78/esp-wifi-connect (3.1.2)
NOTICE: [3/60] 78/esp_lcd_nv3023 (1.0.0)
NOTICE: [4/60] 78/uart-eth-modem (0.3.4)
NOTICE: [5/60] 78/uart-uhci (0.2.2)
NOTICE: [6/60] 78/xiaozhi-fonts (1.6.0)
NOTICE: [7/60] espressif/adc_battery_estimation (0.2.1)
NOTICE: [8/60] espressif/adc_mic (0.2.2)
NOTICE: [9/60] espressif/bmi270_sensor (0.1.1)
NOTICE: [10/60] espressif/button (4.1.6)
NOTICE: [11/60] espressif/cmake_utilities (0.5.3)
NOTICE: [12/60] espressif/dl_fft (0.6.0)
NOTICE: [13/60] espressif/esp-dsp (1.7.0)
NOTICE: [14/60] espressif/esp-sr (2.3.1)
NOTICE: [15/60] espressif/esp32-camera (2.1.5)
NOTICE: [16/60] espressif/esp_audio_codec (2.4.1)
NOTICE: [17/60] espressif/esp_audio_effects (1.2.1)
NOTICE: [18/60] espressif/esp_cam_sensor (1.5.2)
NOTICE: [19/60] espressif/esp_codec_dev (1.5.11)
NOTICE: [20/60] espressif/esp_image_effects (1.0.1)
NOTICE: [21/60] espressif/esp_io_expander (1.2.1)
NOTICE: [22/60] espressif/esp_io_expander_tca9554 (2.0.0)
NOTICE: [23/60] espressif/esp_io_expander_tca95xx_16bit (2.0.2)
NOTICE: [24/60] espressif/esp_jpeg (1.3.1)
NOTICE: [25/60] espressif/esp_lcd_axs15231b (1.0.1~1)
NOTICE: [26/60] espressif/esp_lcd_co5300 (2.0.3)
NOTICE: [27/60] espressif/esp_lcd_gc9a01 (2.0.1)
NOTICE: [28/60] espressif/esp_lcd_ili9341 (1.2.0)
NOTICE: [29/60] espressif/esp_lcd_panel_io_additions (1.0.1)
NOTICE: [30/60] espressif/esp_lcd_spd2010 (1.0.2)
NOTICE: [31/60] espressif/esp_lcd_st7701 (1.1.5)
NOTICE: [32/60] espressif/esp_lcd_st77916 (1.0.1)
NOTICE: [33/60] espressif/esp_lcd_st7796 (1.3.5)
NOTICE: [34/60] espressif/esp_lcd_touch (1.2.1)
NOTICE: [35/60] espressif/esp_lcd_touch_cst816s (1.1.1)
NOTICE: [36/60] espressif/esp_lcd_touch_ft5x06 (1.0.7)
NOTICE: [37/60] espressif/esp_lcd_touch_gt1151 (1.1.0)
NOTICE: [38/60] espressif/esp_lcd_touch_gt911 (1.2.0~1)
NOTICE: [39/60] espressif/esp_lcd_touch_st7123 (1.0.1)
NOTICE: [40/60] espressif/esp_lvgl_port (2.7.2)
NOTICE: [41/60] espressif/esp_mmap_assets (1.4.0)
NOTICE: [42/60] espressif/esp_new_jpeg (0.6.1)
NOTICE: [43/60] espressif/esp_sccb_intf (0.0.8)
NOTICE: [44/60] espressif/esp_video (1.3.1)
NOTICE: [45/60] espressif/i2c_bus (1.5.2)
NOTICE: [46/60] espressif/iot_eth (0.1.0)
NOTICE: [47/60] espressif/iot_usbh_cdc (3.1.0)
NOTICE: [48/60] espressif/iot_usbh_rndis (0.3.1)
NOTICE: [49/60] espressif/knob (1.0.2)
NOTICE: [50/60] espressif/led_strip (3.0.3)
NOTICE: [51/60] espressif/usb_host_uvc (2.3.1)
NOTICE: [52/60] espressif2022/image_player (1.1.1)
NOTICE: [53/60] lvgl/lvgl (9.4.0)
NOTICE: [54/60] tny-robotics/sh1106-esp-idf (1.0.0)
NOTICE: [55/60] txp666/otto-emoji-gif-component (1.1.1)
NOTICE: [56/60] waveshare/custom_io_expander_ch32v003 (1.0.1)
NOTICE: [57/60] waveshare/esp_lcd_sh8601 (1.0.2)
NOTICE: [58/60] waveshare/esp_lcd_touch_cst9217 (1.0.4)
NOTICE: [59/60] wvirgil123/sscma_client (1.0.2)
NOTICE: [60/60] idf (5.5.3)
-- ESP-TEE is currently supported only on the esp32c6;esp32h2;esp32c5 SoCs
NOTICE: Skipping optional dependency: espressif/esp_lcd_jd9365
NOTICE: Skipping optional dependency: waveshare/esp_lcd_st7703
NOTICE: Skipping optional dependency: espressif/esp32_p4_function_ev_board
NOTICE: Skipping optional dependency: espressif/esp_lcd_ili9881c
NOTICE: Skipping optional dependency: espressif/esp_lcd_ek79007
NOTICE: Skipping optional dependency: espressif/esp_hosted
NOTICE: Skipping optional dependency: espressif/esp_wifi_remote
NOTICE: Skipping optional dependency: espfriends/servo_dog_ctrl
NOTICE: Skipping optional dependency: llgok/cpp_bus_driver
NOTICE: Skipping optional dependency: espressif/esp_h264
NOTICE: Skipping optional dependency: espressif/esp_ipa
NOTICE: Skipping optional dependency: espressif/usb
CMake Error at /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:330 (message):
  Failed to resolve component 'ArduinoJson' required by component 'main':
  unknown name.
Call Stack (most recent call first):
  /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:379 (__build_resolve_and_add_req)
  /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:687 (__build_expand_requirements)
  /Users/cobain/esp/esp-idf/tools/cmake/project.cmake:741 (idf_build_process)
  CMakeLists.txt:23 (project)


-- Configuring incomplete, errors occurred!
[0;33mHINT: The component 'ArduinoJson' could not be found. This could be because: component name was misspelled, the component was not added to the build, the component has been moved to the IDF component manager, the component has been removed and refactored into some other component or the component may not be supported by the selected target.
Please look out for component in 'https://components.espressif.com' and add using 'idf.py add-dependency' command.
Refer to the migration guide for more details about moved components.
Refer to the build-system guide for more details about how components are found and included in the build.[0m
cmake failed with exit code 1, output of the command is in the /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build/log/idf_py_stderr_output_51015 and /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build/log/idf_py_stdout_output_51015

## 2026-08-05 22:45:48 reconfigure (yml 恢复) 完成

### 第 12 次：换 v5.5.4（v5.5.3 行为有 bug）

**深挖源码后结论**：
- v5.5.3 (~/esp/esp-idf) 和 v5.5.4 (esp-idf-env/v5.5.4) 的 `tools/cmake/build.cmake` **完全一样**
- `idf_component_manager` 是同一个 Python 库（2.4.11）
- 但 v5.5.4 之前能跑到"toolchain not found"，说明 **v5.5.4 下 ArduinoJson 解析过了**
- v5.5.3 下 ArduinoJson 卡在"unknown name"
- 差异在 v5.5.3 的 idf_component_manager（2.4.11）对 RunCounter 行为有 bug

**改用 v5.5.4**（之前 v5.5.4 报"toolchain not found"但 tools.json 已改成 20251107）：

```bash
cd "/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw"
rm -rf build/ managed_components/
source ../esp-idf-env/v5.5.4/esp-idf/export.sh
export IDF_PATH=$PWD/../esp-idf-env/v5.5.4/esp-idf
idf.py set-target esp32s3
idf.py reconfigure 2>&1 | tee /tmp/reconfigure.log
cat /tmp/reconfigure.log >> /Users/cobain/Documents/.../BUILD_LOG.md
```
zsh: command not found: idf.py

## 2026-08-05 23:38:54 reconfigure (v5.5.4) 完成
zsh: command not found: idf.py

## 2026-08-05 23:39:14 reconfigure (v5.5.4) 完成

## 2026-08-05 23:55:50 阶段 0 编译
```
$ idf.py reconfigure
Executing action: reconfigure
Running cmake in directory /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build
Executing "cmake -G Ninja -DPYTHON_DEPS_CHECKED=1 -DPYTHON=/Users/cobain/.espressif/python_env/idf5.5_py3.14_env/bin/python -DESP_PLATFORM=1 -DCCACHE_ENABLE=0 /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw"...
-- IDF_TARGET is not set, guessed 'esp32s3' from sdkconfig '/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/sdkconfig.defaults'
-- Found Git: /usr/bin/git (found version "2.50.1 (Apple Git-155)")
-- Minimal build - OFF
-- The C compiler identification is GNU 14.2.0
-- The CXX compiler identification is GNU 14.2.0
-- The ASM compiler identification is GNU
-- Found assembler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Building ESP-IDF components for target esp32s3
NOTICE: Skipping optional dependency: espressif/esp_h264
NOTICE: Skipping optional dependency: espressif/esp_ipa
NOTICE: Skipping optional dependency: espressif/usb
NOTICE: Skipping optional dependency: espressif/esp_lcd_jd9365
NOTICE: Skipping optional dependency: waveshare/esp_lcd_st7703
NOTICE: Skipping optional dependency: espressif/esp32_p4_function_ev_board
NOTICE: Skipping optional dependency: espressif/esp_lcd_ili9881c
NOTICE: Skipping optional dependency: espressif/esp_lcd_ek79007
NOTICE: Skipping optional dependency: espressif/esp_hosted
NOTICE: Skipping optional dependency: espressif/esp_wifi_remote
NOTICE: Skipping optional dependency: espfriends/servo_dog_ctrl
NOTICE: Skipping optional dependency: llgok/cpp_bus_driver
NOTICE: Skipping optional dependency: espressif/mqtt
NOTICE: Skipping optional dependency: espressif/cjson
NOTICE: 
Following dependencies have new versions available:
Dependency "78/esp-wifi-connect": "3.1.2" -> "3.1.5"
Dependency "78/esp_lcd_nv3023": "1.0.0" -> "1.0.1"
Dependency "78/uart-eth-modem": "0.3.4" -> "0.3.5"
Dependency "espressif/adc_battery_estimation": "0.2.1" -> "0.2.2"
Dependency "espressif/adc_mic": "0.2.2" -> "0.2.3"
Dependency "espressif/bmi270_sensor": "0.1.1" -> "0.1.2"
Dependency "espressif/button": "4.1.6" -> "4.1.7"
Dependency "espressif/esp32-camera": "2.1.5" -> "2.1.7"
Dependency "espressif/esp_codec_dev": "1.5.11" -> "1.5.4"
Dependency "espressif/esp_image_effects": "1.0.1" -> "1.1.0"
Dependency "espressif/esp_lcd_co5300": "2.0.3" -> "2.1.0"
Dependency "espressif/esp_lcd_panel_io_additions": "1.0.1" -> "1.0.1~1"
Dependency "espressif/esp_lcd_touch_cst816s": "1.1.1" -> "1.1.1~2"
Dependency "espressif/esp_lcd_touch_gt1151": "1.1.0" -> "1.1.0~2"
Dependency "espressif/esp_lcd_touch_gt911": "1.2.0~1" -> "1.2.0~3"
Dependency "espressif/esp_lcd_touch_st7123": "1.0.1" -> "1.0.2"
Dependency "espressif/esp_mmap_assets": "1.4.0" -> "2.0.0"
Dependency "espressif/knob": "1.0.2" -> "1.1.0"
Dependency "tny-robotics/sh1106-esp-idf": "1.0.0" -> "1.0.1"
Dependency "txp666/otto-emoji-gif-component": "1.1.1" -> "1.3.0"
Dependency "waveshare/custom_io_expander_ch32v003": "1.0.1" -> "1.0.2"
Consider running "idf.py update-dependencies" to update your lock file.
NOTICE: Processing 60 dependencies:
NOTICE: [1/60] 78/esp-ml307 (3.6.5)
NOTICE: [2/60] 78/esp-wifi-connect (3.1.2)
NOTICE: [3/60] 78/esp_lcd_nv3023 (1.0.0)
NOTICE: [4/60] 78/uart-eth-modem (0.3.4)
NOTICE: [5/60] 78/uart-uhci (0.2.2)
NOTICE: [6/60] 78/xiaozhi-fonts (1.6.0)
NOTICE: [7/60] espressif/adc_battery_estimation (0.2.1)
NOTICE: [8/60] espressif/adc_mic (0.2.2)
NOTICE: [9/60] espressif/bmi270_sensor (0.1.1)
NOTICE: [10/60] espressif/button (4.1.6)
NOTICE: [11/60] espressif/cmake_utilities (0.5.3)
NOTICE: [12/60] espressif/dl_fft (0.6.0)
NOTICE: [13/60] espressif/esp-dsp (1.7.0)
NOTICE: [14/60] espressif/esp-sr (2.3.1)
NOTICE: [15/60] espressif/esp32-camera (2.1.5)
NOTICE: [16/60] espressif/esp_audio_codec (2.4.1)
NOTICE: [17/60] espressif/esp_audio_effects (1.2.1)
NOTICE: [18/60] espressif/esp_cam_sensor (1.5.2)
NOTICE: [19/60] espressif/esp_codec_dev (1.5.11)
NOTICE: [20/60] espressif/esp_image_effects (1.0.1)
NOTICE: [21/60] espressif/esp_io_expander (1.2.1)
NOTICE: [22/60] espressif/esp_io_expander_tca9554 (2.0.0)
NOTICE: [23/60] espressif/esp_io_expander_tca95xx_16bit (2.0.2)
NOTICE: [24/60] espressif/esp_jpeg (1.3.1)
NOTICE: [25/60] espressif/esp_lcd_axs15231b (1.0.1~1)
NOTICE: [26/60] espressif/esp_lcd_co5300 (2.0.3)
NOTICE: [27/60] espressif/esp_lcd_gc9a01 (2.0.1)
NOTICE: [28/60] espressif/esp_lcd_ili9341 (1.2.0)
NOTICE: [29/60] espressif/esp_lcd_panel_io_additions (1.0.1)
NOTICE: [30/60] espressif/esp_lcd_spd2010 (1.0.2)
NOTICE: [31/60] espressif/esp_lcd_st7701 (1.1.5)
NOTICE: [32/60] espressif/esp_lcd_st77916 (1.0.1)
NOTICE: [33/60] espressif/esp_lcd_st7796 (1.3.5)
NOTICE: [34/60] espressif/esp_lcd_touch (1.2.1)
NOTICE: [35/60] espressif/esp_lcd_touch_cst816s (1.1.1)
NOTICE: [36/60] espressif/esp_lcd_touch_ft5x06 (1.0.7)
NOTICE: [37/60] espressif/esp_lcd_touch_gt1151 (1.1.0)
NOTICE: [38/60] espressif/esp_lcd_touch_gt911 (1.2.0~1)
NOTICE: [39/60] espressif/esp_lcd_touch_st7123 (1.0.1)
NOTICE: [40/60] espressif/esp_lvgl_port (2.7.2)
NOTICE: [41/60] espressif/esp_mmap_assets (1.4.0)
NOTICE: [42/60] espressif/esp_new_jpeg (0.6.1)
NOTICE: [43/60] espressif/esp_sccb_intf (0.0.8)
NOTICE: [44/60] espressif/esp_video (1.3.1)
NOTICE: [45/60] espressif/i2c_bus (1.5.2)
NOTICE: [46/60] espressif/iot_eth (0.1.0)
NOTICE: [47/60] espressif/iot_usbh_cdc (3.1.0)
NOTICE: [48/60] espressif/iot_usbh_rndis (0.3.1)
NOTICE: [49/60] espressif/knob (1.0.2)
NOTICE: [50/60] espressif/led_strip (3.0.3)
NOTICE: [51/60] espressif/usb_host_uvc (2.3.1)
NOTICE: [52/60] espressif2022/image_player (1.1.1)
NOTICE: [53/60] lvgl/lvgl (9.4.0)
NOTICE: [54/60] tny-robotics/sh1106-esp-idf (1.0.0)
NOTICE: [55/60] txp666/otto-emoji-gif-component (1.1.1)
NOTICE: [56/60] waveshare/custom_io_expander_ch32v003 (1.0.1)
NOTICE: [57/60] waveshare/esp_lcd_sh8601 (1.0.2)
NOTICE: [58/60] waveshare/esp_lcd_touch_cst9217 (1.0.4)
NOTICE: [59/60] wvirgil123/sscma_client (1.0.2)
NOTICE: [60/60] idf (5.5.3)
-- ESP-TEE is currently supported only on the esp32c6;esp32h2;esp32c5 SoCs
NOTICE: Skipping optional dependency: espressif/esp_lcd_jd9365
NOTICE: Skipping optional dependency: waveshare/esp_lcd_st7703
NOTICE: Skipping optional dependency: espressif/esp32_p4_function_ev_board
NOTICE: Skipping optional dependency: espressif/esp_lcd_ili9881c
NOTICE: Skipping optional dependency: espressif/esp_lcd_ek79007
NOTICE: Skipping optional dependency: espressif/esp_hosted
NOTICE: Skipping optional dependency: espressif/esp_wifi_remote
NOTICE: Skipping optional dependency: espfriends/servo_dog_ctrl
NOTICE: Skipping optional dependency: llgok/cpp_bus_driver
NOTICE: Skipping optional dependency: espressif/esp_h264
NOTICE: Skipping optional dependency: espressif/esp_ipa
NOTICE: Skipping optional dependency: espressif/usb
CMake Error at /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:330 (message):
  Failed to resolve component 'ArduinoJson' required by component 'main':
  unknown name.
Call Stack (most recent call first):
  /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:379 (__build_resolve_and_add_req)
  /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:687 (__build_expand_requirements)
  /Users/cobain/esp/esp-idf/tools/cmake/project.cmake:741 (idf_build_process)
  CMakeLists.txt:23 (project)


-- Configuring incomplete, errors occurred!
[0;33mHINT: The component 'ArduinoJson' could not be found. This could be because: component name was misspelled, the component was not added to the build, the component has been moved to the IDF component manager, the component has been removed and refactored into some other component or the component may not be supported by the selected target.
Please look out for component in 'https://components.espressif.com' and add using 'idf.py add-dependency' command.
Refer to the migration guide for more details about moved components.
Refer to the build-system guide for more details about how components are found and included in the build.[0m
cmake failed with exit code 1, output of the command is in the /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build/log/idf_py_stderr_output_61818 and /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build/log/idf_py_stdout_output_61818
--- reconfigure exit: 2 ---
$ idf.py build
Executing action: all (aliases: build)
Running cmake in directory /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build
Executing "cmake -G Ninja -DPYTHON_DEPS_CHECKED=1 -DPYTHON=/Users/cobain/.espressif/python_env/idf5.5_py3.14_env/bin/python -DESP_PLATFORM=1 -DCCACHE_ENABLE=0 /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw"...
-- IDF_TARGET is not set, guessed 'esp32s3' from sdkconfig '/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/sdkconfig.defaults'
-- Found Git: /usr/bin/git (found version "2.50.1 (Apple Git-155)")
-- Minimal build - OFF
-- The C compiler identification is GNU 14.2.0
-- The CXX compiler identification is GNU 14.2.0
-- The ASM compiler identification is GNU
-- Found assembler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Building ESP-IDF components for target esp32s3
NOTICE: Skipping optional dependency: espressif/esp_h264
NOTICE: Skipping optional dependency: espressif/esp_ipa
NOTICE: Skipping optional dependency: espressif/usb
NOTICE: Skipping optional dependency: espressif/esp_lcd_jd9365
NOTICE: Skipping optional dependency: waveshare/esp_lcd_st7703
NOTICE: Skipping optional dependency: espressif/esp32_p4_function_ev_board
NOTICE: Skipping optional dependency: espressif/esp_lcd_ili9881c
NOTICE: Skipping optional dependency: espressif/esp_lcd_ek79007
NOTICE: Skipping optional dependency: espressif/esp_hosted
NOTICE: Skipping optional dependency: espressif/esp_wifi_remote
NOTICE: Skipping optional dependency: espfriends/servo_dog_ctrl
NOTICE: Skipping optional dependency: llgok/cpp_bus_driver
NOTICE: Skipping optional dependency: espressif/mqtt
NOTICE: Skipping optional dependency: espressif/cjson
NOTICE: 
Following dependencies have new versions available:
Dependency "78/esp-wifi-connect": "3.1.2" -> "3.1.5"
Dependency "78/esp_lcd_nv3023": "1.0.0" -> "1.0.1"
Dependency "78/uart-eth-modem": "0.3.4" -> "0.3.5"
Dependency "espressif/adc_battery_estimation": "0.2.1" -> "0.2.2"
Dependency "espressif/adc_mic": "0.2.2" -> "0.2.3"
Dependency "espressif/bmi270_sensor": "0.1.1" -> "0.1.2"
Dependency "espressif/button": "4.1.6" -> "4.1.7"
Dependency "espressif/esp32-camera": "2.1.5" -> "2.1.7"
Dependency "espressif/esp_codec_dev": "1.5.11" -> "1.5.4"
Dependency "espressif/esp_image_effects": "1.0.1" -> "1.1.0"
Dependency "espressif/esp_lcd_co5300": "2.0.3" -> "2.1.0"
Dependency "espressif/esp_lcd_panel_io_additions": "1.0.1" -> "1.0.1~1"
Dependency "espressif/esp_lcd_touch_cst816s": "1.1.1" -> "1.1.1~2"
Dependency "espressif/esp_lcd_touch_gt1151": "1.1.0" -> "1.1.0~2"
Dependency "espressif/esp_lcd_touch_gt911": "1.2.0~1" -> "1.2.0~3"
Dependency "espressif/esp_lcd_touch_st7123": "1.0.1" -> "1.0.2"
Dependency "espressif/esp_mmap_assets": "1.4.0" -> "2.0.0"
Dependency "espressif/knob": "1.0.2" -> "1.1.0"
Dependency "tny-robotics/sh1106-esp-idf": "1.0.0" -> "1.0.1"
Dependency "txp666/otto-emoji-gif-component": "1.1.1" -> "1.3.0"
Dependency "waveshare/custom_io_expander_ch32v003": "1.0.1" -> "1.0.2"
Consider running "idf.py update-dependencies" to update your lock file.
NOTICE: Processing 60 dependencies:
NOTICE: [1/60] 78/esp-ml307 (3.6.5)
NOTICE: [2/60] 78/esp-wifi-connect (3.1.2)
NOTICE: [3/60] 78/esp_lcd_nv3023 (1.0.0)
NOTICE: [4/60] 78/uart-eth-modem (0.3.4)
NOTICE: [5/60] 78/uart-uhci (0.2.2)
NOTICE: [6/60] 78/xiaozhi-fonts (1.6.0)
NOTICE: [7/60] espressif/adc_battery_estimation (0.2.1)
NOTICE: [8/60] espressif/adc_mic (0.2.2)
NOTICE: [9/60] espressif/bmi270_sensor (0.1.1)
NOTICE: [10/60] espressif/button (4.1.6)
NOTICE: [11/60] espressif/cmake_utilities (0.5.3)
NOTICE: [12/60] espressif/dl_fft (0.6.0)
NOTICE: [13/60] espressif/esp-dsp (1.7.0)
NOTICE: [14/60] espressif/esp-sr (2.3.1)
NOTICE: [15/60] espressif/esp32-camera (2.1.5)
NOTICE: [16/60] espressif/esp_audio_codec (2.4.1)
NOTICE: [17/60] espressif/esp_audio_effects (1.2.1)
NOTICE: [18/60] espressif/esp_cam_sensor (1.5.2)
NOTICE: [19/60] espressif/esp_codec_dev (1.5.11)
NOTICE: [20/60] espressif/esp_image_effects (1.0.1)
NOTICE: [21/60] espressif/esp_io_expander (1.2.1)
NOTICE: [22/60] espressif/esp_io_expander_tca9554 (2.0.0)
NOTICE: [23/60] espressif/esp_io_expander_tca95xx_16bit (2.0.2)
NOTICE: [24/60] espressif/esp_jpeg (1.3.1)
NOTICE: [25/60] espressif/esp_lcd_axs15231b (1.0.1~1)
NOTICE: [26/60] espressif/esp_lcd_co5300 (2.0.3)
NOTICE: [27/60] espressif/esp_lcd_gc9a01 (2.0.1)
NOTICE: [28/60] espressif/esp_lcd_ili9341 (1.2.0)
NOTICE: [29/60] espressif/esp_lcd_panel_io_additions (1.0.1)
NOTICE: [30/60] espressif/esp_lcd_spd2010 (1.0.2)
NOTICE: [31/60] espressif/esp_lcd_st7701 (1.1.5)
NOTICE: [32/60] espressif/esp_lcd_st77916 (1.0.1)
NOTICE: [33/60] espressif/esp_lcd_st7796 (1.3.5)
NOTICE: [34/60] espressif/esp_lcd_touch (1.2.1)
NOTICE: [35/60] espressif/esp_lcd_touch_cst816s (1.1.1)
NOTICE: [36/60] espressif/esp_lcd_touch_ft5x06 (1.0.7)
NOTICE: [37/60] espressif/esp_lcd_touch_gt1151 (1.1.0)
NOTICE: [38/60] espressif/esp_lcd_touch_gt911 (1.2.0~1)
NOTICE: [39/60] espressif/esp_lcd_touch_st7123 (1.0.1)
NOTICE: [40/60] espressif/esp_lvgl_port (2.7.2)
NOTICE: [41/60] espressif/esp_mmap_assets (1.4.0)
NOTICE: [42/60] espressif/esp_new_jpeg (0.6.1)
NOTICE: [43/60] espressif/esp_sccb_intf (0.0.8)
NOTICE: [44/60] espressif/esp_video (1.3.1)
NOTICE: [45/60] espressif/i2c_bus (1.5.2)
NOTICE: [46/60] espressif/iot_eth (0.1.0)
NOTICE: [47/60] espressif/iot_usbh_cdc (3.1.0)
NOTICE: [48/60] espressif/iot_usbh_rndis (0.3.1)
NOTICE: [49/60] espressif/knob (1.0.2)
NOTICE: [50/60] espressif/led_strip (3.0.3)
NOTICE: [51/60] espressif/usb_host_uvc (2.3.1)
NOTICE: [52/60] espressif2022/image_player (1.1.1)
NOTICE: [53/60] lvgl/lvgl (9.4.0)
NOTICE: [54/60] tny-robotics/sh1106-esp-idf (1.0.0)
NOTICE: [55/60] txp666/otto-emoji-gif-component (1.1.1)
NOTICE: [56/60] waveshare/custom_io_expander_ch32v003 (1.0.1)
NOTICE: [57/60] waveshare/esp_lcd_sh8601 (1.0.2)
NOTICE: [58/60] waveshare/esp_lcd_touch_cst9217 (1.0.4)
NOTICE: [59/60] wvirgil123/sscma_client (1.0.2)
NOTICE: [60/60] idf (5.5.3)
-- ESP-TEE is currently supported only on the esp32c6;esp32h2;esp32c5 SoCs
NOTICE: Skipping optional dependency: espressif/esp_lcd_jd9365
NOTICE: Skipping optional dependency: waveshare/esp_lcd_st7703
NOTICE: Skipping optional dependency: espressif/esp32_p4_function_ev_board
NOTICE: Skipping optional dependency: espressif/esp_lcd_ili9881c
NOTICE: Skipping optional dependency: espressif/esp_lcd_ek79007
NOTICE: Skipping optional dependency: espressif/esp_hosted
NOTICE: Skipping optional dependency: espressif/esp_wifi_remote
NOTICE: Skipping optional dependency: espfriends/servo_dog_ctrl
NOTICE: Skipping optional dependency: llgok/cpp_bus_driver
NOTICE: Skipping optional dependency: espressif/esp_h264
NOTICE: Skipping optional dependency: espressif/esp_ipa
NOTICE: Skipping optional dependency: espressif/usb
CMake Error at /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:330 (message):
  Failed to resolve component 'ArduinoJson' required by component 'main':
  unknown name.
Call Stack (most recent call first):
  /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:379 (__build_resolve_and_add_req)
  /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:687 (__build_expand_requirements)
  /Users/cobain/esp/esp-idf/tools/cmake/project.cmake:741 (idf_build_process)
  CMakeLists.txt:23 (project)


-- Configuring incomplete, errors occurred!
[0;33mHINT: The component 'ArduinoJson' could not be found. This could be because: component name was misspelled, the component was not added to the build, the component has been moved to the IDF component manager, the component has been removed and refactored into some other component or the component may not be supported by the selected target.
Please look out for component in 'https://components.espressif.com' and add using 'idf.py add-dependency' command.
Refer to the migration guide for more details about moved components.
Refer to the build-system guide for more details about how components are found and included in the build.[0m
cmake failed with exit code 1, output of the command is in the /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build/log/idf_py_stderr_output_62193 and /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build/log/idf_py_stdout_output_62193
--- build exit: 2 ---
```

## 2026-08-06 00:07:24 阶段 0 编译 (v5.5.4)
```
$ idf.py reconfigure
zsh: command not found: idf.py
--- reconfigure exit: 127 ---
$ idf.py build
zsh: command not found: idf.py
--- build exit: 127 ---
```

## 2026-08-06 00:11:06 阶段 0 编译 (v5.5.3, yml=esp-now模式)
```
$ idf.py reconfigure
Executing action: reconfigure
Running cmake in directory /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build
Executing "cmake -G Ninja -DPYTHON_DEPS_CHECKED=1 -DPYTHON=/Users/cobain/.espressif/python_env/idf5.5_py3.14_env/bin/python -DESP_PLATFORM=1 -DCCACHE_ENABLE=0 /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw"...
-- IDF_TARGET is not set, guessed 'esp32s3' from sdkconfig '/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/sdkconfig.defaults'
-- Found Git: /usr/bin/git (found version "2.50.1 (Apple Git-155)")
-- Minimal build - OFF
-- The C compiler identification is GNU 14.2.0
-- The CXX compiler identification is GNU 14.2.0
-- The ASM compiler identification is GNU
-- Found assembler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Building ESP-IDF components for target esp32s3
NOTICE: Dependencies lock doesn't exist, solving dependencies.
NOTICE: Skipping optional dependency: espressif/esp_lcd_jd9365
NOTICE: Skipping optional dependency: waveshare/esp_lcd_st7703
NOTICE: Skipping optional dependency: espressif/esp32_p4_function_ev_board
NOTICE: Skipping optional dependency: espressif/esp_lcd_ili9881c
NOTICE: Skipping optional dependency: espressif/esp_lcd_ek79007
NOTICE: Skipping optional dependency: espressif/esp_hosted
NOTICE: Skipping optional dependency: espressif/esp_wifi_remote
NOTICE: Skipping optional dependency: espfriends/servo_dog_ctrl
NOTICE: Skipping optional dependency: llgok/cpp_bus_driver
..NOTICE: Skipping optional dependency: espressif/mqtt
...NOTICE: Skipping optional dependency: espressif/cjson
...............................................................NOTICE: Skipping optional dependency: espressif/esp_h264
NOTICE: Skipping optional dependency: espressif/esp_ipa
NOTICE: Skipping optional dependency: espressif/usb
.....................NOTICE: Updating lock file at /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/dependencies.lock
NOTICE: Processing 60 dependencies:
NOTICE: [1/60] 78/esp-ml307 (3.6.5)
NOTICE: [2/60] 78/esp-wifi-connect (3.1.5)
NOTICE: [3/60] 78/esp_lcd_nv3023 (1.0.1)
NOTICE: [4/60] 78/uart-eth-modem (0.3.5)
NOTICE: [5/60] 78/uart-uhci (0.2.2)
NOTICE: [6/60] 78/xiaozhi-fonts (1.6.0)
NOTICE: [7/60] espressif/adc_battery_estimation (0.2.2)
NOTICE: [8/60] espressif/adc_mic (0.2.3)
NOTICE: [9/60] espressif/bmi270_sensor (0.1.2)
NOTICE: [10/60] espressif/button (4.1.7)
NOTICE: [11/60] espressif/cmake_utilities (0.5.3)
NOTICE: [12/60] espressif/dl_fft (0.6.0)
NOTICE: [13/60] espressif/esp-dsp (1.7.0)
NOTICE: [14/60] espressif/esp-sr (2.3.1)
NOTICE: [15/60] espressif/esp32-camera (2.1.7)
NOTICE: [16/60] espressif/esp_audio_codec (2.4.1)
NOTICE: [17/60] espressif/esp_audio_effects (1.2.1)
NOTICE: [18/60] espressif/esp_cam_sensor (1.5.2)
NOTICE: [19/60] espressif/esp_codec_dev (1.5.4)
NOTICE: [20/60] espressif/esp_image_effects (1.1.0)
NOTICE: [21/60] espressif/esp_io_expander (1.2.1)
NOTICE: [22/60] espressif/esp_io_expander_tca9554 (2.0.0)
NOTICE: [23/60] espressif/esp_io_expander_tca95xx_16bit (2.0.2)
NOTICE: [24/60] espressif/esp_jpeg (1.3.1)
NOTICE: [25/60] espressif/esp_lcd_axs15231b (1.0.1~1)
NOTICE: [26/60] espressif/esp_lcd_co5300 (2.1.0)
NOTICE: [27/60] espressif/esp_lcd_gc9a01 (2.0.1)
NOTICE: [28/60] espressif/esp_lcd_ili9341 (1.2.0)
NOTICE: [29/60] espressif/esp_lcd_panel_io_additions (1.0.1~1)
NOTICE: [30/60] espressif/esp_lcd_spd2010 (1.0.2)
NOTICE: [31/60] espressif/esp_lcd_st7701 (1.1.5)
NOTICE: [32/60] espressif/esp_lcd_st77916 (1.0.1)
NOTICE: [33/60] espressif/esp_lcd_st7796 (1.3.5)
NOTICE: [34/60] espressif/esp_lcd_touch (1.2.1)
NOTICE: [35/60] espressif/esp_lcd_touch_cst816s (1.1.1~2)
NOTICE: [36/60] espressif/esp_lcd_touch_ft5x06 (1.0.7)
NOTICE: [37/60] espressif/esp_lcd_touch_gt1151 (1.1.0~2)
NOTICE: [38/60] espressif/esp_lcd_touch_gt911 (1.2.0~3)
NOTICE: [39/60] espressif/esp_lcd_touch_st7123 (1.0.2)
NOTICE: [40/60] espressif/esp_lvgl_port (2.7.2)
NOTICE: [41/60] espressif/esp_mmap_assets (2.0.0)
NOTICE: [42/60] espressif/esp_new_jpeg (0.6.1)
NOTICE: [43/60] espressif/esp_sccb_intf (0.0.8)
NOTICE: [44/60] espressif/esp_video (1.3.1)
NOTICE: [45/60] espressif/i2c_bus (1.5.2)
NOTICE: [46/60] espressif/iot_eth (0.1.0)
NOTICE: [47/60] espressif/iot_usbh_cdc (3.1.0)
NOTICE: [48/60] espressif/iot_usbh_rndis (0.3.1)
NOTICE: [49/60] espressif/knob (1.1.0)
NOTICE: [50/60] espressif/led_strip (3.0.3)
NOTICE: [51/60] espressif/usb_host_uvc (2.3.1)
NOTICE: [52/60] espressif2022/image_player (1.1.1)
NOTICE: [53/60] lvgl/lvgl (9.4.0)
NOTICE: [54/60] tny-robotics/sh1106-esp-idf (1.0.1)
NOTICE: [55/60] txp666/otto-emoji-gif-component (1.3.0)
NOTICE: [56/60] waveshare/custom_io_expander_ch32v003 (1.0.2)
NOTICE: [57/60] waveshare/esp_lcd_sh8601 (1.0.2)
NOTICE: [58/60] waveshare/esp_lcd_touch_cst9217 (1.0.4)
NOTICE: [59/60] wvirgil123/sscma_client (1.0.2)
NOTICE: [60/60] idf (5.5.3)
-- ESP-TEE is currently supported only on the esp32c6;esp32h2;esp32c5 SoCs
NOTICE: Skipping optional dependency: espressif/esp_lcd_jd9365
NOTICE: Skipping optional dependency: waveshare/esp_lcd_st7703
NOTICE: Skipping optional dependency: espressif/esp32_p4_function_ev_board
NOTICE: Skipping optional dependency: espressif/esp_lcd_ili9881c
NOTICE: Skipping optional dependency: espressif/esp_lcd_ek79007
NOTICE: Skipping optional dependency: espressif/esp_hosted
NOTICE: Skipping optional dependency: espressif/esp_wifi_remote
NOTICE: Skipping optional dependency: espfriends/servo_dog_ctrl
NOTICE: Skipping optional dependency: llgok/cpp_bus_driver
NOTICE: Skipping optional dependency: espressif/cjson
NOTICE: Skipping optional dependency: espressif/esp_h264
NOTICE: Skipping optional dependency: espressif/esp_ipa
NOTICE: Skipping optional dependency: espressif/usb
CMake Error at /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:330 (message):
  Failed to resolve component 'ArduinoJson' required by component 'main':
  unknown name.
Call Stack (most recent call first):
  /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:379 (__build_resolve_and_add_req)
  /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:687 (__build_expand_requirements)
  /Users/cobain/esp/esp-idf/tools/cmake/project.cmake:741 (idf_build_process)
  CMakeLists.txt:23 (project)


-- Configuring incomplete, errors occurred!
[0;33mHINT: The component 'ArduinoJson' could not be found. This could be because: component name was misspelled, the component was not added to the build, the component has been moved to the IDF component manager, the component has been removed and refactored into some other component or the component may not be supported by the selected target.
Please look out for component in 'https://components.espressif.com' and add using 'idf.py add-dependency' command.
Refer to the migration guide for more details about moved components.
Refer to the build-system guide for more details about how components are found and included in the build.[0m
cmake failed with exit code 1, output of the command is in the /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build/log/idf_py_stderr_output_65364 and /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build/log/idf_py_stdout_output_65364
--- reconfigure exit: 2 ---
$ idf.py build
Executing action: all (aliases: build)
Running cmake in directory /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build
Executing "cmake -G Ninja -DPYTHON_DEPS_CHECKED=1 -DPYTHON=/Users/cobain/.espressif/python_env/idf5.5_py3.14_env/bin/python -DESP_PLATFORM=1 -DCCACHE_ENABLE=0 /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw"...
-- IDF_TARGET is not set, guessed 'esp32s3' from sdkconfig '/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/sdkconfig.defaults'
-- Found Git: /usr/bin/git (found version "2.50.1 (Apple Git-155)")
-- Minimal build - OFF
-- The C compiler identification is GNU 14.2.0
-- The CXX compiler identification is GNU 14.2.0
-- The ASM compiler identification is GNU
-- Found assembler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Building ESP-IDF components for target esp32s3
NOTICE: Skipping optional dependency: espressif/cjson
NOTICE: Skipping optional dependency: espressif/esp_h264
NOTICE: Skipping optional dependency: espressif/esp_ipa
NOTICE: Skipping optional dependency: espressif/usb
NOTICE: Skipping optional dependency: espressif/esp_lcd_jd9365
NOTICE: Skipping optional dependency: waveshare/esp_lcd_st7703
NOTICE: Skipping optional dependency: espressif/esp32_p4_function_ev_board
NOTICE: Skipping optional dependency: espressif/esp_lcd_ili9881c
NOTICE: Skipping optional dependency: espressif/esp_lcd_ek79007
NOTICE: Skipping optional dependency: espressif/esp_hosted
NOTICE: Skipping optional dependency: espressif/esp_wifi_remote
NOTICE: Skipping optional dependency: espfriends/servo_dog_ctrl
NOTICE: Skipping optional dependency: llgok/cpp_bus_driver
NOTICE: Skipping optional dependency: espressif/mqtt
NOTICE: Processing 60 dependencies:
NOTICE: [1/60] 78/esp-ml307 (3.6.5)
NOTICE: [2/60] 78/esp-wifi-connect (3.1.5)
NOTICE: [3/60] 78/esp_lcd_nv3023 (1.0.1)
NOTICE: [4/60] 78/uart-eth-modem (0.3.5)
NOTICE: [5/60] 78/uart-uhci (0.2.2)
NOTICE: [6/60] 78/xiaozhi-fonts (1.6.0)
NOTICE: [7/60] espressif/adc_battery_estimation (0.2.2)
NOTICE: [8/60] espressif/adc_mic (0.2.3)
NOTICE: [9/60] espressif/bmi270_sensor (0.1.2)
NOTICE: [10/60] espressif/button (4.1.7)
NOTICE: [11/60] espressif/cmake_utilities (0.5.3)
NOTICE: [12/60] espressif/dl_fft (0.6.0)
NOTICE: [13/60] espressif/esp-dsp (1.7.0)
NOTICE: [14/60] espressif/esp-sr (2.3.1)
NOTICE: [15/60] espressif/esp32-camera (2.1.7)
NOTICE: [16/60] espressif/esp_audio_codec (2.4.1)
NOTICE: [17/60] espressif/esp_audio_effects (1.2.1)
NOTICE: [18/60] espressif/esp_cam_sensor (1.5.2)
NOTICE: [19/60] espressif/esp_codec_dev (1.5.4)
NOTICE: [20/60] espressif/esp_image_effects (1.1.0)
NOTICE: [21/60] espressif/esp_io_expander (1.2.1)
NOTICE: [22/60] espressif/esp_io_expander_tca9554 (2.0.0)
NOTICE: [23/60] espressif/esp_io_expander_tca95xx_16bit (2.0.2)
NOTICE: [24/60] espressif/esp_jpeg (1.3.1)
NOTICE: [25/60] espressif/esp_lcd_axs15231b (1.0.1~1)
NOTICE: [26/60] espressif/esp_lcd_co5300 (2.1.0)
NOTICE: [27/60] espressif/esp_lcd_gc9a01 (2.0.1)
NOTICE: [28/60] espressif/esp_lcd_ili9341 (1.2.0)
NOTICE: [29/60] espressif/esp_lcd_panel_io_additions (1.0.1~1)
NOTICE: [30/60] espressif/esp_lcd_spd2010 (1.0.2)
NOTICE: [31/60] espressif/esp_lcd_st7701 (1.1.5)
NOTICE: [32/60] espressif/esp_lcd_st77916 (1.0.1)
NOTICE: [33/60] espressif/esp_lcd_st7796 (1.3.5)
NOTICE: [34/60] espressif/esp_lcd_touch (1.2.1)
NOTICE: [35/60] espressif/esp_lcd_touch_cst816s (1.1.1~2)
NOTICE: [36/60] espressif/esp_lcd_touch_ft5x06 (1.0.7)
NOTICE: [37/60] espressif/esp_lcd_touch_gt1151 (1.1.0~2)
NOTICE: [38/60] espressif/esp_lcd_touch_gt911 (1.2.0~3)
NOTICE: [39/60] espressif/esp_lcd_touch_st7123 (1.0.2)
NOTICE: [40/60] espressif/esp_lvgl_port (2.7.2)
NOTICE: [41/60] espressif/esp_mmap_assets (2.0.0)
NOTICE: [42/60] espressif/esp_new_jpeg (0.6.1)
NOTICE: [43/60] espressif/esp_sccb_intf (0.0.8)
NOTICE: [44/60] espressif/esp_video (1.3.1)
NOTICE: [45/60] espressif/i2c_bus (1.5.2)
NOTICE: [46/60] espressif/iot_eth (0.1.0)
NOTICE: [47/60] espressif/iot_usbh_cdc (3.1.0)
NOTICE: [48/60] espressif/iot_usbh_rndis (0.3.1)
NOTICE: [49/60] espressif/knob (1.1.0)
NOTICE: [50/60] espressif/led_strip (3.0.3)
NOTICE: [51/60] espressif/usb_host_uvc (2.3.1)
NOTICE: [52/60] espressif2022/image_player (1.1.1)
NOTICE: [53/60] lvgl/lvgl (9.4.0)
NOTICE: [54/60] tny-robotics/sh1106-esp-idf (1.0.1)
NOTICE: [55/60] txp666/otto-emoji-gif-component (1.3.0)
NOTICE: [56/60] waveshare/custom_io_expander_ch32v003 (1.0.2)
NOTICE: [57/60] waveshare/esp_lcd_sh8601 (1.0.2)
NOTICE: [58/60] waveshare/esp_lcd_touch_cst9217 (1.0.4)
NOTICE: [59/60] wvirgil123/sscma_client (1.0.2)
NOTICE: [60/60] idf (5.5.3)
-- ESP-TEE is currently supported only on the esp32c6;esp32h2;esp32c5 SoCs
NOTICE: Skipping optional dependency: espressif/esp_lcd_jd9365
NOTICE: Skipping optional dependency: waveshare/esp_lcd_st7703
NOTICE: Skipping optional dependency: espressif/esp32_p4_function_ev_board
NOTICE: Skipping optional dependency: espressif/esp_lcd_ili9881c
NOTICE: Skipping optional dependency: espressif/esp_lcd_ek79007
NOTICE: Skipping optional dependency: espressif/esp_hosted
NOTICE: Skipping optional dependency: espressif/esp_wifi_remote
NOTICE: Skipping optional dependency: espfriends/servo_dog_ctrl
NOTICE: Skipping optional dependency: llgok/cpp_bus_driver
NOTICE: Skipping optional dependency: espressif/cjson
NOTICE: Skipping optional dependency: espressif/esp_h264
NOTICE: Skipping optional dependency: espressif/esp_ipa
NOTICE: Skipping optional dependency: espressif/usb
CMake Error at /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:330 (message):
  Failed to resolve component 'ArduinoJson' required by component 'main':
  unknown name.
Call Stack (most recent call first):
  /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:379 (__build_resolve_and_add_req)
  /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:687 (__build_expand_requirements)
  /Users/cobain/esp/esp-idf/tools/cmake/project.cmake:741 (idf_build_process)
  CMakeLists.txt:23 (project)


-- Configuring incomplete, errors occurred!
[0;33mHINT: The component 'ArduinoJson' could not be found. This could be because: component name was misspelled, the component was not added to the build, the component has been moved to the IDF component manager, the component has been removed and refactored into some other component or the component may not be supported by the selected target.
Please look out for component in 'https://components.espressif.com' and add using 'idf.py add-dependency' command.
Refer to the migration guide for more details about moved components.
Refer to the build-system guide for more details about how components are found and included in the build.[0m
cmake failed with exit code 1, output of the command is in the /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build/log/idf_py_stderr_output_65683 and /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build/log/idf_py_stdout_output_65683
--- build exit: 2 ---
```

## 2026-08-06 09:00:28 阶段 0 编译 (EXTRA_COMPONENT_DIRS 修复)
```
$ idf.py reconfigure
Executing action: reconfigure
Running cmake in directory /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build
Executing "cmake -G Ninja -DPYTHON_DEPS_CHECKED=1 -DPYTHON=/Users/cobain/.espressif/python_env/idf5.5_py3.14_env/bin/python -DESP_PLATFORM=1 -DCCACHE_ENABLE=0 /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw"...
-- IDF_TARGET is not set, guessed 'esp32s3' from sdkconfig '/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/sdkconfig.defaults'
-- Found Git: /usr/bin/git (found version "2.50.1 (Apple Git-155)")
-- Minimal build - OFF
-- The C compiler identification is GNU 14.2.0
-- The CXX compiler identification is GNU 14.2.0
-- The ASM compiler identification is GNU
-- Found assembler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Building ESP-IDF components for target esp32s3
NOTICE: Dependencies lock doesn't exist, solving dependencies.
NOTICE: Skipping optional dependency: espressif/esp_lcd_jd9365
NOTICE: Skipping optional dependency: waveshare/esp_lcd_st7703
NOTICE: Skipping optional dependency: espressif/esp32_p4_function_ev_board
NOTICE: Skipping optional dependency: espressif/esp_lcd_ili9881c
NOTICE: Skipping optional dependency: espressif/esp_lcd_ek79007
NOTICE: Skipping optional dependency: espressif/esp_hosted
NOTICE: Skipping optional dependency: espressif/esp_wifi_remote
NOTICE: Skipping optional dependency: espfriends/servo_dog_ctrl
NOTICE: Skipping optional dependency: llgok/cpp_bus_driver
..NOTICE: Skipping optional dependency: espressif/mqtt
...NOTICE: Skipping optional dependency: espressif/cjson
...............................................................NOTICE: Skipping optional dependency: espressif/esp_h264
NOTICE: Skipping optional dependency: espressif/esp_ipa
NOTICE: Skipping optional dependency: espressif/usb
.....................NOTICE: Updating lock file at /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/dependencies.lock
NOTICE: Processing 60 dependencies:
NOTICE: [1/60] 78/esp-ml307 (3.6.5)
NOTICE: [2/60] 78/esp-wifi-connect (3.1.5)
NOTICE: [3/60] 78/esp_lcd_nv3023 (1.0.1)
NOTICE: [4/60] 78/uart-eth-modem (0.3.5)
NOTICE: [5/60] 78/uart-uhci (0.2.2)
NOTICE: [6/60] 78/xiaozhi-fonts (1.6.0)
NOTICE: [7/60] espressif/adc_battery_estimation (0.2.2)
NOTICE: [8/60] espressif/adc_mic (0.2.3)
NOTICE: [9/60] espressif/bmi270_sensor (0.1.2)
NOTICE: [10/60] espressif/button (4.1.7)
NOTICE: [11/60] espressif/cmake_utilities (0.5.3)
NOTICE: [12/60] espressif/dl_fft (0.6.0)
NOTICE: [13/60] espressif/esp-dsp (1.7.0)
NOTICE: [14/60] espressif/esp-sr (2.3.1)
NOTICE: [15/60] espressif/esp32-camera (2.1.7)
NOTICE: [16/60] espressif/esp_audio_codec (2.4.1)
NOTICE: [17/60] espressif/esp_audio_effects (1.2.1)
NOTICE: [18/60] espressif/esp_cam_sensor (1.5.2)
NOTICE: [19/60] espressif/esp_codec_dev (1.5.4)
NOTICE: [20/60] espressif/esp_image_effects (1.1.0)
NOTICE: [21/60] espressif/esp_io_expander (1.2.1)
NOTICE: [22/60] espressif/esp_io_expander_tca9554 (2.0.0)
NOTICE: [23/60] espressif/esp_io_expander_tca95xx_16bit (2.0.2)
NOTICE: [24/60] espressif/esp_jpeg (1.3.1)
NOTICE: [25/60] espressif/esp_lcd_axs15231b (1.0.1~1)
NOTICE: [26/60] espressif/esp_lcd_co5300 (2.1.0)
NOTICE: [27/60] espressif/esp_lcd_gc9a01 (2.0.1)
NOTICE: [28/60] espressif/esp_lcd_ili9341 (1.2.0)
NOTICE: [29/60] espressif/esp_lcd_panel_io_additions (1.0.1~1)
NOTICE: [30/60] espressif/esp_lcd_spd2010 (1.0.2)
NOTICE: [31/60] espressif/esp_lcd_st7701 (1.1.5)
NOTICE: [32/60] espressif/esp_lcd_st77916 (1.0.1)
NOTICE: [33/60] espressif/esp_lcd_st7796 (1.3.5)
NOTICE: [34/60] espressif/esp_lcd_touch (1.2.1)
NOTICE: [35/60] espressif/esp_lcd_touch_cst816s (1.1.1~2)
NOTICE: [36/60] espressif/esp_lcd_touch_ft5x06 (1.0.7)
NOTICE: [37/60] espressif/esp_lcd_touch_gt1151 (1.1.0~2)
NOTICE: [38/60] espressif/esp_lcd_touch_gt911 (1.2.0~3)
NOTICE: [39/60] espressif/esp_lcd_touch_st7123 (1.0.2)
NOTICE: [40/60] espressif/esp_lvgl_port (2.7.2)
NOTICE: [41/60] espressif/esp_mmap_assets (2.0.0)
NOTICE: [42/60] espressif/esp_new_jpeg (0.6.1)
NOTICE: [43/60] espressif/esp_sccb_intf (0.0.8)
NOTICE: [44/60] espressif/esp_video (1.3.1)
NOTICE: [45/60] espressif/i2c_bus (1.5.2)
NOTICE: [46/60] espressif/iot_eth (0.1.0)
NOTICE: [47/60] espressif/iot_usbh_cdc (3.1.0)
NOTICE: [48/60] espressif/iot_usbh_rndis (0.3.1)
NOTICE: [49/60] espressif/knob (1.1.0)
NOTICE: [50/60] espressif/led_strip (3.0.3)
NOTICE: [51/60] espressif/usb_host_uvc (2.3.1)
NOTICE: [52/60] espressif2022/image_player (1.1.1)
NOTICE: [53/60] lvgl/lvgl (9.4.0)
NOTICE: [54/60] tny-robotics/sh1106-esp-idf (1.0.1)
NOTICE: [55/60] txp666/otto-emoji-gif-component (1.3.0)
NOTICE: [56/60] waveshare/custom_io_expander_ch32v003 (1.0.2)
NOTICE: [57/60] waveshare/esp_lcd_sh8601 (1.0.2)
NOTICE: [58/60] waveshare/esp_lcd_touch_cst9217 (1.0.4)
NOTICE: [59/60] wvirgil123/sscma_client (1.0.2)
NOTICE: [60/60] idf (5.5.3)
-- ESP-TEE is currently supported only on the esp32c6;esp32h2;esp32c5 SoCs
NOTICE: Skipping optional dependency: espressif/esp_lcd_jd9365
NOTICE: Skipping optional dependency: waveshare/esp_lcd_st7703
NOTICE: Skipping optional dependency: espressif/esp32_p4_function_ev_board
NOTICE: Skipping optional dependency: espressif/esp_lcd_ili9881c
NOTICE: Skipping optional dependency: espressif/esp_lcd_ek79007
NOTICE: Skipping optional dependency: espressif/esp_hosted
NOTICE: Skipping optional dependency: espressif/esp_wifi_remote
NOTICE: Skipping optional dependency: espfriends/servo_dog_ctrl
NOTICE: Skipping optional dependency: llgok/cpp_bus_driver
NOTICE: Skipping optional dependency: espressif/cjson
NOTICE: Skipping optional dependency: espressif/esp_h264
NOTICE: Skipping optional dependency: espressif/esp_ipa
NOTICE: Skipping optional dependency: espressif/usb
CMake Error at /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:330 (message):
  Failed to resolve component 'ArduinoJson' required by component 'main':
  unknown name.
Call Stack (most recent call first):
  /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:379 (__build_resolve_and_add_req)
  /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:687 (__build_expand_requirements)
  /Users/cobain/esp/esp-idf/tools/cmake/project.cmake:741 (idf_build_process)
  CMakeLists.txt:30 (project)


-- Configuring incomplete, errors occurred!
[0;33mHINT: The component 'ArduinoJson' could not be found. This could be because: component name was misspelled, the component was not added to the build, the component has been moved to the IDF component manager, the component has been removed and refactored into some other component or the component may not be supported by the selected target.
Please look out for component in 'https://components.espressif.com' and add using 'idf.py add-dependency' command.
Refer to the migration guide for more details about moved components.
Refer to the build-system guide for more details about how components are found and included in the build.[0m
cmake failed with exit code 1, output of the command is in the /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build/log/idf_py_stderr_output_78739 and /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build/log/idf_py_stdout_output_78739
--- reconfigure exit: 2 ---
$ idf.py build
Executing action: all (aliases: build)
Running cmake in directory /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build
Executing "cmake -G Ninja -DPYTHON_DEPS_CHECKED=1 -DPYTHON=/Users/cobain/.espressif/python_env/idf5.5_py3.14_env/bin/python -DESP_PLATFORM=1 -DCCACHE_ENABLE=0 /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw"...
-- IDF_TARGET is not set, guessed 'esp32s3' from sdkconfig '/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/sdkconfig.defaults'
-- Found Git: /usr/bin/git (found version "2.50.1 (Apple Git-155)")
-- Minimal build - OFF
-- The C compiler identification is GNU 14.2.0
-- The CXX compiler identification is GNU 14.2.0
-- The ASM compiler identification is GNU
-- Found assembler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Building ESP-IDF components for target esp32s3
NOTICE: Skipping optional dependency: espressif/cjson
NOTICE: Skipping optional dependency: espressif/esp_h264
NOTICE: Skipping optional dependency: espressif/esp_ipa
NOTICE: Skipping optional dependency: espressif/usb
NOTICE: Skipping optional dependency: espressif/esp_lcd_jd9365
NOTICE: Skipping optional dependency: waveshare/esp_lcd_st7703
NOTICE: Skipping optional dependency: espressif/esp32_p4_function_ev_board
NOTICE: Skipping optional dependency: espressif/esp_lcd_ili9881c
NOTICE: Skipping optional dependency: espressif/esp_lcd_ek79007
NOTICE: Skipping optional dependency: espressif/esp_hosted
NOTICE: Skipping optional dependency: espressif/esp_wifi_remote
NOTICE: Skipping optional dependency: espfriends/servo_dog_ctrl
NOTICE: Skipping optional dependency: llgok/cpp_bus_driver
NOTICE: Skipping optional dependency: espressif/mqtt
NOTICE: Processing 60 dependencies:
NOTICE: [1/60] 78/esp-ml307 (3.6.5)
NOTICE: [2/60] 78/esp-wifi-connect (3.1.5)
NOTICE: [3/60] 78/esp_lcd_nv3023 (1.0.1)
NOTICE: [4/60] 78/uart-eth-modem (0.3.5)
NOTICE: [5/60] 78/uart-uhci (0.2.2)
NOTICE: [6/60] 78/xiaozhi-fonts (1.6.0)
NOTICE: [7/60] espressif/adc_battery_estimation (0.2.2)
NOTICE: [8/60] espressif/adc_mic (0.2.3)
NOTICE: [9/60] espressif/bmi270_sensor (0.1.2)
NOTICE: [10/60] espressif/button (4.1.7)
NOTICE: [11/60] espressif/cmake_utilities (0.5.3)
NOTICE: [12/60] espressif/dl_fft (0.6.0)
NOTICE: [13/60] espressif/esp-dsp (1.7.0)
NOTICE: [14/60] espressif/esp-sr (2.3.1)
NOTICE: [15/60] espressif/esp32-camera (2.1.7)
NOTICE: [16/60] espressif/esp_audio_codec (2.4.1)
NOTICE: [17/60] espressif/esp_audio_effects (1.2.1)
NOTICE: [18/60] espressif/esp_cam_sensor (1.5.2)
NOTICE: [19/60] espressif/esp_codec_dev (1.5.4)
NOTICE: [20/60] espressif/esp_image_effects (1.1.0)
NOTICE: [21/60] espressif/esp_io_expander (1.2.1)
NOTICE: [22/60] espressif/esp_io_expander_tca9554 (2.0.0)
NOTICE: [23/60] espressif/esp_io_expander_tca95xx_16bit (2.0.2)
NOTICE: [24/60] espressif/esp_jpeg (1.3.1)
NOTICE: [25/60] espressif/esp_lcd_axs15231b (1.0.1~1)
NOTICE: [26/60] espressif/esp_lcd_co5300 (2.1.0)
NOTICE: [27/60] espressif/esp_lcd_gc9a01 (2.0.1)
NOTICE: [28/60] espressif/esp_lcd_ili9341 (1.2.0)
NOTICE: [29/60] espressif/esp_lcd_panel_io_additions (1.0.1~1)
NOTICE: [30/60] espressif/esp_lcd_spd2010 (1.0.2)
NOTICE: [31/60] espressif/esp_lcd_st7701 (1.1.5)
NOTICE: [32/60] espressif/esp_lcd_st77916 (1.0.1)
NOTICE: [33/60] espressif/esp_lcd_st7796 (1.3.5)
NOTICE: [34/60] espressif/esp_lcd_touch (1.2.1)
NOTICE: [35/60] espressif/esp_lcd_touch_cst816s (1.1.1~2)
NOTICE: [36/60] espressif/esp_lcd_touch_ft5x06 (1.0.7)
NOTICE: [37/60] espressif/esp_lcd_touch_gt1151 (1.1.0~2)
NOTICE: [38/60] espressif/esp_lcd_touch_gt911 (1.2.0~3)
NOTICE: [39/60] espressif/esp_lcd_touch_st7123 (1.0.2)
NOTICE: [40/60] espressif/esp_lvgl_port (2.7.2)
NOTICE: [41/60] espressif/esp_mmap_assets (2.0.0)
NOTICE: [42/60] espressif/esp_new_jpeg (0.6.1)
NOTICE: [43/60] espressif/esp_sccb_intf (0.0.8)
NOTICE: [44/60] espressif/esp_video (1.3.1)
NOTICE: [45/60] espressif/i2c_bus (1.5.2)
NOTICE: [46/60] espressif/iot_eth (0.1.0)
NOTICE: [47/60] espressif/iot_usbh_cdc (3.1.0)
NOTICE: [48/60] espressif/iot_usbh_rndis (0.3.1)
NOTICE: [49/60] espressif/knob (1.1.0)
NOTICE: [50/60] espressif/led_strip (3.0.3)
NOTICE: [51/60] espressif/usb_host_uvc (2.3.1)
NOTICE: [52/60] espressif2022/image_player (1.1.1)
NOTICE: [53/60] lvgl/lvgl (9.4.0)
NOTICE: [54/60] tny-robotics/sh1106-esp-idf (1.0.1)
NOTICE: [55/60] txp666/otto-emoji-gif-component (1.3.0)
NOTICE: [56/60] waveshare/custom_io_expander_ch32v003 (1.0.2)
NOTICE: [57/60] waveshare/esp_lcd_sh8601 (1.0.2)
NOTICE: [58/60] waveshare/esp_lcd_touch_cst9217 (1.0.4)
NOTICE: [59/60] wvirgil123/sscma_client (1.0.2)
NOTICE: [60/60] idf (5.5.3)
-- ESP-TEE is currently supported only on the esp32c6;esp32h2;esp32c5 SoCs
NOTICE: Skipping optional dependency: espressif/esp_lcd_jd9365
NOTICE: Skipping optional dependency: waveshare/esp_lcd_st7703
NOTICE: Skipping optional dependency: espressif/esp32_p4_function_ev_board
NOTICE: Skipping optional dependency: espressif/esp_lcd_ili9881c
NOTICE: Skipping optional dependency: espressif/esp_lcd_ek79007
NOTICE: Skipping optional dependency: espressif/esp_hosted
NOTICE: Skipping optional dependency: espressif/esp_wifi_remote
NOTICE: Skipping optional dependency: espfriends/servo_dog_ctrl
NOTICE: Skipping optional dependency: llgok/cpp_bus_driver
NOTICE: Skipping optional dependency: espressif/cjson
NOTICE: Skipping optional dependency: espressif/esp_h264
NOTICE: Skipping optional dependency: espressif/esp_ipa
NOTICE: Skipping optional dependency: espressif/usb
CMake Error at /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:330 (message):
  Failed to resolve component 'ArduinoJson' required by component 'main':
  unknown name.
Call Stack (most recent call first):
  /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:379 (__build_resolve_and_add_req)
  /Users/cobain/esp/esp-idf/tools/cmake/build.cmake:687 (__build_expand_requirements)
  /Users/cobain/esp/esp-idf/tools/cmake/project.cmake:741 (idf_build_process)
  CMakeLists.txt:30 (project)


-- Configuring incomplete, errors occurred!
[0;33mHINT: The component 'ArduinoJson' could not be found. This could be because: component name was misspelled, the component was not added to the build, the component has been moved to the IDF component manager, the component has been removed and refactored into some other component or the component may not be supported by the selected target.
Please look out for component in 'https://components.espressif.com' and add using 'idf.py add-dependency' command.
Refer to the migration guide for more details about moved components.
Refer to the build-system guide for more details about how components are found and included in the build.[0m
cmake failed with exit code 1, output of the command is in the /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build/log/idf_py_stderr_output_79210 and /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/build/log/idf_py_stdout_output_79210
--- build exit: 2 ---
```

---

## 2026-08-06 第 N+1 轮：Windows 压缩包验证 + 格式转换

### 关键发现
1. **Windows 压缩包确认**：robot-updating/ 里有 `build_espidf_short.ps1` (PowerShell 脚本)
2. **CRLF 文件**：`fw/CMakeLists.txt`、`fw/main/CMakeLists.txt`、`fw/sdkconfig.defaults` 都是 CRLF
3. **CMakeLists.txt 修复**：`EXTRA_COMPONENT_DIRS` 已经在 line 28
4. **ArduinoJson 修复**：`idf_component.yml` 已经清理过

### 已做操作
1. ✅ CRLF → LF 全部转换（Python 脚本处理）
2. ✅ 删除 build 缓存
3. ✅ 删除 `esp-idf-env/v5.5.4`（v5.5.4 工具链不完整，不能用）

### 编译失败原因（Trae 沙箱）
```
TRAE Sandbox Error: hit restricted
  Not allow operate files: /Users/cobain/esp/esp-idf/.git/index.lock, ...
  /Users/cobain/esp/esp-idf/.git/modules/.../index.lock (很多)
```

**根因**：Trae 沙箱阻止 idf.py 访问 ESP-IDF 的 .git 目录。
**解决**：必须在 macOS Terminal.app 跑编译，Trae 沙箱外的进程不受限。

### 给用户的 Terminal.app 命令
```bash
# 1. 打开 Terminal.app（必须用 macOS 自带的，不要用 Trae 内置）
# 2. 粘贴并执行：
cd "/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw"
source ~/esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py reconfigure
idf.py build 2>&1 | tee /tmp/robot_updating_build.log
```

### 期望结果
- v5.5.3 工具链完整（之前 stackchan-fixed-blinkfix 用这套工具链能成功）
- CMakeLists.txt 已经有 EXTRA_COMPONENT_DIRS
- CRLF 已转 LF
- 应该能正常编译

### 如果还是失败
- 把 `/tmp/robot_updating_build.log` 给我看
- 不要用 `tail -30` 这种短的，**完整贴出来**

---

## 2026-08-06 11:00 重大根因发现 ⚠️

### 问题
- 路径含方括号：`/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/`
- CMake 的 `file(GLOB "${CMAKE_CURRENT_LIST_DIR}/components/*")` 把 `[0]` 当成字符类通配符
- 导致 components 目录扫描返回空 → ArduinoJson 找不到

### 验证
- stackchan-fixed-blinkfix 在 `/Users/cobain/stackchan-fixed-blinkfix/`（无方括号）能成功
- robot-updating 路径含 `[0]project-JHTech/[2].../[15]Stackstan` → 失败
- 软链到 `/Users/cobain/robot-updating`（无方括号）→ Python glob 正常返回 5 个 component

### 修复
**在 macOS Terminal.app 创建 symlink**（Trae 沙箱限制不能在 /Users/cobain/ 创 symlink）：
```bash
ln -sfn "/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating" /Users/cobain/robot-updating
```

然后所有命令用 `/Users/cobain/robot-updating/fw` 路径，不要再用带方括号的路径。

### 之前的修复可以保留
- CRLF → LF：✅
- fw/CMakeLists.txt 加 EXTRA_COMPONENT_DIRS：✅
- ArduinoJson idf_component.yml 清空：✅

## [2026-08-06 15:07:03] 阶段 1.2 build
```
'/Users/cobain/.espressif/python_env/idf5.5_py3.14_env/bin/python' is currently active in the environment while the project was configured with '/Users/cobain/.espressif/python_env/idf5.5_py3.14_env/bin/python3'. Run 'idf.py fullclean' to start again.
Executing action: all (aliases: build)
```
stack-chan.bin size: 3738624 bytes, mtime: Aug  6 13:39:02 2026
generated_assets.bin size: 4866937 bytes, mtime: Aug  6 13:19:02 2026

## [2026-08-06 15:08:19] 阶段 1.2 fullclean + build（python 路径重置）
原因：当前 python 与上次配置的 python3 路径不一致，CMake 拒绝增量 build

### 1) idf.py fullclean
```
Executing action: fullclean
Executing action: remove_managed_components
Traceback (most recent call last):
  File "/Users/cobain/esp/esp-idf/tools/idf.py", line 934, in <module>
    main()
    ~~~~^^
  File "/Users/cobain/esp/esp-idf/tools/idf.py", line 813, in main
    cli(argv, prog_name=PROG, complete_var=SHELL_COMPLETE_VAR)
    ~~~^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "/Users/cobain/.espressif/python_env/idf5.5_py3.14_env/lib/python3.14/site-packages/click/core.py", line 1161, in __call__
    return self.main(*args, **kwargs)
           ~~~~~~~~~^^^^^^^^^^^^^^^^^
  File "/Users/cobain/.espressif/python_env/idf5.5_py3.14_env/lib/python3.14/site-packages/click/core.py", line 1082, in main
    rv = self.invoke(ctx)
  File "/Users/cobain/.espressif/python_env/idf5.5_py3.14_env/lib/python3.14/site-packages/click/core.py", line 1729, in invoke
    return _process_result(rv)
  File "/Users/cobain/.espressif/python_env/idf5.5_py3.14_env/lib/python3.14/site-packages/click/core.py", line 1666, in _process_result
    value = ctx.invoke(self._result_callback, value, **ctx.params)
  File "/Users/cobain/.espressif/python_env/idf5.5_py3.14_env/lib/python3.14/site-packages/click/core.py", line 788, in invoke
    return __callback(*args, **kwargs)
  File "/Users/cobain/esp/esp-idf/tools/idf.py", line 709, in execute_tasks
    task(ctx, global_args, task.action_args)
    ~~~~^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "/Users/cobain/esp/esp-idf/tools/idf.py", line 239, in __call__
    self.callback(self.name, context, global_args, **action_args)
    ~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "/Users/cobain/.espressif/python_env/idf5.5_py3.14_env/lib/python3.14/site-packages/idf_component_manager/idf_extensions.py", line 177, in callback
    getattr(manager, str(subcommand_name).replace('-', '_'))(**kwargs)
    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~^^^^^^^^^^
  File "/Users/cobain/.espressif/python_env/idf5.5_py3.14_env/lib/python3.14/site-packages/idf_component_manager/core.py", line 111, in wrapper
    return func(self, *args, **kwargs)
  File "/Users/cobain/.espressif/python_env/idf5.5_py3.14_env/lib/python3.14/site-packages/idf_component_manager/core.py", line 512, in remove_managed_components
    shutil.rmtree(str(managed_components_dir))
    ~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "/opt/homebrew/Cellar/python@3.14/3.14.3_1/Frameworks/Python.framework/Versions/3.14/lib/python3.14/shutil.py", line 852, in rmtree
    _rmtree_impl(path, dir_fd, onexc)
    ~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^^
  File "/opt/homebrew/Cellar/python@3.14/3.14.3_1/Frameworks/Python.framework/Versions/3.14/lib/python3.14/shutil.py", line 721, in _rmtree_safe_fd
    _rmtree_safe_fd_step(stack, onexc)
    ~~~~~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^
  File "/opt/homebrew/Cellar/python@3.14/3.14.3_1/Frameworks/Python.framework/Versions/3.14/lib/python3.14/shutil.py", line 802, in _rmtree_safe_fd_step
    onexc(func, path, err)
    ~~~~~^^^^^^^^^^^^^^^^^
  File "/opt/homebrew/Cellar/python@3.14/3.14.3_1/Frameworks/Python.framework/Versions/3.14/lib/python3.14/shutil.py", line 771, in _rmtree_safe_fd_step
    raise OSError("Cannot call rmtree on a symbolic link")
OSError: [Errno None] None: '/Users/cobain/robot-build/fw/managed_components'
```

### 2) idf.py build（从 0 开始，约 5-10 分钟）
```
Executing action: all (aliases: build)
Running cmake in directory /Users/cobain/robot-build/fw/build
Executing "cmake -G Ninja -DPYTHON_DEPS_CHECKED=1 -DPYTHON=/Users/cobain/.espressif/python_env/idf5.5_py3.14_env/bin/python -DESP_PLATFORM=1 -DCCACHE_ENABLE=0 /Users/cobain/robot-build/fw"...
-- IDF_TARGET is not set, guessed 'esp32s3' from sdkconfig '/Users/cobain/robot-build/fw/sdkconfig'
-- Found Git: /usr/bin/git (found version "2.50.1 (Apple Git-155)")
-- Minimal build - OFF
-- The C compiler identification is GNU 14.2.0
-- The CXX compiler identification is GNU 14.2.0
-- The ASM compiler identification is GNU
-- Found assembler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Building ESP-IDF components for target esp32s3
NOTICE: Skipping optional dependency: espressif/cjson
NOTICE: Skipping optional dependency: espressif/esp_h264
NOTICE: Skipping optional dependency: espressif/esp_ipa
NOTICE: Skipping optional dependency: espressif/usb
NOTICE: Skipping optional dependency: espressif/esp_lcd_jd9365
NOTICE: Skipping optional dependency: waveshare/esp_lcd_st7703
NOTICE: Skipping optional dependency: espressif/esp32_p4_function_ev_board
NOTICE: Skipping optional dependency: espressif/esp_lcd_ili9881c
NOTICE: Skipping optional dependency: espressif/esp_lcd_ek79007
NOTICE: Skipping optional dependency: espressif/esp_hosted
NOTICE: Skipping optional dependency: espressif/esp_wifi_remote
NOTICE: Skipping optional dependency: espfriends/servo_dog_ctrl
NOTICE: Skipping optional dependency: llgok/cpp_bus_driver
NOTICE: Skipping optional dependency: espressif/mqtt
NOTICE: Processing 60 dependencies:
NOTICE: [1/60] 78/esp-ml307 (3.6.5)
NOTICE: [2/60] 78/esp-wifi-connect (3.1.5)
NOTICE: [3/60] 78/esp_lcd_nv3023 (1.0.1)
NOTICE: [4/60] 78/uart-eth-modem (0.3.5)
NOTICE: [5/60] 78/uart-uhci (0.2.2)
NOTICE: [6/60] 78/xiaozhi-fonts (1.6.0)
NOTICE: [7/60] espressif/adc_battery_estimation (0.2.2)
NOTICE: [8/60] espressif/adc_mic (0.2.3)
NOTICE: [9/60] espressif/bmi270_sensor (0.1.2)
NOTICE: [10/60] espressif/button (4.1.7)
NOTICE: [11/60] espressif/cmake_utilities (0.5.3)
NOTICE: [12/60] espressif/dl_fft (0.6.0)
NOTICE: [13/60] espressif/esp-dsp (1.7.0)
NOTICE: [14/60] espressif/esp-sr (2.3.1)
NOTICE: [15/60] espressif/esp32-camera (2.1.7)
NOTICE: [16/60] espressif/esp_audio_codec (2.4.1)
NOTICE: [17/60] espressif/esp_audio_effects (1.2.1)
NOTICE: [18/60] espressif/esp_cam_sensor (1.5.2)
NOTICE: [19/60] espressif/esp_codec_dev (1.5.4)
NOTICE: [20/60] espressif/esp_image_effects (1.1.0)
NOTICE: [21/60] espressif/esp_io_expander (1.2.1)
NOTICE: [22/60] espressif/esp_io_expander_tca9554 (2.0.0)
NOTICE: [23/60] espressif/esp_io_expander_tca95xx_16bit (2.0.2)
NOTICE: [24/60] espressif/esp_jpeg (1.3.1)
NOTICE: [25/60] espressif/esp_lcd_axs15231b (1.0.1~1)
NOTICE: [26/60] espressif/esp_lcd_co5300 (2.1.0)
NOTICE: [27/60] espressif/esp_lcd_gc9a01 (2.0.1)
NOTICE: [28/60] espressif/esp_lcd_ili9341 (1.2.0)
NOTICE: [29/60] espressif/esp_lcd_panel_io_additions (1.0.1~1)
NOTICE: [30/60] espressif/esp_lcd_spd2010 (1.0.2)
NOTICE: [31/60] espressif/esp_lcd_st7701 (1.1.5)
NOTICE: [32/60] espressif/esp_lcd_st77916 (1.0.1)
NOTICE: [33/60] espressif/esp_lcd_st7796 (1.3.5)
NOTICE: [34/60] espressif/esp_lcd_touch (1.2.1)
NOTICE: [35/60] espressif/esp_lcd_touch_cst816s (1.1.1~2)
NOTICE: [36/60] espressif/esp_lcd_touch_ft5x06 (1.0.7)
NOTICE: [37/60] espressif/esp_lcd_touch_gt1151 (1.1.0~2)
NOTICE: [38/60] espressif/esp_lcd_touch_gt911 (1.2.0~3)
NOTICE: [39/60] espressif/esp_lcd_touch_st7123 (1.0.2)
NOTICE: [40/60] espressif/esp_lvgl_port (2.7.2)
NOTICE: [41/60] espressif/esp_mmap_assets (2.0.0)
NOTICE: [42/60] espressif/esp_new_jpeg (0.6.1)
NOTICE: [43/60] espressif/esp_sccb_intf (0.0.8)
NOTICE: [44/60] espressif/esp_video (1.3.1)
NOTICE: [45/60] espressif/i2c_bus (1.5.2)
NOTICE: [46/60] espressif/iot_eth (0.1.0)
NOTICE: [47/60] espressif/iot_usbh_cdc (3.1.0)
NOTICE: [48/60] espressif/iot_usbh_rndis (0.3.1)
NOTICE: [49/60] espressif/knob (1.1.0)
NOTICE: [50/60] espressif/led_strip (3.0.3)
NOTICE: [51/60] espressif/usb_host_uvc (2.3.1)
NOTICE: [52/60] espressif2022/image_player (1.1.1)
NOTICE: [53/60] lvgl/lvgl (9.4.0)
NOTICE: [54/60] tny-robotics/sh1106-esp-idf (1.0.1)
NOTICE: [55/60] txp666/otto-emoji-gif-component (1.3.0)
NOTICE: [56/60] waveshare/custom_io_expander_ch32v003 (1.0.2)
NOTICE: [57/60] waveshare/esp_lcd_sh8601 (1.0.2)
NOTICE: [58/60] waveshare/esp_lcd_touch_cst9217 (1.0.4)
NOTICE: [59/60] wvirgil123/sscma_client (1.0.2)
NOTICE: [60/60] idf (5.5.3)
-- Using en-US fallback for missing audio: 0.ogg
-- Using en-US fallback for missing audio: 1.ogg
-- Using en-US fallback for missing audio: 2.ogg
-- Using en-US fallback for missing audio: 3.ogg
-- Using en-US fallback for missing audio: 4.ogg
-- Using en-US fallback for missing audio: 5.ogg
-- Using en-US fallback for missing audio: 6.ogg
-- Using en-US fallback for missing audio: 7.ogg
-- Using en-US fallback for missing audio: 8.ogg
-- Using en-US fallback for missing audio: 9.ogg
-- Using en-US fallback for missing audio: activation.ogg
-- Using en-US fallback for missing audio: err_pin.ogg
-- Using en-US fallback for missing audio: err_reg.ogg
-- Using en-US fallback for missing audio: upgrade.ogg
-- Using en-US fallback for missing audio: welcome.ogg
-- Using en-US fallback for missing audio: wificonfig.ogg
-- ESP-TEE is currently supported only on the esp32c6;esp32h2;esp32c5 SoCs
NOTICE: Skipping optional dependency: espressif/esp_lcd_jd9365
NOTICE: Skipping optional dependency: waveshare/esp_lcd_st7703
NOTICE: Skipping optional dependency: espressif/esp32_p4_function_ev_board
NOTICE: Skipping optional dependency: espressif/esp_lcd_ili9881c
NOTICE: Skipping optional dependency: espressif/esp_lcd_ek79007
NOTICE: Skipping optional dependency: espressif/esp_hosted
NOTICE: Skipping optional dependency: espressif/esp_wifi_remote
NOTICE: Skipping optional dependency: espfriends/servo_dog_ctrl
NOTICE: Skipping optional dependency: llgok/cpp_bus_driver
NOTICE: Skipping optional dependency: espressif/cjson
NOTICE: Skipping optional dependency: espressif/esp_h264
NOTICE: Skipping optional dependency: espressif/esp_ipa
NOTICE: Skipping optional dependency: espressif/usb
-- Project sdkconfig file /Users/cobain/robot-build/fw/sdkconfig
Loading defaults file /Users/cobain/robot-build/fw/sdkconfig.defaults...
-- Compiler supported targets: xtensa-esp-elf
-- Found Python3: /Users/cobain/.espressif/python_env/idf5.5_py3.14_env/bin/python (found version "3.14.3") found components: Interpreter
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD - Success
-- Found Threads: TRUE
-- Performing Test C_COMPILER_SUPPORTS_WFORMAT_SIGNEDNESS
-- Performing Test C_COMPILER_SUPPORTS_WFORMAT_SIGNEDNESS - Success
-- USING O3
-- App "stack-chan" version: 1.4.3
-- Adding linker script /Users/cobain/robot-build/fw/build/esp-idf/esp_system/ld/memory.ld
-- Adding linker script /Users/cobain/robot-build/fw/build/esp-idf/esp_system/ld/sections.ld.in
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.api.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.bt_funcs.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.libgcc.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.wdt.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.version.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.ble_cca.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.ble_test.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.libc.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.newlib.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/soc/esp32s3/ld/esp32s3.peripherals.ld
-- ESP_NOW: 2.5.2
78/esp_wifi_connect: IDF_VER is v5.5.3
78/esp_wifi_connect: idf_component_register for v5 used
-- ESP_LCD_NV3023: 1.0.1
-- IOT_ETH: 0.1.0
-- ADC_BATTERY_ESTIMATION: 0.2.2
-- ADC_MIC: 0.2.3
-- I2C_BUS: 1.5.2
-- BUTTON: 4.1.7
-- ESP_LCD_AXS15231B: 1.0.1
-- ESP_LCD_CO5300: 2.1.0
-- ESP_LCD_GC9A01: 2.0.1
-- ESP_LCD_ILI9341: 1.2.0
-- ESP_LCD_PANEL_IO_ADDITIONS: 1.0.1
-- ESP_LCD_SPD2010: 1.0.2
-- ESP_LCD_ST7701: 1.1.5
-- ESP_LCD_ST77916: 1.0.1
-- ESP_LCD_ST7796: 1.3.5
-- ESP_LCD_TOUCH_ST7123: 1.0.2
-- LVGL version: 9.4.0
-- ESP_MMAP_ASSETS: 2.0.0
-- ESP_CAM_SENSOR: 1.5.2
-- ESP_VIDEO: 1.3.1
-- IOT_USBH_CDC: 3.1.0
-- IOT_USBH_RNDIS: 0.3.1
-- KNOB: 1.1.0
-- Otto Emoji GIF Component: gifs only at /Users/cobain/robot-build/fw/managed_components/txp666__otto-emoji-gif-component/gifs
-- ESP_LCD_SH8601: 1.0.2
-- 米宝资源: 已同步 /Users/cobain/robot-build/fw/main/assets/mibao_bin/mibao_chat_60.bin;/Users/cobain/robot-build/fw/main/assets/mibao_bin/mibao_iot_150.bin;/Users/cobain/robot-build/fw/main/assets/mibao_bin/mibao_look_close_150.bin;/Users/cobain/robot-build/fw/main/assets/mibao_bin/mibao_look_open_150.bin;/Users/cobain/robot-build/fw/main/assets/mibao_bin/mibao_meeting_150.bin;/Users/cobain/robot-build/fw/main/assets/mibao_bin/mibao_personal_150.bin;/Users/cobain/robot-build/fw/main/assets/mibao_bin/mibao_work_150.bin 个 bin 到 /Users/cobain/robot-build/fw/main/assets/assets_bin
-- Default assets build configured: /Users/cobain/robot-build/fw/build/generated_assets.bin
-- Generated default assets flash configured: /Users/cobain/robot-build/fw/build/generated_assets.bin -> assets partition
-- Component idf::main will be linked with -Wl,--whole-archive
-- Components: 78__esp-ml307 78__esp-wifi-connect 78__esp_lcd_nv3023 78__uart-eth-modem 78__uart-uhci 78__xiaozhi-fonts ArduinoJson app_trace app_update bootloader bootloader_support bt cmock console cxx driver efuse esp-now esp-tls esp_adc esp_app_format esp_bootloader_format esp_coex esp_common esp_driver_ana_cmpr esp_driver_bitscrambler esp_driver_cam esp_driver_dac esp_driver_gpio esp_driver_gptimer esp_driver_i2c esp_driver_i2s esp_driver_isp esp_driver_jpeg esp_driver_ledc esp_driver_mcpwm esp_driver_parlio esp_driver_pcnt esp_driver_ppa esp_driver_rmt esp_driver_sdio esp_driver_sdm esp_driver_sdmmc esp_driver_sdspi esp_driver_spi esp_driver_touch_sens esp_driver_tsens esp_driver_twai esp_driver_uart esp_driver_usb_serial_jtag esp_eth esp_event esp_gdbstub esp_hal_ieee802154 esp_hid esp_http_client esp_http_server esp_https_ota esp_https_server esp_hw_support esp_lcd esp_local_ctrl esp_mm esp_netif esp_netif_stack esp_partition esp_phy esp_pm esp_psram esp_ringbuf esp_rom esp_security esp_system esp_timer esp_vfs_console esp_wifi espcoredump espressif2022__image_player espressif__adc_battery_estimation espressif__adc_mic espressif__bmi270_sensor espressif__button espressif__cmake_utilities espressif__dl_fft espressif__esp-dsp espressif__esp-sr espressif__esp32-camera espressif__esp_audio_codec espressif__esp_audio_effects espressif__esp_cam_sensor espressif__esp_codec_dev espressif__esp_image_effects espressif__esp_io_expander espressif__esp_io_expander_tca9554 espressif__esp_io_expander_tca95xx_16bit espressif__esp_jpeg espressif__esp_lcd_axs15231b espressif__esp_lcd_co5300 espressif__esp_lcd_gc9a01 espressif__esp_lcd_ili9341 espressif__esp_lcd_panel_io_additions espressif__esp_lcd_spd2010 espressif__esp_lcd_st7701 espressif__esp_lcd_st77916 espressif__esp_lcd_st7796 espressif__esp_lcd_touch espressif__esp_lcd_touch_cst816s espressif__esp_lcd_touch_ft5x06 espressif__esp_lcd_touch_gt1151 espressif__esp_lcd_touch_gt911 espressif__esp_lcd_touch_st7123 espressif__esp_lvgl_port espressif__esp_mmap_assets espressif__esp_new_jpeg espressif__esp_sccb_intf espressif__esp_video espressif__i2c_bus espressif__iot_eth espressif__iot_usbh_cdc espressif__iot_usbh_rndis espressif__knob espressif__led_strip espressif__usb_host_uvc esptool_py fatfs freertos hal heap http_parser idf_test ieee802154 json log lvgl__lvgl lwip main mbedtls mooncake mooncake_log mqtt newlib nvs_flash nvs_sec_provider openthread partition_table perfmon protobuf-c protocomm pthread rt sdmmc smooth_ui_toolkit soc spi_flash spiffs tcp_transport tny-robotics__sh1106-esp-idf touch_element txp666__otto-emoji-gif-component ulp unity usb vfs waveshare__custom_io_expander_ch32v003 waveshare__esp_lcd_sh8601 waveshare__esp_lcd_touch_cst9217 wear_levelling wifi_provisioning wpa_supplicant wvirgil123__sscma_client xtensa
-- Component paths: /Users/cobain/robot-build/fw/managed_components/78__esp-ml307 /Users/cobain/robot-build/fw/managed_components/78__esp-wifi-connect /Users/cobain/robot-build/fw/managed_components/78__esp_lcd_nv3023 /Users/cobain/robot-build/fw/managed_components/78__uart-eth-modem /Users/cobain/robot-build/fw/managed_components/78__uart-uhci /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts /Users/cobain/robot-build/fw/components/ArduinoJson /Users/cobain/esp/esp-idf/components/app_trace /Users/cobain/esp/esp-idf/components/app_update /Users/cobain/esp/esp-idf/components/bootloader /Users/cobain/esp/esp-idf/components/bootloader_support /Users/cobain/esp/esp-idf/components/bt /Users/cobain/esp/esp-idf/components/cmock /Users/cobain/esp/esp-idf/components/console /Users/cobain/esp/esp-idf/components/cxx /Users/cobain/esp/esp-idf/components/driver /Users/cobain/esp/esp-idf/components/efuse /Users/cobain/robot-build/fw/components/esp-now /Users/cobain/esp/esp-idf/components/esp-tls /Users/cobain/esp/esp-idf/components/esp_adc /Users/cobain/esp/esp-idf/components/esp_app_format /Users/cobain/esp/esp-idf/components/esp_bootloader_format /Users/cobain/esp/esp-idf/components/esp_coex /Users/cobain/esp/esp-idf/components/esp_common /Users/cobain/esp/esp-idf/components/esp_driver_ana_cmpr /Users/cobain/esp/esp-idf/components/esp_driver_bitscrambler /Users/cobain/esp/esp-idf/components/esp_driver_cam /Users/cobain/esp/esp-idf/components/esp_driver_dac /Users/cobain/esp/esp-idf/components/esp_driver_gpio /Users/cobain/esp/esp-idf/components/esp_driver_gptimer /Users/cobain/esp/esp-idf/components/esp_driver_i2c /Users/cobain/esp/esp-idf/components/esp_driver_i2s /Users/cobain/esp/esp-idf/components/esp_driver_isp /Users/cobain/esp/esp-idf/components/esp_driver_jpeg /Users/cobain/esp/esp-idf/components/esp_driver_ledc /Users/cobain/esp/esp-idf/components/esp_driver_mcpwm /Users/cobain/esp/esp-idf/components/esp_driver_parlio /Users/cobain/esp/esp-idf/components/esp_driver_pcnt /Users/cobain/esp/esp-idf/components/esp_driver_ppa /Users/cobain/esp/esp-idf/components/esp_driver_rmt /Users/cobain/esp/esp-idf/components/esp_driver_sdio /Users/cobain/esp/esp-idf/components/esp_driver_sdm /Users/cobain/esp/esp-idf/components/esp_driver_sdmmc /Users/cobain/esp/esp-idf/components/esp_driver_sdspi /Users/cobain/esp/esp-idf/components/esp_driver_spi /Users/cobain/esp/esp-idf/components/esp_driver_touch_sens /Users/cobain/esp/esp-idf/components/esp_driver_tsens /Users/cobain/esp/esp-idf/components/esp_driver_twai /Users/cobain/esp/esp-idf/components/esp_driver_uart /Users/cobain/esp/esp-idf/components/esp_driver_usb_serial_jtag /Users/cobain/esp/esp-idf/components/esp_eth /Users/cobain/esp/esp-idf/components/esp_event /Users/cobain/esp/esp-idf/components/esp_gdbstub /Users/cobain/esp/esp-idf/components/esp_hal_ieee802154 /Users/cobain/esp/esp-idf/components/esp_hid /Users/cobain/esp/esp-idf/components/esp_http_client /Users/cobain/esp/esp-idf/components/esp_http_server /Users/cobain/esp/esp-idf/components/esp_https_ota /Users/cobain/esp/esp-idf/components/esp_https_server /Users/cobain/esp/esp-idf/components/esp_hw_support /Users/cobain/esp/esp-idf/components/esp_lcd /Users/cobain/esp/esp-idf/components/esp_local_ctrl /Users/cobain/esp/esp-idf/components/esp_mm /Users/cobain/esp/esp-idf/components/esp_netif /Users/cobain/esp/esp-idf/components/esp_netif_stack /Users/cobain/esp/esp-idf/components/esp_partition /Users/cobain/esp/esp-idf/components/esp_phy /Users/cobain/esp/esp-idf/components/esp_pm /Users/cobain/esp/esp-idf/components/esp_psram /Users/cobain/esp/esp-idf/components/esp_ringbuf /Users/cobain/esp/esp-idf/components/esp_rom /Users/cobain/esp/esp-idf/components/esp_security /Users/cobain/esp/esp-idf/components/esp_system /Users/cobain/esp/esp-idf/components/esp_timer /Users/cobain/esp/esp-idf/components/esp_vfs_console /Users/cobain/esp/esp-idf/components/esp_wifi /Users/cobain/esp/esp-idf/components/espcoredump /Users/cobain/robot-build/fw/managed_components/espressif2022__image_player /Users/cobain/robot-build/fw/managed_components/espressif__adc_battery_estimation /Users/cobain/robot-build/fw/managed_components/espressif__adc_mic /Users/cobain/robot-build/fw/managed_components/espressif__bmi270_sensor /Users/cobain/robot-build/fw/managed_components/espressif__button /Users/cobain/robot-build/fw/managed_components/espressif__cmake_utilities /Users/cobain/robot-build/fw/managed_components/espressif__dl_fft /Users/cobain/robot-build/fw/managed_components/espressif__esp-dsp /Users/cobain/robot-build/fw/managed_components/espressif__esp-sr /Users/cobain/robot-build/fw/managed_components/espressif__esp32-camera /Users/cobain/robot-build/fw/managed_components/espressif__esp_audio_codec /Users/cobain/robot-build/fw/managed_components/espressif__esp_audio_effects /Users/cobain/robot-build/fw/managed_components/espressif__esp_cam_sensor /Users/cobain/robot-build/fw/managed_components/espressif__esp_codec_dev /Users/cobain/robot-build/fw/managed_components/espressif__esp_image_effects /Users/cobain/robot-build/fw/managed_components/espressif__esp_io_expander /Users/cobain/robot-build/fw/managed_components/espressif__esp_io_expander_tca9554 /Users/cobain/robot-build/fw/managed_components/espressif__esp_io_expander_tca95xx_16bit /Users/cobain/robot-build/fw/managed_components/espressif__esp_jpeg /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_axs15231b /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_co5300 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_gc9a01 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_ili9341 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_panel_io_additions /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_spd2010 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_st7701 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_st77916 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_st7796 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_touch /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_touch_cst816s /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_touch_ft5x06 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_touch_gt1151 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_touch_gt911 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_touch_st7123 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lvgl_port /Users/cobain/robot-build/fw/managed_components/espressif__esp_mmap_assets /Users/cobain/robot-build/fw/managed_components/espressif__esp_new_jpeg /Users/cobain/robot-build/fw/managed_components/espressif__esp_sccb_intf /Users/cobain/robot-build/fw/managed_components/espressif__esp_video /Users/cobain/robot-build/fw/managed_components/espressif__i2c_bus /Users/cobain/robot-build/fw/managed_components/espressif__iot_eth /Users/cobain/robot-build/fw/managed_components/espressif__iot_usbh_cdc /Users/cobain/robot-build/fw/managed_components/espressif__iot_usbh_rndis /Users/cobain/robot-build/fw/managed_components/espressif__knob /Users/cobain/robot-build/fw/managed_components/espressif__led_strip /Users/cobain/robot-build/fw/managed_components/espressif__usb_host_uvc /Users/cobain/esp/esp-idf/components/esptool_py /Users/cobain/esp/esp-idf/components/fatfs /Users/cobain/esp/esp-idf/components/freertos /Users/cobain/esp/esp-idf/components/hal /Users/cobain/esp/esp-idf/components/heap /Users/cobain/esp/esp-idf/components/http_parser /Users/cobain/esp/esp-idf/components/idf_test /Users/cobain/esp/esp-idf/components/ieee802154 /Users/cobain/esp/esp-idf/components/json /Users/cobain/esp/esp-idf/components/log /Users/cobain/robot-build/fw/managed_components/lvgl__lvgl /Users/cobain/esp/esp-idf/components/lwip /Users/cobain/robot-build/fw/main /Users/cobain/esp/esp-idf/components/mbedtls /Users/cobain/robot-build/fw/components/mooncake /Users/cobain/robot-build/fw/components/mooncake_log /Users/cobain/esp/esp-idf/components/mqtt /Users/cobain/esp/esp-idf/components/newlib /Users/cobain/esp/esp-idf/components/nvs_flash /Users/cobain/esp/esp-idf/components/nvs_sec_provider /Users/cobain/esp/esp-idf/components/openthread /Users/cobain/esp/esp-idf/components/partition_table /Users/cobain/esp/esp-idf/components/perfmon /Users/cobain/esp/esp-idf/components/protobuf-c /Users/cobain/esp/esp-idf/components/protocomm /Users/cobain/esp/esp-idf/components/pthread /Users/cobain/esp/esp-idf/components/rt /Users/cobain/esp/esp-idf/components/sdmmc /Users/cobain/robot-build/fw/components/smooth_ui_toolkit /Users/cobain/esp/esp-idf/components/soc /Users/cobain/esp/esp-idf/components/spi_flash /Users/cobain/esp/esp-idf/components/spiffs /Users/cobain/esp/esp-idf/components/tcp_transport /Users/cobain/robot-build/fw/managed_components/tny-robotics__sh1106-esp-idf /Users/cobain/esp/esp-idf/components/touch_element /Users/cobain/robot-build/fw/managed_components/txp666__otto-emoji-gif-component /Users/cobain/esp/esp-idf/components/ulp /Users/cobain/esp/esp-idf/components/unity /Users/cobain/esp/esp-idf/components/usb /Users/cobain/esp/esp-idf/components/vfs /Users/cobain/robot-build/fw/managed_components/waveshare__custom_io_expander_ch32v003 /Users/cobain/robot-build/fw/managed_components/waveshare__esp_lcd_sh8601 /Users/cobain/robot-build/fw/managed_components/waveshare__esp_lcd_touch_cst9217 /Users/cobain/esp/esp-idf/components/wear_levelling /Users/cobain/esp/esp-idf/components/wifi_provisioning /Users/cobain/esp/esp-idf/components/wpa_supplicant /Users/cobain/robot-build/fw/managed_components/wvirgil123__sscma_client /Users/cobain/esp/esp-idf/components/xtensa
-- Configuring done (281.4s)
-- Generating done (3.0s)
-- Build files have been written to: /Users/cobain/robot-build/fw/build
Running ninja in directory /Users/cobain/robot-build/fw/build
Executing "ninja all"...
[0/2] Re-checking globbed directories...
[1/2509] Generating project_elf_src_esp32s3.c
[2/2509] Generating ../../ota_data_initial.bin
[3/2509] Generating en-US language config
Processing language: en-US
Input file path: /Users/cobain/robot-build/fw/main/../xiaozhi-esp32/main/assets/locales/en-US/language.json
Output file path: /Users/cobain/robot-build/fw/main/../xiaozhi-esp32/main/assets/lang_config.h
Loaded base language en-US with 52 strings
Language en-US string statistics:
  - Base language (en-US): 52 strings
  - User language: 52 strings
  - Total: 52 strings
Language en-US sound statistics:
  - Base language (en-US): 16 sounds
  - User language: 16 sounds
  - Common sounds: 5 sounds
Successfully generated language config file: /Users/cobain/robot-build/fw/main/../xiaozhi-esp32/main/assets/lang_config.h
[4/2509] Generating /Users/cobain/robot-build/fw/build/esp-idf/esp_system/ld/sections.ld.in linker script...
[5/2509] Generating /Users/cobain/robot-build/fw/build/esp-idf/esp_system/ld/memory.ld linker script...
[6/2509] Generating ../../partition_table/partition-table.bin
Partition table binary generated. Contents:
*******************************************************************************
# ESP-IDF Partition Table
# Name, Type, SubType, Offset, Size, Flags
nvs,data,nvs,0x9000,16K,
otadata,data,ota,0xd000,8K,
phy_init,data,phy,0xf000,4K,
ota_0,app,ota_0,0x20000,5056K,
ota_1,app,ota_1,0x510000,5056K,
assets,data,spiffs,0xa00000,6080K,
coredump,data,coredump,0xff0000,64K,
*******************************************************************************
[7/2509] Building C object esp-idf/esp_https_ota/CMakeFiles/__idf_esp_https_ota.dir/src/esp_https_ota.c.obj
[8/2509] Building C object esp-idf/esp_http_server/CMakeFiles/__idf_esp_http_server.dir/src/httpd_main.c.obj
[9/2509] Building C object esp-idf/esp_http_server/CMakeFiles/__idf_esp_http_server.dir/src/httpd_parse.c.obj
[10/2509] Building C object esp-idf/esp_http_server/CMakeFiles/__idf_esp_http_server.dir/src/httpd_sess.c.obj
[11/2509] Building C object esp-idf/esp_http_server/CMakeFiles/__idf_esp_http_server.dir/src/httpd_txrx.c.obj
[12/2509] Building C object esp-idf/esp_http_server/CMakeFiles/__idf_esp_http_server.dir/src/httpd_uri.c.obj
[13/2509] Building C object esp-idf/esp_http_server/CMakeFiles/__idf_esp_http_server.dir/src/util/ctrl_sock.c.obj
[14/2509] Building default assets.bin based on configuration
Building default assets...
  sdkconfig: /Users/cobain/robot-build/fw/sdkconfig
  builtin_text_font: font_puhui_basic_20_4
  emoji_collection: twemoji_64
  output: /Users/cobain/robot-build/fw/build/generated_assets.bin
  Note: Found wakenet models ['wn9_histackchan_tts3'] but wake word type is not ESP/AFE, skipping
  multinet models: mn7_cn, fst (will be packaged)
  custom wake word: ni hao mi bao (你好，米宝)
  wake word language: cn
  wake word threshold: 0.2
Starting to build assets...
Copied directory: /Users/cobain/robot-build/fw/managed_components/espressif__esp-sr/model/multinet_model/mn7_cn -> /Users/cobain/robot-build/fw/build/temp_build/srmodels/mn7_cn
Added multinet model: mn7_cn
Copied directory: /Users/cobain/robot-build/fw/managed_components/espressif__esp-sr/model/multinet_model/fst -> /Users/cobain/robot-build/fw/build/temp_build/srmodels/fst
Added multinet model: fst
Generated: /Users/cobain/robot-build/fw/build/temp_build/srmodels/srmodels.bin
Copied: /Users/cobain/robot-build/fw/build/temp_build/srmodels/srmodels.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/srmodels.bin
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/cbin/font_puhui_common_20_4.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/font_puhui_common_20_4.bin
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/happy.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/happy.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/delicious.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/delicious.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/neutral.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/neutral.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/crying.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/crying.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/silly.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/silly.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/sad.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/sad.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/surprised.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/surprised.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/confident.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/confident.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/winking.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/winking.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/relaxed.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/relaxed.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/embarrassed.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/embarrassed.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/confused.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/confused.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/loving.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/loving.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/kissy.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/kissy.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/sleepy.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/sleepy.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/angry.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/angry.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/thinking.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/thinking.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/funny.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/funny.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/cool.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/cool.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/shocked.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/shocked.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/laughing.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/laughing.png
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_wifi_high.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_wifi_high.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_iot_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_iot_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_wifi_slash.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_wifi_slash.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_wifi_medium.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_wifi_medium.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_look_close_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_look_close_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_setup.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_setup.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_look_open_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_look_open_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_wifi_low.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_wifi_low.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_personal_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_personal_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_controller.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_controller.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/app_center_bg.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/app_center_bg.png
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_work_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_work_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_indicator_left.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_indicator_left.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_bat_lightning.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_bat_lightning.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_app_center.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_app_center.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_ezdata.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_ezdata.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_meeting_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_meeting_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_sentinel.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_sentinel.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_bell.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_bell.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_chat_60.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_chat_60.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_indicator_right.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_indicator_right.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_dance.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_dance.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_meeting.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_meeting.png
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/setup_stackchan_front_view.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/setup_stackchan_front_view.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/meeting_bg.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/meeting_bg.png
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_ai_agent.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_ai_agent.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_home.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_home.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_iot_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_iot_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_look_close_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_look_close_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_look_open_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_look_open_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_personal_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_personal_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_work_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_work_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_meeting_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_meeting_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_chat_60.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_chat_60.bin
Processed 34 extra files from: /Users/cobain/robot-build/fw/main/assets/assets_bin
Generated: /Users/cobain/robot-build/fw/build/temp_build/assets/index.json
Generated: /Users/cobain/robot-build/fw/build/temp_build/config.json
All files have been merged into assets.bin
Successfully generated assets.bin: /Users/cobain/robot-build/fw/build/generated_assets.bin
Assets file size: 5295.11K (5422195 bytes)
Build completed successfully!
[15/2509] Linking C static library esp-idf/esp_https_ota/libesp_https_ota.a
[16/2509] Building C object esp-idf/esp_http_client/CMakeFiles/__idf_esp_http_client.dir/lib/http_utils.c.obj
[17/2509] Building C object esp-idf/esp_http_client/CMakeFiles/__idf_esp_http_client.dir/lib/http_auth.c.obj
[18/2509] Building C object esp-idf/tcp_transport/CMakeFiles/__idf_tcp_transport.dir/transport_internal.c.obj
[19/2509] Building C object esp-idf/esp_adc/CMakeFiles/__idf_esp_adc.dir/adc_oneshot.c.obj
[20/2509] Building C object esp-idf/tcp_transport/CMakeFiles/__idf_tcp_transport.dir/transport_ws.c.obj
[21/2509] Building C object esp-idf/esp_adc/CMakeFiles/__idf_esp_adc.dir/adc_common.c.obj
[22/2509] Building C object esp-idf/tcp_transport/CMakeFiles/__idf_tcp_transport.dir/transport_ssl.c.obj
[23/2509] Building C object esp-idf/esp_adc/CMakeFiles/__idf_esp_adc.dir/deprecated/esp_adc_cal_common_legacy.c.obj
[24/2509] Building C object esp-idf/esp_adc/CMakeFiles/__idf_esp_adc.dir/adc_cali.c.obj
[25/2509] Building C object esp-idf/esp_http_client/CMakeFiles/__idf_esp_http_client.dir/esp_http_client.c.obj
[26/2509] Building C object esp-idf/esp_adc/CMakeFiles/__idf_esp_adc.dir/esp32s3/curve_fitting_coefficients.c.obj
[27/2509] Building C object esp-idf/esp_adc/CMakeFiles/__idf_esp_adc.dir/gdma/adc_dma.c.obj
[28/2509] Building C object esp-idf/esp_adc/CMakeFiles/__idf_esp_adc.dir/adc_filter.c.obj
[29/2509] Building C object esp-idf/esp_adc/CMakeFiles/__idf_esp_adc.dir/adc_continuous.c.obj
[30/2509] Building C object esp-idf/esp_adc/CMakeFiles/__idf_esp_adc.dir/deprecated/esp32s3/esp_adc_cal_legacy.c.obj
[31/2509] Building C object esp-idf/esp-tls/CMakeFiles/__idf_esp-tls.dir/esp-tls-crypto/esp_tls_crypto.c.obj
[32/2509] Building C object esp-idf/esp-tls/CMakeFiles/__idf_esp-tls.dir/esp_tls_platform_port.c.obj
[33/2509] Building C object esp-idf/esp-tls/CMakeFiles/__idf_esp-tls.dir/esp_tls_error_capture.c.obj
[34/2509] Building C object esp-idf/esp_http_client/CMakeFiles/__idf_esp_http_client.dir/lib/http_header.c.obj
[35/2509] Building C object esp-idf/esp_adc/CMakeFiles/__idf_esp_adc.dir/adc_monitor.c.obj
[36/2509] Building C object esp-idf/esp-tls/CMakeFiles/__idf_esp-tls.dir/esp_tls.c.obj
[37/2509] Building C object esp-idf/tcp_transport/CMakeFiles/__idf_tcp_transport.dir/transport_socks_proxy.c.obj
[38/2509] Building C object esp-idf/esp-tls/CMakeFiles/__idf_esp-tls.dir/esp_tls_mbedtls.c.obj
[39/2509] Building C object esp-idf/tcp_transport/CMakeFiles/__idf_tcp_transport.dir/transport.c.obj
[40/2509] Building C object esp-idf/esp_http_server/CMakeFiles/__idf_esp_http_server.dir/src/httpd_ws.c.obj
[41/2509] Linking C static library esp-idf/esp_http_server/libesp_http_server.a
[42/2509] Building C object esp-idf/http_parser/CMakeFiles/__idf_http_parser.dir/http_parser.c.obj
[43/2509] Building C object esp-idf/driver/CMakeFiles/__idf_driver.dir/deprecated/i2s_legacy.c.obj
[44/2509] Building C object esp-idf/driver/CMakeFiles/__idf_driver.dir/deprecated/pcnt_legacy.c.obj
[45/2509] Building C object esp-idf/driver/CMakeFiles/__idf_driver.dir/deprecated/rtc_temperature_legacy.c.obj
[46/2509] Building C object esp-idf/driver/CMakeFiles/__idf_driver.dir/deprecated/sigma_delta_legacy.c.obj
[47/2509] Building C object esp-idf/driver/CMakeFiles/__idf_driver.dir/touch_sensor/touch_sensor_common.c.obj
[48/2509] Building C object esp-idf/driver/CMakeFiles/__idf_driver.dir/deprecated/adc_legacy.c.obj
[49/2509] Building C object esp-idf/driver/CMakeFiles/__idf_driver.dir/deprecated/rmt_legacy.c.obj
[50/2509] Building C object esp-idf/esp_driver_twai/CMakeFiles/__idf_esp_driver_twai.dir/esp_twai.c.obj
[51/2509] Building C object esp-idf/driver/CMakeFiles/__idf_driver.dir/deprecated/adc_dma_legacy.c.obj
[52/2509] Building C object esp-idf/driver/CMakeFiles/__idf_driver.dir/twai/twai.c.obj
[53/2509] Building C object esp-idf/driver/CMakeFiles/__idf_driver.dir/deprecated/mcpwm_legacy.c.obj
[54/2509] Building C object esp-idf/esp_driver_ledc/CMakeFiles/__idf_esp_driver_ledc.dir/src/ledc.c.obj
[55/2509] Building C object esp-idf/esp_driver_i2c/CMakeFiles/__idf_esp_driver_i2c.dir/i2c_master.c.obj
[56/2509] Building C object esp-idf/esp_driver_i2c/CMakeFiles/__idf_esp_driver_i2c.dir/i2c_slave.c.obj
[57/2509] Building C object esp-idf/esp_driver_tsens/CMakeFiles/__idf_esp_driver_tsens.dir/src/temperature_sensor.c.obj
[58/2509] Building C object esp-idf/esp_driver_sdm/CMakeFiles/__idf_esp_driver_sdm.dir/src/sdm.c.obj
[59/2509] Building C object esp-idf/esp_driver_twai/CMakeFiles/__idf_esp_driver_twai.dir/esp_twai_onchip.c.obj
[60/2509] Building C object esp-idf/esp_driver_rmt/CMakeFiles/__idf_esp_driver_rmt.dir/src/rmt_encoder_bytes.c.obj
[61/2509] Building C object esp-idf/esp_driver_rmt/CMakeFiles/__idf_esp_driver_rmt.dir/src/rmt_encoder_copy.c.obj
[62/2509] Building C object esp-idf/esp_driver_rmt/CMakeFiles/__idf_esp_driver_rmt.dir/src/rmt_encoder.c.obj
[63/2509] Building C object esp-idf/esp_driver_rmt/CMakeFiles/__idf_esp_driver_rmt.dir/src/rmt_rx.c.obj
[64/2509] Building C object esp-idf/esp_adc/CMakeFiles/__idf_esp_adc.dir/adc_cali_curve_fitting.c.obj
[65/2509] Building C object esp-idf/esp_driver_sdspi/CMakeFiles/__idf_esp_driver_sdspi.dir/src/sdspi_crc.c.obj
[66/2509] Building C object esp-idf/esp_driver_i2c/CMakeFiles/__idf_esp_driver_i2c.dir/i2c_common.c.obj
[67/2509] Building C object esp-idf/esp_driver_rmt/CMakeFiles/__idf_esp_driver_rmt.dir/src/rmt_encoder_simple.c.obj
[68/2509] Linking C static library esp-idf/esp_http_client/libesp_http_client.a
[69/2509] Building C object esp-idf/esp_driver_sdspi/CMakeFiles/__idf_esp_driver_sdspi.dir/src/sdspi_transaction.c.obj
[70/2509] Building C object esp-idf/esp_driver_sdspi/CMakeFiles/__idf_esp_driver_sdspi.dir/src/sdspi_host.c.obj
[71/2509] Building C object esp-idf/driver/CMakeFiles/__idf_driver.dir/deprecated/timer_legacy.c.obj
[72/2509] Building C object esp-idf/esp_driver_rmt/CMakeFiles/__idf_esp_driver_rmt.dir/src/rmt_common.c.obj
[73/2509] Linking C static library esp-idf/tcp_transport/libtcp_transport.a
[74/2509] Building C object esp-idf/driver/CMakeFiles/__idf_driver.dir/touch_sensor/esp32s3/touch_sensor.c.obj
[75/2509] Building C object esp-idf/sdmmc/CMakeFiles/__idf_sdmmc.dir/sdmmc_init.c.obj
[76/2509] Building C object esp-idf/esp_driver_rmt/CMakeFiles/__idf_esp_driver_rmt.dir/src/rmt_tx.c.obj
[77/2509] Linking C static library esp-idf/esp_adc/libesp_adc.a
[78/2509] Building C object esp-idf/sdmmc/CMakeFiles/__idf_sdmmc.dir/sdmmc_common.c.obj
[79/2509] Building C object esp-idf/sdmmc/CMakeFiles/__idf_sdmmc.dir/sdmmc_io.c.obj
[80/2509] Building C object esp-idf/sdmmc/CMakeFiles/__idf_sdmmc.dir/sdmmc_cmd.c.obj
[81/2509] Building C object esp-idf/sdmmc/CMakeFiles/__idf_sdmmc.dir/sd_pwr_ctrl/sd_pwr_ctrl.c.obj
[82/2509] Linking C static library esp-idf/esp-tls/libesp-tls.a
[83/2509] Building C object esp-idf/sdmmc/CMakeFiles/__idf_sdmmc.dir/sdmmc_mmc.c.obj
[84/2509] Linking C static library esp-idf/http_parser/libhttp_parser.a
[85/2509] Building C object esp-idf/esp_driver_sdmmc/CMakeFiles/__idf_esp_driver_sdmmc.dir/src/sdmmc_host.c.obj
[86/2509] Building C object esp-idf/sdmmc/CMakeFiles/__idf_sdmmc.dir/sdmmc_sd.c.obj
[87/2509] Building C object esp-idf/esp_driver_i2s/CMakeFiles/__idf_esp_driver_i2s.dir/i2s_std.c.obj
[88/2509] Building C object esp-idf/esp_driver_i2s/CMakeFiles/__idf_esp_driver_i2s.dir/i2s_platform.c.obj
[89/2509] Building C object esp-idf/esp_driver_i2s/CMakeFiles/__idf_esp_driver_i2s.dir/i2s_tdm.c.obj
[90/2509] Building C object esp-idf/esp_driver_i2s/CMakeFiles/__idf_esp_driver_i2s.dir/i2s_pdm.c.obj
[91/2509] Building C object esp-idf/esp_driver_sdmmc/CMakeFiles/__idf_esp_driver_sdmmc.dir/src/sdmmc_transaction.c.obj
[92/2509] Building C object esp-idf/esp_driver_i2s/CMakeFiles/__idf_esp_driver_i2s.dir/i2s_common.c.obj
[93/2509] Building C object esp-idf/esp_driver_mcpwm/CMakeFiles/__idf_esp_driver_mcpwm.dir/src/mcpwm_oper.c.obj
[94/2509] Building C object esp-idf/esp_driver_mcpwm/CMakeFiles/__idf_esp_driver_mcpwm.dir/src/mcpwm_sync.c.obj
[95/2509] Building C object esp-idf/esp_driver_mcpwm/CMakeFiles/__idf_esp_driver_mcpwm.dir/src/mcpwm_fault.c.obj
[96/2509] Building C object esp-idf/driver/CMakeFiles/__idf_driver.dir/i2c/i2c.c.obj
[97/2509] Building C object esp-idf/esp_driver_mcpwm/CMakeFiles/__idf_esp_driver_mcpwm.dir/src/mcpwm_cap.c.obj
[98/2509] Building C object esp-idf/esp_driver_mcpwm/CMakeFiles/__idf_esp_driver_mcpwm.dir/src/mcpwm_timer.c.obj
[99/2509] Building C object esp-idf/esp_gdbstub/CMakeFiles/__idf_esp_gdbstub.dir/src/gdbstub_transport.c.obj
[100/2509] Building ASM object esp-idf/esp_gdbstub/CMakeFiles/__idf_esp_gdbstub.dir/src/port/xtensa/xt_debugexception.S.obj
[101/2509] Building C object esp-idf/esp_gdbstub/CMakeFiles/__idf_esp_gdbstub.dir/src/gdbstub.c.obj
[102/2509] Building C object esp-idf/esp_driver_mcpwm/CMakeFiles/__idf_esp_driver_mcpwm.dir/src/mcpwm_cmpr.c.obj
[103/2509] Building ASM object esp-idf/esp_gdbstub/CMakeFiles/__idf_esp_gdbstub.dir/src/port/xtensa/gdbstub-entry.S.obj
[104/2509] Building C object esp-idf/esp_driver_pcnt/CMakeFiles/__idf_esp_driver_pcnt.dir/src/pulse_cnt.c.obj
[105/2509] Building C object esp-idf/esp_gdbstub/CMakeFiles/__idf_esp_gdbstub.dir/src/packet.c.obj
[106/2509] Building C object esp-idf/esp_driver_mcpwm/CMakeFiles/__idf_esp_driver_mcpwm.dir/src/mcpwm_com.c.obj
[107/2509] Building C object esp-idf/esp_gdbstub/CMakeFiles/__idf_esp_gdbstub.dir/src/port/xtensa/gdbstub_xtensa.c.obj
[108/2509] Building C object esp-idf/esp_wifi/CMakeFiles/__idf_esp_wifi.dir/src/mesh_event.c.obj
[109/2509] Building C object esp-idf/esp_wifi/CMakeFiles/__idf_esp_wifi.dir/src/lib_printf.c.obj
[110/2509] Building C object esp-idf/esp_wifi/CMakeFiles/__idf_esp_wifi.dir/src/smartconfig.c.obj
[111/2509] Linking C static library esp-idf/driver/libdriver.a
[112/2509] Linking C static library esp-idf/esp_driver_twai/libesp_driver_twai.a
[113/2509] Building C object esp-idf/esp_wifi/CMakeFiles/__idf_esp_wifi.dir/src/wifi_default_ap.c.obj
[114/2509] Linking C static library esp-idf/esp_driver_ledc/libesp_driver_ledc.a
[115/2509] Building C object esp-idf/esp_wifi/CMakeFiles/__idf_esp_wifi.dir/src/wifi_init.c.obj
[116/2509] Building C object esp-idf/esp_wifi/CMakeFiles/__idf_esp_wifi.dir/src/wifi_netif.c.obj
[117/2509] Building C object esp-idf/esp_wifi/CMakeFiles/__idf_esp_wifi.dir/src/wifi_default.c.obj
[118/2509] Building C object esp-idf/esp_driver_spi/CMakeFiles/__idf_esp_driver_spi.dir/src/gpspi/spi_slave.c.obj
[119/2509] Building C object esp-idf/esp_driver_spi/CMakeFiles/__idf_esp_driver_spi.dir/src/gpspi/spi_common.c.obj
[120/2509] Linking C static library esp-idf/esp_driver_i2c/libesp_driver_i2c.a
[121/2509] Building C object esp-idf/esp_driver_spi/CMakeFiles/__idf_esp_driver_spi.dir/src/gpspi/spi_slave_hd.c.obj
[122/2509] Building C object esp-idf/esp_coex/CMakeFiles/__idf_esp_coex.dir/src/coexist.c.obj
[123/2509] Building C object esp-idf/esp_driver_mcpwm/CMakeFiles/__idf_esp_driver_mcpwm.dir/src/mcpwm_gen.c.obj
[124/2509] Building C object esp-idf/esp_coex/CMakeFiles/__idf_esp_coex.dir/src/coexist_debug_diagram.c.obj
[125/2509] Linking C static library esp-idf/esp_driver_sdm/libesp_driver_sdm.a
[126/2509] Building C object esp-idf/esp_coex/CMakeFiles/__idf_esp_coex.dir/src/coexist_debug.c.obj
[127/2509] Linking C static library esp-idf/esp_driver_tsens/libesp_driver_tsens.a
[128/2509] Building C object esp-idf/esp_coex/CMakeFiles/__idf_esp_coex.dir/src/lib_printf.c.obj
[129/2509] Building C object esp-idf/esp_wifi/CMakeFiles/__idf_esp_wifi.dir/src/smartconfig_ack.c.obj
[130/2509] Linking C static library esp-idf/esp_driver_rmt/libesp_driver_rmt.a
[131/2509] Linking C static library esp-idf/esp_driver_sdspi/libesp_driver_sdspi.a
[132/2509] Building C object esp-idf/esp_wifi/CMakeFiles/__idf_esp_wifi.dir/regulatory/esp_wifi_regulatory.c.obj
[133/2509] Building C object esp-idf/esp_coex/CMakeFiles/__idf_esp_coex.dir/esp32s3/esp_coex_adapter.c.obj
[134/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/port/os_xtensa.c.obj
[135/2509] Linking C static library esp-idf/esp_driver_sdmmc/libesp_driver_sdmmc.a
[136/2509] Linking C static library esp-idf/sdmmc/libsdmmc.a
[137/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/port/eloop.c.obj
[138/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/ap/ap_config.c.obj
[139/2509] Linking C static library esp-idf/esp_driver_i2s/libesp_driver_i2s.a
[140/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/ap/ieee802_1x.c.obj
[141/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/ap/sta_info.c.obj
[142/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/ap/wpa_auth_ie.c.obj
[143/2509] Building C object esp-idf/esp_driver_spi/CMakeFiles/__idf_esp_driver_spi.dir/src/gpspi/spi_master.c.obj
[144/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/ap/comeback_token.c.obj
[145/2509] Building C object esp-idf/esp_wifi/CMakeFiles/__idf_esp_wifi.dir/esp32s3/esp_adapter.c.obj
[146/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/ap/pmksa_cache_auth.c.obj
[147/2509] Linking C static library esp-idf/esp_driver_mcpwm/libesp_driver_mcpwm.a
[148/2509] Linking C static library esp-idf/esp_driver_pcnt/libesp_driver_pcnt.a
[149/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/ap/wpa_auth.c.obj
[150/2509] Linking C static library esp-idf/esp_gdbstub/libesp_gdbstub.a
[151/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/ap/ieee802_11.c.obj
[152/2509] Linking C static library esp-idf/esp_driver_spi/libesp_driver_spi.a
[153/2509] Linking C static library esp-idf/esp_wifi/libesp_wifi.a
[154/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/dh_group5.c.obj
[155/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/crypto_ops.c.obj
[156/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/utils/bitfield.c.obj
[157/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/dh_groups.c.obj
[158/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/ms_funcs.c.obj
[159/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/aes-gcm.c.obj
[160/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/sha256-tlsprf.c.obj
[161/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/sha256-kdf.c.obj
[162/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/sha1-tlsprf.c.obj
[163/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/sha1-prf.c.obj
[164/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/ccmp.c.obj
[165/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/sha384-tlsprf.c.obj
[166/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/common/dragonfly.c.obj
[167/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/eap_common/eap_wsc_common.c.obj
[168/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/sha256-prf.c.obj
[169/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/sha384-prf.c.obj
[170/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/aes-siv.c.obj
[171/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/sha1-tprf.c.obj
[172/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/common/wpa_common.c.obj
[173/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/md4-internal.c.obj
[174/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/eap_peer/chap.c.obj
[175/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/common/ieee802_11_common.c.obj
[176/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/common/sae.c.obj
[177/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/eap_peer/eap_common.c.obj
[178/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/eap_peer/eap_peap_common.c.obj
[179/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/eap_peer/eap_tls.c.obj
[180/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/eap_peer/mschapv2.c.obj
[181/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/eap_peer/eap_fast_common.c.obj
[182/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/eap_peer/eap_mschapv2.c.obj
[183/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/eap_peer/eap_tls_common.c.obj
[184/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/eap_peer/eap.c.obj
[185/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/utils/base64.c.obj
[186/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/rsn_supp/pmksa_cache.c.obj
[187/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/eap_peer/eap_fast_pac.c.obj
[188/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/rsn_supp/wpa_ie.c.obj
[189/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/utils/ext_password.c.obj
[190/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/utils/uuid.c.obj
[191/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/eap_peer/eap_peap.c.obj
[192/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/eap_peer/eap_ttls.c.obj
[193/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/utils/wpa_debug.c.obj
[194/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/utils/common.c.obj
[195/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/utils/wpabuf.c.obj
[196/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/wps/wps_attr_process.c.obj
[197/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/wps/wps_attr_build.c.obj
[198/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/wps/wps_attr_parse.c.obj
[199/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/wps/wps.c.obj
[200/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/wps/wps_dev_attr.c.obj
[201/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/utils/json.c.obj
[202/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/esp_supplicant/src/esp_wpa2_api_port.c.obj
[203/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/wps/wps_common.c.obj
[204/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/esp_supplicant/src/esp_wpas_glue.c.obj
[205/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/eap_peer/eap_fast.c.obj
[206/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/esp_supplicant/src/esp_wpa_main.c.obj
[207/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/esp_supplicant/src/esp_common.c.obj
[208/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/common/sae_pk.c.obj
[209/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/esp_supplicant/src/esp_owe.c.obj
[210/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/wps/wps_enrollee.c.obj
[211/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/rsn_supp/wpa.c.obj
[212/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/tls/pkcs1.c.obj
[213/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/esp_supplicant/src/esp_wpa3.c.obj
[214/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/tls/pkcs8.c.obj
[215/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/esp_supplicant/src/esp_eap_client.c.obj
[216/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/esp_supplicant/src/esp_hostap.c.obj
[217/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/tls/rsa.c.obj
[218/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/tls/asn1.c.obj
[219/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/tls/pkcs5.c.obj
[220/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/tls_internal.c.obj
[221/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/tls/tlsv1_common.c.obj
[222/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/tls/tlsv1_cred.c.obj
[223/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/tls/tlsv1_record.c.obj
[224/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/esp_supplicant/src/esp_wps.c.obj
[225/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/tls/tlsv1_client.c.obj
[226/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/tls/tlsv1_client_write.c.obj
[227/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/tls/tlsv1_client_read.c.obj
[228/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/esp_supplicant/src/crypto/fastpsk.c.obj
[229/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/rc4.c.obj
[230/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/esp_supplicant/src/crypto/crypto_mbedtls-bignum.c.obj
[231/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/tls/bignum.c.obj
[232/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/tls/tlsv1_client_ocsp.c.obj
[233/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/esp_supplicant/src/crypto/crypto_mbedtls-rsa.c.obj
[234/2509] Linking C static library esp-idf/esp_coex/libesp_coex.a
[235/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/esp_supplicant/src/crypto/crypto_mbedtls.c.obj
[236/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/aes-wrap.c.obj
[237/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/aes-unwrap.c.obj
[238/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/esp_supplicant/src/crypto/fastpbkdf2.c.obj
[239/2509] Building C object esp-idf/esp_netif/CMakeFiles/__idf_esp_netif.dir/esp_netif_defaults.c.obj
[240/2509] Building C object esp-idf/esp_netif/CMakeFiles/__idf_esp_netif.dir/esp_netif_handlers.c.obj
[241/2509] Building C object esp-idf/esp_netif/CMakeFiles/__idf_esp_netif.dir/esp_netif_objects.c.obj
[242/2509] Building C object esp-idf/esp_netif/CMakeFiles/__idf_esp_netif.dir/lwip/esp_netif_lwip_defaults.c.obj
[243/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/des-internal.c.obj
[244/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/crypto/aes-ccm.c.obj
[245/2509] Building C object esp-idf/esp_netif/CMakeFiles/__idf_esp_netif.dir/lwip/netif/esp_pbuf_ref.c.obj
[246/2509] Building C object esp-idf/esp_netif/CMakeFiles/__idf_esp_netif.dir/lwip/netif/ethernetif.c.obj
[247/2509] Building C object esp-idf/esp_netif/CMakeFiles/__idf_esp_netif.dir/lwip/netif/wlanif.c.obj
[248/2509] Building C object esp-idf/esp_netif/CMakeFiles/__idf_esp_netif.dir/lwip/esp_netif_sntp.c.obj
[249/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/api/err.c.obj
[250/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/esp_supplicant/src/crypto/crypto_mbedtls-ec.c.obj
[251/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/api/if_api.c.obj
[252/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/apps/sntp/sntp.c.obj
[253/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/api/netbuf.c.obj
[254/2509] Building C object esp-idf/wpa_supplicant/CMakeFiles/__idf_wpa_supplicant.dir/src/tls/x509v3.c.obj
[255/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/api/netifapi.c.obj
[256/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/apps/netbiosns/netbiosns.c.obj
[257/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/api/tcpip.c.obj
[258/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/api/netdb.c.obj
[259/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/api/api_lib.c.obj
[260/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/apps/sntp/sntp.c.obj
[261/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/def.c.obj
[262/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/init.c.obj
[263/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/ip.c.obj
[264/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/mem.c.obj
[265/2509] Linking C static library esp-idf/wpa_supplicant/libwpa_supplicant.a
[266/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/inet_chksum.c.obj
[267/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/stats.c.obj
[268/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/sys.c.obj
[269/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/memp.c.obj
[270/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/api/api_msg.c.obj
[271/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/raw.c.obj
[272/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/dns.c.obj
[273/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/pbuf.c.obj
[274/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/netif.c.obj
[275/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/ipv4/autoip.c.obj
[276/2509] Building C object esp-idf/esp_netif/CMakeFiles/__idf_esp_netif.dir/lwip/esp_netif_lwip.c.obj
[277/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/timeouts.c.obj
[278/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/ipv4/icmp.c.obj
[279/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/ipv4/ip4_napt.c.obj
[280/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/tcp_out.c.obj
[281/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/ipv4/igmp.c.obj
[282/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/ipv4/ip4.c.obj
[283/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/ipv6/dhcp6.c.obj
[284/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/ipv4/etharp.c.obj
[285/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/ipv4/ip4_frag.c.obj
[286/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/udp.c.obj
[287/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/ipv6/ethip6.c.obj
[288/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/ipv6/inet6.c.obj
[289/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/ipv6/icmp6.c.obj
[290/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/ipv4/ip4_addr.c.obj
[291/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/tcp.c.obj
[292/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/tcp_in.c.obj
[293/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/bridgeif.c.obj
[294/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/ipv6/ip6_frag.c.obj
[295/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ethernet.c.obj
[296/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/ipv6/ip6_addr.c.obj
[297/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/api/sockets.c.obj
[298/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/ipv4/dhcp.c.obj
[299/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/ipv6/mld6.c.obj
[300/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/auth.c.obj
[301/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/chap-md5.c.obj
[302/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/ccp.c.obj
[303/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/demand.c.obj
[304/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/slipif.c.obj
[305/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/chap-new.c.obj
[306/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/bridgeif_fdb.c.obj
[307/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/eui64.c.obj
[308/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/chap_ms.c.obj
[309/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/eap.c.obj
[310/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/ipcp.c.obj
[311/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/ipv6cp.c.obj
[312/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/ecp.c.obj
[313/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/fsm.c.obj
[314/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/magic.c.obj
[315/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/lcp.c.obj
[316/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/mppe.c.obj
[317/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/pppapi.c.obj
[318/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/pppcrypt.c.obj
[319/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/multilink.c.obj
[320/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/pppol2tp.c.obj
[321/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/pppoe.c.obj
[322/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/ppp.c.obj
[323/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/upap.c.obj
[324/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/pppos.c.obj
[325/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/ipv6/ip6.c.obj
[326/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/utils.c.obj
[327/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/vj.c.obj
[328/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/port/hooks/tcp_isn_default.c.obj
[329/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/port/sockets_ext.c.obj
[330/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/port/if_index.c.obj
[331/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/port/hooks/lwip_default_hooks.c.obj
[332/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/port/esp32xx/vfs_lwip.c.obj
[333/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/port/acd_dhcp_check.c.obj
[334/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/apps/ping/ping.c.obj
[335/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/port/freertos/sys_arch.c.obj
[336/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/port/debug/lwip_debug.c.obj
[337/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/apps/ping/esp_ping.c.obj
[338/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/polarssl/arc4.c.obj
[339/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/polarssl/des.c.obj
[340/2509] Linking C static library esp-idf/esp_netif/libesp_netif.a
[341/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/polarssl/md4.c.obj
[342/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/polarssl/md5.c.obj
[343/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/netif/ppp/polarssl/sha1.c.obj
[344/2509] Building C object esp-idf/esp_vfs_console/CMakeFiles/__idf_esp_vfs_console.dir/vfs_console.c.obj
[345/2509] Building C object esp-idf/esp_driver_usb_serial_jtag/CMakeFiles/__idf_esp_driver_usb_serial_jtag.dir/src/usb_serial_jtag_connection_monitor.c.obj
[346/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/apps/ping/ping_sock.c.obj
[347/2509] Building C object esp-idf/vfs/CMakeFiles/__idf_vfs.dir/nullfs.c.obj
[348/2509] Building C object esp-idf/esp_phy/CMakeFiles/__idf_esp_phy.dir/src/phy_override.c.obj
[349/2509] Building C object esp-idf/esp_phy/CMakeFiles/__idf_esp_phy.dir/src/lib_printf.c.obj
[350/2509] Building C object esp-idf/esp_phy/CMakeFiles/__idf_esp_phy.dir/esp32s3/phy_init_data.c.obj
[351/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/lwip/src/core/ipv6/nd6.c.obj
[352/2509] Building C object esp-idf/vfs/CMakeFiles/__idf_vfs.dir/vfs_eventfd.c.obj
[353/2509] Building C object esp-idf/esp_phy/CMakeFiles/__idf_esp_phy.dir/src/btbb_init.c.obj
[354/2509] Building C object esp-idf/esp_phy/CMakeFiles/__idf_esp_phy.dir/src/phy_common.c.obj
[355/2509] Building C object esp-idf/esp_driver_usb_serial_jtag/CMakeFiles/__idf_esp_driver_usb_serial_jtag.dir/src/usb_serial_jtag.c.obj
[356/2509] Building C object esp-idf/vfs/CMakeFiles/__idf_vfs.dir/vfs_semihost.c.obj
[357/2509] Building C object esp-idf/esp_phy/CMakeFiles/__idf_esp_phy.dir/src/phy_init.c.obj
[358/2509] Building C object esp-idf/esp_driver_usb_serial_jtag/CMakeFiles/__idf_esp_driver_usb_serial_jtag.dir/src/usb_serial_jtag_vfs.c.obj
[359/2509] Building C object esp-idf/lwip/CMakeFiles/__idf_lwip.dir/apps/dhcpserver/dhcpserver.c.obj
[360/2509] Linking C static library esp-idf/lwip/liblwip.a
[361/2509] Building C object esp-idf/vfs/CMakeFiles/__idf_vfs.dir/vfs.c.obj
[362/2509] Linking C static library esp-idf/vfs/libvfs.a
[363/2509] Linking C static library esp-idf/esp_vfs_console/libesp_vfs_console.a
[364/2509] Linking C static library esp-idf/esp_driver_usb_serial_jtag/libesp_driver_usb_serial_jtag.a
[365/2509] Building CXX object esp-idf/nvs_flash/CMakeFiles/__idf_nvs_flash.dir/src/nvs_partition.cpp.obj
[366/2509] Building CXX object esp-idf/nvs_flash/CMakeFiles/__idf_nvs_flash.dir/src/nvs_partition_lookup.cpp.obj
[367/2509] Building CXX object esp-idf/nvs_flash/CMakeFiles/__idf_nvs_flash.dir/src/nvs_item_hash_list.cpp.obj
[368/2509] Building CXX object esp-idf/nvs_flash/CMakeFiles/__idf_nvs_flash.dir/src/nvs_platform.cpp.obj
[369/2509] Building CXX object esp-idf/nvs_flash/CMakeFiles/__idf_nvs_flash.dir/src/nvs_cxx_api.cpp.obj
[370/2509] Building CXX object esp-idf/nvs_flash/CMakeFiles/__idf_nvs_flash.dir/src/nvs_handle_locked.cpp.obj
[371/2509] Building C object esp-idf/nvs_flash/CMakeFiles/__idf_nvs_flash.dir/src/nvs_bootloader_aes.c.obj
[372/2509] Building CXX object esp-idf/nvs_flash/CMakeFiles/__idf_nvs_flash.dir/src/nvs_pagemanager.cpp.obj
[373/2509] Linking C static library esp-idf/esp_phy/libesp_phy.a
[374/2509] Building CXX object esp-idf/nvs_flash/CMakeFiles/__idf_nvs_flash.dir/src/nvs_page.cpp.obj
[375/2509] Building C object esp-idf/nvs_flash/CMakeFiles/__idf_nvs_flash.dir/src/nvs_bootloader.c.obj
[376/2509] Building C object esp-idf/esp_event/CMakeFiles/__idf_esp_event.dir/default_event_loop.c.obj
[377/2509] Building CXX object esp-idf/nvs_flash/CMakeFiles/__idf_nvs_flash.dir/src/nvs_handle_simple.cpp.obj
[378/2509] Building C object esp-idf/nvs_flash/CMakeFiles/__idf_nvs_flash.dir/src/nvs_bootloader_xts_aes.c.obj
[379/2509] Building C object esp-idf/esp_event/CMakeFiles/__idf_esp_event.dir/esp_event_private.c.obj
[380/2509] Building C object esp-idf/esp_driver_uart/CMakeFiles/__idf_esp_driver_uart.dir/src/uart_wakeup.c.obj
[381/2509] Building C object esp-idf/esp_psram/CMakeFiles/__idf_esp_psram.dir/system_layer/esp_psram_mspi.c.obj
[382/2509] Building CXX object esp-idf/nvs_flash/CMakeFiles/__idf_nvs_flash.dir/src/nvs_api.cpp.obj
[383/2509] Building C object esp-idf/esp_event/CMakeFiles/__idf_esp_event.dir/esp_event.c.obj
[384/2509] Building C object esp-idf/esp_psram/CMakeFiles/__idf_esp_psram.dir/system_layer/esp_psram.c.obj
[385/2509] Building C object esp-idf/esp_psram/CMakeFiles/__idf_esp_psram.dir/xip_impl/mmu_psram_flash.c.obj
[386/2509] Building C object esp-idf/esp_psram/CMakeFiles/__idf_esp_psram.dir/device/esp_psram_impl_ap_quad.c.obj
[387/2509] Building CXX object esp-idf/nvs_flash/CMakeFiles/__idf_nvs_flash.dir/src/nvs_storage.cpp.obj
[388/2509] Building C object esp-idf/esp_driver_uart/CMakeFiles/__idf_esp_driver_uart.dir/src/uhci.c.obj
[389/2509] Building C object esp-idf/esp_timer/CMakeFiles/__idf_esp_timer.dir/src/esp_timer_init.c.obj
[390/2509] Building C object esp-idf/esp_timer/CMakeFiles/__idf_esp_timer.dir/src/ets_timer_legacy.c.obj
[391/2509] Building C object esp-idf/esp_driver_uart/CMakeFiles/__idf_esp_driver_uart.dir/src/uart_vfs.c.obj
[392/2509] Building C object esp-idf/esp_driver_gptimer/CMakeFiles/__idf_esp_driver_gptimer.dir/src/gptimer_common.c.obj
[393/2509] Building C object esp-idf/esp_timer/CMakeFiles/__idf_esp_timer.dir/src/system_time.c.obj
[394/2509] Building C object esp-idf/esp_timer/CMakeFiles/__idf_esp_timer.dir/src/esp_timer_impl_common.c.obj
[395/2509] Building C object esp-idf/esp_timer/CMakeFiles/__idf_esp_timer.dir/src/esp_timer.c.obj
[396/2509] Building C object esp-idf/esp_timer/CMakeFiles/__idf_esp_timer.dir/src/esp_timer_impl_systimer.c.obj
[397/2509] Building C object esp-idf/esp_driver_gptimer/CMakeFiles/__idf_esp_driver_gptimer.dir/src/gptimer.c.obj
[398/2509] Building CXX object esp-idf/cxx/CMakeFiles/__idf_cxx.dir/cxx_exception_stubs.cpp.obj
[399/2509] Building CXX object esp-idf/nvs_flash/CMakeFiles/__idf_nvs_flash.dir/src/nvs_types.cpp.obj
[400/2509] Building C object esp-idf/esp_ringbuf/CMakeFiles/__idf_esp_ringbuf.dir/ringbuf.c.obj
[401/2509] Building CXX object esp-idf/cxx/CMakeFiles/__idf_cxx.dir/cxx_init.cpp.obj
[402/2509] Building CXX object esp-idf/nvs_flash/CMakeFiles/__idf_nvs_flash.dir/src/nvs_partition_manager.cpp.obj
[403/2509] Building CXX object esp-idf/cxx/CMakeFiles/__idf_cxx.dir/cxx_guards.cpp.obj
[404/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/init.c.obj
[405/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/abort.c.obj
[406/2509] Building C object esp-idf/pthread/CMakeFiles/__idf_pthread.dir/pthread_local_storage.c.obj
[407/2509] Building C object esp-idf/pthread/CMakeFiles/__idf_pthread.dir/pthread_cond_var.c.obj
[408/2509] Building C object esp-idf/pthread/CMakeFiles/__idf_pthread.dir/pthread_rwlock.c.obj
[409/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/assert.c.obj
[410/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/heap.c.obj
[411/2509] Building C object esp-idf/pthread/CMakeFiles/__idf_pthread.dir/pthread_semaphore.c.obj
[412/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/getentropy.c.obj
[413/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/pthread.c.obj
[414/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/random.c.obj
[415/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/poll.c.obj
[416/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/termios.c.obj
[417/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/syscalls.c.obj
[418/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/locks.c.obj
[419/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/sysconf.c.obj
[420/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/realpath.c.obj
[421/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/scandir.c.obj
[422/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/reent_syscalls.c.obj
[423/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/port/esp_time_impl.c.obj
[424/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/flockfile.c.obj
[425/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/time.c.obj
[426/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/reent_init.c.obj
[427/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/newlib_init.c.obj
[428/2509] Building C object esp-idf/pthread/CMakeFiles/__idf_pthread.dir/pthread.c.obj
[429/2509] Building C object esp-idf/freertos/CMakeFiles/__idf_freertos.dir/heap_idf.c.obj
[430/2509] Building C object esp-idf/freertos/CMakeFiles/__idf_freertos.dir/app_startup.c.obj
[431/2509] Building C object esp-idf/freertos/CMakeFiles/__idf_freertos.dir/port_common.c.obj
[432/2509] Building C object esp-idf/freertos/CMakeFiles/__idf_freertos.dir/FreeRTOS-Kernel/list.c.obj
[433/2509] Building C object esp-idf/freertos/CMakeFiles/__idf_freertos.dir/port_systick.c.obj
[434/2509] Building C object esp-idf/esp_driver_uart/CMakeFiles/__idf_esp_driver_uart.dir/src/uart.c.obj
[435/2509] Building ASM object esp-idf/freertos/CMakeFiles/__idf_freertos.dir/FreeRTOS-Kernel/portable/xtensa/portasm.S.obj
[436/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/port/xtensa/stdatomic_s32c1i.c.obj
[437/2509] Building C object esp-idf/freertos/CMakeFiles/__idf_freertos.dir/FreeRTOS-Kernel/portable/xtensa/xtensa_init.c.obj
[438/2509] Building C object esp-idf/freertos/CMakeFiles/__idf_freertos.dir/FreeRTOS-Kernel/portable/xtensa/xtensa_overlay_os_hook.c.obj
[439/2509] Building C object esp-idf/freertos/CMakeFiles/__idf_freertos.dir/FreeRTOS-Kernel/event_groups.c.obj
[440/2509] Building C object esp-idf/freertos/CMakeFiles/__idf_freertos.dir/esp_additions/idf_additions_event_groups.c.obj
[441/2509] Building C object esp-idf/freertos/CMakeFiles/__idf_freertos.dir/FreeRTOS-Kernel/portable/xtensa/port.c.obj
[442/2509] Building C object esp-idf/freertos/CMakeFiles/__idf_freertos.dir/esp_additions/freertos_compatibility.c.obj
[443/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp32s3/esp_cpu_intr.c.obj
[444/2509] Building C object esp-idf/freertos/CMakeFiles/__idf_freertos.dir/FreeRTOS-Kernel/timers.c.obj
[445/2509] Building C object esp-idf/freertos/CMakeFiles/__idf_freertos.dir/esp_additions/idf_additions.c.obj
[446/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp32s3/cpu_region_protect.c.obj
[447/2509] Building CXX object esp-idf/nvs_flash/CMakeFiles/__idf_nvs_flash.dir/src/nvs_encrypted_partition.cpp.obj
[448/2509] Building C object esp-idf/freertos/CMakeFiles/__idf_freertos.dir/FreeRTOS-Kernel/stream_buffer.c.obj
[449/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/esp_memory_utils.c.obj
[450/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/cpu.c.obj
[451/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/clk_ctrl_os.c.obj
[452/2509] Building C object esp-idf/freertos/CMakeFiles/__idf_freertos.dir/FreeRTOS-Kernel/queue.c.obj
[453/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/hw_random.c.obj
[454/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/revision.c.obj
[455/2509] Linking C static library esp-idf/nvs_flash/libnvs_flash.a
[456/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/esp_gpio_reserve.c.obj
[457/2509] Linking C static library esp-idf/esp_event/libesp_event.a
[458/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/esp_clk.c.obj
[459/2509] Linking C static library esp-idf/esp_driver_uart/libesp_driver_uart.a
[460/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/mac_addr.c.obj
[461/2509] Building C object esp-idf/newlib/CMakeFiles/__idf_newlib.dir/src/stdatomic.c.obj
[462/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/regi2c_ctrl.c.obj
[463/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/rtc_module.c.obj
[464/2509] Linking C static library esp-idf/esp_psram/libesp_psram.a
[465/2509] Linking C static library esp-idf/esp_ringbuf/libesp_ringbuf.a
[466/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/periph_ctrl.c.obj
[467/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/sar_periph_ctrl_common.c.obj
[468/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp32s3/io_mux.c.obj
[469/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp32s3/esp_clk_tree.c.obj
[470/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/clk_utils.c.obj
[471/2509] Linking C static library esp-idf/esp_driver_gptimer/libesp_driver_gptimer.a
[472/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/spi_bus_lock.c.obj
[473/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/intr_alloc.c.obj
[474/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/dma/esp_dma_utils.c.obj
[475/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp_clk_tree_common.c.obj
[476/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/dma/gdma_link.c.obj
[477/2509] Linking C static library esp-idf/esp_timer/libesp_timer.a
[478/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/sleep_mspi.c.obj
[479/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/sleep_modem.c.obj
[480/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/sleep_usb.c.obj
[481/2509] Linking C static library esp-idf/cxx/libcxx.a
[482/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/sleep_console.c.obj
[483/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/adc_share_hw_ctrl.c.obj
[484/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/spi_share_hw_ctrl.c.obj
[485/2509] Linking C static library esp-idf/pthread/libpthread.a
[486/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/sleep_event.c.obj
[487/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/dma/esp_async_memcpy.c.obj
[488/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp32s3/systimer.c.obj
[489/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/deprecated/gdma_legacy.c.obj
[490/2509] Linking C static library esp-idf/newlib/libnewlib.a
[491/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/sleep_wake_stub.c.obj
[492/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/mspi_timing_tuning/mspi_timing_tuning.c.obj
[493/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/sleep_gpio.c.obj
[494/2509] Building C object esp-idf/freertos/CMakeFiles/__idf_freertos.dir/FreeRTOS-Kernel/tasks.c.obj
[495/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/esp_clock_output.c.obj
[496/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp32s3/rtc_clk_init.c.obj
[497/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/power_supply/brownout.c.obj
[498/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/dma/async_memcpy_gdma.c.obj
[499/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp32s3/chip_info.c.obj
[500/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp32s3/rtc_time.c.obj
[501/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp_memprot_conv.c.obj
[502/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp32s3/rtc_sleep.c.obj
[503/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/mspi_timing_tuning/port/esp32s3/mspi_timing_config.c.obj
[504/2509] Linking C static library esp-idf/freertos/libfreertos.a
[505/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/mspi_timing_tuning/port/esp32s3/mspi_timing_by_mspi_delay.c.obj
[506/2509] Building C object esp-idf/esp_security/CMakeFiles/__idf_esp_security.dir/src/init.c.obj
[507/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp32s3/sar_periph_ctrl.c.obj
[508/2509] Building C object esp-idf/esp_security/CMakeFiles/__idf_esp_security.dir/src/esp_crypto_lock.c.obj
[509/2509] Building C object esp-idf/esp_security/CMakeFiles/__idf_esp_security.dir/src/esp_crypto_periph_clk.c.obj
[510/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/dport_access_common.c.obj
[511/2509] Building C object esp-idf/esp_security/CMakeFiles/__idf_esp_security.dir/src/esp_hmac.c.obj
[512/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp32s3/rtc_clk.c.obj
[513/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp32s3/rtc_init.c.obj
[514/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/lldesc.c.obj
[515/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/interrupts.c.obj
[516/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/lowpower/port/esp32s3/sleep_cpu.c.obj
[517/2509] Building C object esp-idf/esp_security/CMakeFiles/__idf_esp_security.dir/src/esp_ds.c.obj
[518/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/adc_periph.c.obj
[519/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/gdma_periph.c.obj
[520/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/uart_periph.c.obj
[521/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/gpio_periph.c.obj
[522/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/ledc_periph.c.obj
[523/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/dedic_gpio_periph.c.obj
[524/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/dma/gdma.c.obj
[525/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/sdm_periph.c.obj
[526/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/spi_periph.c.obj
[527/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/pcnt_periph.c.obj
[528/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/rmt_periph.c.obj
[529/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/i2s_periph.c.obj
[530/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/i2c_periph.c.obj
[531/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/temperature_sensor_periph.c.obj
[532/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/timer_periph.c.obj
[533/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/touch_sensor_periph.c.obj
[534/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/lcd_periph.c.obj
[535/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/mpi_periph.c.obj
[536/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/sdmmc_periph.c.obj
[537/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/sleep_modes.c.obj
[538/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/cam_periph.c.obj
[539/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/mcpwm_periph.c.obj
[540/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/power_supply_periph.c.obj
[541/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/usb_dwc_periph.c.obj
[542/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/twai_periph.c.obj
[543/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/rtc_io_periph.c.obj
[544/2509] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/wdt_periph.c.obj
[545/2509] Building C object esp-idf/heap/CMakeFiles/__idf_heap.dir/port/esp32s3/memory_layout.c.obj
[546/2509] Building C object esp-idf/heap/CMakeFiles/__idf_heap.dir/port/memory_layout_utils.c.obj
[547/2509] Building C object esp-idf/heap/CMakeFiles/__idf_heap.dir/heap_caps_base.c.obj
[548/2509] Building C object esp-idf/heap/CMakeFiles/__idf_heap.dir/multi_heap.c.obj
[549/2509] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/util.c.obj
[550/2509] Building C object esp-idf/heap/CMakeFiles/__idf_heap.dir/heap_caps_init.c.obj
[551/2509] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/os/log_timestamp.c.obj
[552/2509] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/os/log_lock.c.obj
[553/2509] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/os/util.c.obj
[554/2509] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/log_timestamp_common.c.obj
[555/2509] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/log_format_text.c.obj
[556/2509] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/log_print.c.obj
[557/2509] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/log_level/tag_log_level/tag_log_level.c.obj
[558/2509] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/os/log_write.c.obj
[559/2509] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/log_level/log_level.c.obj
[560/2509] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/log.c.obj
[561/2509] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/buffer/log_buffers.c.obj
[562/2509] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/log_level/tag_log_level/linked_list/log_linked_list.c.obj
[563/2509] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp32s3/esp_memprot.c.obj
[564/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/mpu_hal.c.obj
[565/2509] Building C object esp-idf/heap/CMakeFiles/__idf_heap.dir/heap_caps.c.obj
[566/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/color_hal.c.obj
[567/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/efuse_hal.c.obj
[568/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/hal_utils.c.obj
[569/2509] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/log_level/tag_log_level/cache/log_binary_heap.c.obj
[570/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/esp32s3/efuse_hal.c.obj
[571/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/spi_flash_encrypt_hal_iram.c.obj
[572/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/mmu_hal.c.obj
[573/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/cache_hal.c.obj
[574/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/spi_flash_hal.c.obj
[575/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/esp32s3/clk_tree_hal.c.obj
[576/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/ledc_hal.c.obj
[577/2509] Linking C static library esp-idf/esp_hw_support/libesp_hw_support.a
[578/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/uart_hal_iram.c.obj
[579/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/timer_hal.c.obj
[580/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/systimer_hal.c.obj
[581/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/rtc_io_hal.c.obj
[582/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/uart_hal.c.obj
[583/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/gpio_hal.c.obj
[584/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/ledc_hal_iram.c.obj
[585/2509] Linking C static library esp-idf/esp_security/libesp_security.a
[586/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/spi_flash_hal_iram.c.obj
[587/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/pcnt_hal.c.obj
[588/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/i2c_hal_iram.c.obj
[589/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/uhci_hal.c.obj
[590/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/rmt_hal.c.obj
[591/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/gdma_hal_top.c.obj
[592/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/mcpwm_hal.c.obj
[593/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/sdm_hal.c.obj
[594/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/i2c_hal.c.obj
[595/2509] Linking C static library esp-idf/soc/libsoc.a
[596/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/sdmmc_hal.c.obj
[597/2509] Building C object esp-idf/heap/CMakeFiles/__idf_heap.dir/tlsf/tlsf.c.obj
[598/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/lcd_hal.c.obj
[599/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/gdma_hal_ahb_v1.c.obj
[600/2509] Linking C static library esp-idf/heap/libheap.a
[601/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/mpi_hal.c.obj
[602/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/sha_hal.c.obj
[603/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/adc_oneshot_hal.c.obj
[604/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/aes_hal.c.obj
[605/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/adc_hal_common.c.obj
[606/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/brownout_hal.c.obj
[607/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/twai_hal_v1.c.obj
[608/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/spi_slave_hal.c.obj
[609/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/hmac_hal.c.obj
[610/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/i2s_hal.c.obj
[611/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/adc_hal.c.obj
[612/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/usb_serial_jtag_hal.c.obj
[613/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/ds_hal.c.obj
[614/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/spi_hal.c.obj
[615/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/usb_wrap_hal.c.obj
[616/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/spi_slave_hal_iram.c.obj
[617/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/touch_sensor_hal.c.obj
[618/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/cam_hal.c.obj
[619/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/xt_wdt_hal.c.obj
[620/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/spi_flash_hal_gpspi.c.obj
[621/2509] Building C object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_sys.c.obj
[622/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/touch_sens_hal.c.obj
[623/2509] Linking C static library esp-idf/log/liblog.a
[624/2509] Building C object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_crc.c.obj
[625/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/esp32s3/rtc_cntl_hal.c.obj
[626/2509] Building C object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_uart.c.obj
[627/2509] Building C object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_efuse.c.obj
[628/2509] Building C object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_gpio.c.obj
[629/2509] Building C object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_spiflash.c.obj
[630/2509] Building ASM object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_longjmp.S.obj
[631/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/esp32s3/touch_sensor_hal.c.obj
[632/2509] Building ASM object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_cache_writeback_esp32s3.S.obj
[633/2509] Building C object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_systimer.c.obj
[634/2509] Building C object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_wdt.c.obj
[635/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/spi_slave_hd_hal.c.obj
[636/2509] Building C object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_cache_esp32s2_esp32s3.c.obj
[637/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/usb_dwc_hal.c.obj
[638/2509] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/spi_hal_iram.c.obj
[639/2509] Building C object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_print.c.obj
[640/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/esp_err.c.obj
[641/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/esp_system_console.c.obj
[642/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/int_wdt.c.obj
[643/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/startup.c.obj
[644/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/crosscore_int.c.obj
[645/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/esp_system.c.obj
[646/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/esp_ipc.c.obj
[647/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/stack_check.c.obj
[648/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/freertos_hooks.c.obj
[649/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/system_time.c.obj
[650/2509] Linking C static library esp-idf/hal/libhal.a
[651/2509] Building C object esp-idf/esp_common/CMakeFiles/__idf_esp_common.dir/src/esp_err_to_name.c.obj
[652/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/startup_funcs.c.obj
[653/2509] Linking C static library esp-idf/esp_rom/libesp_rom.a
[654/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/ubsan.c.obj
[655/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/xt_wdt.c.obj
[656/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/panic.c.obj
[657/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/arch/xtensa/esp_ipc_isr_port.c.obj
[658/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/esp_system_chip.c.obj
[659/2509] Building ASM object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/arch/xtensa/esp_ipc_isr_routines.S.obj
[660/2509] Building ASM object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/arch/xtensa/esp_ipc_isr_handler.S.obj
[661/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/panic_handler.c.obj
[662/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/esp_ipc_isr.c.obj
[663/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/task_wdt/task_wdt_impl_timergroup.c.obj
[664/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/image_process.c.obj
[665/2509] Building ASM object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/arch/xtensa/panic_handler_asm.S.obj
[666/2509] Building ASM object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/arch/xtensa/expression_with_stack_asm.S.obj
[667/2509] Building ASM object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/arch/xtensa/debug_helpers_asm.S.obj
[668/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/cpu_start.c.obj
[669/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/arch/xtensa/expression_with_stack.c.obj
[670/2509] Building ASM object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/soc/esp32s3/highint_hdl.S.obj
[671/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/arch/xtensa/trax.c.obj
[672/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/arch/xtensa/debug_stubs.c.obj
[673/2509] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/flash_brownout_hook.c.obj
[674/2509] Linking C static library esp-idf/esp_common/libesp_common.a
[675/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/arch/xtensa/panic_arch.c.obj
[676/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/soc/esp32s3/reset_reason.c.obj
[677/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/soc/esp32s3/apb_backup_dma.c.obj
[678/2509] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/spi_flash_chip_drivers.c.obj
[679/2509] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/spi_flash_hpm_enable.c.obj
[680/2509] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/spi_flash_chip_issi.c.obj
[681/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/task_wdt/task_wdt.c.obj
[682/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/arch/xtensa/debug_helpers.c.obj
[683/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/soc/esp32s3/cache_err_int.c.obj
[684/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/soc/esp32s3/clk.c.obj
[685/2509] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/esp32s3/spi_flash_oct_flash_init.c.obj
[686/2509] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/port/soc/esp32s3/system_internal.c.obj
[687/2509] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/spi_flash_chip_mxic.c.obj
[688/2509] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/spi_flash_chip_boya.c.obj
[689/2509] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/spi_flash_chip_th.c.obj
[690/2509] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/spi_flash_chip_gd.c.obj
[691/2509] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/spi_flash_chip_winbond.c.obj
[692/2509] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/spi_flash_wrap.c.obj
[693/2509] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/spi_flash_os_func_noos.c.obj
[694/2509] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/memspi_host_driver.c.obj
[695/2509] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/flash_ops.c.obj
[696/2509] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/spi_flash_chip_mxic_opi.c.obj
[697/2509] Building C object esp-idf/esp_mm/CMakeFiles/__idf_esp_mm.dir/port/esp32s3/ext_mem_layout.c.obj
[698/2509] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/flash_mmap.c.obj
[699/2509] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/cache_utils.c.obj
[700/2509] Linking C static library esp-idf/esp_system/libesp_system.a
[701/2509] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/spi_flash_os_func_app.c.obj
[702/2509] Building C object esp-idf/esp_mm/CMakeFiles/__idf_esp_mm.dir/esp_cache_utils.c.obj
[703/2509] Building C object esp-idf/esp_mm/CMakeFiles/__idf_esp_mm.dir/heap_align_hw.c.obj
[704/2509] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/spi_flash_chip_generic.c.obj
[705/2509] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/esp_flash_spi_init.c.obj
[706/2509] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_clock_init.c.obj
[707/2509] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_mem.c.obj
[708/2509] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_efuse.c.obj
[709/2509] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_random.c.obj
[710/2509] Building C object esp-idf/esp_mm/CMakeFiles/__idf_esp_mm.dir/esp_cache_msync.c.obj
[711/2509] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_common.c.obj
[712/2509] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_random_esp32s3.c.obj
[713/2509] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_common_loader.c.obj
[714/2509] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/flash_encrypt.c.obj
[715/2509] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/secure_boot.c.obj
[716/2509] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/flash_partitions.c.obj
[717/2509] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/bootloader_flash/src/flash_qio_mode.c.obj
[718/2509] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_sha.c.obj
[719/2509] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/esp32s3/secure_boot_secure_features.c.obj
[720/2509] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/bootloader_flash/src/bootloader_flash_config_esp32s3.c.obj
[721/2509] Building C object esp-idf/esp_mm/CMakeFiles/__idf_esp_mm.dir/esp_mmu_map.c.obj
[722/2509] Building C object esp-idf/efuse/CMakeFiles/__idf_efuse.dir/esp32s3/esp_efuse_table.c.obj
[723/2509] Building C object esp-idf/efuse/CMakeFiles/__idf_efuse.dir/esp32s3/esp_efuse_fields.c.obj
[724/2509] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/bootloader_flash/src/bootloader_flash.c.obj
[725/2509] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/esp_flash_api.c.obj
[726/2509] Building C object esp-idf/efuse/CMakeFiles/__idf_efuse.dir/esp32s3/esp_efuse_rtc_calib.c.obj
[727/2509] Building C object esp-idf/efuse/CMakeFiles/__idf_efuse.dir/src/esp_efuse_fields.c.obj
[728/2509] Building C object esp-idf/efuse/CMakeFiles/__idf_efuse.dir/esp32s3/esp_efuse_utility.c.obj
[729/2509] Building C object esp-idf/efuse/CMakeFiles/__idf_efuse.dir/src/esp_efuse_startup.c.obj
[730/2509] Linking C static library esp-idf/spi_flash/libspi_flash.a
[731/2509] Building C object esp-idf/app_update/CMakeFiles/__idf_app_update.dir/esp_ota_app_desc.c.obj
[732/2509] Linking C static library esp-idf/esp_mm/libesp_mm.a
[733/2509] Building C object esp-idf/esp_partition/CMakeFiles/__idf_esp_partition.dir/partition_target.c.obj
[734/2509] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_utility.c.obj
[735/2509] Building C object esp-idf/efuse/CMakeFiles/__idf_efuse.dir/src/esp_efuse_utility.c.obj
[736/2509] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/esp_image_format.c.obj
[737/2509] Building C object esp-idf/esp_bootloader_format/CMakeFiles/__idf_esp_bootloader_format.dir/esp_bootloader_desc.c.obj
[738/2509] Building C object esp-idf/efuse/CMakeFiles/__idf_efuse.dir/src/esp_efuse_api.c.obj
[739/2509] Building C object esp-idf/efuse/CMakeFiles/__idf_efuse.dir/src/efuse_controller/keys/with_key_purposes/esp_efuse_api_key.c.obj
[740/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/debug.c.obj
[741/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/mps_reader.c.obj
[742/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/mps_trace.c.obj
[743/2509] Linking C static library esp-idf/bootloader_support/libbootloader_support.a
[744/2509] Building C object esp-idf/esp_app_format/CMakeFiles/__idf_esp_app_format.dir/esp_app_desc.c.obj
[745/2509] Building C object esp-idf/esp_partition/CMakeFiles/__idf_esp_partition.dir/partition.c.obj
[746/2509] Linking C static library esp-idf/efuse/libefuse.a
[747/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/ssl_debug_helpers_generated.c.obj
[748/2509] Linking C static library esp-idf/esp_partition/libesp_partition.a
[749/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/ssl_cache.c.obj
[750/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/ssl_ciphersuites.c.obj
[751/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/ssl_cookie.c.obj
[752/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/ssl_tls13_keys.c.obj
[753/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/ssl_tls13_server.c.obj
[754/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/ssl_tls13_client.c.obj
[755/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/ssl_tls13_generic.c.obj
[756/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/esp_platform_time.c.obj
[757/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/mbedtls_debug.c.obj
[758/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/ssl_ticket.c.obj
[759/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/ssl_client.c.obj
[760/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/dynamic/esp_ssl_cli.c.obj
[761/2509] Building C object esp-idf/app_update/CMakeFiles/__idf_app_update.dir/esp_ota_ops.c.obj
[762/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/dynamic/esp_mbedtls_dynamic_impl.c.obj
[763/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/dynamic/esp_ssl_srv.c.obj
[764/2509] Linking C static library esp-idf/app_update/libapp_update.a
[765/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/dynamic/esp_ssl_tls.c.obj
[766/2509] Linking C static library esp-idf/esp_bootloader_format/libesp_bootloader_format.a
[767/2509] Linking C static library esp-idf/esp_app_format/libesp_app_format.a
[768/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/net_sockets.c.obj
[769/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/ssl_tls12_client.c.obj
[770/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/ssl_msg.c.obj
[771/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedx509.dir/pkcs7.c.obj
[772/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedx509.dir/x509_create.c.obj
[773/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedx509.dir/x509write.c.obj
[774/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedx509.dir/x509_crl.c.obj
[775/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/aesni.c.obj
[776/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/aesce.c.obj
[777/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedx509.dir/x509write_csr.c.obj
[778/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/ssl_tls12_server.c.obj
[779/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedx509.dir/x509_csr.c.obj
[780/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/aes.c.obj
[781/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedx509.dir/x509write_crt.c.obj
[782/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/asn1parse.c.obj
[783/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/bignum_mod.c.obj
[784/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/base64.c.obj
[785/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/asn1write.c.obj
[786/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/bignum_mod_raw.c.obj
[787/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/block_cipher.c.obj
[788/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/camellia.c.obj
[789/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/chacha20.c.obj
[790/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/aria.c.obj
[791/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/chachapoly.c.obj
[792/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedx509.dir/x509.c.obj
[793/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/constant_time.c.obj
[794/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/cipher_wrap.c.obj
[795/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/ccm.c.obj
[796/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedtls.dir/ssl_tls.c.obj
[797/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/des.c.obj
[798/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/dhm.c.obj
[799/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/ctr_drbg.c.obj
[800/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/bignum_core.c.obj
[801/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/ecjpake.c.obj
[802/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/cmac.c.obj
[803/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/ecdh.c.obj
[804/2509] Linking CXX static library esp-idf/mbedtls/mbedtls/library/libmbedtls.a
[805/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/ecp_curves_new.c.obj
[806/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/entropy_poll.c.obj
[807/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/cipher.c.obj
[808/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedx509.dir/x509_crt.c.obj
[809/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/hkdf.c.obj
[810/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/ecdsa.c.obj
[811/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/entropy.c.obj
[812/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/hmac_drbg.c.obj
[813/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/lmots.c.obj
[814/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/error.c.obj
[815/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/lms.c.obj
[816/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/md5.c.obj
[817/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/memory_buffer_alloc.c.obj
[818/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/nist_kw.c.obj
[819/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/md.c.obj
[820/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/padlock.c.obj
[821/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/bignum.c.obj
[822/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/pk_ecc.c.obj
[823/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/gcm.c.obj
[824/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/pem.c.obj
[825/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/pk_wrap.c.obj
[826/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/pkcs12.c.obj
[827/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/platform.c.obj
[828/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/pkcs5.c.obj
[829/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/platform_util.c.obj
[830/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/poly1305.c.obj
[831/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/oid.c.obj
[832/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/ecp_curves.c.obj
[833/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/psa_crypto_client.c.obj
[834/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/pkwrite.c.obj
[835/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/psa_crypto_driver_wrappers_no_static.c.obj
[836/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/pk.c.obj
[837/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/psa_crypto_aead.c.obj
[838/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/psa_crypto_ffdh.c.obj
[839/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/pkparse.c.obj
[840/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/psa_crypto_pake.c.obj
[841/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/psa_crypto_hash.c.obj
[842/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/psa_crypto_se.c.obj
[843/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/psa_crypto_ecp.c.obj
[844/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/ecp.c.obj
[845/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/psa_crypto_cipher.c.obj
[846/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/ripemd160.c.obj
[847/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/psa_its_file.c.obj
[848/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/psa_crypto_mac.c.obj
[849/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/psa_crypto_storage.c.obj
[850/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/sha1.c.obj
[851/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/psa_util.c.obj
[852/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/rsa_alt_helpers.c.obj
[853/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/psa_crypto_rsa.c.obj
[854/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/sha3.c.obj
[855/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/sha256.c.obj
[856/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/threading.c.obj
[857/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/sha512.c.obj
[858/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/timing.c.obj
[859/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/version.c.obj
[860/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/version_features.c.obj
[861/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/psa_crypto_slot_management.c.obj
[862/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/sha/core/esp_sha_gdma_impl.c.obj
[863/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/aes/dma/esp_aes_gdma_impl.c.obj
[864/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/esp_mem.c.obj
[865/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/esp_hardware.c.obj
[866/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/esp_timing.c.obj
[867/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/crypto_shared_gdma/esp_crypto_shared_gdma.c.obj
[868/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/aes/esp_aes_common.c.obj
[869/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/aes/esp_aes_xts.c.obj
[870/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/sha/esp_sha.c.obj
[871/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/aes/dma/esp_aes_dma_core.c.obj
[872/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/aes/dma/esp_aes.c.obj
[873/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/esp_ds/esp_rsa_sign_alt.c.obj
[874/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/sha/core/sha.c.obj
[875/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/esp_hmac_pbkdf2.c.obj
[876/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/esp_ds/esp_rsa_dec_alt.c.obj
[877/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/bignum/bignum_alt.c.obj
[878/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/esp_ds/esp_ds_common.c.obj
[879/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/md/esp_md.c.obj
[880/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/sha/core/esp_sha1.c.obj
[881/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/sha/core/esp_sha256.c.obj
[882/2509] Linking CXX static library esp-idf/mbedtls/mbedtls/library/libmbedx509.a
[883/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/sha/core/esp_sha512.c.obj
[884/2509] Building C object esp-idf/mbedtls/mbedtls/3rdparty/p256-m/CMakeFiles/p256m.dir/p256-m_driver_entrypoints.c.obj
[885/2509] Building C object esp-idf/mbedtls/mbedtls/3rdparty/everest/CMakeFiles/everest.dir/library/everest.c.obj
[886/2509] Building C object esp-idf/mbedtls/mbedtls/3rdparty/p256-m/CMakeFiles/p256m.dir/p256-m/p256-m.c.obj
[887/2509] Building C object esp-idf/mbedtls/mbedtls/3rdparty/everest/CMakeFiles/everest.dir/library/x25519.c.obj
[888/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/bignum/esp_bignum.c.obj
[889/2509] Creating directories for 'bootloader'
[890/2509] Building C object esp-idf/mbedtls/mbedtls/3rdparty/everest/CMakeFiles/everest.dir/library/Hacl_Curve25519_joined.c.obj
[891/2509] No download step for 'bootloader'
[892/2509] No update step for 'bootloader'
[893/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/Users/cobain/esp/esp-idf/components/mbedtls/port/aes/esp_aes_gcm.c.obj
[894/2509] No patch step for 'bootloader'
[895/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/rsa.c.obj
[896/2509] Building C object esp-idf/mbedtls/mbedtls/library/CMakeFiles/mbedcrypto.dir/psa_crypto.c.obj
[897/2509] Linking CXX static library esp-idf/mbedtls/mbedtls/library/libmbedcrypto.a
[898/2509] Linking CXX static library esp-idf/mbedtls/mbedtls/3rdparty/p256-m/libp256m.a
[899/2509] Linking CXX static library esp-idf/mbedtls/mbedtls/3rdparty/everest/libeverest.a
[900/2509] Generating x509_crt_bundle
[901/2509] Generating ../../x509_crt_bundle.S
[902/2509] Building ASM object esp-idf/mbedtls/CMakeFiles/__idf_mbedtls.dir/__/__/x509_crt_bundle.S.obj
[903/2509] Building C object esp-idf/esp_pm/CMakeFiles/__idf_esp_pm.dir/pm_trace.c.obj
[904/2509] Building C object esp-idf/esp_driver_gpio/CMakeFiles/__idf_esp_driver_gpio.dir/src/gpio_glitch_filter_ops.c.obj
[905/2509] Building C object esp-idf/esp_pm/CMakeFiles/__idf_esp_pm.dir/pm_locks.c.obj
[906/2509] Building C object esp-idf/xtensa/CMakeFiles/__idf_xtensa.dir/eri.c.obj
[907/2509] Building C object esp-idf/esp_driver_gpio/CMakeFiles/__idf_esp_driver_gpio.dir/src/gpio_pin_glitch_filter.c.obj
[908/2509] Building ASM object esp-idf/xtensa/CMakeFiles/__idf_xtensa.dir/xtensa_context.S.obj
[909/2509] Building C object esp-idf/xtensa/CMakeFiles/__idf_xtensa.dir/xt_trax.c.obj
[910/2509] Building ASM object esp-idf/xtensa/CMakeFiles/__idf_xtensa.dir/xtensa_intr_asm.S.obj
[911/2509] Building ASM object esp-idf/xtensa/CMakeFiles/__idf_xtensa.dir/xtensa_vectors.S.obj
[912/2509] Building C object esp-idf/esp_pm/CMakeFiles/__idf_esp_pm.dir/pm_impl.c.obj
[913/2509] Building C object esp-idf/xtensa/CMakeFiles/__idf_xtensa.dir/xtensa_intr.c.obj
[914/2509] Building C object esp-idf/mbedtls/CMakeFiles/__idf_mbedtls.dir/esp_crt_bundle/esp_crt_bundle.c.obj
[915/2509] Linking C static library esp-idf/mbedtls/libmbedtls.a
[916/2509] Building C object esp-idf/esp_driver_gpio/CMakeFiles/__idf_esp_driver_gpio.dir/src/dedic_gpio.c.obj
[917/2509] Building C object esp-idf/esp_driver_gpio/CMakeFiles/__idf_esp_driver_gpio.dir/src/rtc_io.c.obj
[918/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/btc/core/btc_alarm.c.obj
[919/2509] Linking C static library esp-idf/esp_pm/libesp_pm.a
[920/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/api/esp_blufi_api.c.obj
[921/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/hci_log/bt_hci_log.c.obj
[922/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/btc/core/btc_manage.c.obj
[923/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/btc/profile/esp/blufi/blufi_protocol.c.obj
[924/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/osi/allocator.c.obj
[925/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/btc/profile/esp/blufi/blufi_prf.c.obj
[926/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/btc/core/btc_task.c.obj
[927/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/osi/buffer.c.obj
[928/2509] Building C object esp-idf/json/CMakeFiles/__idf_json.dir/cJSON/cJSON_Utils.c.obj
[929/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/osi/alarm.c.obj
[930/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/osi/fixed_queue.c.obj
[931/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/osi/fixed_pkt_queue.c.obj
[932/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/osi/hash_functions.c.obj
[933/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/controller/esp32c3/bt.c.obj
[934/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/osi/future.c.obj
[935/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/osi/pkt_queue.c.obj
[936/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/osi/osi.c.obj
[937/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/osi/hash_map.c.obj
[938/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/osi/semaphore.c.obj
[939/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/osi/mutex.c.obj
[940/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/osi/config.c.obj
[941/2509] Building C object esp-idf/esp_driver_gpio/CMakeFiles/__idf_esp_driver_gpio.dir/src/gpio.c.obj
[942/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/porting/mem/bt_osi_mem.c.obj
[943/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/osi/list.c.obj
[944/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/ble_log/ble_log_spi_out.c.obj
[945/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/tinycrypt/src/utils.c.obj
[946/2509] Linking C static library esp-idf/esp_driver_gpio/libesp_driver_gpio.a
[947/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/osi/thread.c.obj
[948/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/tinycrypt/src/ctr_mode.c.obj
[949/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/ble_log/ble_log_uhci_out.c.obj
[950/2509] Linking C static library esp-idf/xtensa/libxtensa.a
[951/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/tinycrypt/src/sha256.c.obj
[952/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/tinycrypt/src/aes_encrypt.c.obj
[953/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/tinycrypt/src/aes_decrypt.c.obj
[954/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/tinycrypt/src/ctr_prng.c.obj
[955/2509] Building C object esp-idf/json/CMakeFiles/__idf_json.dir/cJSON/cJSON.c.obj
[956/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/tinycrypt/src/cmac_mode.c.obj
[957/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/tinycrypt/src/ccm_mode.c.obj
[958/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/tinycrypt/src/hmac_prng.c.obj
[959/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/tinycrypt/src/ecc_dh.c.obj
[960/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/tinycrypt/src/ecc_platform_specific.c.obj
[961/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/tinycrypt/src/ecc_dsa.c.obj
[962/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/tinycrypt/port/esp_tinycrypt_port.c.obj
[963/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/tinycrypt/src/hmac.c.obj
[964/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/tinycrypt/src/cbc_mode.c.obj
[965/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/util/src/addr.c.obj
[966/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/services/gatt/src/ble_svc_gatt.c.obj
[967/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/tinycrypt/src/ecc.c.obj
[968/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/services/ipss/src/ble_svc_ipss.c.obj
[969/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/services/tps/src/ble_svc_tps.c.obj
[970/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/services/ias/src/ble_svc_ias.c.obj
[971/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/transport/src/transport.c.obj
[972/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/services/hr/src/ble_svc_hr.c.obj
[973/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/services/ans/src/ble_svc_ans.c.obj
[974/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/services/htp/src/ble_svc_htp.c.obj
[975/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/services/hid/src/ble_svc_hid.c.obj
[976/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/services/lls/src/ble_svc_lls.c.obj
[977/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/services/dis/src/ble_svc_dis.c.obj
[978/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/services/bas/src/ble_svc_bas.c.obj
[979/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/services/prox/src/ble_svc_prox.c.obj
[980/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/services/gap/src/ble_svc_gap.c.obj
[981/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_cs.c.obj
[982/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_hs_shutdown.c.obj
[983/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/services/cts/src/ble_svc_cts.c.obj
[984/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/services/cte/src/ble_svc_cte.c.obj
[985/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/services/sps/src/ble_svc_sps.c.obj
[986/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_store_util.c.obj
[987/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_hs_hci_cmd.c.obj
[988/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_l2cap_sig_cmd.c.obj
[989/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_hs_conn.c.obj
[990/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/services/ras/src/ble_svc_ras.c.obj
[991/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_hs_atomic.c.obj
[992/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_hs_id.c.obj
[993/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_ibeacon.c.obj
[994/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_gatts_lcl.c.obj
[995/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_hs_stop.c.obj
[996/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_hs_periodic_sync.c.obj
[997/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_hs_mqueue.c.obj
[998/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_sm_alg.c.obj
[999/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_aes_ccm.c.obj
[1000/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_ead.c.obj
[1001/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_att.c.obj
[1002/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_hs.c.obj
[1003/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_hs_hci_evt.c.obj
[1004/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_hs_cfg.c.obj
[1005/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_sm_lgcy.c.obj
[1006/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_store.c.obj
[1007/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_l2cap_coc.c.obj
[1008/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_hs_mbuf.c.obj
[1009/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_hs_log.c.obj
[1010/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_sm.c.obj
[1011/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_eddystone.c.obj
[1012/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_att_cmd.c.obj
[1013/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_hs_startup.c.obj
[1014/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_sm_cmd.c.obj
[1015/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_att_clt.c.obj
[1016/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_att_svr.c.obj
[1017/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_l2cap_sig.c.obj
[1018/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_hs_flow.c.obj
[1019/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_hs_misc.c.obj
[1020/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_hs_pvcy.c.obj
[1021/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_uuid.c.obj
[1022/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_l2cap.c.obj
[1023/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_hs_resolv.c.obj
[1024/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_sm_sc.c.obj
[1025/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/store/config/src/ble_store_nvs.c.obj
[1026/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/store/ram/src/ble_store_ram.c.obj
[1027/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_gattc_cache.c.obj
[1028/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_eatt.c.obj
[1029/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_hs_hci_util.c.obj
[1030/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_hs_hci.c.obj
[1031/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_gattc_cache_conn.c.obj
[1032/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/porting/npl/freertos/src/nimble_port_freertos.c.obj
[1033/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/port/src/nvs_port.c.obj
[1034/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_gattc.c.obj
[1035/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/porting/nimble/src/endian.c.obj
[1036/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/store/config/src/ble_store_config.c.obj
[1037/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/porting/nimble/src/nimble_port.c.obj
[1038/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/porting/nimble/src/mem.c.obj
[1039/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/porting/nimble/src/os_msys_init.c.obj
[1040/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/porting/nimble/src/os_mempool.c.obj
[1041/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_hs_adv.c.obj
[1042/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/common/btc/profile/esp/blufi/nimble_host/esp_blufi.c.obj
[1043/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/transport/esp_ipc_legacy/src/hci_esp_ipc_legacy.c.obj
[1044/2509] Building C object esp-idf/console/CMakeFiles/__idf_console.dir/split_argv.c.obj
[1045/2509] Building C object esp-idf/console/CMakeFiles/__idf_console.dir/esp_console_common.c.obj
[1046/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/esp-hci/src/esp_nimble_hci.c.obj
[1047/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_gatts.c.obj
[1048/2509] Building C object esp-idf/console/CMakeFiles/__idf_console.dir/esp_console_repl_internal.c.obj
[1049/2509] Building C object esp-idf/console/CMakeFiles/__idf_console.dir/commands.c.obj
[1050/2509] Building C object esp-idf/console/CMakeFiles/__idf_console.dir/esp_console_repl_chip.c.obj
[1051/2509] Building C object esp-idf/console/CMakeFiles/__idf_console.dir/argtable3/arg_dbl.c.obj
[1052/2509] Building C object esp-idf/console/CMakeFiles/__idf_console.dir/argtable3/arg_end.c.obj
[1053/2509] Building C object esp-idf/console/CMakeFiles/__idf_console.dir/argtable3/arg_cmd.c.obj
[1054/2509] Building C object esp-idf/console/CMakeFiles/__idf_console.dir/argtable3/arg_dstr.c.obj
[1055/2509] Building C object esp-idf/console/CMakeFiles/__idf_console.dir/argtable3/arg_file.c.obj
[1056/2509] Building C object esp-idf/console/CMakeFiles/__idf_console.dir/argtable3/arg_rem.c.obj
[1057/2509] Building C object esp-idf/console/CMakeFiles/__idf_console.dir/argtable3/arg_int.c.obj
[1058/2509] Building C object esp-idf/console/CMakeFiles/__idf_console.dir/argtable3/arg_lit.c.obj
[1059/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/nimble/host/src/ble_gap.c.obj
[1060/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/porting/nimble/src/os_mbuf.c.obj
[1061/2509] Building C object esp-idf/console/CMakeFiles/__idf_console.dir/argtable3/arg_hashtable.c.obj
[1062/2509] Building C object esp-idf/bt/CMakeFiles/__idf_bt.dir/host/nimble/nimble/porting/npl/freertos/src/npl_os_freertos.c.obj
[1063/2509] Building C object esp-idf/console/CMakeFiles/__idf_console.dir/argtable3/arg_date.c.obj
[1064/2509] Building C object esp-idf/esp_driver_cam/CMakeFiles/__idf_esp_driver_cam.dir/dvp_share_ctrl.c.obj
[1065/2509] Building C object esp-idf/console/CMakeFiles/__idf_console.dir/argtable3/arg_utils.c.obj
[1066/2509] Building C object esp-idf/console/CMakeFiles/__idf_console.dir/argtable3/arg_str.c.obj
[1067/2509] Building C object esp-idf/esp_driver_cam/CMakeFiles/__idf_esp_driver_cam.dir/esp_cam_ctlr.c.obj
[1068/2509] Building C object esp-idf/esp_driver_cam/CMakeFiles/__idf_esp_driver_cam.dir/dvp/src/esp_cam_ctlr_dvp_gdma.c.obj
[1069/2509] Building C object esp-idf/esp_lcd/CMakeFiles/__idf_esp_lcd.dir/src/esp_lcd_common.c.obj
[1070/2509] Building C object esp-idf/esp_lcd/CMakeFiles/__idf_esp_lcd.dir/src/esp_lcd_panel_io.c.obj
[1071/2509] Building C object esp-idf/console/CMakeFiles/__idf_console.dir/linenoise/linenoise.c.obj
[1072/2509] Building C object esp-idf/esp_lcd/CMakeFiles/__idf_esp_lcd.dir/src/esp_lcd_panel_ops.c.obj
[1073/2509] Building C object esp-idf/esp_lcd/CMakeFiles/__idf_esp_lcd.dir/src/esp_lcd_panel_ssd1306.c.obj
[1074/2509] Building C object esp-idf/esp_lcd/CMakeFiles/__idf_esp_lcd.dir/src/esp_lcd_panel_nt35510.c.obj
[1075/2509] Building C object esp-idf/esp_lcd/CMakeFiles/__idf_esp_lcd.dir/src/esp_lcd_panel_st7789.c.obj
[1076/2509] Building C object esp-idf/console/CMakeFiles/__idf_console.dir/argtable3/arg_rex.c.obj
[1077/2509] Building C object esp-idf/esp_lcd/CMakeFiles/__idf_esp_lcd.dir/i2c/esp_lcd_panel_io_i2c_v2.c.obj
[1078/2509] Building C object esp-idf/esp_lcd/CMakeFiles/__idf_esp_lcd.dir/i2c/esp_lcd_panel_io_i2c_v1.c.obj
[1079/2509] Building C object esp-idf/console/CMakeFiles/__idf_console.dir/argtable3/argtable3.c.obj
[1080/2509] Building CXX object esp-idf/wear_levelling/CMakeFiles/__idf_wear_levelling.dir/SPI_Flash.cpp.obj
[1081/2509] Building C object esp-idf/esp_driver_cam/CMakeFiles/__idf_esp_driver_cam.dir/dvp/src/esp_cam_ctlr_dvp_cam.c.obj
[1082/2509] Building CXX object esp-idf/wear_levelling/CMakeFiles/__idf_wear_levelling.dir/Partition.cpp.obj
[1083/2509] Building CXX object esp-idf/wear_levelling/CMakeFiles/__idf_wear_levelling.dir/crc32.cpp.obj
[1084/2509] Building CXX object esp-idf/wear_levelling/CMakeFiles/__idf_wear_levelling.dir/WL_Ext_Perf.cpp.obj
[1085/2509] Linking C static library esp-idf/json/libjson.a
[1086/2509] Building C object esp-idf/esp_lcd/CMakeFiles/__idf_esp_lcd.dir/spi/esp_lcd_panel_io_spi.c.obj
[1087/2509] Building CXX object esp-idf/wear_levelling/CMakeFiles/__idf_wear_levelling.dir/wear_levelling.cpp.obj
[1088/2509] Building CXX object esp-idf/wear_levelling/CMakeFiles/__idf_wear_levelling.dir/WL_Ext_Safe.cpp.obj
[1089/2509] Performing configure step for 'bootloader'
-- Found Git: /usr/bin/git (found version "2.50.1 (Apple Git-155)")
-- Minimal build - OFF
-- The C compiler identification is GNU 14.2.0
-- The CXX compiler identification is GNU 14.2.0
-- The ASM compiler identification is GNU
-- Found assembler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Building ESP-IDF components for target esp32s3
-- ESP-TEE is currently supported only on the esp32c6;esp32h2;esp32c5 SoCs
-- Project sdkconfig file /Users/cobain/robot-build/fw/sdkconfig
-- Compiler supported targets: xtensa-esp-elf
-- Adding linker script /Users/cobain/esp/esp-idf/components/soc/esp32s3/ld/esp32s3.peripherals.ld
-- Bootloader project name: "bootloader" version: 1
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.api.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.bt_funcs.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.libgcc.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.wdt.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.version.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.libc.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.newlib.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/bootloader/subproject/main/ld/esp32s3/bootloader.rom.ld
-- Components: bootloader bootloader_support efuse esp_app_format esp_bootloader_format esp_common esp_hw_support esp_rom esp_security esp_system esptool_py freertos hal log main micro-ecc newlib partition_table soc spi_flash xtensa
-- Component paths: /Users/cobain/esp/esp-idf/components/bootloader /Users/cobain/esp/esp-idf/components/bootloader_support /Users/cobain/esp/esp-idf/components/efuse /Users/cobain/esp/esp-idf/components/esp_app_format /Users/cobain/esp/esp-idf/components/esp_bootloader_format /Users/cobain/esp/esp-idf/components/esp_common /Users/cobain/esp/esp-idf/components/esp_hw_support /Users/cobain/esp/esp-idf/components/esp_rom /Users/cobain/esp/esp-idf/components/esp_security /Users/cobain/esp/esp-idf/components/esp_system /Users/cobain/esp/esp-idf/components/esptool_py /Users/cobain/esp/esp-idf/components/freertos /Users/cobain/esp/esp-idf/components/hal /Users/cobain/esp/esp-idf/components/log /Users/cobain/esp/esp-idf/components/bootloader/subproject/main /Users/cobain/esp/esp-idf/components/bootloader/subproject/components/micro-ecc /Users/cobain/esp/esp-idf/components/newlib /Users/cobain/esp/esp-idf/components/partition_table /Users/cobain/esp/esp-idf/components/soc /Users/cobain/esp/esp-idf/components/spi_flash /Users/cobain/esp/esp-idf/components/xtensa
-- Adding linker script /Users/cobain/esp/esp-idf/components/bootloader/subproject/main/ld/esp32s3/bootloader.ld
-- Configuring done (36.7s)
-- Generating done (0.4s)
-- Build files have been written to: /Users/cobain/robot-build/fw/build/bootloader
[1090/2509] Building C object esp-idf/usb/CMakeFiles/__idf_usb.dir/usb_helpers.c.obj
[1091/2509] Building C object esp-idf/usb/CMakeFiles/__idf_usb.dir/usb_private.c.obj
[1092/2509] Building C object esp-idf/usb/CMakeFiles/__idf_usb.dir/hub.c.obj
[1093/2509] Building C object esp-idf/esp_lcd/CMakeFiles/__idf_esp_lcd.dir/i80/esp_lcd_panel_io_i80.c.obj
[1094/2509] Building C object esp-idf/usb/CMakeFiles/__idf_usb.dir/enum.c.obj
[1095/2509] Building C object esp-idf/espressif__esp_sccb_intf/CMakeFiles/__idf_espressif__esp_sccb_intf.dir/sccb_i2c/src/sccb_i2c.c.obj
[1096/2509] Building C object esp-idf/espressif__esp_sccb_intf/CMakeFiles/__idf_espressif__esp_sccb_intf.dir/src/sccb.c.obj
[1097/2509] Building C object esp-idf/usb/CMakeFiles/__idf_usb.dir/usb_phy.c.obj
[1098/2509] Linking C static library esp-idf/console/libconsole.a
[1099/2509] Linking C static library esp-idf/esp_driver_cam/libesp_driver_cam.a
[1100/2509] Building C object esp-idf/protocomm/CMakeFiles/__idf_protocomm.dir/proto-c/constants.pb-c.c.obj
[1101/2509] Building CXX object esp-idf/wear_levelling/CMakeFiles/__idf_wear_levelling.dir/WL_Flash.cpp.obj
[1102/2509] Building C object esp-idf/protocomm/CMakeFiles/__idf_protocomm.dir/proto-c/sec0.pb-c.c.obj
[1103/2509] Linking C static library esp-idf/bt/libbt.a
[1104/2509] Building C object esp-idf/protocomm/CMakeFiles/__idf_protocomm.dir/proto-c/session.pb-c.c.obj
[1105/2509] Building C object esp-idf/protocomm/CMakeFiles/__idf_protocomm.dir/proto-c/sec1.pb-c.c.obj
[1106/2509] Building C object esp-idf/protocomm/CMakeFiles/__idf_protocomm.dir/proto-c/sec2.pb-c.c.obj
[1107/2509] Building C object esp-idf/protocomm/CMakeFiles/__idf_protocomm.dir/src/common/protocomm.c.obj
[1108/2509] Building C object esp-idf/usb/CMakeFiles/__idf_usb.dir/hcd_dwc.c.obj
[1109/2509] Building C object esp-idf/protocomm/CMakeFiles/__idf_protocomm.dir/src/transports/protocomm_console.c.obj
[1110/2509] Building C object esp-idf/protocomm/CMakeFiles/__idf_protocomm.dir/src/security/security0.c.obj
[1111/2509] Building C object esp-idf/esp_lcd/CMakeFiles/__idf_esp_lcd.dir/rgb/esp_lcd_panel_rgb.c.obj
[1112/2509] Linking C static library esp-idf/esp_lcd/libesp_lcd.a
[1113/2509] Building C object esp-idf/protocomm/CMakeFiles/__idf_protocomm.dir/src/transports/protocomm_httpd.c.obj
[1114/2509] Building C object esp-idf/usb/CMakeFiles/__idf_usb.dir/usb_host.c.obj
[1115/2509] Linking C static library esp-idf/wear_levelling/libwear_levelling.a
[1116/2509] Building C object esp-idf/usb/CMakeFiles/__idf_usb.dir/usbh.c.obj
[1117/2509] Building C object esp-idf/protocomm/CMakeFiles/__idf_protocomm.dir/src/crypto/srp6a/esp_srp_mpi.c.obj
[1118/2509] Building C object esp-idf/fatfs/CMakeFiles/__idf_fatfs.dir/diskio/diskio.c.obj
[1119/2509] Building C object esp-idf/fatfs/CMakeFiles/__idf_fatfs.dir/diskio/diskio_rawflash.c.obj
[1120/2509] Building C object esp-idf/fatfs/CMakeFiles/__idf_fatfs.dir/diskio/diskio_wl.c.obj
[1121/2509] Building C object esp-idf/fatfs/CMakeFiles/__idf_fatfs.dir/port/freertos/ffsystem.c.obj
[1122/2509] Building C object esp-idf/protocomm/CMakeFiles/__idf_protocomm.dir/src/security/security2.c.obj
[1123/2509] Building C object esp-idf/protocomm/CMakeFiles/__idf_protocomm.dir/src/crypto/srp6a/esp_srp.c.obj
[1124/2509] Building C object esp-idf/fatfs/CMakeFiles/__idf_fatfs.dir/src/ffunicode.c.obj
[1125/2509] Building C object esp-idf/protocomm/CMakeFiles/__idf_protocomm.dir/src/security/security1.c.obj
[1126/2509] Building C object esp-idf/protobuf-c/CMakeFiles/__idf_protobuf-c.dir/protobuf-c/protobuf-c/protobuf-c.c.obj
[1127/2509] Building C object esp-idf/fatfs/CMakeFiles/__idf_fatfs.dir/diskio/diskio_sdmmc.c.obj
[1128/2509] Linking C static library esp-idf/protobuf-c/libprotobuf-c.a
[1129/2509] Building C object esp-idf/protocomm/CMakeFiles/__idf_protocomm.dir/src/transports/protocomm_nimble.c.obj
[1130/2509] Building C object esp-idf/mqtt/CMakeFiles/__idf_mqtt.dir/esp-mqtt/lib/platform_esp32_idf.c.obj
[1131/2509] Building C object esp-idf/fatfs/CMakeFiles/__idf_fatfs.dir/vfs/vfs_fat_sdmmc.c.obj
[1132/2509] Building C object esp-idf/spiffs/CMakeFiles/__idf_spiffs.dir/spiffs_api.c.obj
[1133/2509] Building C object esp-idf/mqtt/CMakeFiles/__idf_mqtt.dir/esp-mqtt/lib/mqtt_outbox.c.obj
[1134/2509] Building C object esp-idf/fatfs/CMakeFiles/__idf_fatfs.dir/vfs/vfs_fat_spiflash.c.obj
[1135/2509] Building C object esp-idf/spiffs/CMakeFiles/__idf_spiffs.dir/spiffs/src/spiffs_cache.c.obj
[1136/2509] Linking C static library esp-idf/usb/libusb.a
[1137/2509] Building C object esp-idf/mqtt/CMakeFiles/__idf_mqtt.dir/esp-mqtt/lib/mqtt_msg.c.obj
[1138/2509] Building C object esp-idf/fatfs/CMakeFiles/__idf_fatfs.dir/vfs/vfs_fat.c.obj
[1139/2509] Generating ../../wifi_configuration_done.html.S
[1140/2509] Building C object esp-idf/spiffs/CMakeFiles/__idf_spiffs.dir/spiffs/src/spiffs_gc.c.obj
[1141/2509] Generating ../../wifi_configuration.html.S
[1142/2509] Building C object esp-idf/espressif__iot_eth/CMakeFiles/__idf_espressif__iot_eth.dir/iot_eth.c.obj
[1143/2509] Building C object esp-idf/espressif__iot_eth/CMakeFiles/__idf_espressif__iot_eth.dir/iot_eth_netif_glue.c.obj
[1144/2509] Building C object esp-idf/spiffs/CMakeFiles/__idf_spiffs.dir/spiffs/src/spiffs_check.c.obj
[1145/2509] Building C object esp-idf/spiffs/CMakeFiles/__idf_spiffs.dir/esp_spiffs.c.obj
[1146/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/core/lv_obj_class.c.obj
[1147/2509] Building C object esp-idf/spiffs/CMakeFiles/__idf_spiffs.dir/spiffs/src/spiffs_hydrogen.c.obj
[1148/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/core/lv_obj_id_builtin.c.obj
[1149/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/core/lv_group.c.obj
[1150/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/core/lv_obj_event.c.obj
[1151/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/core/lv_obj_property.c.obj
[1152/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/core/lv_obj_draw.c.obj
[1153/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/core/lv_obj.c.obj
[1154/2509] Building C object esp-idf/mqtt/CMakeFiles/__idf_mqtt.dir/esp-mqtt/mqtt_client.c.obj
[1155/2509] Building CXX object esp-idf/78__uart-uhci/CMakeFiles/__idf_78__uart-uhci.dir/src/uart_uhci.cc.obj
[1156/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/convert/helium/lv_draw_buf_convert_helium.c.obj
[1157/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/convert/lv_draw_buf_convert.c.obj
[1158/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/convert/neon/lv_draw_buf_convert_neon.c.obj
[1159/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/core/lv_obj_style_gen.c.obj
[1160/2509] Building C object esp-idf/spiffs/CMakeFiles/__idf_spiffs.dir/spiffs/src/spiffs_nucleus.c.obj
[1161/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/dma2d/lv_draw_dma2d.c.obj
[1162/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/dma2d/lv_draw_dma2d_fill.c.obj
[1163/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/dma2d/lv_draw_dma2d_img.c.obj
[1164/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/espressif/ppa/lv_draw_ppa.c.obj
[1165/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/espressif/ppa/lv_draw_ppa_buf.c.obj
[1166/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/espressif/ppa/lv_draw_ppa_fill.c.obj
[1167/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/espressif/ppa/lv_draw_ppa_img.c.obj
[1168/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/eve/lv_draw_eve.c.obj
[1169/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/eve/lv_draw_eve_arc.c.obj
[1170/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/eve/lv_draw_eve_fill.c.obj
[1171/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/eve/lv_draw_eve_image.c.obj
[1172/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/eve/lv_draw_eve_letter.c.obj
[1173/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/eve/lv_draw_eve_line.c.obj
[1174/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/core/lv_obj_tree.c.obj
[1175/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/eve/lv_draw_eve_ram_g.c.obj
[1176/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/eve/lv_draw_eve_triangle.c.obj
[1177/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/eve/lv_eve.c.obj
[1178/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/lv_draw_3d.c.obj
[1179/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/core/lv_obj_scroll.c.obj
[1180/2509] Building C object esp-idf/fatfs/CMakeFiles/__idf_fatfs.dir/src/ff.c.obj
[1181/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/core/lv_obj_pos.c.obj
[1182/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/lv_draw_line.c.obj
[1183/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/core/lv_refr.c.obj
[1184/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/lv_draw_arc.c.obj
[1185/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/lv_draw_image.c.obj
[1186/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/core/lv_obj_style.c.obj
[1187/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/lv_draw_vector.c.obj
[1188/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/lv_draw_mask.c.obj
[1189/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/lv_draw.c.obj
[1190/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/lv_draw_triangle.c.obj
[1191/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nema_gfx/lv_draw_nema_gfx_arc.c.obj
[1192/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nema_gfx/lv_draw_nema_gfx_fill.c.obj
[1193/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nema_gfx/lv_draw_nema_gfx_border.c.obj
[1194/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/lv_draw_rect.c.obj
[1195/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nema_gfx/lv_draw_nema_gfx_img.c.obj
[1196/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/display/lv_display.c.obj
[1197/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nema_gfx/lv_draw_nema_gfx_layer.c.obj
[1198/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nema_gfx/lv_draw_nema_gfx_line.c.obj
[1199/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nema_gfx/lv_draw_nema_gfx_label.c.obj
[1200/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nema_gfx/lv_draw_nema_gfx_stm32_hal.c.obj
[1201/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nema_gfx/lv_draw_nema_gfx.c.obj
[1202/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nema_gfx/lv_draw_nema_gfx_triangle.c.obj
[1203/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nema_gfx/lv_draw_nema_gfx_utils.c.obj
[1204/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nema_gfx/lv_draw_nema_gfx_vector.c.obj
[1205/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nxp/g2d/lv_draw_buf_g2d.c.obj
[1206/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nxp/g2d/lv_draw_g2d.c.obj
[1207/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nxp/g2d/lv_draw_g2d_img.c.obj
[1208/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/lv_draw_buf.c.obj
[1209/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nxp/g2d/lv_draw_g2d_fill.c.obj
[1210/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nxp/g2d/lv_g2d_buf_map.c.obj
[1211/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nxp/g2d/lv_g2d_utils.c.obj
[1212/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nxp/pxp/lv_draw_buf_pxp.c.obj
[1213/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nxp/pxp/lv_draw_pxp_fill.c.obj
[1214/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nxp/pxp/lv_draw_pxp_img.c.obj
[1215/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nxp/pxp/lv_draw_pxp.c.obj
[1216/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nxp/pxp/lv_draw_pxp_layer.c.obj
[1217/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nema_gfx/lv_nema_gfx_path.c.obj
[1218/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nxp/pxp/lv_pxp_osa.c.obj
[1219/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nxp/pxp/lv_pxp_cfg.c.obj
[1220/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/lv_draw_label.c.obj
[1221/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/nxp/pxp/lv_pxp_utils.c.obj
[1222/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/opengles/lv_draw_opengles.c.obj
[1223/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/renesas/dave2d/lv_draw_dave2d_arc.c.obj
[1224/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/renesas/dave2d/lv_draw_dave2d.c.obj
[1225/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/renesas/dave2d/lv_draw_dave2d_fill.c.obj
[1226/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/renesas/dave2d/lv_draw_dave2d_image.c.obj
[1227/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/renesas/dave2d/lv_draw_dave2d_label.c.obj
[1228/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/renesas/dave2d/lv_draw_dave2d_mask_rectangle.c.obj
[1229/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/renesas/dave2d/lv_draw_dave2d_border.c.obj
[1230/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/renesas/dave2d/lv_draw_dave2d_triangle.c.obj
[1231/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/renesas/dave2d/lv_draw_dave2d_utils.c.obj
[1232/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/lv_image_decoder.c.obj
[1233/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/renesas/dave2d/lv_draw_dave2d_line.c.obj
[1234/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sdl/lv_draw_sdl.c.obj
[1235/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/blend/neon/lv_draw_sw_blend_neon_to_rgb565.c.obj
[1236/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/blend/neon/lv_draw_sw_blend_neon_to_rgb888.c.obj
[1237/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/blend/lv_draw_sw_blend.c.obj
[1238/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/blend/lv_draw_sw_blend_to_rgb565_swapped.c.obj
[1239/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/lv_draw_sw.c.obj
[1240/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/lv_draw_sw_arc.c.obj
[1241/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/lv_draw_sw_border.c.obj
[1242/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/blend/lv_draw_sw_blend_to_l8.c.obj
[1243/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/lv_draw_sw_grad.c.obj
[1244/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/blend/lv_draw_sw_blend_to_al88.c.obj
[1245/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/lv_draw_sw_fill.c.obj
[1246/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/lv_draw_sw_letter.c.obj
[1247/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/blend/lv_draw_sw_blend_to_argb8888_premultiplied.c.obj
[1248/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/lv_draw_sw_mask_rect.c.obj
[1249/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/blend/lv_draw_sw_blend_to_argb8888.c.obj
[1250/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/blend/lv_draw_sw_blend_to_i1.c.obj
[1251/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/lv_draw_sw_box_shadow.c.obj
[1252/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/lv_draw_sw_line.c.obj
[1253/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/vg_lite/lv_draw_buf_vg_lite.c.obj
[1254/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/vg_lite/lv_draw_vg_lite.c.obj
[1255/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/blend/lv_draw_sw_blend_to_rgb888.c.obj
[1256/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/lv_draw_sw_vector.c.obj
[1257/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/vg_lite/lv_draw_vg_lite_arc.c.obj
[1258/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/vg_lite/lv_draw_vg_lite_box_shadow.c.obj
[1259/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/vg_lite/lv_draw_vg_lite_fill.c.obj
[1260/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/vg_lite/lv_draw_vg_lite_layer.c.obj
[1261/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/lv_draw_sw_triangle.c.obj
[1262/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/vg_lite/lv_draw_vg_lite_border.c.obj
[1263/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/vg_lite/lv_draw_vg_lite_line.c.obj
[1264/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/vg_lite/lv_draw_vg_lite_img.c.obj
[1265/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/vg_lite/lv_draw_vg_lite_vector.c.obj
[1266/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/vg_lite/lv_draw_vg_lite_triangle.c.obj
[1267/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/vg_lite/lv_draw_vg_lite_mask_rect.c.obj
[1268/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/vg_lite/lv_vg_lite_math.c.obj
[1269/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/vg_lite/lv_vg_lite_decoder.c.obj
[1270/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/lv_draw_sw_utils.c.obj
[1271/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/vg_lite/lv_draw_vg_lite_label.c.obj
[1272/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/display/drm/lv_linux_drm.c.obj
[1273/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/blend/lv_draw_sw_blend_to_rgb565.c.obj
[1274/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/lv_draw_sw_img.c.obj
[1275/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/display/drm/lv_linux_drm_egl.c.obj
[1276/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/display/drm/lv_linux_drm_common.c.obj
[1277/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/display/ft81x/lv_ft81x.c.obj
[1278/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/display/fb/lv_linux_fbdev.c.obj
[1279/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/display/ili9341/lv_ili9341.c.obj
[1280/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/vg_lite/lv_vg_lite_path.c.obj
[1281/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/display/lcd/lv_lcd_generic_mipi.c.obj
[1282/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/display/nv3007/lv_nv3007.c.obj
[1283/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/vg_lite/lv_vg_lite_grad.c.obj
[1284/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/display/lovyan_gfx/lv_lovyan_gfx.cpp.obj
[1285/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/lv_draw_sw_transform.c.obj
[1286/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/display/renesas_glcdc/lv_renesas_glcdc.c.obj
[1287/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/vg_lite/lv_vg_lite_pending.c.obj
[1288/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/display/st7789/lv_st7789.c.obj
[1289/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/display/st7735/lv_st7735.c.obj
[1290/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/display/st_ltdc/lv_st_ltdc.c.obj
[1291/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/draw/eve/lv_draw_eve_display.c.obj
[1292/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/display/st7796/lv_st7796.c.obj
[1293/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/evdev/lv_evdev.c.obj
[1294/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/libinput/lv_xkb.c.obj
[1295/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/vg_lite/lv_vg_lite_utils.c.obj
[1296/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/vg_lite/lv_vg_lite_stroke.c.obj
[1297/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/nuttx/lv_nuttx_entry.c.obj
[1298/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/display/tft_espi/lv_tft_espi.cpp.obj
[1299/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/libinput/lv_libinput.c.obj
[1300/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/nuttx/lv_nuttx_fbdev.c.obj
[1301/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/draw/sw/lv_draw_sw_mask.c.obj
[1302/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/nuttx/lv_nuttx_lcd.c.obj
[1303/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/opengles/assets/lv_opengles_shader.c.obj
[1304/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/opengles/glad/src/egl.c.obj
[1305/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/nuttx/lv_nuttx_touchscreen.c.obj
[1306/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/opengles/glad/src/gles2.c.obj
[1307/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/display/nxp_elcdif/lv_nxp_elcdif.c.obj
[1308/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/nuttx/lv_nuttx_mouse.c.obj
[1309/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/opengles/lv_opengles_driver.c.obj
[1310/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/opengles/lv_opengles_debug.c.obj
[1311/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/opengles/lv_opengles_egl.c.obj
[1312/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/opengles/lv_opengles_texture.c.obj
[1313/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/opengles/lv_opengles_glfw.c.obj
[1314/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/opengles/opengl_shader/lv_opengl_shader_manager.c.obj
[1315/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/opengles/opengl_shader/lv_opengl_shader_program.c.obj
[1316/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/sdl/lv_sdl_keyboard.c.obj
[1317/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/sdl/lv_sdl_mouse.c.obj
[1318/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/qnx/lv_qnx.c.obj
[1319/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/sdl/lv_sdl_window.c.obj
[1320/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/sdl/lv_sdl_mousewheel.c.obj
[1321/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/nuttx/lv_nuttx_cache.c.obj
[1322/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/nuttx/lv_nuttx_image_cache.c.obj
[1323/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/wayland/lv_wayland.c.obj
[1324/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/wayland/lv_wayland_smm.c.obj
[1325/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/nuttx/lv_nuttx_profiler.c.obj
[1326/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/wayland/lv_wl_cache.c.obj
[1327/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/nuttx/lv_nuttx_libuv.c.obj
[1328/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/wayland/lv_wl_dmabuf.c.obj
[1329/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/wayland/lv_wl_seat.c.obj
[1330/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/uefi/lv_uefi_indev_keyboard.c.obj
[1331/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/wayland/lv_wl_keyboard.c.obj
[1332/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/wayland/lv_wl_shm.c.obj
[1333/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/uefi/lv_uefi_context.c.obj
[1334/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/wayland/lv_wl_pointer.c.obj
[1335/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/wayland/lv_wl_window_decorations.c.obj
[1336/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/wayland/lv_wl_pointer_axis.c.obj
[1337/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/wayland/lv_wl_window.c.obj
[1338/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/wayland/lv_wl_xdg_shell.c.obj
[1339/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/uefi/lv_uefi_display.c.obj
[1340/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/uefi/lv_uefi_private.c.obj
[1341/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/wayland/lv_wl_touch.c.obj
[1342/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/windows/lv_windows_input.c.obj
[1343/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/windows/lv_windows_context.c.obj
[1344/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/windows/lv_windows_display.c.obj
[1345/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/uefi/lv_uefi_indev_touch.c.obj
[1346/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/uefi/lv_uefi_indev_pointer.c.obj
[1347/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/x11/lv_x11_display.c.obj
[1348/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/drivers/x11/lv_x11_input.c.obj
[1349/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font.c.obj
[1350/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_fmt_txt.c.obj
[1351/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_12.c.obj
[1352/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_10.c.obj
[1353/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_dejavu_16_persian_hebrew.c.obj
[1354/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_14.c.obj
[1355/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_18.c.obj
[1356/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_14_aligned.c.obj
[1357/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_16.c.obj
[1358/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_20.c.obj
[1359/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_binfont_loader.c.obj
[1360/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_22.c.obj
[1361/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_26.c.obj
[1362/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_28.c.obj
[1363/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_30.c.obj
[1364/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_24.c.obj
[1365/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_28_compressed.c.obj
[1366/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_32.c.obj
[1367/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_34.c.obj
[1368/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_36.c.obj
[1369/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_38.c.obj
[1370/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_40.c.obj
[1371/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_8.c.obj
[1372/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_42.c.obj
[1373/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_46.c.obj
[1374/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_44.c.obj
[1375/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_montserrat_48.c.obj
[1376/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_unscii_16.c.obj
[1377/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/indev/lv_indev_gesture.c.obj
[1378/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/FT800-FT813/EVE_supplemental.c.obj
[1379/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/FT800-FT813/EVE_commands.c.obj
[1380/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_source_han_sans_sc_14_cjk.c.obj
[1381/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/layouts/lv_layout.c.obj
[1382/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_unscii_8.c.obj
[1383/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/expat/xmlparse.c.obj
[1384/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/expat/xmlrole.c.obj
[1385/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/indev/lv_indev_scroll.c.obj
[1386/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/expat/xmltok.c.obj
[1387/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/layouts/flex/lv_flex.c.obj
[1388/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/expat/xmltok_impl.c.obj
[1389/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/layouts/grid/lv_grid.c.obj
[1390/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/barcode/code128.c.obj
[1391/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/expat/xmltok_ns.c.obj
[1392/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/ffmpeg/lv_ffmpeg.c.obj
[1393/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/freetype/lv_freetype.c.obj
[1394/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/barcode/lv_barcode.c.obj
[1395/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/frogfs/src/frogfs.c.obj
[1396/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/bin_decoder/lv_bin_decoder.c.obj
[1397/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/frogfs/src/decomp_raw.c.obj
[1398/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/fsdrv/lv_fs_cbfs.c.obj
[1399/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/bmp/lv_bmp.c.obj
[1400/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/font/lv_font_source_han_sans_sc_16_cjk.c.obj
[1401/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/freetype/lv_freetype_glyph.c.obj
[1402/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/freetype/lv_ftsystem.c.obj
[1403/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/freetype/lv_freetype_outline.c.obj
[1404/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/indev/lv_indev.c.obj
[1405/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/freetype/lv_freetype_image.c.obj
[1406/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/fsdrv/lv_fs_fatfs.c.obj
[1407/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/fsdrv/lv_fs_arduino_esp_littlefs.cpp.obj
[1408/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/fsdrv/lv_fs_arduino_sd.cpp.obj
[1409/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/fsdrv/lv_fs_frogfs.c.obj
[1410/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gif/lv_gif.c.obj
[1411/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gltf/gltf_data/lv_gltf_bind.cpp.obj
[1412/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gltf/gltf_data/lv_gltf_data.cpp.obj
[1413/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gltf/gltf_data/lv_gltf_data_animations.cpp.obj
[1414/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/fsdrv/lv_fs_littlefs.c.obj
[1415/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gltf/gltf_data/lv_gltf_data_cache.cpp.obj
[1416/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gltf/gltf_data/lv_gltf_data_injest.cpp.obj
[1417/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gltf/gltf_data/lv_gltf_data_mesh.cpp.obj
[1418/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gltf/gltf_data/lv_gltf_data_node.cpp.obj
[1419/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gltf/gltf_data/lv_gltf_data_primitive.cpp.obj
[1420/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gltf/gltf_data/lv_gltf_data_shader.cpp.obj
[1421/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gltf/gltf_data/lv_gltf_data_skin.cpp.obj
[1422/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/fsdrv/lv_fs_memfs.c.obj
[1423/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gltf/gltf_data/lv_gltf_data_texture.cpp.obj
[1424/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gltf/gltf_data/lv_gltf_uniform_locations.cpp.obj
[1425/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gltf/gltf_view/assets/chromatic.c.obj
[1426/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gltf/gltf_view/assets/lv_gltf_view_shader.c.obj
[1427/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gltf/gltf_view/ibl/lv_gltf_ibl_sampler.c.obj
[1428/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gltf/gltf_view/lv_gltf_view.cpp.obj
[1429/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gltf/gltf_view/lv_gltf_view_shader.cpp.obj
[1430/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gltf/gltf_view/lv_gltf_view_render.cpp.obj
[1431/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gltf/math/lv_gltf_math.cpp.obj
[1432/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/fsdrv/lv_fs_uefi.c.obj
[1433/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/fsdrv/lv_fs_posix.c.obj
[1434/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gstreamer/lv_gstreamer.c.obj
[1435/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/fsdrv/lv_fs_stdio.c.obj
[1436/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/fsdrv/lv_fs_win32.c.obj
[1437/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/lz4/lz4.c.obj
[1438/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/rle/lv_rle.c.obj
[1439/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/svg/lv_svg.c.obj
[1440/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/svg/lv_svg_parser.c.obj
[1441/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/libjpeg_turbo/lv_libjpeg_turbo.c.obj
[1442/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/libpng/lv_libpng.c.obj
[1443/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/svg/lv_svg_token.c.obj
[1444/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgAccessor.cpp.obj
[1445/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/svg/lv_svg_render.c.obj
[1446/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgAnimation.cpp.obj
[1447/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgCanvas.cpp.obj
[1448/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/lodepng/lv_lodepng.c.obj
[1449/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/svg/lv_svg_decoder.c.obj
[1450/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgCapi.cpp.obj
[1451/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgCompressor.cpp.obj
[1452/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgFill.cpp.obj
[1453/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/rlottie/lv_rlottie.c.obj
[1454/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgGlCanvas.cpp.obj
[1455/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgInitializer.cpp.obj
[1456/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgLoader.cpp.obj
[1457/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgLottieAnimation.cpp.obj
[1458/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgLottieBuilder.cpp.obj
[1459/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgLottieExpressions.cpp.obj
[1460/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgLottieLoader.cpp.obj
[1461/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgLottieInterpolator.cpp.obj
[1462/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgLottieModel.cpp.obj
[1463/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgLottieModifier.cpp.obj
[1464/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgLottieParser.cpp.obj
[1465/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgLottieParserHandler.cpp.obj
[1466/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgMath.cpp.obj
[1467/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgPaint.cpp.obj
[1468/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgPicture.cpp.obj
[1469/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgRawLoader.cpp.obj
[1470/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgRender.cpp.obj
[1471/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/qrcode/lv_qrcode.c.obj
[1472/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgSaver.cpp.obj
[1473/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgScene.cpp.obj
[1474/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgShape.cpp.obj
[1475/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgSvgCssStyle.cpp.obj
[1476/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgStr.cpp.obj
[1477/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgSvgLoader.cpp.obj
[1478/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgSvgPath.cpp.obj
[1479/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgSvgSceneBuilder.cpp.obj
[1480/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgSvgUtil.cpp.obj
[1481/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgSwCanvas.cpp.obj
[1482/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgSwFill.cpp.obj
[1483/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgSwImage.cpp.obj
[1484/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgSwMath.cpp.obj
[1485/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgSwMemPool.cpp.obj
[1486/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgSwPostEffect.cpp.obj
[1487/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgSwRaster.cpp.obj
[1488/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/gif/AnimatedGIF/src/gif.c.obj
[1489/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgSwRenderer.cpp.obj
[1490/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgSwRle.cpp.obj
[1491/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgSwStroke.cpp.obj
[1492/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgTaskScheduler.cpp.obj
[1493/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgSwShape.cpp.obj
[1494/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgText.cpp.obj
[1495/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgWgCanvas.cpp.obj
[1496/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/thorvg/tvgXmlParser.cpp.obj
[1497/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/vg_lite_driver/VGLite/vg_lite_image.c.obj
[1498/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/tjpgd/tjpgd.c.obj
[1499/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/vg_lite_driver/VGLite/vg_lite.c.obj
[1500/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/vg_lite_driver/VGLite/vg_lite_matrix.c.obj
[1501/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/vg_lite_driver/VGLite/vg_lite_path.c.obj
[1502/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/vg_lite_driver/VGLite/vg_lite_stroke.c.obj
[1503/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/vg_lite_driver/VGLiteKernel/vg_lite_kernel.c.obj
[1504/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/vg_lite_driver/lv_vg_lite_hal/lv_vg_lite_hal.c.obj
[1505/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/vg_lite_driver/lv_vg_lite_hal/vg_lite_os.c.obj
[1506/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/cache/class/lv_cache_lru_ll.c.obj
[1507/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/cache/class/lv_cache_lru_rb.c.obj
[1508/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/cache/class/lv_cache_sc_da.c.obj
[1509/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/cache/instance/lv_image_cache.c.obj
[1510/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/cache/instance/lv_image_header_cache.c.obj
[1511/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/lv_init.c.obj
[1512/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/cache/lv_cache_entry.c.obj
[1513/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/cache/lv_cache.c.obj
[1514/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_array.c.obj
[1515/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_async.c.obj
[1516/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_anim_timeline.c.obj
[1517/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/tjpgd/lv_tjpgd.c.obj
[1518/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_bidi.c.obj
[1519/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/tiny_ttf/lv_tiny_ttf.c.obj
[1520/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_color_op.c.obj
[1521/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_circle_buf.c.obj
[1522/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_grad.c.obj
[1523/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_log.c.obj
[1524/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_color.c.obj
[1525/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/qrcode/qrcodegen.c.obj
[1526/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_iter.c.obj
[1527/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_ll.c.obj
[1528/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_area.c.obj
[1529/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_anim.c.obj
[1530/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_matrix.c.obj
[1531/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_profiler_builtin_posix.c.obj
[1532/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_palette.c.obj
[1533/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_event.c.obj
[1534/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_lru.c.obj
[1535/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_templ.c.obj
[1536/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_text_ap.c.obj
[1537/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_rb.c.obj
[1538/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_fs.c.obj
[1539/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_tree.c.obj
[1540/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_utils.c.obj
[1541/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_style.c.obj
[1542/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_math.c.obj
[1543/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/osal/lv_cmsis_rtos2.c.obj
[1544/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_timer.c.obj
[1545/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/osal/lv_linux.c.obj
[1546/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/osal/lv_freertos.c.obj
[1547/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_style_gen.c.obj
[1548/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/osal/lv_mqx.c.obj
[1549/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/osal/lv_os_none.c.obj
[1550/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/osal/lv_pthread.c.obj
[1551/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/osal/lv_rtthread.c.obj
[1552/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/osal/lv_sdl2.c.obj
[1553/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/osal/lv_windows.c.obj
[1554/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/osal/lv_os.c.obj
[1555/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/font_manager/lv_font_manager_recycle.c.obj
[1556/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_text.c.obj
[1557/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/font_manager/lv_font_manager.c.obj
[1558/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/file_explorer/lv_file_explorer.c.obj
[1559/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/monkey/lv_monkey.c.obj
[1560/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/fragment/lv_fragment_manager.c.obj
[1561/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/fragment/lv_fragment.c.obj
[1562/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/gridnav/lv_gridnav.c.obj
[1563/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/ime/lv_ime_pinyin.c.obj
[1564/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/test/lv_test_display.c.obj
[1565/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/test/lv_test_helpers.c.obj
[1566/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/test/lv_test_indev.c.obj
[1567/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/misc/lv_profiler_builtin.c.obj
[1568/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/test/lv_test_indev_gesture.c.obj
[1569/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/test/lv_test_screenshot_compare.c.obj
[1570/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/translation/lv_translation.c.obj
[1571/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/sysmon/lv_sysmon.c.obj
[1572/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/vg_lite_tvg/vg_lite_matrix.c.obj
[1573/2509] Building CXX object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/vg_lite_tvg/vg_lite_tvg.cpp.obj
[1574/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/lv_xml.c.obj
[1575/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/lv_xml_component.c.obj
[1576/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/lv_xml_load.c.obj
[1577/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/snapshot/lv_snapshot.c.obj
[1578/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/lv_xml_parser.c.obj
[1579/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/lv_xml_test.c.obj
[1580/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/lv_xml_update.c.obj
[1581/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/lv_xml_utils.c.obj
[1582/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/lv_xml_widget.c.obj
[1583/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_arc_parser.c.obj
[1584/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_bar_parser.c.obj
[1585/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_button_parser.c.obj
[1586/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_buttonmatrix_parser.c.obj
[1587/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_calendar_parser.c.obj
[1588/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_chart_parser.c.obj
[1589/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_canvas_parser.c.obj
[1590/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_dropdown_parser.c.obj
[1591/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_checkbox_parser.c.obj
[1592/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_image_parser.c.obj
[1593/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_keyboard_parser.c.obj
[1594/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/imgfont/lv_imgfont.c.obj
[1595/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_label_parser.c.obj
[1596/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_obj_parser.c.obj
[1597/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_qrcode_parser.c.obj
[1598/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_roller_parser.c.obj
[1599/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_scale_parser.c.obj
[1600/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_slider_parser.c.obj
[1601/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/lv_xml_base_types.c.obj
[1602/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_spangroup_parser.c.obj
[1603/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_spinbox_parser.c.obj
[1604/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_switch_parser.c.obj
[1605/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/lv_xml_style.c.obj
[1606/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_table_parser.c.obj
[1607/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_tabview_parser.c.obj
[1608/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/lv_xml_translation.c.obj
[1609/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/xml/parsers/lv_xml_textarea_parser.c.obj
[1610/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/stdlib/builtin/lv_sprintf_builtin.c.obj
[1611/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/stdlib/builtin/lv_mem_core_builtin.c.obj
[1612/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/stdlib/builtin/lv_string_builtin.c.obj
[1613/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/stdlib/builtin/lv_tlsf.c.obj
[1614/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/stdlib/clib/lv_mem_core_clib.c.obj
[1615/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/stdlib/rtthread/lv_sprintf_rtthread.c.obj
[1616/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/stdlib/micropython/lv_mem_core_micropython.c.obj
[1617/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/stdlib/clib/lv_sprintf_clib.c.obj
[1618/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/stdlib/rtthread/lv_string_rtthread.c.obj
[1619/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/stdlib/rtthread/lv_mem_core_rtthread.c.obj
[1620/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/stdlib/uefi/lv_mem_core_uefi.c.obj
[1621/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/stdlib/clib/lv_string_clib.c.obj
[1622/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/3dtexture/lv_3dtexture.c.obj
[1623/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/others/observer/lv_observer.c.obj
[1624/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/stdlib/lv_mem.c.obj
[1625/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/animimage/lv_animimage.c.obj
[1626/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/tick/lv_tick.c.obj
[1627/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/button/lv_button.c.obj
[1628/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/themes/mono/lv_theme_mono.c.obj
[1629/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/arclabel/lv_arclabel.c.obj
[1630/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/themes/lv_theme.c.obj
[1631/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/bar/lv_bar.c.obj
[1632/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/calendar/lv_calendar_chinese.c.obj
[1633/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/themes/simple/lv_theme_simple.c.obj
[1634/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/calendar/lv_calendar_header_arrow.c.obj
[1635/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/calendar/lv_calendar_header_dropdown.c.obj
[1636/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/arc/lv_arc.c.obj
[1637/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/checkbox/lv_checkbox.c.obj
[1638/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/canvas/lv_canvas.c.obj
[1639/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/buttonmatrix/lv_buttonmatrix.c.obj
[1640/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/imagebutton/lv_imagebutton.c.obj
[1641/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/keyboard/lv_keyboard.c.obj
[1642/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/led/lv_led.c.obj
[1643/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/calendar/lv_calendar.c.obj
[1644/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/list/lv_list.c.obj
[1645/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/lottie/lv_lottie.c.obj
[1646/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/line/lv_line.c.obj
[1647/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/objx_templ/lv_objx_templ.c.obj
[1648/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/menu/lv_menu.c.obj
[1649/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/msgbox/lv_msgbox.c.obj
[1650/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/themes/default/lv_theme_default.c.obj
[1651/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/property/lv_animimage_properties.c.obj
[1652/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/property/lv_dropdown_properties.c.obj
[1653/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/image/lv_image.c.obj
[1654/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/property/lv_image_properties.c.obj
[1655/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/dropdown/lv_dropdown.c.obj
[1656/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/property/lv_label_properties.c.obj
[1657/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/property/lv_keyboard_properties.c.obj
[1658/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/property/lv_obj_properties.c.obj
[1659/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/property/lv_style_properties.c.obj
[1660/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/property/lv_roller_properties.c.obj
[1661/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/property/lv_textarea_properties.c.obj
[1662/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/property/lv_slider_properties.c.obj
[1663/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/span/lv_span.c.obj
[1664/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/spinbox/lv_spinbox.c.obj
[1665/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/switch/lv_switch.c.obj
[1666/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/label/lv_label.c.obj
[1667/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/tileview/lv_tileview.c.obj
[1668/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/spinner/lv_spinner.c.obj
[1669/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/chart/lv_chart.c.obj
[1670/2509] Building C object esp-idf/espressif__esp_codec_dev/CMakeFiles/__idf_espressif__esp_codec_dev.dir/esp_codec_dev_vol.c.obj
[1671/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/slider/lv_slider.c.obj
[1672/2509] Building C object esp-idf/espressif__esp_codec_dev/CMakeFiles/__idf_espressif__esp_codec_dev.dir/esp_codec_dev_if.c.obj
[1673/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/tabview/lv_tabview.c.obj
[1674/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/roller/lv_roller.c.obj
[1675/2509] Building C object esp-idf/espressif__esp_codec_dev/CMakeFiles/__idf_espressif__esp_codec_dev.dir/audio_codec_sw_vol.c.obj
[1676/2509] Building C object esp-idf/espressif__esp_codec_dev/CMakeFiles/__idf_espressif__esp_codec_dev.dir/platform/audio_codec_gpio.c.obj
[1677/2509] Building C object esp-idf/espressif__esp_codec_dev/CMakeFiles/__idf_espressif__esp_codec_dev.dir/esp_codec_dev.c.obj
[1678/2509] Building C object esp-idf/espressif__esp_codec_dev/CMakeFiles/__idf_espressif__esp_codec_dev.dir/platform/esp_codec_dev_os.c.obj
[1679/2509] Building C object esp-idf/espressif__esp_codec_dev/CMakeFiles/__idf_espressif__esp_codec_dev.dir/platform/audio_codec_ctrl_i2c.c.obj
[1680/2509] Building C object esp-idf/espressif__esp_codec_dev/CMakeFiles/__idf_espressif__esp_codec_dev.dir/platform/audio_codec_ctrl_spi.c.obj
[1681/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/win/lv_win.c.obj
[1682/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/table/lv_table.c.obj
[1683/2509] Building C object esp-idf/espressif__esp_codec_dev/CMakeFiles/__idf_espressif__esp_codec_dev.dir/device/es8156/es8156.c.obj
[1684/2509] Building C object esp-idf/espressif__esp_codec_dev/CMakeFiles/__idf_espressif__esp_codec_dev.dir/device/es7243e/es7243e.c.obj
[1685/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/scale/lv_scale.c.obj
[1686/2509] Building C object esp-idf/espressif__esp_codec_dev/CMakeFiles/__idf_espressif__esp_codec_dev.dir/device/es7243/es7243.c.obj
[1687/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/widgets/textarea/lv_textarea.c.obj
[1688/2509] Building C object esp-idf/espressif__esp_codec_dev/CMakeFiles/__idf_espressif__esp_codec_dev.dir/device/es8311/es8311.c.obj
[1689/2509] Building C object esp-idf/espressif__esp_codec_dev/CMakeFiles/__idf_espressif__esp_codec_dev.dir/platform/audio_codec_data_i2s.c.obj
[1690/2509] Building C object esp-idf/espressif__esp_codec_dev/CMakeFiles/__idf_espressif__esp_codec_dev.dir/device/tas5805m/tas5805m.c.obj
[1691/2509] Building C object esp-idf/espressif__esp_codec_dev/CMakeFiles/__idf_espressif__esp_codec_dev.dir/device/es8388/es8388.c.obj
[1692/2509] Building C object esp-idf/espressif__button/CMakeFiles/__idf_espressif__button.dir/button_gpio.c.obj
[1693/2509] Building C object esp-idf/espressif__esp_codec_dev/CMakeFiles/__idf_espressif__esp_codec_dev.dir/device/es7210/es7210.c.obj
[1694/2509] Building C object esp-idf/espressif__esp_codec_dev/CMakeFiles/__idf_espressif__esp_codec_dev.dir/device/aw88298/aw88298.c.obj
[1695/2509] Building C object esp-idf/espressif__dl_fft/CMakeFiles/__idf_espressif__dl_fft.dir/dl_fft_f32.c.obj
[1696/2509] Building C object esp-idf/espressif__button/CMakeFiles/__idf_espressif__button.dir/button_matrix.c.obj
[1697/2509] Building C object esp-idf/espressif__dl_fft/CMakeFiles/__idf_espressif__dl_fft.dir/dl_fft_s16.c.obj
[1698/2509] Building C object esp-idf/espressif__dl_fft/CMakeFiles/__idf_espressif__dl_fft.dir/dl_rfft_s16.c.obj
[1699/2509] Building C object esp-idf/espressif__dl_fft/CMakeFiles/__idf_espressif__dl_fft.dir/dl_rfft_f32.c.obj
[1700/2509] Building C object esp-idf/espressif__esp_codec_dev/CMakeFiles/__idf_espressif__esp_codec_dev.dir/device/es8374/es8374.c.obj
[1701/2509] Building ASM object esp-idf/espressif__dl_fft/CMakeFiles/__idf_espressif__dl_fft.dir/base/isa/esp32s3/dl_fft2r_fc32_aes3.S.obj
[1702/2509] Building ASM object esp-idf/espressif__dl_fft/CMakeFiles/__idf_espressif__dl_fft.dir/base/isa/esp32s3/dl_fft4r_fc32_aes3.S.obj
[1703/2509] Building C object esp-idf/espressif__esp_codec_dev/CMakeFiles/__idf_espressif__esp_codec_dev.dir/device/es8389/es8389.c.obj
[1704/2509] Building ASM object esp-idf/espressif__dl_fft/CMakeFiles/__idf_espressif__dl_fft.dir/base/isa/esp32s3/dl_fft2r_sc16_aes3.S.obj
[1705/2509] Building C object esp-idf/espressif__button/CMakeFiles/__idf_espressif__button.dir/button_adc.c.obj
[1706/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/float/dsps_dotprod_f32_ae32.S.obj
[1707/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/float/dsps_dotprod_f32_m_ae32.S.obj
[1708/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/common/misc/aes3_tie_log.c.obj
[1709/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/float/dsps_dotprode_f32_ae32.S.obj
[1710/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/float/dsps_dotprode_f32_m_ae32.S.obj
[1711/2509] Building C object esp-idf/espressif__dl_fft/CMakeFiles/__idf_espressif__dl_fft.dir/base/dl_fft2r_fc32_ansi.c.obj
[1712/2509] Building CXX object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/common/misc/dsps_pwroftwo.cpp.obj
[1713/2509] Building C object esp-idf/espressif__dl_fft/CMakeFiles/__idf_espressif__dl_fft.dir/base/dl_fft4r_fc32_ansi.c.obj
[1714/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/float/dsps_dotprod_f32_aes3.S.obj
[1715/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/float/dsps_dotprod_f32_arp4.S.obj
[1716/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/float/dsps_dotprode_f32_ansi.c.obj
[1717/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/float/dsps_dotprode_f32_arp4.S.obj
[1718/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dsps_dotprod_s16_m_ae32.S.obj
[1719/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dsps_dotprod_s16_ae32.S.obj
[1720/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/float/dsps_dotprod_f32_ansi.c.obj
[1721/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dsps_dotprod_s16_arp4.S.obj
[1722/2509] Building C object esp-idf/espressif__dl_fft/CMakeFiles/__idf_espressif__dl_fft.dir/base/dl_fft_base.c.obj
[1723/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/float/dspi_dotprod_f32_ansi.c.obj
[1724/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_s16_ansi.c.obj
[1725/2509] Building C object esp-idf/espressif__dl_fft/CMakeFiles/__idf_espressif__dl_fft.dir/base/dl_fft2r_sc16_dif_ansi.c.obj
[1726/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dsps_dotprod_s16_ansi.c.obj
[1727/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_s8_ansi.c.obj
[1728/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_u8_ansi.c.obj
[1729/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/float/dspi_dotprod_off_f32_ansi.c.obj
[1730/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_u16_ansi.c.obj
[1731/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_off_u16_ansi.c.obj
[1732/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_s16_aes3.S.obj
[1733/2509] Building C object esp-idf/espressif__dl_fft/CMakeFiles/__idf_espressif__dl_fft.dir/base/dl_fft2r_sc16_ansi.c.obj
[1734/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_u16_aes3.S.obj
[1735/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_off_u16_aes3.S.obj
[1736/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_off_u8_ansi.c.obj
[1737/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_s8_aes3.S.obj
[1738/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_off_s16_aes3.S.obj
[1739/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_u8_aes3.S.obj
[1740/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_off_s8_ansi.c.obj
[1741/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_off_u8_aes3.S.obj
[1742/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_off_s16_ansi.c.obj
[1743/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_off_s8_aes3.S.obj
[1744/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_s8_arp4.S.obj
[1745/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_u8_arp4.S.obj
[1746/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_u16_arp4.S.obj
[1747/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_s16_arp4.S.obj
[1748/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_off_s16_arp4.S.obj
[1749/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_off_s8_arp4.S.obj
[1750/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_off_u16_arp4.S.obj
[1751/2509] Building C object esp-idf/espressif__button/CMakeFiles/__idf_espressif__button.dir/iot_button.c.obj
[1752/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dotprod/fixed/dspi_dotprod_off_u8_arp4.S.obj
[1753/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/mul/float/dspm_mult_4x4x1_f32_ae32.S.obj
[1754/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/mul/float/dspm_mult_f32_aes3.S.obj
[1755/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/mul/float/dspm_mult_4x4x4_f32_ae32.S.obj
[1756/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/mul/float/dspm_mult_3x3x1_f32_ae32.S.obj
[1757/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/mul/float/dspm_mult_3x3x3_f32_ae32.S.obj
[1758/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/mul/float/dspm_mult_f32_ae32.S.obj
[1759/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/mul/float/dspm_mult_f32_arp4.S.obj
[1760/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/mul/fixed/dspm_mult_s16_m_ae32_vector.S.obj
[1761/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/mul/float/dspm_mult_ex_f32_arp4.S.obj
[1762/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/mul/float/dspm_mult_ex_f32_aes3.S.obj
[1763/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/mul/float/dspm_mult_ex_f32_ae32.S.obj
[1764/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/mul/fixed/dspm_mult_s16_ae32.S.obj
[1765/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/mul/fixed/dspm_mult_s16_m_ae32.S.obj
[1766/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/mul/float/dspm_mult_ex_f32_ansi.c.obj
[1767/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/mul/fixed/dspm_mult_s16_arp4.S.obj
[1768/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/mul/fixed/dspm_mult_s16_aes3.S.obj
[1769/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/mul/float/dspm_mult_f32_ansi.c.obj
[1770/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/add/float/dspm_add_f32_ae32.S.obj
[1771/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/addc/float/dspm_addc_f32_ae32.S.obj
[1772/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/sub/float/dspm_sub_f32_ae32.S.obj
[1773/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/mulc/float/dspm_mulc_f32_ae32.S.obj
[1774/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/add/float/dspm_add_f32_ansi.c.obj
[1775/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/addc/float/dspm_addc_f32_ansi.c.obj
[1776/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/mul/fixed/dspm_mult_s16_ansi.c.obj
[1777/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/mulc/float/dspm_mulc_f32_ansi.c.obj
[1778/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/mulc/fixed/dsps_mulc_s16_ae32.S.obj
[1779/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/sub/float/dspm_sub_f32_ansi.c.obj
[1780/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/mulc/float/dsps_mulc_f32_ansi.c.obj
[1781/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/add/fixed/dsps_add_s16_ae32.S.obj
[1782/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/add/fixed/dsps_add_s16_aes3.S.obj
[1783/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/addc/float/dsps_addc_f32_ansi.c.obj
[1784/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/add/fixed/dsps_add_s8_aes3.S.obj
[1785/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/add/fixed/dsps_add_s16_ansi.c.obj
[1786/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/add/float/dsps_add_f32_ansi.c.obj
[1787/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/sub/fixed/dsps_sub_s16_ae32.S.obj
[1788/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/sub/fixed/dsps_sub_s16_aes3.S.obj
[1789/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/sub/fixed/dsps_sub_s8_aes3.S.obj
[1790/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/mulc/fixed/dsps_mulc_s16_ansi.c.obj
[1791/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/add/fixed/dsps_add_s8_ansi.c.obj
[1792/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/sub/float/dsps_sub_f32_ansi.c.obj
[1793/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/mul/fixed/dsps_mul_s16_ae32.S.obj
[1794/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/sub/fixed/dsps_sub_s8_ansi.c.obj
[1795/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/sub/fixed/dsps_sub_s16_ansi.c.obj
[1796/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/mul/fixed/dsps_mul_s16_aes3.S.obj
[1797/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/mul/fixed/dsps_mul_s16_ansi.c.obj
[1798/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/mul/float/dsps_mul_f32_ansi.c.obj
[1799/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/mulc/float/dsps_mulc_f32_ae32.S.obj
[1800/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/addc/float/dsps_addc_f32_ae32.S.obj
[1801/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/add/float/dsps_add_f32_ae32.S.obj
[1802/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/mul/fixed/dsps_mul_s8_aes3.S.obj
[1803/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/sub/float/dsps_sub_f32_ae32.S.obj
[1804/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/mul/fixed/dsps_mul_s8_ansi.c.obj
[1805/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/mul/float/dsps_mul_f32_ae32.S.obj
[1806/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fft/float/dsps_fft2r_fc32_ae32_.S.obj
[1807/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fft/float/dsps_fft2r_fc32_aes3_.S.obj
[1808/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fft/float/dsps_fft2r_fc32_arp4.S.obj
[1809/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fft/float/dsps_bit_rev_lookup_fc32_aes3.S.obj
[1810/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fft/float/dsps_fft4r_fc32_aes3_.S.obj
[1811/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/math/sqrt/float/dsps_sqrt_f32_ansi.c.obj
[1812/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fft/float/dsps_fft4r_fc32_ae32_.S.obj
[1813/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fft/float/dsps_fft4r_fc32_arp4.S.obj
[1814/2509] Building C object esp-idf/lvgl__lvgl/CMakeFiles/__idf_lvgl__lvgl.dir/src/libs/lodepng/lodepng.c.obj
[1815/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fft/float/dsps_fft2r_fc32_ae32.c.obj
[1816/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fft/fixed/dsps_fft2r_sc16_ae32.S.obj
[1817/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fft/fixed/dsps_fft2r_sc16_aes3.S.obj
[1818/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fft/float/dsps_fft2r_bitrev_tables_fc32.c.obj
[1819/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fft/float/dsps_fft4r_fc32_ae32.c.obj
[1820/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fft/fixed/dsps_fft2r_sc16_arp4.S.obj
[1821/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fft/float/dsps_fft4r_bitrev_tables_fc32.c.obj
[1822/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/support/misc/dsps_d_gen.c.obj
[1823/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/support/misc/dsps_h_gen.c.obj
[1824/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fft/float/dsps_fft4r_fc32_ansi.c.obj
[1825/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fft/float/dsps_fft2r_fc32_ansi.c.obj
[1826/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dct/float/dsps_dstiv_f32.c.obj
[1827/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/support/cplx_gen/dsps_cplx_gen.S.obj
[1828/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dct/float/dsps_dctiv_f32.c.obj
[1829/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/dct/float/dsps_dct_f32.c.obj
[1830/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/support/misc/dsps_tone_gen.c.obj
[1831/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/support/cplx_gen/dsps_cplx_gen.c.obj
[1832/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fft/fixed/dsps_fft2r_sc16_ansi.c.obj
[1833/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/support/mem/esp32s3/dsps_memset_aes3.S.obj
[1834/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/support/mem/esp32s3/dsps_memcpy_aes3.S.obj
[1835/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/windows/blackman/float/dsps_wind_blackman_f32.c.obj
[1836/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/windows/hann/float/dsps_wind_hann_f32.c.obj
[1837/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/windows/nuttall/float/dsps_wind_nuttall_f32.c.obj
[1838/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/windows/blackman_nuttall/float/dsps_wind_blackman_nuttall_f32.c.obj
[1839/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/windows/blackman_harris/float/dsps_wind_blackman_harris_f32.c.obj
[1840/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/conv/float/dsps_conv_f32_ae32.S.obj
[1841/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/conv/float/dsps_corr_f32_ae32.S.obj
[1842/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/windows/flat_top/float/dsps_wind_flat_top_f32.c.obj
[1843/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/conv/float/dsps_corr_f32_ansi.c.obj
[1844/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/support/cplx_gen/dsps_cplx_gen_init.c.obj
[1845/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/conv/float/dsps_ccorr_f32_ae32.S.obj
[1846/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/iir/biquad/dsps_biquad_sf32_ae32.S.obj
[1847/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/iir/biquad/dsps_biquad_f32_ae32.S.obj
[1848/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/iir/biquad/dsps_biquad_f32_aes3.S.obj
[1849/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/iir/biquad/dsps_biquad_f32_arp4.S.obj
[1850/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/iir/biquad/dsps_biquad_sf32_arp4.S.obj
[1851/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/conv/float/dsps_conv_f32_ansi.c.obj
[1852/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/iir/biquad/dsps_biquad_f32_ansi.c.obj
[1853/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fir/float/dsps_fir_f32_ae32.S.obj
[1854/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/iir/biquad/dsps_biquad_sf32_ansi.c.obj
[1855/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fir/float/dsps_fir_f32_aes3.S.obj
[1856/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/conv/float/dsps_ccorr_f32_ansi.c.obj
[1857/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fir/float/dsps_fird_f32_ae32.S.obj
[1858/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fir/float/dsps_fird_f32_aes3.S.obj
[1859/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fir/float/dsps_fird_f32_arp4.S.obj
[1860/2509] Building CXX object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/support/sfdr/float/dsps_sfdr_f32.cpp.obj
[1861/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fir/float/dsps_fir_init_f32.c.obj
[1862/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/iir/biquad/dsps_biquad_gen_f32.c.obj
[1863/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fir/float/dsps_fird_init_f32.c.obj
[1864/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/conv/float/dspi_conv_f32_ansi.c.obj
[1865/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fir/float/dsps_fir_f32_ansi.c.obj
[1866/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fir/fixed/dsps_fird_s16_ae32.S.obj
[1867/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fir/float/dsps_fird_f32_ansi.c.obj
[1868/2509] Building CXX object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/support/snr/float/dsps_snr_f32.cpp.obj
[1869/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fir/fixed/dsps_fir_s16_m_ae32.S.obj
[1870/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fir/fixed/dsps_fird_s16_aes3.S.obj
[1871/2509] Building ASM object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fir/fixed/dsps_fird_s16_arp4.S.obj
[1872/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fir/fixed/dsps_fird_s16_ansi.c.obj
[1873/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fir/fixed/dsps_fird_init_s16.c.obj
[1874/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fir/fixed/dsps_firmr_init_s16.c.obj
[1875/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fir/float/dsps_firmr_init_f32.c.obj
[1876/2509] Building CXX object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/support/view/dsps_view.cpp.obj
[1877/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fir/float/dsps_firmr_f32_ansi.c.obj
[1878/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fir/resampler/dsps_resampler_mr.c.obj
[1879/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fir/fixed/dsps_firmr_s16_ansi.c.obj
[1880/2509] Building C object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/fir/resampler/dsps_resampler_ph.c.obj
[1881/2509] Linking C static library esp-idf/espressif__esp_sccb_intf/libespressif__esp_sccb_intf.a
[1882/2509] Building C object esp-idf/espressif__esp_io_expander/CMakeFiles/__idf_espressif__esp_io_expander.dir/esp_io_expander_gpio_wrapper_weak.c.obj
[1883/2509] Building C object esp-idf/espressif__esp_jpeg/CMakeFiles/__idf_espressif__esp_jpeg.dir/jpeg_decoder.c.obj
[1884/2509] Building C object esp-idf/espressif__esp_cam_sensor/CMakeFiles/__idf_espressif__esp_cam_sensor.dir/src/esp_cam_sensor_xclk.c.obj
[1885/2509] Building C object esp-idf/espressif__esp_cam_sensor/CMakeFiles/__idf_espressif__esp_cam_sensor.dir/src/esp_cam_sensor.c.obj
[1886/2509] Building C object esp-idf/espressif__esp_io_expander/CMakeFiles/__idf_espressif__esp_io_expander.dir/esp_io_expander_gpio_wrapper.c.obj
[1887/2509] Building C object esp-idf/espressif__esp_cam_sensor/CMakeFiles/__idf_espressif__esp_cam_sensor.dir/src/esp_cam_motor.c.obj
[1888/2509] Building C object esp-idf/espressif__esp_io_expander/CMakeFiles/__idf_espressif__esp_io_expander.dir/esp_io_expander.c.obj
[1889/2509] Building C object esp-idf/espressif__esp_lcd_touch/CMakeFiles/__idf_espressif__esp_lcd_touch.dir/esp_lcd_touch.c.obj
[1890/2509] Building C object esp-idf/espressif__esp_cam_sensor/CMakeFiles/__idf_espressif__esp_cam_sensor.dir/sensors/gc0308/gc0308.c.obj
[1891/2509] Building C object esp-idf/espressif__usb_host_uvc/CMakeFiles/__idf_espressif__usb_host_uvc.dir/uvc_frame.c.obj
[1892/2509] Building C object esp-idf/espressif__usb_host_uvc/CMakeFiles/__idf_espressif__usb_host_uvc.dir/uvc_descriptor_printing.c.obj
[1893/2509] Building C object esp-idf/espressif__esp_cam_sensor/CMakeFiles/__idf_espressif__esp_cam_sensor.dir/src/driver_cam/esp_cam_ctlr_spi_cam.c.obj
[1894/2509] Building C object esp-idf/espressif__esp_cam_sensor/CMakeFiles/__idf_espressif__esp_cam_sensor.dir/src/driver_spi/spi_slave.c.obj
[1895/2509] Building C object esp-idf/espressif__usb_host_uvc/CMakeFiles/__idf_espressif__usb_host_uvc.dir/uvc_descriptor_parsing.c.obj
[1896/2509] Building C object esp-idf/espressif__usb_host_uvc/CMakeFiles/__idf_espressif__usb_host_uvc.dir/uvc_isoc.c.obj
[1897/2509] Building C object esp-idf/espressif__usb_host_uvc/CMakeFiles/__idf_espressif__usb_host_uvc.dir/uvc_bulk.c.obj
[1898/2509] Building C object esp-idf/espressif__usb_host_uvc/CMakeFiles/__idf_espressif__usb_host_uvc.dir/uvc_control.c.obj
[1899/2509] Building C object esp-idf/espressif__iot_usbh_cdc/CMakeFiles/__idf_espressif__iot_usbh_cdc.dir/iot_usbh_descriptor.c.obj
[1900/2509] Building C object esp-idf/espressif__iot_usbh_cdc/CMakeFiles/__idf_espressif__iot_usbh_cdc.dir/usbh_helper.c.obj
[1901/2509] Building C object esp-idf/espressif__knob/CMakeFiles/__idf_espressif__knob.dir/knob_gpio.c.obj
[1902/2509] Building C object esp-idf/espressif__knob/CMakeFiles/__idf_espressif__knob.dir/knob_rtc.c.obj
[1903/2509] Building C object esp-idf/espressif__usb_host_uvc/CMakeFiles/__idf_espressif__usb_host_uvc.dir/uvc_host.c.obj
[1904/2509] Linking C static library esp-idf/protocomm/libprotocomm.a
[1905/2509] Linking C static library esp-idf/mqtt/libmqtt.a
[1906/2509] Linking C static library esp-idf/fatfs/libfatfs.a
[1907/2509] Linking C static library esp-idf/spiffs/libspiffs.a
[1908/2509] Building C object esp-idf/espressif__knob/CMakeFiles/__idf_espressif__knob.dir/iot_knob.c.obj
[1909/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/debug/src/espnow_log.c.obj
[1910/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/control/src/espnow_ctrl.c.obj
[1911/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/debug/src/commands/pcap/pcap.c.obj
[1912/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/debug/src/espnow_console.c.obj
[1913/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/debug/src/espnow_log_flash.c.obj
[1914/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/debug/src/sdcard/sdcard.c.obj
[1915/2509] Building C object esp-idf/espressif__iot_usbh_cdc/CMakeFiles/__idf_espressif__iot_usbh_cdc.dir/iot_usbh_cdc.c.obj
[1916/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/debug/src/commands/cmd_sdcard.c.obj
[1917/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/debug/src/commands/cmd_peripherals.c.obj
[1918/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/debug/src/commands/cmd_wifi_sniffer.c.obj
[1919/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/debug/src/commands/espnow_commands.c.obj
[1920/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/debug/src/commands/cmd_system.c.obj
[1921/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/debug/src/commands/cmd_wifi.c.obj
[1922/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/espnow/src/espnow_group.c.obj
[1923/2509] Building CXX object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/matrix/mat/mat.cpp.obj
[1924/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/debug/src/commands/cmd_iperf.c.obj
[1925/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/security/src/espnow_security.c.obj
[1926/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/provisioning/src/espnow_prov.c.obj
[1927/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/security/src/espnow_security_responder.c.obj
[1928/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/ota/espnow_ota_responder.c.obj
[1929/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/ota/espnow_ota_initiator.c.obj
[1930/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/utils/src/espnow_mem.c.obj
/Users/cobain/robot-build/fw/components/esp-now/src/utils/src/espnow_mem.c: In function 'espnow_mem_print_task':
/Users/cobain/robot-build/fw/components/esp-now/src/utils/src/espnow_mem.c:170:2: warning: #warning configTASKLIST_INCLUDE_COREID must also be set to 1 in FreeRTOSConfig.h to use xCoreID. [-Wcpp]
  170 | #warning configTASKLIST_INCLUDE_COREID must also be set to 1 in FreeRTOSConfig.h to use xCoreID.
      |  ^~~~~~~
[1931/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/debug/src/commands/cmd_espnow.c.obj
[1932/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/security/src/espnow_security_initiator.c.obj
[1933/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/utils/src/espnow_storage.c.obj
[1934/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/utils/src/espnow_reboot.c.obj
[1935/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/utils/src/espnow_timesync.c.obj
[1936/2509] Building CXX object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/kalman/ekf_imu13states/ekf_imu13states.cpp.obj
[1937/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/security/src/protocomm/security/client_security1.c.obj
[1938/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/espnow/src/espnow.c.obj
[1939/2509] Building C object esp-idf/esp-now/CMakeFiles/__idf_esp-now.dir/src/utils/src/espnow_utils.c.obj
[1940/2509] Building CXX object esp-idf/espressif__esp-dsp/CMakeFiles/__idf_espressif__esp-dsp.dir/modules/kalman/ekf/common/ekf.cpp.obj
[1941/2509] Building CXX object esp-idf/mooncake/CMakeFiles/__idf_mooncake.dir/src/ability/ui_ability.cpp.obj
[1942/2509] Building CXX object esp-idf/mooncake/CMakeFiles/__idf_mooncake.dir/src/ability/app_ability.cpp.obj
[1943/2509] Building CXX object esp-idf/mooncake/CMakeFiles/__idf_mooncake.dir/src/ability/worker_ability.cpp.obj
[1944/2509] Building CXX object esp-idf/smooth_ui_toolkit/CMakeFiles/__idf_smooth_ui_toolkit.dir/src/core/animation/generators/easing/easing.cpp.obj
[1945/2509] Building CXX object esp-idf/smooth_ui_toolkit/CMakeFiles/__idf_smooth_ui_toolkit.dir/src/core/animation/generators/spring/spring.cpp.obj
[1946/2509] Building CXX object esp-idf/mooncake/CMakeFiles/__idf_mooncake.dir/src/ability_manager/ability_api_wrapping.cpp.obj
[1947/2509] Building CXX object esp-idf/mooncake/CMakeFiles/__idf_mooncake.dir/src/ability_manager/ability_manager.cpp.obj
[1948/2509] Building CXX object esp-idf/smooth_ui_toolkit/CMakeFiles/__idf_smooth_ui_toolkit.dir/src/core/animation/animate_value/animate_value.cpp.obj
[1949/2509] Building CXX object esp-idf/mooncake/CMakeFiles/__idf_mooncake.dir/src/mooncake.cpp.obj
[1950/2509] Building CXX object esp-idf/smooth_ui_toolkit/CMakeFiles/__idf_smooth_ui_toolkit.dir/src/core/animation/animate/animate.cpp.obj
[1951/2509] Building CXX object esp-idf/smooth_ui_toolkit/CMakeFiles/__idf_smooth_ui_toolkit.dir/src/core/animation/animate_vector/animate_vector2.cpp.obj
[1952/2509] Building CXX object esp-idf/smooth_ui_toolkit/CMakeFiles/__idf_smooth_ui_toolkit.dir/src/core/animation/animate_vector/animate_vector4.cpp.obj
[1953/2509] Building CXX object esp-idf/smooth_ui_toolkit/CMakeFiles/__idf_smooth_ui_toolkit.dir/src/core/easing/ease.cpp.obj
[1954/2509] Building CXX object esp-idf/smooth_ui_toolkit/CMakeFiles/__idf_smooth_ui_toolkit.dir/src/core/easing/cubic_bezier/cubic_bezier.cpp.obj
[1955/2509] Linking C static library esp-idf/78__uart-uhci/lib78__uart-uhci.a
[1956/2509] Building CXX object esp-idf/mooncake_log/CMakeFiles/__idf_mooncake_log.dir/src/mooncake_log.cpp.obj
[1957/2509] Building CXX object esp-idf/smooth_ui_toolkit/CMakeFiles/__idf_smooth_ui_toolkit.dir/src/core/animation/sequence/animate_sequence.cpp.obj
[1958/2509] Building CXX object esp-idf/smooth_ui_toolkit/CMakeFiles/__idf_smooth_ui_toolkit.dir/src/core/color/color.cpp.obj
[1959/2509] Building CXX object esp-idf/78__esp-ml307/CMakeFiles/__idf_78__esp-ml307.dir/src/esp/esp_ssl.cc.obj
[1960/2509] Building CXX object esp-idf/smooth_ui_toolkit/CMakeFiles/__idf_smooth_ui_toolkit.dir/src/games/core/game_object.cpp.obj
[1961/2509] Building CXX object esp-idf/smooth_ui_toolkit/CMakeFiles/__idf_smooth_ui_toolkit.dir/src/core/hal/hal.cpp.obj
[1962/2509] Building CXX object esp-idf/78__esp-ml307/CMakeFiles/__idf_78__esp-ml307.dir/src/esp/esp_tcp.cc.obj
[1963/2509] Building CXX object esp-idf/smooth_ui_toolkit/CMakeFiles/__idf_smooth_ui_toolkit.dir/src/widget/select_menu/smooth_selector.cpp.obj
[1964/2509] Building CXX object esp-idf/smooth_ui_toolkit/CMakeFiles/__idf_smooth_ui_toolkit.dir/src/widget/select_menu/smooth_options.cpp.obj
[1965/2509] Building CXX object esp-idf/78__esp-ml307/CMakeFiles/__idf_78__esp-ml307.dir/src/esp/esp_mqtt.cc.obj
[1966/2509] Building CXX object esp-idf/smooth_ui_toolkit/CMakeFiles/__idf_smooth_ui_toolkit.dir/src/games/core/system/area_system.cpp.obj
[1967/2509] Building CXX object esp-idf/78__esp-ml307/CMakeFiles/__idf_78__esp-ml307.dir/src/esp/esp_udp.cc.obj
[1968/2509] Building CXX object esp-idf/78__esp-ml307/CMakeFiles/__idf_78__esp-ml307.dir/src/esp/esp_network.cc.obj
[1969/2509] Building CXX object esp-idf/mooncake_log/CMakeFiles/__idf_mooncake_log.dir/src/fmt/format.cc.obj
[1970/2509] Building CXX object esp-idf/78__esp-ml307/CMakeFiles/__idf_78__esp-ml307.dir/src/web_socket.cc.obj
[1971/2509] Building CXX object esp-idf/78__esp-ml307/CMakeFiles/__idf_78__esp-ml307.dir/src/ec801e/ec801e_tcp.cc.obj
[1972/2509] Building CXX object esp-idf/78__esp-ml307/CMakeFiles/__idf_78__esp-ml307.dir/src/ec801e/ec801e_ssl.cc.obj
[1973/2509] Building CXX object esp-idf/78__esp-ml307/CMakeFiles/__idf_78__esp-ml307.dir/src/at_modem.cc.obj
[1974/2509] Building CXX object esp-idf/78__esp-ml307/CMakeFiles/__idf_78__esp-ml307.dir/src/ec801e/ec801e_udp.cc.obj
[1975/2509] Building CXX object esp-idf/78__esp-ml307/CMakeFiles/__idf_78__esp-ml307.dir/src/ec801e/ec801e_at_modem.cc.obj
[1976/2509] Building CXX object esp-idf/78__esp-ml307/CMakeFiles/__idf_78__esp-ml307.dir/src/at_uart.cc.obj
[1977/2509] Building CXX object esp-idf/78__esp-ml307/CMakeFiles/__idf_78__esp-ml307.dir/src/ec801e/ec801e_mqtt.cc.obj
[1978/2509] Building CXX object esp-idf/78__esp-ml307/CMakeFiles/__idf_78__esp-ml307.dir/src/ml307/ml307_at_modem.cc.obj
[1979/2509] Building CXX object esp-idf/78__esp-ml307/CMakeFiles/__idf_78__esp-ml307.dir/src/http_client.cc.obj
[1980/2509] Building CXX object esp-idf/78__esp-wifi-connect/CMakeFiles/__idf_78__esp-wifi-connect.dir/dns_server.cc.obj
[1981/2509] Building CXX object esp-idf/78__esp-ml307/CMakeFiles/__idf_78__esp-ml307.dir/src/ml307/ml307_ssl.cc.obj
[1982/2509] Building ASM object esp-idf/78__esp-wifi-connect/CMakeFiles/__idf_78__esp-wifi-connect.dir/__/__/wifi_configuration.html.S.obj
[1983/2509] Building CXX object esp-idf/78__esp-wifi-connect/CMakeFiles/__idf_78__esp-wifi-connect.dir/ssid_manager.cc.obj
[1984/2509] Building ASM object esp-idf/78__esp-wifi-connect/CMakeFiles/__idf_78__esp-wifi-connect.dir/__/__/wifi_configuration_done.html.S.obj
[1985/2509] Linking C static library esp-idf/espressif__iot_eth/libespressif__iot_eth.a
[1986/2509] Building CXX object esp-idf/78__esp-wifi-connect/CMakeFiles/__idf_78__esp-wifi-connect.dir/wifi_station.cc.obj
[1987/2509] Building C object esp-idf/78__esp_lcd_nv3023/CMakeFiles/__idf_78__esp_lcd_nv3023.dir/esp_lcd_nv3023.c.obj
[1988/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/font_awesome.c.obj
[1989/2509] Building CXX object esp-idf/78__esp-ml307/CMakeFiles/__idf_78__esp-ml307.dir/src/ml307/ml307_tcp.cc.obj
[1990/2509] Building CXX object esp-idf/78__esp-ml307/CMakeFiles/__idf_78__esp-ml307.dir/src/ml307/ml307_udp.cc.obj
[1991/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/cbin_font.c.obj
[1992/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/font_awesome_14_1.c.obj
[1993/2509] Building CXX object esp-idf/78__esp-ml307/CMakeFiles/__idf_78__esp-ml307.dir/src/ml307/ml307_mqtt.cc.obj
[1994/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/font_awesome_16_4.c.obj
[1995/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/font_awesome_20_4.c.obj
[1996/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/font_awesome_30_1.c.obj
[1997/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/font_emoji_32.c.obj
[1998/2509] Building CXX object esp-idf/78__esp-wifi-connect/CMakeFiles/__idf_78__esp-wifi-connect.dir/wifi_manager.cc.obj
[1999/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/font_awesome_30_4.c.obj
[2000/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/font_emoji_64.c.obj
[2001/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/font_noto_basic_14_1.c.obj
[2002/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/font_noto_basic_16_4.c.obj
[2003/2509] Building CXX object esp-idf/78__esp-wifi-connect/CMakeFiles/__idf_78__esp-wifi-connect.dir/wifi_configuration_ap.cc.obj
[2004/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/font_noto_basic_20_4.c.obj
[2005/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/font_puhui_basic_14_1.c.obj
[2006/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/font_noto_basic_30_4.c.obj
[2007/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/font_puhui_14_1.c.obj
[2008/2509] Building CXX object esp-idf/78__esp-ml307/CMakeFiles/__idf_78__esp-ml307.dir/src/ml307/ml307_http.cc.obj
[2009/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/font_puhui_basic_16_4.c.obj
[2010/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/font_puhui_test_20_4.c.obj
[2011/2509] Linking C static library esp-idf/lvgl__lvgl/liblvgl__lvgl.a
[2012/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f602_32.c.obj
[2013/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/font_puhui_basic_20_4.c.obj
[2014/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f602_64.c.obj
[2015/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f606_32.c.obj
[2016/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f606_64.c.obj
[2017/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/font_puhui_basic_30_4.c.obj
[2018/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f609_32.c.obj
[2019/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f60c_32.c.obj
[2020/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f609_64.c.obj
[2021/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f60c_64.c.obj
[2022/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f60d_32.c.obj
[2023/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f60e_32.c.obj
[2024/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f60d_64.c.obj
[2025/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f60f_32.c.obj
[2026/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f60e_64.c.obj
[2027/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/font_puhui_16_4.c.obj
[2028/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f60f_64.c.obj
[2029/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f614_32.c.obj
[2030/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f618_32.c.obj
[2031/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f614_64.c.obj
[2032/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f61c_32.c.obj
[2033/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f618_64.c.obj
[2034/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f61c_64.c.obj
[2035/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f620_32.c.obj
[2036/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f620_64.c.obj
[2037/2509] Building CXX object esp-idf/78__uart-eth-modem/CMakeFiles/__idf_78__uart-eth-modem.dir/src/uart_eth_modem.cc.obj
[2038/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f62d_32.c.obj
[2039/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f62d_64.c.obj
[2040/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f62f_32.c.obj
[2041/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f62f_64.c.obj
[2042/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f631_32.c.obj
[2043/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f633_32.c.obj
[2044/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f631_64.c.obj
[2045/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f634_32.c.obj
[2046/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f633_64.c.obj
[2047/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f634_64.c.obj
[2048/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f636_32.c.obj
[2049/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/font_puhui_20_4.c.obj
[2050/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f636_64.c.obj
[2051/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f642_32.c.obj
[2052/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f644_32.c.obj
[2053/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f642_64.c.obj
[2054/2509] Building C object esp-idf/espressif__adc_battery_estimation/CMakeFiles/__idf_espressif__adc_battery_estimation.dir/adc_battery_estimation.c.obj
[2055/2509] Linking C static library esp-idf/espressif__esp_codec_dev/libespressif__esp_codec_dev.a
[2056/2509] Linking C static library esp-idf/espressif__button/libespressif__button.a
[2057/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f644_64.c.obj
[2058/2509] Linking C static library esp-idf/espressif__dl_fft/libespressif__dl_fft.a
[2059/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f914_32.c.obj
[2060/2509] Building C object esp-idf/espressif__esp-sr/CMakeFiles/__idf_espressif__esp-sr.dir/src/esp_sr_debug.c.obj
[2061/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f914_64.c.obj
[2062/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f924_32.c.obj
[2063/2509] Linking C static library esp-idf/espressif__esp_jpeg/libespressif__esp_jpeg.a
[2064/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/conversions/yuv.c.obj
[2065/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/emoji/emoji_1f924_64.c.obj
[2066/2509] Building C object esp-idf/espressif__esp-sr/CMakeFiles/__idf_espressif__esp-sr.dir/src/esp_process_sdkconfig.c.obj
[2067/2509] Building C object esp-idf/espressif__adc_mic/CMakeFiles/__idf_espressif__adc_mic.dir/adc_mic.c.obj
[2068/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/driver/esp_camera_af.c.obj
[2069/2509] Building C object esp-idf/espressif__esp-sr/CMakeFiles/__idf_espressif__esp-sr.dir/src/esp_mn_speech_commands.c.obj
[2070/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/conversions/to_bmp.c.obj
[2071/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/driver/sensor.c.obj
[2072/2509] Building C object esp-idf/espressif__i2c_bus/CMakeFiles/__idf_espressif__i2c_bus.dir/i2c_bus_v2.c.obj
[2073/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/driver/esp_camera.c.obj
[2074/2509] Building CXX object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/conversions/to_jpg.cpp.obj
[2075/2509] Building C object esp-idf/espressif__esp-sr/CMakeFiles/__idf_espressif__esp-sr.dir/src/model_path.c.obj
[2076/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/sensors/ov5640_af.c.obj
[2077/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/sensors/ov7670.c.obj
[2078/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/sensors/ov7725.c.obj
[2079/2509] Building CXX object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/conversions/jpge.cpp.obj
[2080/2509] Linking C static library esp-idf/espressif__esp-dsp/libespressif__esp-dsp.a
[2081/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/sensors/ov2640.c.obj
[2082/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/driver/cam_hal.c.obj
[2083/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/sensors/gc032a.c.obj
[2084/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/sensors/nt99141.c.obj
[2085/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/sensors/gc2145.c.obj
[2086/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/sensors/bf20a6.c.obj
[2087/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/sensors/bf3005.c.obj
[2088/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/sensors/gc0308.c.obj
[2089/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/sensors/ov3660.c.obj
[2090/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/sensors/ov5640.c.obj
[2091/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/sensors/sc101iot.c.obj
[2092/2509] Building C object esp-idf/espressif__esp_audio_codec/CMakeFiles/__idf_espressif__esp_audio_codec.dir/src/audio_encoder_reg.c.obj
[2093/2509] Building C object esp-idf/espressif__esp_audio_codec/CMakeFiles/__idf_espressif__esp_audio_codec.dir/src/audio_decoder_reg.c.obj
[2094/2509] Building C object esp-idf/espressif__esp_audio_codec/CMakeFiles/__idf_espressif__esp_audio_codec.dir/src/simple_decoder_reg.c.obj
[2095/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/sensors/sc030iot.c.obj
[2096/2509] Linking C static library esp-idf/espressif__esp_io_expander/libespressif__esp_io_expander.a
[2097/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/sensors/mega_ccm.c.obj
[2098/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/sensors/sc031gs.c.obj
[2099/2509] Linking C static library esp-idf/espressif__esp_lcd_touch/libespressif__esp_lcd_touch.a
[2100/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/driver/sccb-ng.c.obj
[2101/2509] Building C object esp-idf/espressif__esp_lcd_co5300/CMakeFiles/__idf_espressif__esp_lcd_co5300.dir/esp_lcd_co5300.c.obj
[2102/2509] Building C object esp-idf/espressif__esp_io_expander_tca9554/CMakeFiles/__idf_espressif__esp_io_expander_tca9554.dir/esp_io_expander_tca9554.c.obj
[2103/2509] Building C object esp-idf/espressif__esp_io_expander_tca95xx_16bit/CMakeFiles/__idf_espressif__esp_io_expander_tca95xx_16bit.dir/esp_io_expander_tca95xx_16bit.c.obj
[2104/2509] Building C object esp-idf/espressif__esp_lcd_co5300/CMakeFiles/__idf_espressif__esp_lcd_co5300.dir/esp_lcd_co5300_mipi.c.obj
[2105/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/sensors/hm0360.c.obj
[2106/2509] Building C object esp-idf/espressif__esp_lcd_st7701/CMakeFiles/__idf_espressif__esp_lcd_st7701.dir/esp_lcd_st7701.c.obj
[2107/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/sensors/hm1055.c.obj
[2108/2509] Building C object esp-idf/espressif__esp32-camera/CMakeFiles/__idf_espressif__esp32-camera.dir/target/esp32s3/ll_cam.c.obj
[2109/2509] Building C object esp-idf/espressif__esp_lcd_st7701/CMakeFiles/__idf_espressif__esp_lcd_st7701.dir/esp_lcd_st7701_mipi.c.obj
[2110/2509] Building C object esp-idf/espressif__esp_lcd_co5300/CMakeFiles/__idf_espressif__esp_lcd_co5300.dir/esp_lcd_co5300_spi.c.obj
[2111/2509] Building C object esp-idf/espressif__esp_lcd_gc9a01/CMakeFiles/__idf_espressif__esp_lcd_gc9a01.dir/esp_lcd_gc9a01.c.obj
[2112/2509] Building C object esp-idf/espressif__esp_lcd_st7796/CMakeFiles/__idf_espressif__esp_lcd_st7796.dir/esp_lcd_st7796.c.obj
[2113/2509] Building C object esp-idf/espressif__esp_lcd_ili9341/CMakeFiles/__idf_espressif__esp_lcd_ili9341.dir/esp_lcd_ili9341.c.obj
[2114/2509] Building C object esp-idf/espressif__esp_lcd_axs15231b/CMakeFiles/__idf_espressif__esp_lcd_axs15231b.dir/esp_lcd_axs15231b.c.obj
[2115/2509] Building C object esp-idf/espressif__esp_lcd_spd2010/CMakeFiles/__idf_espressif__esp_lcd_spd2010.dir/esp_lcd_spd2010.c.obj
[2116/2509] Building C object esp-idf/espressif__esp_lcd_panel_io_additions/CMakeFiles/__idf_espressif__esp_lcd_panel_io_additions.dir/esp_lcd_panel_io_3wire_spi.c.obj
[2117/2509] Building C object esp-idf/espressif__esp_lcd_st7701/CMakeFiles/__idf_espressif__esp_lcd_st7701.dir/esp_lcd_st7701_rgb.c.obj
[2118/2509] Building C object esp-idf/espressif__esp_lcd_st7796/CMakeFiles/__idf_espressif__esp_lcd_st7796.dir/esp_lcd_st7796_general.c.obj
[2119/2509] Building C object esp-idf/espressif__esp_lcd_st77916/CMakeFiles/__idf_espressif__esp_lcd_st77916.dir/esp_lcd_st77916.c.obj
[2120/2509] Building C object esp-idf/espressif__esp_lcd_touch_cst816s/CMakeFiles/__idf_espressif__esp_lcd_touch_cst816s.dir/esp_lcd_touch_cst816s.c.obj
[2121/2509] Building C object esp-idf/espressif__esp_lcd_touch_ft5x06/CMakeFiles/__idf_espressif__esp_lcd_touch_ft5x06.dir/esp_lcd_touch_ft5x06.c.obj
[2122/2509] Building C object esp-idf/espressif__esp_lcd_touch_st7123/CMakeFiles/__idf_espressif__esp_lcd_touch_st7123.dir/esp_lcd_touch_st7123.c.obj
[2123/2509] Building C object esp-idf/espressif__esp_lcd_touch_gt1151/CMakeFiles/__idf_espressif__esp_lcd_touch_gt1151.dir/esp_lcd_touch_gt1151.c.obj
[2124/2509] Linking C static library esp-idf/espressif__esp_cam_sensor/libespressif__esp_cam_sensor.a
[2125/2509] Building C object esp-idf/espressif__esp_lcd_touch_gt911/CMakeFiles/__idf_espressif__esp_lcd_touch_gt911.dir/esp_lcd_touch_gt911.c.obj
[2126/2509] Linking C static library esp-idf/espressif__usb_host_uvc/libespressif__usb_host_uvc.a
[2127/2509] Building C object esp-idf/espressif__esp_video/CMakeFiles/__idf_espressif__esp_video.dir/src/esp_video_buffer.c.obj
[2128/2509] Building C object esp-idf/espressif__esp_mmap_assets/CMakeFiles/__idf_espressif__esp_mmap_assets.dir/esp_mmap_assets.c.obj
[2129/2509] Building C object esp-idf/espressif__esp_video/CMakeFiles/__idf_espressif__esp_video.dir/src/esp_video_ioctl.c.obj
[2130/2509] Building C object esp-idf/espressif__esp_video/CMakeFiles/__idf_espressif__esp_video.dir/src/esp_video_init.c.obj
[2131/2509] Building C object esp-idf/espressif__esp_video/CMakeFiles/__idf_espressif__esp_video.dir/src/esp_video_mman.c.obj
[2132/2509] Building C object esp-idf/espressif__esp_lvgl_port/CMakeFiles/lvgl_port_lib.dir/src/lvgl9/esp_lvgl_port_button.c.obj
[2133/2509] Building C object esp-idf/espressif__esp_lvgl_port/CMakeFiles/lvgl_port_lib.dir/src/lvgl9/esp_lvgl_port.c.obj
[2134/2509] Building C object esp-idf/espressif__esp_lvgl_port/CMakeFiles/lvgl_port_lib.dir/src/lvgl9/esp_lvgl_port_knob.c.obj
[2135/2509] Building C object esp-idf/espressif__esp_lvgl_port/CMakeFiles/lvgl_port_lib.dir/src/lvgl9/esp_lvgl_port_touch.c.obj
[2136/2509] Linking C static library esp-idf/espressif__iot_usbh_cdc/libespressif__iot_usbh_cdc.a
[2137/2509] Linking C static library esp-idf/espressif__knob/libespressif__knob.a
[2138/2509] Building C object esp-idf/espressif__iot_usbh_rndis/CMakeFiles/__idf_espressif__iot_usbh_rndis.dir/iot_usbh_rndis_descriptor.c.obj
[2139/2509] Building C object esp-idf/espressif__esp_video/CMakeFiles/__idf_espressif__esp_video.dir/src/esp_video_vfs.c.obj
[2140/2509] Building C object esp-idf/espressif__esp_lvgl_port/CMakeFiles/lvgl_port_lib.dir/src/lvgl9/esp_lvgl_port_disp.c.obj
[2141/2509] Building C object esp-idf/espressif__led_strip/CMakeFiles/__idf_espressif__led_strip.dir/src/led_strip_api.c.obj
[2142/2509] Building C object esp-idf/espressif__led_strip/CMakeFiles/__idf_espressif__led_strip.dir/src/led_strip_rmt_encoder.c.obj
[2143/2509] Building C object esp-idf/espressif__esp_video/CMakeFiles/__idf_espressif__esp_video.dir/src/esp_video_cam.c.obj
[2144/2509] Building C object esp-idf/espressif__esp_video/CMakeFiles/__idf_espressif__esp_video.dir/src/device/esp_video_dvp_device.c.obj
[2145/2509] Building C object esp-idf/txp666__otto-emoji-gif-component/CMakeFiles/__idf_txp666__otto-emoji-gif-component.dir/src/otto_emoji_gif.c.obj
[2146/2509] Building C object esp-idf/espressif__led_strip/CMakeFiles/__idf_espressif__led_strip.dir/src/led_strip_rmt_dev.c.obj
[2147/2509] Building C object esp-idf/espressif2022__image_player/CMakeFiles/__idf_espressif2022__image_player.dir/anim_vfs.c.obj
[2148/2509] Building C object esp-idf/espressif2022__image_player/CMakeFiles/__idf_espressif2022__image_player.dir/anim_dec.c.obj
[2149/2509] Building C object esp-idf/espressif__led_strip/CMakeFiles/__idf_espressif__led_strip.dir/src/led_strip_spi_dev.c.obj
[2150/2509] Building C object esp-idf/waveshare__custom_io_expander_ch32v003/CMakeFiles/__idf_waveshare__custom_io_expander_ch32v003.dir/custom_io_expander_ch32v003.c.obj
[2151/2509] Building C object esp-idf/tny-robotics__sh1106-esp-idf/CMakeFiles/__idf_tny-robotics__sh1106-esp-idf.dir/esp_lcd_panel_sh1106.c.obj
[2152/2509] Building C object esp-idf/espressif__iot_usbh_rndis/CMakeFiles/__idf_espressif__iot_usbh_rndis.dir/iot_usbh_rndis.c.obj
[2153/2509] Building C object esp-idf/wvirgil123__sscma_client/CMakeFiles/__idf_wvirgil123__sscma_client.dir/src/sscma_client_io.c.obj
[2154/2509] Building C object esp-idf/waveshare__esp_lcd_touch_cst9217/CMakeFiles/__idf_waveshare__esp_lcd_touch_cst9217.dir/esp_lcd_touch_cst9217.c.obj
[2155/2509] Building C object esp-idf/espressif2022__image_player/CMakeFiles/__idf_espressif2022__image_player.dir/anim_player.c.obj
[2156/2509] Building C object esp-idf/waveshare__esp_lcd_sh8601/CMakeFiles/__idf_waveshare__esp_lcd_sh8601.dir/esp_lcd_sh8601.c.obj
[2157/2509] Building C object esp-idf/wvirgil123__sscma_client/CMakeFiles/__idf_wvirgil123__sscma_client.dir/src/sscma_client_io_uart.c.obj
[2158/2509] Building C object esp-idf/espressif__esp_video/CMakeFiles/__idf_espressif__esp_video.dir/src/esp_video.c.obj
[2159/2509] Linking C static library esp-idf/mooncake/libmooncake.a
[2160/2509] Building C object esp-idf/wvirgil123__sscma_client/CMakeFiles/__idf_wvirgil123__sscma_client.dir/src/sscma_client_flasher.c.obj
[2161/2509] Building C object esp-idf/wvirgil123__sscma_client/CMakeFiles/__idf_wvirgil123__sscma_client.dir/src/sscma_client_io_i2c.c.obj
[2162/2509] Linking C static library esp-idf/mooncake_log/libmooncake_log.a
[2163/2509] Linking C static library esp-idf/esp-now/libesp-now.a
[2164/2509] Linking C static library esp-idf/78__esp_lcd_nv3023/lib78__esp_lcd_nv3023.a
[2165/2509] Linking C static library esp-idf/78__esp-wifi-connect/lib78__esp-wifi-connect.a
[2166/2509] Linking C static library esp-idf/smooth_ui_toolkit/libsmooth_ui_toolkit.a
[2167/2509] Linking C static library esp-idf/78__uart-eth-modem/lib78__uart-eth-modem.a
[2168/2509] Linking C static library esp-idf/espressif__adc_battery_estimation/libespressif__adc_battery_estimation.a
[2169/2509] Linking C static library esp-idf/espressif__adc_mic/libespressif__adc_mic.a
[2170/2509] Building C object esp-idf/wvirgil123__sscma_client/CMakeFiles/__idf_wvirgil123__sscma_client.dir/src/sscma_client_io_spi.c.obj
[2171/2509] Linking C static library esp-idf/espressif__i2c_bus/libespressif__i2c_bus.a
[2172/2509] Linking C static library esp-idf/78__esp-ml307/lib78__esp-ml307.a
[2173/2509] Linking C static library esp-idf/espressif__esp-sr/libespressif__esp-sr.a
[2174/2509] Linking C static library esp-idf/espressif__esp_audio_codec/libespressif__esp_audio_codec.a
[2175/2509] Linking C static library esp-idf/espressif__esp_io_expander_tca9554/libespressif__esp_io_expander_tca9554.a
[2176/2509] Building C object esp-idf/wvirgil123__sscma_client/CMakeFiles/__idf_wvirgil123__sscma_client.dir/src/sscma_client_flasher_we2_spi.c.obj
[2177/2509] Linking C static library esp-idf/espressif__esp_io_expander_tca95xx_16bit/libespressif__esp_io_expander_tca95xx_16bit.a
[2178/2509] Linking C static library esp-idf/espressif__esp_lcd_axs15231b/libespressif__esp_lcd_axs15231b.a
[2179/2509] Building C object esp-idf/wvirgil123__sscma_client/CMakeFiles/__idf_wvirgil123__sscma_client.dir/src/sscma_client_flasher_we2_uart.c.obj
[2180/2509] Linking C static library esp-idf/espressif__esp_lcd_co5300/libespressif__esp_lcd_co5300.a
[2181/2509] Linking C static library esp-idf/espressif__esp_lcd_panel_io_additions/libespressif__esp_lcd_panel_io_additions.a
[2182/2509] Linking C static library esp-idf/espressif__esp_lcd_gc9a01/libespressif__esp_lcd_gc9a01.a
[2183/2509] Linking C static library esp-idf/espressif__esp_lcd_ili9341/libespressif__esp_lcd_ili9341.a
[2184/2509] Linking C static library esp-idf/espressif__esp_lcd_spd2010/libespressif__esp_lcd_spd2010.a
[2185/2509] Linking C static library esp-idf/espressif__esp32-camera/libespressif__esp32-camera.a
[2186/2509] Linking C static library esp-idf/espressif__esp_lcd_st7701/libespressif__esp_lcd_st7701.a
[2187/2509] Linking C static library esp-idf/espressif__esp_lcd_st77916/libespressif__esp_lcd_st77916.a
[2188/2509] Linking C static library esp-idf/espressif__esp_lcd_touch_gt1151/libespressif__esp_lcd_touch_gt1151.a
[2189/2509] Linking C static library esp-idf/espressif__esp_lcd_touch_cst816s/libespressif__esp_lcd_touch_cst816s.a
[2190/2509] Linking C static library esp-idf/espressif__esp_lcd_touch_ft5x06/libespressif__esp_lcd_touch_ft5x06.a
[2191/2509] Linking C static library esp-idf/espressif__esp_lcd_st7796/libespressif__esp_lcd_st7796.a
[2192/2509] Linking C static library esp-idf/espressif__esp_lcd_touch_gt911/libespressif__esp_lcd_touch_gt911.a
[2193/2509] Linking C static library esp-idf/espressif__esp_lcd_touch_st7123/libespressif__esp_lcd_touch_st7123.a
[2194/2509] Linking C static library esp-idf/espressif__esp_mmap_assets/libespressif__esp_mmap_assets.a
[2195/2509] Linking C static library esp-idf/espressif__iot_usbh_rndis/libespressif__iot_usbh_rndis.a
[2196/2509] Linking C static library esp-idf/espressif__led_strip/libespressif__led_strip.a
[2197/2509] Linking CXX static library esp-idf/espressif__esp_lvgl_port/liblvgl_port_lib.a
[2198/2509] Linking C static library esp-idf/espressif2022__image_player/libespressif2022__image_player.a
[2199/2509] Linking C static library esp-idf/tny-robotics__sh1106-esp-idf/libtny-robotics__sh1106-esp-idf.a
[2200/2509] Linking C static library esp-idf/espressif__esp_video/libespressif__esp_video.a
[2201/2509] Linking C static library esp-idf/txp666__otto-emoji-gif-component/libtxp666__otto-emoji-gif-component.a
[2202/2509] Linking C static library esp-idf/waveshare__custom_io_expander_ch32v003/libwaveshare__custom_io_expander_ch32v003.a
[2203/2509] Linking C static library esp-idf/waveshare__esp_lcd_sh8601/libwaveshare__esp_lcd_sh8601.a
[2204/2509] Linking C static library esp-idf/waveshare__esp_lcd_touch_cst9217/libwaveshare__esp_lcd_touch_cst9217.a
[2205/2509] Building C object esp-idf/unity/CMakeFiles/__idf_unity.dir/unity_compat.c.obj
[2206/2509] Building C object esp-idf/unity/CMakeFiles/__idf_unity.dir/port/esp/unity_utils_memory_esp.c.obj
[2207/2509] Building C object esp-idf/unity/CMakeFiles/__idf_unity.dir/unity_utils_cache.c.obj
[2208/2509] Building C object esp-idf/unity/CMakeFiles/__idf_unity.dir/unity_utils_freertos.c.obj
[2209/2509] Building C object esp-idf/unity/CMakeFiles/__idf_unity.dir/unity_utils_memory.c.obj
[2210/2509] Building C object esp-idf/unity/CMakeFiles/__idf_unity.dir/unity_port_esp32.c.obj
[2211/2509] Building C object esp-idf/app_trace/CMakeFiles/__idf_app_trace.dir/host_file_io.c.obj
[2212/2509] Building C object esp-idf/unity/CMakeFiles/__idf_unity.dir/unity_runner.c.obj
[2213/2509] Building C object esp-idf/app_trace/CMakeFiles/__idf_app_trace.dir/port/port_uart.c.obj
[2214/2509] Building C object esp-idf/app_trace/CMakeFiles/__idf_app_trace.dir/app_trace_util.c.obj
[2215/2509] Building C object esp-idf/cmock/CMakeFiles/__idf_cmock.dir/CMock/src/cmock.c.obj
[2216/2509] Building C object esp-idf/wvirgil123__sscma_client/CMakeFiles/__idf_wvirgil123__sscma_client.dir/src/sscma_client_ops.c.obj
[2217/2509] Building C object esp-idf/app_trace/CMakeFiles/__idf_app_trace.dir/app_trace.c.obj
[2218/2509] Linking C static library esp-idf/wvirgil123__sscma_client/libwvirgil123__sscma_client.a
[2219/2509] Building C object esp-idf/78__xiaozhi-fonts/CMakeFiles/__idf_78__xiaozhi-fonts.dir/src/font_puhui_30_4.c.obj
[2220/2509] Building C object esp-idf/esp_eth/CMakeFiles/__idf_esp_eth.dir/src/esp_eth_netif_glue.c.obj
[2221/2509] Building C object esp-idf/esp_https_server/CMakeFiles/__idf_esp_https_server.dir/src/https_server.c.obj
[2222/2509] Building C object esp-idf/esp_hid/CMakeFiles/__idf_esp_hid.dir/src/esp_hidd.c.obj
[2223/2509] Building C object esp-idf/esp_driver_touch_sens/CMakeFiles/__idf_esp_driver_touch_sens.dir/common/touch_sens_common.c.obj
[2224/2509] Building C object esp-idf/esp_eth/CMakeFiles/__idf_esp_eth.dir/src/esp_eth.c.obj
[2225/2509] Building C object esp-idf/esp_driver_touch_sens/CMakeFiles/__idf_esp_driver_touch_sens.dir/hw_ver2/touch_version_specific.c.obj
[2226/2509] Building C object esp-idf/unity/CMakeFiles/__idf_unity.dir/unity/src/unity.c.obj
[2227/2509] Linking C static library esp-idf/esp_https_server/libesp_https_server.a
[2228/2509] Linking C static library esp-idf/unity/libunity.a
[2229/2509] Building C object esp-idf/esp_hid/CMakeFiles/__idf_esp_hid.dir/src/esp_hid_common.c.obj
[2230/2509] Building C object esp-idf/esp_hid/CMakeFiles/__idf_esp_hid.dir/src/nimble_hidh.c.obj
[2231/2509] Building C object esp-idf/esp_hid/CMakeFiles/__idf_esp_hid.dir/src/nimble_hidd.c.obj
[2232/2509] Building C object esp-idf/esp_eth/CMakeFiles/__idf_esp_eth.dir/src/phy/esp_eth_phy_802_3.c.obj
[2233/2509] Building C object esp-idf/esp_hid/CMakeFiles/__idf_esp_hid.dir/src/esp_hidh.c.obj
[2234/2509] Building C object esp-idf/espcoredump/CMakeFiles/__idf_espcoredump.dir/src/core_dump_init.c.obj
[2235/2509] Building C object esp-idf/esp_local_ctrl/CMakeFiles/__idf_esp_local_ctrl.dir/src/esp_local_ctrl_handler.c.obj
[2236/2509] Linking C static library esp-idf/78__xiaozhi-fonts/lib78__xiaozhi-fonts.a
[2237/2509] Building C object esp-idf/esp_local_ctrl/CMakeFiles/__idf_esp_local_ctrl.dir/proto-c/esp_local_ctrl.pb-c.c.obj
[2238/2509] Building C object esp-idf/espcoredump/CMakeFiles/__idf_espcoredump.dir/src/core_dump_common.c.obj
[2239/2509] Building C object esp-idf/espcoredump/CMakeFiles/__idf_espcoredump.dir/src/core_dump_uart.c.obj
[2240/2509] Generating ../../1.ogg.S
[2241/2509] Generating ../../0.ogg.S
[2242/2509] Building C object esp-idf/esp_local_ctrl/CMakeFiles/__idf_esp_local_ctrl.dir/src/esp_local_ctrl_transport_httpd.c.obj
[2243/2509] Generating ../../2.ogg.S
[2244/2509] Generating ../../3.ogg.S
[2245/2509] Building C object esp-idf/esp_local_ctrl/CMakeFiles/__idf_esp_local_ctrl.dir/src/esp_local_ctrl_transport_ble.c.obj
[2246/2509] Building C object esp-idf/esp_local_ctrl/CMakeFiles/__idf_esp_local_ctrl.dir/src/esp_local_ctrl.c.obj
[2247/2509] Building C object esp-idf/espcoredump/CMakeFiles/__idf_espcoredump.dir/src/core_dump_elf.c.obj
[2248/2509] Generating ../../4.ogg.S
[2249/2509] Generating ../../6.ogg.S
[2250/2509] Generating ../../5.ogg.S
[2251/2509] Generating ../../7.ogg.S
[2252/2509] Building C object esp-idf/espcoredump/CMakeFiles/__idf_espcoredump.dir/src/core_dump_binary.c.obj
[2253/2509] Building C object esp-idf/espcoredump/CMakeFiles/__idf_espcoredump.dir/src/core_dump_flash.c.obj
[2254/2509] Generating ../../9.ogg.S
[2255/2509] Generating ../../8.ogg.S
[2256/2509] Generating ../../err_pin.ogg.S
[2257/2509] Generating ../../activation.ogg.S
[2258/2509] Generating ../../upgrade.ogg.S
[2259/2509] Generating ../../welcome.ogg.S
[2260/2509] Generating ../../wificonfig.ogg.S
[2261/2509] Generating ../../err_reg.ogg.S
[2262/2509] Generating ../../popup.ogg.S
[2263/2509] Generating ../../low_battery.ogg.S
[2264/2509] Generating ../../exclamation.ogg.S
[2265/2509] Generating ../../vibration.ogg.S
[2266/2509] Generating ../../success.ogg.S
[2267/2509] Generating ../../new_notification.ogg.S
[2268/2509] Generating ../../camera_shutter.ogg.S
[2269/2509] Building C object esp-idf/espcoredump/CMakeFiles/__idf_espcoredump.dir/src/core_dump_crc.c.obj
[2270/2509] Building C object esp-idf/espcoredump/CMakeFiles/__idf_espcoredump.dir/src/core_dump_sha.c.obj
[2271/2509] Building ASM object esp-idf/espcoredump/CMakeFiles/__idf_espcoredump.dir/src/port/xtensa/core_dump_stack_switch.S.obj
[2272/2509] Building C object esp-idf/perfmon/CMakeFiles/__idf_perfmon.dir/xtensa_perfmon_masks.c.obj
[2273/2509] Building C object esp-idf/perfmon/CMakeFiles/__idf_perfmon.dir/xtensa_perfmon_access.c.obj
[2274/2509] Building C object esp-idf/espcoredump/CMakeFiles/__idf_espcoredump.dir/src/port/xtensa/core_dump_port.c.obj
[2275/2509] Building C object esp-idf/perfmon/CMakeFiles/__idf_perfmon.dir/xtensa_perfmon_apis.c.obj
[2276/2509] Building C object esp-idf/nvs_sec_provider/CMakeFiles/__idf_nvs_sec_provider.dir/nvs_sec_provider.c.obj
[2277/2509] Building C object esp-idf/rt/CMakeFiles/__idf_rt.dir/FreeRTOS_POSIX_utils.c.obj
[2278/2509] Building C object esp-idf/wifi_provisioning/CMakeFiles/__idf_wifi_provisioning.dir/src/wifi_ctrl.c.obj
[2279/2509] Building C object esp-idf/wifi_provisioning/CMakeFiles/__idf_wifi_provisioning.dir/src/wifi_config.c.obj
[2280/2509] Building C object esp-idf/rt/CMakeFiles/__idf_rt.dir/FreeRTOS_POSIX_mqueue.c.obj
[2281/2509] Building C object esp-idf/touch_element/CMakeFiles/__idf_touch_element.dir/touch_button.c.obj
[2282/2509] Building C object esp-idf/wifi_provisioning/CMakeFiles/__idf_wifi_provisioning.dir/src/scheme_console.c.obj
[2283/2509] Building C object esp-idf/wifi_provisioning/CMakeFiles/__idf_wifi_provisioning.dir/src/wifi_scan.c.obj
[2284/2509] Building C object esp-idf/wifi_provisioning/CMakeFiles/__idf_wifi_provisioning.dir/src/handlers.c.obj
[2285/2509] Building C object esp-idf/wifi_provisioning/CMakeFiles/__idf_wifi_provisioning.dir/proto-c/wifi_config.pb-c.c.obj
[2286/2509] Building C object esp-idf/touch_element/CMakeFiles/__idf_touch_element.dir/touch_matrix.c.obj
[2287/2509] Building C object esp-idf/wifi_provisioning/CMakeFiles/__idf_wifi_provisioning.dir/proto-c/wifi_ctrl.pb-c.c.obj
[2288/2509] Building C object esp-idf/wifi_provisioning/CMakeFiles/__idf_wifi_provisioning.dir/proto-c/wifi_constants.pb-c.c.obj
[2289/2509] Building C object esp-idf/wifi_provisioning/CMakeFiles/__idf_wifi_provisioning.dir/proto-c/wifi_scan.pb-c.c.obj
[2290/2509] Building C object esp-idf/touch_element/CMakeFiles/__idf_touch_element.dir/touch_slider.c.obj
[2291/2509] Building C object esp-idf/wifi_provisioning/CMakeFiles/__idf_wifi_provisioning.dir/src/scheme_ble.c.obj
[2292/2509] Building C object esp-idf/wifi_provisioning/CMakeFiles/__idf_wifi_provisioning.dir/src/scheme_softap.c.obj
[2293/2509] Building C object esp-idf/touch_element/CMakeFiles/__idf_touch_element.dir/touch_element.c.obj
[2294/2509] Building C object esp-idf/wifi_provisioning/CMakeFiles/__idf_wifi_provisioning.dir/src/manager.c.obj
[2295/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/audio/demuxer/ogg_demuxer.cc.obj
[2296/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/audio/processors/audio_debugger.cc.obj
[2297/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/audio/codecs/es8311_audio_codec.cc.obj
[2298/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/audio/codecs/box_audio_codec.cc.obj
[2299/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/audio/audio_codec.cc.obj
[2300/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/audio/codecs/es8374_audio_codec.cc.obj
[2301/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/audio/codecs/es8389_audio_codec.cc.obj
[2302/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/audio/codecs/dummy_audio_codec.cc.obj
[2303/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/audio/codecs/es8388_audio_codec.cc.obj
[2304/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/audio/codecs/no_audio_codec.cc.obj
[2305/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/led/single_led.cc.obj
[2306/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/audio/audio_service.cc.obj
[2307/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/display/lvgl_display/lvgl_font.cc.obj
[2308/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/display/lvgl_display/lvgl_image.cc.obj
[2309/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/led/gpio_led.cc.obj
[2310/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/display/lvgl_display/emoji_collection.cc.obj
[2311/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/led/circular_strip.cc.obj
[2312/2509] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/display/lvgl_display/jpg/jpeg_to_image.c.obj
[2313/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/display/lvgl_display/lvgl_theme.cc.obj
[2314/2509] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/display/lvgl_display/gif/gifdec.c.obj
[2315/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/display/lvgl_display/jpg/image_to_jpeg.cpp.obj
[2316/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/display/display.cc.obj
[2317/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/display/oled_display.cc.obj
[2318/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/display/lvgl_display/lvgl_display.cc.obj
In file included from /Users/cobain/robot-build/fw/managed_components/espressif__esp_video/include/linux/videodev2.h:62,
                 from /Users/cobain/robot-build/fw/xiaozhi-esp32/main/display/lvgl_display/jpg/image_to_jpeg.h:12,
                 from /Users/cobain/robot-build/fw/xiaozhi-esp32/main/display/lvgl_display/lvgl_display.cc:14:
/Users/cobain/robot-build/fw/managed_components/espressif__esp_video/include/linux/ioctl.h:54:9: warning: "_IO" redefined
   54 | #define _IO(type,nr)        _IOC(_IOC_NONE,(type),(nr),0)
      |         ^~~
In file included from /Users/cobain/esp/esp-idf/components/lwip/include/lwip/sockets.h:8,
                 from /Users/cobain/esp/esp-idf/components/lwip/port/esp32xx/include/sys/socket.h:15,
                 from /Users/cobain/robot-build/fw/xiaozhi-esp32/main/audio/processors/audio_debugger.h:7,
                 from /Users/cobain/robot-build/fw/xiaozhi-esp32/main/audio/audio_service.h:23,
                 from /Users/cobain/robot-build/fw/xiaozhi-esp32/main/application.h:16,
                 from /Users/cobain/robot-build/fw/xiaozhi-esp32/main/display/lvgl_display/lvgl_display.cc:10:
/Users/cobain/esp/esp-idf/components/lwip/lwip/src/include/lwip/sockets.h:419:9: note: this is the location of the previous definition
  419 | #define _IO(x,y)        ((long)(IOC_VOID|((x)<<8)|(y)))
      |         ^~~
/Users/cobain/robot-build/fw/managed_components/espressif__esp_video/include/linux/ioctl.h:55:9: warning: "_IOR" redefined
   55 | #define _IOR(type,nr,size)  _IOC(_IOC_READ,(type),(nr),(_IOC_TYPECHECK(size)))
      |         ^~~~
/Users/cobain/esp/esp-idf/components/lwip/lwip/src/include/lwip/sockets.h:421:9: note: this is the location of the previous definition
  421 | #define _IOR(x,y,t)     ((long)(IOC_OUT|((sizeof(t)&IOCPARM_MASK)<<16)|((x)<<8)|(y)))
      |         ^~~~
/Users/cobain/robot-build/fw/managed_components/espressif__esp_video/include/linux/ioctl.h:56:9: warning: "_IOW" redefined
   56 | #define _IOW(type,nr,size)  _IOC(_IOC_WRITE,(type),(nr),(_IOC_TYPECHECK(size)))
      |         ^~~~
/Users/cobain/esp/esp-idf/components/lwip/lwip/src/include/lwip/sockets.h:423:9: note: this is the location of the previous definition
  423 | #define _IOW(x,y,t)     ((long)(IOC_IN|((sizeof(t)&IOCPARM_MASK)<<16)|((x)<<8)|(y)))
      |         ^~~~
[2319/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/display/lcd_display.cc.obj
[2320/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/display/lvgl_display/gif/lvgl_gif.cc.obj
[2321/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/system_info.cc.obj
[2322/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/settings.cc.obj
[2323/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/protocols/protocol.cc.obj
[2324/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/device_state_machine.cc.obj
[2325/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/protocols/websocket_protocol.cc.obj
[2326/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/ota.cc.obj
[2327/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/protocols/mqtt_protocol.cc.obj
[2328/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/boards/common/adc_battery_monitor.cc.obj
[2329/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/boards/common/board.cc.obj
[2330/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/assets.cc.obj
[2331/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/boards/common/ml307_board.cc.obj
[2332/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/boards/common/backlight.cc.obj
[2333/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/boards/common/wifi_board.cc.obj
[2334/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/boards/common/i2c_device.cc.obj
[2335/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/boards/common/nt26_board.cc.obj
[2336/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/mcp_server.cc.obj
[2337/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/application.cc.obj
[2338/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/boards/common/button.cc.obj
[2339/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/boards/common/knob.cc.obj
[2340/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/boards/common/system_reset.cc.obj
[2341/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/boards/common/dual_network_board.cc.obj
[2342/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/boards/common/axp2101.cc.obj
[2343/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/boards/common/press_to_talk_mcp_tool.cc.obj
[2344/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/boards/common/afsk_demod.cc.obj
[2345/2509] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_avatar/view/assets/icon_phone.c.obj
[2346/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/boards/common/power_save_timer.cc.obj
[2347/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/boards/common/sy6970.cc.obj
[2348/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/boards/common/sleep_timer.cc.obj
[2349/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_ai_agent/app_ai_agent.cpp.obj
[2350/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_launcher/view/screensaver.cpp.obj
FAILED: [code=1] esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_launcher/view/screensaver.cpp.obj 
/Users/cobain/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++ -DBOARD_NAME=\"m5stack-stack-chan\" -DBOARD_TYPE=\"m5stack-stack-chan\" -DBUILTIN_ICON_FONT=font_awesome_20_4 -DBUILTIN_TEXT_FONT=font_puhui_basic_20_4 -DESP_PLATFORM -DFIRMWARE_VERSION=\"1.4.3\" -DIDF_VER=\"v5.5.3\" -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -DMBEDTLS_CONFIG_FILE=\"mbedtls/esp_config.h\" -DSOC_MMU_PAGE_SIZE=CONFIG_MMU_PAGE_SIZE -DSOC_XTAL_FREQ_MHZ=CONFIG_XTAL_FREQ -D_GLIBCXX_HAVE_POSIX_SEMAPHORE -D_GLIBCXX_USE_POSIX_SEMAPHORE -D_GNU_SOURCE -D_POSIX_READER_WRITER_LOCKS -I/Users/cobain/robot-build/fw/build/config -I/Users/cobain/robot-build/fw/xiaozhi-esp32/main -I/Users/cobain/robot-build/fw/xiaozhi-esp32/main/display -I/Users/cobain/robot-build/fw/xiaozhi-esp32/main/display/lvgl_display -I/Users/cobain/robot-build/fw/xiaozhi-esp32/main/display/lvgl_display/jpg -I/Users/cobain/robot-build/fw/xiaozhi-esp32/main/audio -I/Users/cobain/robot-build/fw/xiaozhi-esp32/main/audio/demuxer -I/Users/cobain/robot-build/fw/xiaozhi-esp32/main/protocols -I/Users/cobain/robot-build/fw/xiaozhi-esp32/main/boards/common -I/Users/cobain/robot-build/fw/main -I/Users/cobain/esp/esp-idf/components/newlib/platform_include -I/Users/cobain/esp/esp-idf/components/freertos/config/include -I/Users/cobain/esp/esp-idf/components/freertos/config/include/freertos -I/Users/cobain/esp/esp-idf/components/freertos/config/xtensa/include -I/Users/cobain/esp/esp-idf/components/freertos/FreeRTOS-Kernel/include -I/Users/cobain/esp/esp-idf/components/freertos/FreeRTOS-Kernel/portable/xtensa/include -I/Users/cobain/esp/esp-idf/components/freertos/FreeRTOS-Kernel/portable/xtensa/include/freertos -I/Users/cobain/esp/esp-idf/components/freertos/esp_additions/include -I/Users/cobain/esp/esp-idf/components/esp_hw_support/include -I/Users/cobain/esp/esp-idf/components/esp_hw_support/include/soc -I/Users/cobain/esp/esp-idf/components/esp_hw_support/include/soc/esp32s3 -I/Users/cobain/esp/esp-idf/components/esp_hw_support/dma/include -I/Users/cobain/esp/esp-idf/components/esp_hw_support/ldo/include -I/Users/cobain/esp/esp-idf/components/esp_hw_support/debug_probe/include -I/Users/cobain/esp/esp-idf/components/esp_hw_support/mspi_timing_tuning/include -I/Users/cobain/esp/esp-idf/components/esp_hw_support/mspi_timing_tuning/tuning_scheme_impl/include -I/Users/cobain/esp/esp-idf/components/esp_hw_support/power_supply/include -I/Users/cobain/esp/esp-idf/components/esp_hw_support/port/esp32s3/. -I/Users/cobain/esp/esp-idf/components/esp_hw_support/port/esp32s3/include -I/Users/cobain/esp/esp-idf/components/esp_hw_support/mspi_timing_tuning/port/esp32s3/. -I/Users/cobain/esp/esp-idf/components/esp_hw_support/mspi_timing_tuning/port/esp32s3/include -I/Users/cobain/esp/esp-idf/components/heap/include -I/Users/cobain/esp/esp-idf/components/heap/tlsf -I/Users/cobain/esp/esp-idf/components/log/include -I/Users/cobain/esp/esp-idf/components/soc/include -I/Users/cobain/esp/esp-idf/components/soc/esp32s3 -I/Users/cobain/esp/esp-idf/components/soc/esp32s3/include -I/Users/cobain/esp/esp-idf/components/soc/esp32s3/register -I/Users/cobain/esp/esp-idf/components/hal/platform_port/include -I/Users/cobain/esp/esp-idf/components/hal/esp32s3/include -I/Users/cobain/esp/esp-idf/components/hal/include -I/Users/cobain/esp/esp-idf/components/esp_rom/include -I/Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/include -I/Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/include/esp32s3 -I/Users/cobain/esp/esp-idf/components/esp_rom/esp32s3 -I/Users/cobain/esp/esp-idf/components/esp_common/include -I/Users/cobain/esp/esp-idf/components/esp_system/include -I/Users/cobain/esp/esp-idf/components/esp_system/port/soc -I/Users/cobain/esp/esp-idf/components/esp_system/port/include/private -I/Users/cobain/esp/esp-idf/components/xtensa/esp32s3/include -I/Users/cobain/esp/esp-idf/components/xtensa/include -I/Users/cobain/esp/esp-idf/components/xtensa/deprecated_include -I/Users/cobain/esp/esp-idf/components/esp_timer/include -I/Users/cobain/esp/esp-idf/components/lwip/include -I/Users/cobain/esp/esp-idf/components/lwip/include/apps -I/Users/cobain/esp/esp-idf/components/lwip/include/apps/sntp -I/Users/cobain/esp/esp-idf/components/lwip/lwip/src/include -I/Users/cobain/esp/esp-idf/components/lwip/port/include -I/Users/cobain/esp/esp-idf/components/lwip/port/freertos/include -I/Users/cobain/esp/esp-idf/components/lwip/port/esp32xx/include -I/Users/cobain/esp/esp-idf/components/lwip/port/esp32xx/include/arch -I/Users/cobain/esp/esp-idf/components/lwip/port/esp32xx/include/sys -I/Users/cobain/esp/esp-idf/components/esp_pm/include -I/Users/cobain/esp/esp-idf/components/esp_psram/include -I/Users/cobain/esp/esp-idf/components/esp_psram/xip_impl/include -I/Users/cobain/esp/esp-idf/components/esp_netif/include -I/Users/cobain/esp/esp-idf/components/esp_event/include -I/Users/cobain/esp/esp-idf/components/esp_driver_gpio/include -I/Users/cobain/esp/esp-idf/components/esp_driver_uart/include -I/Users/cobain/esp/esp-idf/components/vfs/include -I/Users/cobain/esp/esp-idf/components/esp_driver_spi/include -I/Users/cobain/esp/esp-idf/components/esp_driver_i2c/include -I/Users/cobain/esp/esp-idf/components/esp_driver_i2s/include -I/Users/cobain/esp/esp-idf/components/esp_driver_jpeg/include -I/Users/cobain/esp/esp-idf/components/esp_driver_ppa/include -I/Users/cobain/esp/esp-idf/components/esp_app_format/include -I/Users/cobain/esp/esp-idf/components/app_update/include -I/Users/cobain/esp/esp-idf/components/bootloader_support/include -I/Users/cobain/esp/esp-idf/components/bootloader_support/bootloader_flash/include -I/Users/cobain/esp/esp-idf/components/esp_bootloader_format/include -I/Users/cobain/esp/esp-idf/components/esp_partition/include -I/Users/cobain/esp/esp-idf/components/spi_flash/include -I/Users/cobain/esp/esp-idf/components/console -I/Users/cobain/esp/esp-idf/components/esp_vfs_console/include -I/Users/cobain/esp/esp-idf/components/efuse/include -I/Users/cobain/esp/esp-idf/components/efuse/esp32s3/include -I/Users/cobain/esp/esp-idf/components/bt/include/esp32c3/include -I/Users/cobain/esp/esp-idf/components/bt/common/osi/include -I/Users/cobain/esp/esp-idf/components/bt/common/api/include/api -I/Users/cobain/esp/esp-idf/components/bt/common/btc/profile/esp/blufi/include -I/Users/cobain/esp/esp-idf/components/bt/common/btc/profile/esp/include -I/Users/cobain/esp/esp-idf/components/bt/common/hci_log/include -I/Users/cobain/esp/esp-idf/components/bt/common/ble_log/include -I/Users/cobain/esp/esp-idf/components/bt/common/tinycrypt/include -I/Users/cobain/esp/esp-idf/components/bt/common/tinycrypt/port -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/services/ans/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/services/bas/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/services/dis/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/services/gap/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/services/gatt/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/services/hr/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/services/htp/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/services/ias/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/services/ipss/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/services/lls/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/services/prox/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/services/cts/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/services/tps/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/services/hid/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/services/sps/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/services/cte/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/util/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/store/ram/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/store/config/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/host/services/ras/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/porting/nimble/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/port/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/nimble/transport/include -I/Users/cobain/esp/esp-idf/components/bt/porting/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/nimble/porting/npl/freertos/include -I/Users/cobain/esp/esp-idf/components/bt/host/nimble/esp-hci/include -I/Users/cobain/esp/esp-idf/components/esp_wifi/include -I/Users/cobain/esp/esp-idf/components/esp_wifi/include/local -I/Users/cobain/esp/esp-idf/components/esp_wifi/wifi_apps/include -I/Users/cobain/esp/esp-idf/components/esp_wifi/wifi_apps/nan_app/include -I/Users/cobain/esp/esp-idf/components/esp_phy/include -I/Users/cobain/esp/esp-idf/components/esp_phy/esp32s3/include -I/Users/cobain/esp/esp-idf/components/fatfs/diskio -I/Users/cobain/esp/esp-idf/components/fatfs/src -I/Users/cobain/esp/esp-idf/components/fatfs/vfs -I/Users/cobain/esp/esp-idf/components/wear_levelling/include -I/Users/cobain/esp/esp-idf/components/sdmmc/include -I/Users/cobain/esp/esp-idf/components/esp_driver_sdmmc/include -I/Users/cobain/esp/esp-idf/components/esp_driver_sdspi/include -I/Users/cobain/robot-build/fw/components/ArduinoJson/src -I/Users/cobain/robot-build/fw/components/esp-now/src/control/include -I/Users/cobain/robot-build/fw/components/esp-now/src/debug/include -I/Users/cobain/robot-build/fw/components/esp-now/src/debug/include/sdcard -I/Users/cobain/robot-build/fw/components/esp-now/src/debug/src -I/Users/cobain/robot-build/fw/components/esp-now/src/debug/src/commands/pcap -I/Users/cobain/robot-build/fw/components/esp-now/src/espnow/include -I/Users/cobain/robot-build/fw/components/esp-now/src/ota/include -I/Users/cobain/robot-build/fw/components/esp-now/src/provisioning/include -I/Users/cobain/robot-build/fw/components/esp-now/src/security/include -I/Users/cobain/robot-build/fw/components/esp-now/src/security/include/protocomm/security -I/Users/cobain/robot-build/fw/components/esp-now/src/utils/include -I/Users/cobain/esp/esp-idf/components/protocomm/proto-c -I/Users/cobain/esp/esp-idf/components/spiffs/include -I/Users/cobain/esp/esp-idf/components/esp_http_client/include -I/Users/cobain/esp/esp-idf/components/esp_https_ota/include -I/Users/cobain/esp/esp-idf/components/mbedtls/port/include -I/Users/cobain/esp/esp-idf/components/mbedtls/mbedtls/include -I/Users/cobain/esp/esp-idf/components/mbedtls/mbedtls/library -I/Users/cobain/esp/esp-idf/components/mbedtls/esp_crt_bundle/include -I/Users/cobain/esp/esp-idf/components/mbedtls/mbedtls/3rdparty/everest/include -I/Users/cobain/esp/esp-idf/components/mbedtls/mbedtls/3rdparty/p256-m -I/Users/cobain/esp/esp-idf/components/mbedtls/mbedtls/3rdparty/p256-m/p256-m -I/Users/cobain/esp/esp-idf/components/protobuf-c/protobuf-c -I/Users/cobain/esp/esp-idf/components/protocomm/include/common -I/Users/cobain/esp/esp-idf/components/protocomm/include/security -I/Users/cobain/esp/esp-idf/components/protocomm/include/transports -I/Users/cobain/esp/esp-idf/components/protocomm/include/crypto/srp6a -I/Users/cobain/esp/esp-idf/components/nvs_flash/include -I/Users/cobain/esp/esp-idf/components/driver/deprecated -I/Users/cobain/esp/esp-idf/components/driver/i2c/include -I/Users/cobain/esp/esp-idf/components/driver/touch_sensor/include -I/Users/cobain/esp/esp-idf/components/driver/twai/include -I/Users/cobain/esp/esp-idf/components/driver/touch_sensor/esp32s3/include -I/Users/cobain/esp/esp-idf/components/esp_ringbuf/include -I/Users/cobain/esp/esp-idf/components/esp_driver_pcnt/include -I/Users/cobain/esp/esp-idf/components/esp_driver_gptimer/include -I/Users/cobain/esp/esp-idf/components/esp_driver_mcpwm/include -I/Users/cobain/esp/esp-idf/components/esp_driver_ana_cmpr/include -I/Users/cobain/esp/esp-idf/components/esp_driver_sdio/include -I/Users/cobain/esp/esp-idf/components/esp_driver_dac/include -I/Users/cobain/esp/esp-idf/components/esp_driver_rmt/include -I/Users/cobain/esp/esp-idf/components/esp_driver_tsens/include -I/Users/cobain/esp/esp-idf/components/esp_driver_sdm/include -I/Users/cobain/esp/esp-idf/components/esp_driver_ledc/include -I/Users/cobain/esp/esp-idf/components/esp_driver_parlio/include -I/Users/cobain/esp/esp-idf/components/esp_driver_usb_serial_jtag/include -I/Users/cobain/esp/esp-idf/components/esp_driver_twai/include -I/Users/cobain/robot-build/fw/components/mooncake/src -I/Users/cobain/robot-build/fw/components/mooncake_log/src -I/Users/cobain/robot-build/fw/components/smooth_ui_toolkit/src -I/Users/cobain/robot-build/fw/components/smooth_ui_toolkit/src/lvgl -I/Users/cobain/robot-build/fw/managed_components/78__esp-ml307/include -I/Users/cobain/esp/esp-idf/components/esp-tls -I/Users/cobain/esp/esp-idf/components/esp-tls/esp-tls-crypto -I/Users/cobain/esp/esp-idf/components/pthread/include -I/Users/cobain/esp/esp-idf/components/mqtt/esp-mqtt/include -I/Users/cobain/esp/esp-idf/components/tcp_transport/include -I/Users/cobain/robot-build/fw/managed_components/78__esp-wifi-connect/include -I/Users/cobain/esp/esp-idf/components/esp_http_server/include -I/Users/cobain/esp/esp-idf/components/http_parser -I/Users/cobain/esp/esp-idf/components/json/cJSON -I/Users/cobain/robot-build/fw/managed_components/78__esp_lcd_nv3023/include -I/Users/cobain/esp/esp-idf/components/esp_lcd/include -I/Users/cobain/esp/esp-idf/components/esp_lcd/interface -I/Users/cobain/esp/esp-idf/components/esp_lcd/rgb/include -I/Users/cobain/robot-build/fw/managed_components/78__uart-eth-modem/include -I/Users/cobain/esp/esp-idf/components/esp_mm/include -I/Users/cobain/robot-build/fw/managed_components/espressif__iot_eth/include -I/Users/cobain/robot-build/fw/managed_components/espressif__iot_eth/interface -I/Users/cobain/robot-build/fw/managed_components/78__uart-uhci/include -I/Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/include -I/Users/cobain/robot-build/fw/managed_components/espressif__adc_battery_estimation/include -I/Users/cobain/esp/esp-idf/components/esp_adc/include -I/Users/cobain/esp/esp-idf/components/esp_adc/interface -I/Users/cobain/esp/esp-idf/components/esp_adc/esp32s3/include -I/Users/cobain/esp/esp-idf/components/esp_adc/deprecated/include -I/Users/cobain/robot-build/fw/managed_components/espressif__adc_mic -I/Users/cobain/robot-build/fw/managed_components/espressif__bmi270_sensor/include -I/Users/cobain/robot-build/fw/managed_components/espressif__i2c_bus/include -I/Users/cobain/robot-build/fw/managed_components/espressif__button/include -I/Users/cobain/robot-build/fw/managed_components/espressif__button/interface -I/Users/cobain/robot-build/fw/managed_components/espressif__esp-sr/esp-tts/esp_tts_chinese/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp-sr/include/esp32s3 -I/Users/cobain/robot-build/fw/managed_components/espressif__esp-sr/src/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp32-camera/driver/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp32-camera/conversions/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_jpeg/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_audio_codec/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_audio_codec/include/decoder -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_audio_codec/include/decoder/impl -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_audio_codec/include/encoder -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_audio_codec/include/encoder/impl -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_audio_codec/include/simple_dec -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_audio_effects/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_codec_dev/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_codec_dev/interface -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_codec_dev/device/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_image_effects/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_io_expander_tca9554/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_io_expander/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_io_expander_tca95xx_16bit/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_axs15231b/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_touch/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_co5300/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_gc9a01/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_ili9341/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_panel_io_additions/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_spd2010/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_st7701/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_st77916/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_st7796/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_touch_cst816s/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_touch_ft5x06/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_touch_gt1151/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_touch_gt911/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_touch_st7123/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_lvgl_port/include -I/Users/cobain/robot-build/fw/managed_components/lvgl__lvgl -I/Users/cobain/robot-build/fw/managed_components/lvgl__lvgl/src -I/Users/cobain/robot-build/fw/managed_components -I/Users/cobain/robot-build/fw/managed_components/lvgl__lvgl/examples -I/Users/cobain/robot-build/fw/managed_components/lvgl__lvgl/demos -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_mmap_assets/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_new_jpeg/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_video/include -I/Users/cobain/esp/esp-idf/components/esp_driver_cam/include -I/Users/cobain/esp/esp-idf/components/esp_driver_cam/interface -I/Users/cobain/esp/esp-idf/components/esp_driver_cam/dvp/include -I/Users/cobain/esp/esp-idf/components/esp_driver_isp/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_cam_sensor/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_cam_sensor/sensors/gc0308/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_sccb_intf/include -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_sccb_intf/interface -I/Users/cobain/robot-build/fw/managed_components/espressif__esp_sccb_intf/sccb_i2c/include -I/Users/cobain/robot-build/fw/managed_components/espressif__iot_usbh_rndis/include -I/Users/cobain/robot-build/fw/managed_components/espressif__iot_usbh_cdc -I/Users/cobain/robot-build/fw/managed_components/espressif__iot_usbh_cdc/include -I/Users/cobain/esp/esp-idf/components/usb/include -I/Users/cobain/robot-build/fw/managed_components/espressif__knob/include -I/Users/cobain/robot-build/fw/managed_components/espressif__led_strip/include -I/Users/cobain/robot-build/fw/managed_components/espressif__led_strip/interface -I/Users/cobain/robot-build/fw/managed_components/espressif2022__image_player/include -I/Users/cobain/robot-build/fw/managed_components/tny-robotics__sh1106-esp-idf/include -I/Users/cobain/robot-build/fw/managed_components/txp666__otto-emoji-gif-component/include -I/Users/cobain/robot-build/fw/managed_components/waveshare__custom_io_expander_ch32v003/include -I/Users/cobain/robot-build/fw/managed_components/waveshare__esp_lcd_sh8601/include -I/Users/cobain/robot-build/fw/managed_components/waveshare__esp_lcd_touch_cst9217/include -I/Users/cobain/robot-build/fw/managed_components/wvirgil123__sscma_client/include -I/Users/cobain/robot-build/fw/managed_components/wvirgil123__sscma_client/interface -mlongcalls  -fno-builtin-memcpy -fno-builtin-memset -fno-builtin-bzero -fno-builtin-stpcpy -fno-builtin-strncpy -fdiagnostics-color=always -Wno-missing-field-initializers -fdiagnostics-color=always -ffunction-sections -fdata-sections -Wall -Werror=all -Wno-error=unused-function -Wno-error=unused-variable -Wno-error=unused-but-set-variable -Wno-error=deprecated-declarations -Wextra -Wno-error=extra -Wno-unused-parameter -Wno-sign-compare -Wno-enum-conversion -gdwarf-4 -ggdb -mdisable-hardware-atomics -Os -freorder-blocks -fmacro-prefix-map=/Users/cobain/robot-build/fw=. -fmacro-prefix-map=/Users/cobain/esp/esp-idf=/IDF -fstrict-volatile-bitfields -fno-jump-tables -fno-tree-switch-conversion -std=gnu++2b -fexceptions -frtti -fuse-cxa-atexit -DESP_NOW_VER_MAJOR=2 -DESP_NOW_VER_MINOR=5 -DESP_NOW_VER_PATCH=2 -DESP_LCD_NV3023_VER_MAJOR=1 -DESP_LCD_NV3023_VER_MINOR=0 -DESP_LCD_NV3023_VER_PATCH=1 -DIOT_ETH_VER_MAJOR=0 -DIOT_ETH_VER_MINOR=1 -DIOT_ETH_VER_PATCH=0 -DADC_BATTERY_ESTIMATION_VER_MAJOR=0 -DADC_BATTERY_ESTIMATION_VER_MINOR=2 -DADC_BATTERY_ESTIMATION_VER_PATCH=2 -DADC_MIC_VER_MAJOR=0 -DADC_MIC_VER_MINOR=2 -DADC_MIC_VER_PATCH=3 -DI2C_BUS_VER_MAJOR=1 -DI2C_BUS_VER_MINOR=5 -DI2C_BUS_VER_PATCH=2 -DBUTTON_VER_MAJOR=4 -DBUTTON_VER_MINOR=1 -DBUTTON_VER_PATCH=7 -DESP_LCD_AXS15231B_VER_MAJOR=1 -DESP_LCD_AXS15231B_VER_MINOR=0 -DESP_LCD_AXS15231B_VER_PATCH=1 -DESP_LCD_CO5300_VER_MAJOR=2 -DESP_LCD_CO5300_VER_MINOR=1 -DESP_LCD_CO5300_VER_PATCH=0 -DESP_LCD_GC9A01_VER_MAJOR=2 -DESP_LCD_GC9A01_VER_MINOR=0 -DESP_LCD_GC9A01_VER_PATCH=1 -DESP_LCD_ILI9341_VER_MAJOR=1 -DESP_LCD_ILI9341_VER_MINOR=2 -DESP_LCD_ILI9341_VER_PATCH=0 -DESP_LCD_PANEL_IO_ADDITIONS_VER_MAJOR=1 -DESP_LCD_PANEL_IO_ADDITIONS_VER_MINOR=0 -DESP_LCD_PANEL_IO_ADDITIONS_VER_PATCH=1 -DESP_LCD_SPD2010_VER_MAJOR=1 -DESP_LCD_SPD2010_VER_MINOR=0 -DESP_LCD_SPD2010_VER_PATCH=2 -DESP_LCD_ST7701_VER_MAJOR=1 -DESP_LCD_ST7701_VER_MINOR=1 -DESP_LCD_ST7701_VER_PATCH=5 -DESP_LCD_ST77916_VER_MAJOR=1 -DESP_LCD_ST77916_VER_MINOR=0 -DESP_LCD_ST77916_VER_PATCH=1 -DESP_LCD_ST7796_VER_MAJOR=1 -DESP_LCD_ST7796_VER_MINOR=3 -DESP_LCD_ST7796_VER_PATCH=5 -DESP_LCD_TOUCH_ST7123_VER_MAJOR=1 -DESP_LCD_TOUCH_ST7123_VER_MINOR=0 -DESP_LCD_TOUCH_ST7123_VER_PATCH=2 -DESP_MMAP_ASSETS_VER_MAJOR=2 -DESP_MMAP_ASSETS_VER_MINOR=0 -DESP_MMAP_ASSETS_VER_PATCH=0 -DESP_VIDEO_VER_MAJOR=1 -DESP_VIDEO_VER_MINOR=3 -DESP_VIDEO_VER_PATCH=1 -DESP_CAM_SENSOR_VER_MAJOR=1 -DESP_CAM_SENSOR_VER_MINOR=5 -DESP_CAM_SENSOR_VER_PATCH=2 -DIOT_USBH_RNDIS_VER_MAJOR=0 -DIOT_USBH_RNDIS_VER_MINOR=3 -DIOT_USBH_RNDIS_VER_PATCH=1 -DIOT_USBH_CDC_VER_MAJOR=3 -DIOT_USBH_CDC_VER_MINOR=1 -DIOT_USBH_CDC_VER_PATCH=0 -DKNOB_VER_MAJOR=1 -DKNOB_VER_MINOR=1 -DKNOB_VER_PATCH=0 -DESP_LCD_SH8601_VER_MAJOR=1 -DESP_LCD_SH8601_VER_MINOR=0 -DESP_LCD_SH8601_VER_PATCH=2 -MD -MT esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_launcher/view/screensaver.cpp.obj -MF esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_launcher/view/screensaver.cpp.obj.d -o esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_launcher/view/screensaver.cpp.obj -c /Users/cobain/robot-build/fw/main/apps/app_launcher/view/screensaver.cpp
/Users/cobain/robot-build/fw/main/apps/app_launcher/view/screensaver.cpp: In member function 'void view::Screensaver::init()':
/Users/cobain/robot-build/fw/main/apps/app_launcher/view/screensaver.cpp:124:49: error: 'LV_TIMER_REPEAT_INFINITE' was not declared in this scope; did you mean 'LV_ANIM_REPEAT_INFINITE'?
  124 |         lv_timer_set_repeat_count(_blink_timer, LV_TIMER_REPEAT_INFINITE);
      |                                                 ^~~~~~~~~~~~~~~~~~~~~~~~
      |                                                 LV_ANIM_REPEAT_INFINITE
[2351/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_avatar/view/ws_call.cpp.obj
[2352/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_dance/app_dance.cpp.obj
[2353/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_avatar/app_avatar.cpp.obj
[2354/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_app_center/app_app_center.cpp.obj
[2355/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_launcher/view/view.cpp.obj
/Users/cobain/robot-build/fw/main/apps/app_launcher/view/view.cpp: In member function 'void view::LauncherView::handle_state_normal()':
/Users/cobain/robot-build/fw/main/apps/app_launcher/view/view.cpp:577:9: warning: unused variable 'center_set_start_x' [-Wunused-variable]
  577 |     int center_set_start_x = _center_copy_index * set_width_px;
      |         ^~~~~~~~~~~~~~~~~~
[2356/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_launcher/app_launcher.cpp.obj
[2357/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_ezdata/app_ezdata.cpp.obj
[2358/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_meeting/app_meeting.cpp.obj
[2359/2509] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_espnow_ctrl/app_espnow_ctrl.cpp.obj
ninja: build stopped: subcommand failed.
ninja failed with exit code 1, output of the command is in the /Users/cobain/robot-build/fw/build/log/idf_py_stderr_output_31767 and /Users/cobain/robot-build/fw/build/log/idf_py_stdout_output_31767
```
build exit code: 2

### 3) 产物
```
generated_assets.bin: mtime=Aug  6 15:13:28 2026 size=5422195
```

## [2026-08-06 15:55:58] 阶段 1.2 build（修 LV_TIMER_REPEAT_INFINITE → 默认无限循环）
```
Executing action: all (aliases: build)
Running ninja in directory /Users/cobain/robot-build/fw/build
Executing "ninja all"...
[0/2] Re-checking globbed directories...
[1/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_meeting/wav_writer.cpp.obj
[2/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_meeting/meeting_recorder.cpp.obj
[3/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_meeting/meeting_protocol.cpp.obj
[4/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_meeting/meeting_ui_model.cpp.obj
[5/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_meeting/meeting_uploader.cpp.obj
[6/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_meeting/meeting_manifest.cpp.obj
[7/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_meeting/segment_store.cpp.obj
[8/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_launcher/view/screensaver.cpp.obj
[9/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_meeting/stackchan_audio_source.cpp.obj
[10/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_mibao_iot/view/view.cpp.obj
[11/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_mibao_iot/app_mibao_iot.cpp.obj
[12/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_setup/view/view.cpp.obj
[13/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_mibao_personal/app_mibao_personal.cpp.obj
[14/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_mibao_personal/view/view.cpp.obj
[15/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_setup/workers/account.cpp.obj
[16/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_setup/workers/about.cpp.obj
/Users/cobain/robot-build/fw/main/apps/app_setup/workers/about.cpp: In constructor 'setup_workers::SystemUpdateWorker::SystemUpdateWorker()':
/Users/cobain/robot-build/fw/main/apps/app_setup/workers/about.cpp:311:10: warning: unused variable 'result' [-Wunused-variable]
  311 |     bool result = GetHAL().updateFirmware([&](std::string_view msg) {
      |          ^~~~~~
[17/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_template/app_template.cpp.obj
[18/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_setup/workers/audio.cpp.obj
[19/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_setup/workers/ai_agent.cpp.obj
/Users/cobain/robot-build/fw/main/apps/app_setup/workers/ai_agent.cpp: In constructor 'setup_workers::XiaozhiPowerSavingWorker::XiaozhiPowerSavingWorker()':
/Users/cobain/robot-build/fw/main/apps/app_setup/workers/ai_agent.cpp:104:76: warning: bitwise operation between different enumeration types 'lv_part_t' and 'lv_state_t' is deprecated [-Wdeprecated-enum-enum-conversion]
  104 |     _switch_charging->setBgColor(lv_color_hex(0x615B9E), LV_PART_INDICATOR | LV_STATE_CHECKED);
      |                                                          ~~~~~~~~~~~~~~~~~~^~~~~~~~~~~~~~~~~~
/Users/cobain/robot-build/fw/main/apps/app_setup/workers/ai_agent.cpp: In constructor 'setup_workers::XiaozhiGeneralWorker::XiaozhiGeneralWorker()':
/Users/cobain/robot-build/fw/main/apps/app_setup/workers/ai_agent.cpp:231:84: warning: bitwise operation between different enumeration types 'lv_part_t' and 'lv_state_t' is deprecated [-Wdeprecated-enum-enum-conversion]
  231 |     _switch_start_ai_on_boot->setBgColor(lv_color_hex(0x615B9E), LV_PART_INDICATOR | LV_STATE_CHECKED);
      |                                                                  ~~~~~~~~~~~~~~~~~~^~~~~~~~~~~~~~~~~~
[20/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_setup/app_setup.cpp.obj
[21/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/common/home_indicator/home_indicator.cpp.obj
[22/151] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/assets/fonts/MontserratSemiBold26.c.obj
[23/151] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/assets/fonts/meeting_zh_font.c.obj
[24/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_setup/workers/display.cpp.obj
[25/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_setup/workers/startup.cpp.obj
[26/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_setup/workers/servo.cpp.obj
[27/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/assets/assets.cpp.obj
[28/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_setup/workers/system.cpp.obj
[29/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/board/cores3_audio_codec.cc.obj
[30/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/drivers/FTServo_Arduino/src/HLSCL.cpp.obj
[31/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_setup/workers/connectivity.cpp.obj
[32/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/drivers/FTServo_Arduino/src/SCS.cpp.obj
[33/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/common/status_bar/status_bar.cpp.obj
[34/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/drivers/FTServo_Arduino/src/SCSerial.cpp.obj
[35/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/common/toast/toast.cpp.obj
[36/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/drivers/FTServo_Arduino/src/SMS_STS.cpp.obj
[37/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/drivers/FTServo_Arduino/src/SCSCL.cpp.obj
[38/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/drivers/PCF8563_Class/PCF8563_Class.cpp.obj
[39/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/common/reminder/reminder.cpp.obj
[40/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/drivers/PY32IOExpander_Class/PY32IOExpander_Class.cpp.obj
[41/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/drivers/Si12T/Si12T.cpp.obj
[42/151] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/hal/drivers/bmi270/BMI270_SensorAPI/bmi270_context.c.obj
[43/151] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/hal/drivers/bmi270/BMI270_SensorAPI/bmi270_dsd.c.obj
[44/151] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/hal/drivers/bmi270/BMI270_SensorAPI/bmi270_maximum_fifo.c.obj
[45/151] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/hal/drivers/bmi270/BMI270_SensorAPI/bmi270_legacy.c.obj
[46/151] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/hal/drivers/bmi270/BMI270_SensorAPI/bmi270.c.obj
[47/151] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/hal/drivers/bmi270/BMI270_SensorAPI/bmi2_ois.c.obj
[48/151] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/hal/drivers/bmi270/BMI270_SensorAPI/bmi2.c.obj
[49/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/drivers/bmi270/bmi270.cpp.obj
[50/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/board/hal_bridge.cc.obj
In file included from /Users/cobain/esp/esp-idf/components/lwip/include/lwip/sockets.h:8,
                 from /Users/cobain/esp/esp-idf/components/lwip/port/esp32xx/include/sys/socket.h:15,
                 from /Users/cobain/robot-build/fw/xiaozhi-esp32/main/audio/processors/audio_debugger.h:7,
                 from /Users/cobain/robot-build/fw/xiaozhi-esp32/main/audio/audio_service.h:23,
                 from /Users/cobain/robot-build/fw/xiaozhi-esp32/main/application.h:16,
                 from /Users/cobain/robot-build/fw/main/hal/board/hal_bridge.cc:14:
/Users/cobain/esp/esp-idf/components/lwip/lwip/src/include/lwip/sockets.h:419:9: warning: "_IO" redefined
  419 | #define _IO(x,y)        ((long)(IOC_VOID|((x)<<8)|(y)))
      |         ^~~
In file included from /Users/cobain/robot-build/fw/managed_components/espressif__esp_video/include/linux/videodev2.h:62,
                 from /Users/cobain/robot-build/fw/xiaozhi-esp32/main/display/lvgl_display/jpg/image_to_jpeg.h:12,
                 from /Users/cobain/robot-build/fw/main/hal/board/stackchan_camera.h:14,
                 from /Users/cobain/robot-build/fw/main/hal/board/hal_bridge.h:7,
                 from /Users/cobain/robot-build/fw/main/hal/board/hal_bridge.cc:6:
/Users/cobain/robot-build/fw/managed_components/espressif__esp_video/include/linux/ioctl.h:54:9: note: this is the location of the previous definition
   54 | #define _IO(type,nr)        _IOC(_IOC_NONE,(type),(nr),0)
      |         ^~~
/Users/cobain/esp/esp-idf/components/lwip/lwip/src/include/lwip/sockets.h:421:9: warning: "_IOR" redefined
  421 | #define _IOR(x,y,t)     ((long)(IOC_OUT|((sizeof(t)&IOCPARM_MASK)<<16)|((x)<<8)|(y)))
      |         ^~~~
/Users/cobain/robot-build/fw/managed_components/espressif__esp_video/include/linux/ioctl.h:55:9: note: this is the location of the previous definition
   55 | #define _IOR(type,nr,size)  _IOC(_IOC_READ,(type),(nr),(_IOC_TYPECHECK(size)))
      |         ^~~~
/Users/cobain/esp/esp-idf/components/lwip/lwip/src/include/lwip/sockets.h:423:9: warning: "_IOW" redefined
  423 | #define _IOW(x,y,t)     ((long)(IOC_IN|((sizeof(t)&IOCPARM_MASK)<<16)|((x)<<8)|(y)))
      |         ^~~~
/Users/cobain/robot-build/fw/managed_components/espressif__esp_video/include/linux/ioctl.h:56:9: note: this is the location of the previous definition
   56 | #define _IOW(type,nr,size)  _IOC(_IOC_WRITE,(type),(nr),(_IOC_TYPECHECK(size)))
      |         ^~~~
/Users/cobain/robot-build/fw/main/hal/board/hal_bridge.cc: In function 'void hal_bridge::xiaozhi_board_init()':
/Users/cobain/robot-build/fw/main/hal/board/hal_bridge.cc:109:11: warning: unused variable 'board' [-Wunused-variable]
  109 |     auto& board = Board::GetInstance();
      |           ^~~~~
/Users/cobain/robot-build/fw/main/hal/board/hal_bridge.cc: At global scope:
/Users/cobain/robot-build/fw/main/hal/board/hal_bridge.cc:21:20: warning: '_tag' defined but not used [-Wunused-variable]
   21 | static const char* _tag = "HAL_BRIDGE";
      |                    ^~~~
[51/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/audio.cpp.obj
[52/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/board/stackchan_camera.cc.obj
[53/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/board/stackchan.cc.obj
In file included from /Users/cobain/robot-build/fw/managed_components/espressif__esp_video/include/linux/videodev2.h:62,
                 from /Users/cobain/robot-build/fw/xiaozhi-esp32/main/display/lvgl_display/jpg/image_to_jpeg.h:12,
                 from /Users/cobain/robot-build/fw/main/hal/board/stackchan_camera.h:14,
                 from /Users/cobain/robot-build/fw/main/hal/board/stackchan.cc:29:
/Users/cobain/robot-build/fw/managed_components/espressif__esp_video/include/linux/ioctl.h:54:9: warning: "_IO" redefined
   54 | #define _IO(type,nr)        _IOC(_IOC_NONE,(type),(nr),0)
      |         ^~~
In file included from /Users/cobain/esp/esp-idf/components/lwip/include/lwip/sockets.h:8,
                 from /Users/cobain/esp/esp-idf/components/lwip/port/esp32xx/include/sys/socket.h:15,
                 from /Users/cobain/robot-build/fw/xiaozhi-esp32/main/audio/processors/audio_debugger.h:7,
                 from /Users/cobain/robot-build/fw/xiaozhi-esp32/main/audio/audio_service.h:23,
                 from /Users/cobain/robot-build/fw/xiaozhi-esp32/main/application.h:16,
                 from /Users/cobain/robot-build/fw/main/hal/board/stackchan.cc:5:
/Users/cobain/esp/esp-idf/components/lwip/lwip/src/include/lwip/sockets.h:419:9: note: this is the location of the previous definition
  419 | #define _IO(x,y)        ((long)(IOC_VOID|((x)<<8)|(y)))
      |         ^~~
/Users/cobain/robot-build/fw/managed_components/espressif__esp_video/include/linux/ioctl.h:55:9: warning: "_IOR" redefined
   55 | #define _IOR(type,nr,size)  _IOC(_IOC_READ,(type),(nr),(_IOC_TYPECHECK(size)))
      |         ^~~~
/Users/cobain/esp/esp-idf/components/lwip/lwip/src/include/lwip/sockets.h:421:9: note: this is the location of the previous definition
  421 | #define _IOR(x,y,t)     ((long)(IOC_OUT|((sizeof(t)&IOCPARM_MASK)<<16)|((x)<<8)|(y)))
      |         ^~~~
/Users/cobain/robot-build/fw/managed_components/espressif__esp_video/include/linux/ioctl.h:56:9: warning: "_IOW" redefined
   56 | #define _IOW(type,nr,size)  _IOC(_IOC_WRITE,(type),(nr),(_IOC_TYPECHECK(size)))
      |         ^~~~
/Users/cobain/esp/esp-idf/components/lwip/lwip/src/include/lwip/sockets.h:423:9: note: this is the location of the previous definition
  423 | #define _IOW(x,y,t)     ((long)(IOC_IN|((sizeof(t)&IOCPARM_MASK)<<16)|((x)<<8)|(y)))
      |         ^~~~
[54/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/board/stackchan_display.cc.obj
/Users/cobain/robot-build/fw/main/hal/board/stackchan_display.cc: In member function 'virtual void StackChanAvatarDisplay::SetStatus(const char*)':
/Users/cobain/robot-build/fw/main/hal/board/stackchan_display.cc:491:11: warning: unused variable 'motion' [-Wunused-variable]
  491 |     auto& motion = stackchan.motion();
      |           ^~~~~~
/Users/cobain/robot-build/fw/main/hal/board/stackchan_display.cc:496:10: warning: unused variable 'is_listening' [-Wunused-variable]
  496 |     bool is_listening = false;
      |          ^~~~~~~~~~~~
[55/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/hal_account.cpp.obj
[56/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/hal_head_touch.cpp.obj
[57/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/hal_app_center.cpp.obj
[58/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/hal_imu.cpp.obj
[59/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/hal_espnow.cpp.obj
[60/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/hal_io_expander.cpp.obj
[61/151] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/hal/utils/bleprph/bleprph.c.obj
[62/151] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/hal/utils/bleprph/gatt_svr.c.obj
/Users/cobain/robot-build/fw/main/hal/utils/bleprph/gatt_svr.c:291:12: warning: 'gatt_svc_access' defined but not used [-Wunused-function]
  291 | static int gatt_svc_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
      |            ^~~~~~~~~~~~~~~
[63/151] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/hal/utils/bleprph/nimble_peripheral_utils/misc.c.obj
[64/151] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/hal/utils/bleprph/nimble_peripheral_utils/scli.c.obj
[65/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/hal_ble.cpp.obj
[66/151] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/hal/utils/ota/ota.c.obj
[67/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/hal.cpp.obj
[68/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/utils/secret_logic/secret_logic.cpp.obj
[69/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/utils/wifi_connect/wifi_station.cc.obj
[70/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/hal_ota.cpp.obj
[71/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/hal_ezdata.cpp.obj
[72/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/hal_network.cpp.obj
[73/151] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/avatar/decorators/assets/decorator_dizzy.c.obj
[74/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/hal_mcp.cpp.obj
[75/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/hal_rtc.cpp.obj
[76/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/addons/neon_light/neon_light.cpp.obj
[77/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/hal_servo.cpp.obj
[78/151] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/avatar/decorators/assets/decorator_angry.c.obj
[79/151] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/avatar/decorators/assets/decorator_sweat.c.obj
[80/151] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/avatar/decorators/assets/decorator_shy.c.obj
[81/151] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/avatar/decorators/assets/decorator_heart.c.obj
[82/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/hal_ws_avatar.cpp.obj
[83/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/avatar/decorators/shy.cpp.obj
[84/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/avatar/decorators/sweat.cpp.obj
[85/151] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/avatar/skins/default/assets/default_bubble_arrow.c.obj
[86/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/avatar/decorators/dizzy.cpp.obj
[87/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/avatar/decorators/angry.cpp.obj
[88/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/avatar/decorators/heart.cpp.obj
[89/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/motion/motion_math.cpp.obj
[90/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/hal/utils/jpeg_to_image/jpeg_decoder.cpp.obj
[91/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/animation/animation.cpp.obj
[92/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/avatar/skins/default/mouth.cpp.obj
[93/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/motion/motion.cpp.obj
[94/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/stackchan.cpp.obj
[95/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/audio/wake_words/custom_wake_word.cc.obj
[96/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/avatar/skins/default/eyes.cpp.obj
[97/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/avatar/skins/default/default.cpp.obj
[98/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/0.ogg.S.obj
[99/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/1.ogg.S.obj
[100/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/2.ogg.S.obj
[101/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/json/json_helper.cpp.obj
[102/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/4.ogg.S.obj
[103/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/5.ogg.S.obj
[104/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/6.ogg.S.obj
[105/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/7.ogg.S.obj
[106/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/8.ogg.S.obj
[107/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/9.ogg.S.obj
[108/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/activation.ogg.S.obj
[109/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/audio/processors/afe_audio_processor.cc.obj
[110/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/err_reg.ogg.S.obj
[111/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/upgrade.ogg.S.obj
[112/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/welcome.ogg.S.obj
[113/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/wificonfig.ogg.S.obj
[114/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/exclamation.ogg.S.obj
[115/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/low_battery.ogg.S.obj
[116/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/popup.ogg.S.obj
[117/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/success.ogg.S.obj
[118/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/vibration.ogg.S.obj
[119/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/audio/wake_words/afe_wake_word.cc.obj
[120/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/new_notification.ogg.S.obj
[121/151] Linking C static library esp-idf/app_trace/libapp_trace.a
[122/151] Linking C static library esp-idf/cmock/libcmock.a
[123/151] Linking C static library esp-idf/esp_driver_touch_sens/libesp_driver_touch_sens.a
[124/151] Linking C static library esp-idf/esp_eth/libesp_eth.a
[125/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/3.ogg.S.obj
[126/151] Linking C static library esp-idf/esp_local_ctrl/libesp_local_ctrl.a
[127/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/motion/servo.cpp.obj
[128/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/boards/common/rndis_board.cc.obj
[129/151] Linking C static library esp-idf/nvs_sec_provider/libnvs_sec_provider.a
[130/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/stackchan/avatar/skins/default/speech_bubble.cpp.obj
[131/151] Linking C static library esp-idf/rt/librt.a
[132/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/camera_shutter.ogg.S.obj
[133/151] Linking C static library esp-idf/perfmon/libperfmon.a
[134/151] Linking C static library esp-idf/touch_element/libtouch_element.a
[135/151] Building ASM object esp-idf/main/CMakeFiles/__idf_main.dir/__/__/err_pin.ogg.S.obj
[136/151] Linking C static library esp-idf/wifi_provisioning/libwifi_provisioning.a
[137/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/boards/common/esp_video.cc.obj
[138/151] Linking C static library esp-idf/esp_hid/libesp_hid.a
[139/151] Linking C static library esp-idf/espcoredump/libespcoredump.a
[140/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/__/xiaozhi-esp32/main/boards/common/esp32_camera.cc.obj
[141/151] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/main.cpp.obj
[142/151] Linking C static library esp-idf/main/libmain.a
[143/151] Performing build step for 'bootloader'
[1/123] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/noos/log_lock.c.obj
[2/123] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/noos/util.c.obj
[3/123] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/util.c.obj
[4/123] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/log_print.c.obj
[5/123] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/log_timestamp_common.c.obj
[6/123] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/log.c.obj
[7/123] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/log_format_text.c.obj
[8/123] Building C object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_sys.c.obj
[9/123] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/noos/log_timestamp.c.obj
[10/123] Building ASM object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_longjmp.S.obj
[11/123] Building C object esp-idf/log/CMakeFiles/__idf_log.dir/src/buffer/log_buffers.c.obj
[12/123] Building C object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_cache_esp32s2_esp32s3.c.obj
[13/123] Building ASM object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_cache_writeback_esp32s3.S.obj
[14/123] Linking C static library esp-idf/log/liblog.a
[15/123] Building C object esp-idf/esp_common/CMakeFiles/__idf_esp_common.dir/src/esp_err_to_name.c.obj
[16/123] Building C object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_gpio.c.obj
[17/123] Building C object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_systimer.c.obj
[18/123] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/cpu.c.obj
[19/123] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp32s3/esp_cpu_intr.c.obj
[20/123] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/esp_memory_utils.c.obj
[21/123] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp32s3/cpu_region_protect.c.obj
[22/123] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp32s3/rtc_init.c.obj
[23/123] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp32s3/rtc_sleep.c.obj
[24/123] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp32s3/chip_info.c.obj
[25/123] Building C object esp-idf/esp_system/CMakeFiles/__idf_esp_system.dir/esp_err.c.obj
[26/123] Building C object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_spiflash.c.obj
[27/123] Building C object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_crc.c.obj
[28/123] Building C object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_efuse.c.obj
[29/123] Building C object esp-idf/efuse/CMakeFiles/__idf_efuse.dir/esp32s3/esp_efuse_rtc_calib.c.obj
[30/123] Building C object esp-idf/efuse/CMakeFiles/__idf_efuse.dir/src/esp_efuse_api.c.obj
[31/123] Building C object esp-idf/efuse/CMakeFiles/__idf_efuse.dir/src/esp_efuse_fields.c.obj
[32/123] Building C object esp-idf/efuse/CMakeFiles/__idf_efuse.dir/src/esp_efuse_utility.c.obj
[33/123] Building C object esp-idf/efuse/CMakeFiles/__idf_efuse.dir/src/efuse_controller/keys/with_key_purposes/esp_efuse_api_key.c.obj
[34/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_common.c.obj
[35/123] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp32s3/rtc_clk.c.obj
[36/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_clock_init.c.obj
[37/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_common_loader.c.obj
[38/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_random.c.obj
[39/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_mem.c.obj
[40/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_efuse.c.obj
[41/123] Building C object esp-idf/efuse/CMakeFiles/__idf_efuse.dir/esp32s3/esp_efuse_table.c.obj
[42/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/flash_encrypt.c.obj
[43/123] Building C object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_uart.c.obj
[44/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/bootloader_flash/src/bootloader_flash.c.obj
[45/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/bootloader_flash/src/bootloader_flash_config_esp32s3.c.obj
[46/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/bootloader_flash/src/flash_qio_mode.c.obj
[47/123] Building C object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_print.c.obj
[48/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/flash_partitions.c.obj
[49/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_utility.c.obj
[50/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/esp_image_format.c.obj
[51/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_init.c.obj
[52/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_clock_loader.c.obj
[53/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_console.c.obj
[54/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_sha.c.obj
[55/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_console_loader.c.obj
[56/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/esp32s3/bootloader_soc.c.obj
[57/123] Building C object esp-idf/esp_bootloader_format/CMakeFiles/__idf_esp_bootloader_format.dir/esp_bootloader_desc.c.obj
[58/123] Building C object esp-idf/spi_flash/CMakeFiles/__idf_spi_flash.dir/spi_flash_wrap.c.obj
[59/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/esp32s3/bootloader_esp32s3.c.obj
[60/123] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/mpu_hal.c.obj
[61/123] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/efuse_hal.c.obj
[62/123] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/esp32s3/efuse_hal.c.obj
[63/123] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/hal_utils.c.obj
[64/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_panic.c.obj
[65/123] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/cache_hal.c.obj
[66/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/lldesc.c.obj
[67/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/dport_access_common.c.obj
[68/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/interrupts.c.obj
[69/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/gpio_periph.c.obj
[70/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/uart_periph.c.obj
[71/123] Building C object esp-idf/esp_rom/CMakeFiles/__idf_esp_rom.dir/patches/esp_rom_wdt.c.obj
[72/123] Building C object esp-idf/micro-ecc/CMakeFiles/__idf_micro-ecc.dir/uECC_verify_antifault.c.obj
[73/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/dedic_gpio_periph.c.obj
[74/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/gdma_periph.c.obj
[75/123] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp32s3/rtc_time.c.obj
[76/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/ledc_periph.c.obj
[77/123] Building C object esp-idf/hal/CMakeFiles/__idf_hal.dir/mmu_hal.c.obj
[78/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/rmt_periph.c.obj
[79/123] Linking C static library esp-idf/esp_rom/libesp_rom.a
[80/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/sdm_periph.c.obj
[81/123] Linking C static library esp-idf/esp_common/libesp_common.a
[82/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/i2s_periph.c.obj
[83/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/adc_periph.c.obj
[84/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/timer_periph.c.obj
[85/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/pcnt_periph.c.obj
[86/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/lcd_periph.c.obj
[87/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/mcpwm_periph.c.obj
[88/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/mpi_periph.c.obj
[89/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/i2c_periph.c.obj
[90/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/twai_periph.c.obj
[91/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/wdt_periph.c.obj
[92/123] Building C object esp-idf/efuse/CMakeFiles/__idf_efuse.dir/esp32s3/esp_efuse_utility.c.obj
[93/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/spi_periph.c.obj
[94/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/usb_dwc_periph.c.obj
[95/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/touch_sensor_periph.c.obj
[96/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/power_supply_periph.c.obj
[97/123] Building C object esp-idf/xtensa/CMakeFiles/__idf_xtensa.dir/eri.c.obj
[98/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/cam_periph.c.obj
[99/123] Building C object esp-idf/xtensa/CMakeFiles/__idf_xtensa.dir/xt_trax.c.obj
[100/123] Generating project_elf_src_esp32s3.c
[101/123] Building C object CMakeFiles/bootloader.elf.dir/project_elf_src_esp32s3.c.obj
[102/123] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/bootloader_start.c.obj
[103/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/rtc_io_periph.c.obj
[104/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/sdmmc_periph.c.obj
[105/123] Building C object esp-idf/esp_hw_support/CMakeFiles/__idf_esp_hw_support.dir/port/esp32s3/rtc_clk_init.c.obj
[106/123] Linking C static library esp-idf/esp_hw_support/libesp_hw_support.a
[107/123] Linking C static library esp-idf/esp_system/libesp_system.a
[108/123] Building C object esp-idf/soc/CMakeFiles/__idf_soc.dir/esp32s3/temperature_sensor_periph.c.obj
[109/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/secure_boot.c.obj
[110/123] Building C object esp-idf/bootloader_support/CMakeFiles/__idf_bootloader_support.dir/src/bootloader_random_esp32s3.c.obj
[111/123] Building C object esp-idf/efuse/CMakeFiles/__idf_efuse.dir/esp32s3/esp_efuse_fields.c.obj
[112/123] Linking C static library esp-idf/efuse/libefuse.a
[113/123] Linking C static library esp-idf/bootloader_support/libbootloader_support.a
[114/123] Linking C static library esp-idf/esp_bootloader_format/libesp_bootloader_format.a
[115/123] Linking C static library esp-idf/spi_flash/libspi_flash.a
[116/123] Linking C static library esp-idf/hal/libhal.a
[117/123] Linking C static library esp-idf/micro-ecc/libmicro-ecc.a
[118/123] Linking C static library esp-idf/soc/libsoc.a
[119/123] Linking C static library esp-idf/xtensa/libxtensa.a
[120/123] Linking C static library esp-idf/main/libmain.a
[121/123] Linking C executable bootloader.elf
[122/123] Generating binary image from built executable
esptool.py v4.12.dev2
Creating esp32s3 image...
Merged 2 ELF sections
Successfully created esp32s3 image.
Generated /Users/cobain/robot-build/fw/build/bootloader/bootloader.bin
[123/123] cd /Users/cobain/robot-build/fw/build/bootloader/esp-idf/esptool_py && /Users/cobain/.espressif/python_env/idf5.5_py3.14_env/bin/python /Users/cobain/esp/esp-idf/components/partition_table/check_sizes.py --offset 0x8000 bootloader 0x0 /Users/cobain/robot-build/fw/build/bootloader/bootloader.bin
Bootloader binary size 0x5cf0 bytes. 0x2310 bytes (27%) free.
[144/151] No install step for 'bootloader'
[145/151] Completed 'bootloader'
[146/151] Generating esp-idf/esp_system/ld/sections.ld
[147/151] Building C object CMakeFiles/stack-chan.elf.dir/project_elf_src_esp32s3.c.obj
[148/151] Linking CXX executable stack-chan.elf
[149/151] Generating binary image from built executable
esptool.py v4.12.dev2
Creating esp32s3 image...
Merged 2 ELF sections
Successfully created esp32s3 image.
Generated /Users/cobain/robot-build/fw/build/stack-chan.bin
[150/151] cd /Users/cobain/robot-build/fw/build/esp-idf/esptool_py && /Users/cobain/.espressif/python_env/idf5.5_py3.14_env/bin/python /Users/cobain/esp/esp-idf/components/partition_table/check_sizes.py --offset 0x8000 partition --type app /Users/cobain/robot-build/fw/build/partition_table/partition-table.bin /Users/cobain/robot-build/fw/build/stack-chan.bin
stack-chan.bin binary size 0x38dfe0 bytes. Smallest app partition is 0x4f0000 bytes. 0x162020 bytes (28%) free.

Project build complete. To flash, run:
 idf.py flash
or
 idf.py -p PORT flash
or
 python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0xd000 build/ota_data_initial.bin 0x20000 build/stack-chan.bin 0xa00000 build/generated_assets.bin
or from the "/Users/cobain/robot-build/fw/build" directory
 python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash "@flash_args"
```
build exit code: 0

### 产物
```
stack-chan.bin: mtime=Aug  6 16:18:21 2026 size=3727328
generated_assets.bin: mtime=Aug  6 15:13:28 2026 size=5422195
```

## [2026-08-06 16:53:53] 阶段 1.2 build（修 LV_TIMER_REPEAT_INFINITE → 默认无限循环）
```
Executing action: all (aliases: build)
Running ninja in directory /Users/cobain/robot-build/fw/build
Executing "ninja all"...
[0/2] Re-checking globbed directories...
[1/6] cd /Users/cobain/robot-build/fw/build/esp-idf/esptool_py && /Users/cobain/.espressif/python_env/idf5.5_py3.14_env/bin/python /Users/cobain/esp/esp-idf/components/partition_table/check_sizes.py --offset 0x8000 partition --type app /Users/cobain/robot-build/fw/build/partition_table/partition-table.bin /Users/cobain/robot-build/fw/build/stack-chan.bin
stack-chan.bin binary size 0x38dfe0 bytes. Smallest app partition is 0x4f0000 bytes. 0x162020 bytes (28%) free.
[2/6] Performing build step for 'bootloader'
[1/1] cd /Users/cobain/robot-build/fw/build/bootloader/esp-idf/esptool_py && /Users/cobain/.espressif/python_env/idf5.5_py3.14_env/bin/python /Users/cobain/esp/esp-idf/components/partition_table/check_sizes.py --offset 0x8000 bootloader 0x0 /Users/cobain/robot-build/fw/build/bootloader/bootloader.bin
Bootloader binary size 0x5cf0 bytes. 0x2310 bytes (27%) free.
[3/6] No install step for 'bootloader'
[4/6] Completed 'bootloader'
[5/6] Building default assets.bin based on configuration
Building default assets...
  sdkconfig: /Users/cobain/robot-build/fw/sdkconfig
  builtin_text_font: font_puhui_basic_20_4
  emoji_collection: twemoji_64
  output: /Users/cobain/robot-build/fw/build/generated_assets.bin
  Note: Found wakenet models ['wn9_histackchan_tts3'] but wake word type is not ESP/AFE, skipping
  multinet models: mn7_cn, fst (will be packaged)
  custom wake word: ni hao mi bao (你好，米宝)
  wake word language: cn
  wake word threshold: 0.2
Starting to build assets...
Copied directory: /Users/cobain/robot-build/fw/managed_components/espressif__esp-sr/model/multinet_model/mn7_cn -> /Users/cobain/robot-build/fw/build/temp_build/srmodels/mn7_cn
Added multinet model: mn7_cn
Copied directory: /Users/cobain/robot-build/fw/managed_components/espressif__esp-sr/model/multinet_model/fst -> /Users/cobain/robot-build/fw/build/temp_build/srmodels/fst
Added multinet model: fst
Generated: /Users/cobain/robot-build/fw/build/temp_build/srmodels/srmodels.bin
Copied: /Users/cobain/robot-build/fw/build/temp_build/srmodels/srmodels.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/srmodels.bin
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/cbin/font_puhui_common_20_4.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/font_puhui_common_20_4.bin
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/happy.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/happy.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/delicious.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/delicious.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/neutral.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/neutral.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/crying.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/crying.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/silly.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/silly.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/sad.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/sad.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/surprised.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/surprised.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/confident.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/confident.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/winking.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/winking.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/relaxed.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/relaxed.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/embarrassed.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/embarrassed.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/confused.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/confused.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/loving.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/loving.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/kissy.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/kissy.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/sleepy.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/sleepy.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/angry.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/angry.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/thinking.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/thinking.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/funny.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/funny.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/cool.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/cool.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/shocked.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/shocked.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/laughing.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/laughing.png
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_wifi_high.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_wifi_high.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_iot_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_iot_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_wifi_slash.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_wifi_slash.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_wifi_medium.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_wifi_medium.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_look_close_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_look_close_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_setup.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_setup.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_look_open_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_look_open_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_wifi_low.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_wifi_low.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_personal_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_personal_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_controller.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_controller.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/app_center_bg.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/app_center_bg.png
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_work_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_work_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_indicator_left.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_indicator_left.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_bat_lightning.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_bat_lightning.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_app_center.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_app_center.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_ezdata.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_ezdata.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_meeting_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_meeting_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_sentinel.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_sentinel.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_bell.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_bell.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_chat_60.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_chat_60.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_indicator_right.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_indicator_right.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_dance.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_dance.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_meeting.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_meeting.png
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/setup_stackchan_front_view.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/setup_stackchan_front_view.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/meeting_bg.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/meeting_bg.png
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_ai_agent.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_ai_agent.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_home.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_home.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_iot_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_iot_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_look_close_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_look_close_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_look_open_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_look_open_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_personal_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_personal_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_work_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_work_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_meeting_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_meeting_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_chat_60.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_chat_60.bin
Processed 34 extra files from: /Users/cobain/robot-build/fw/main/assets/assets_bin
Generated: /Users/cobain/robot-build/fw/build/temp_build/assets/index.json
Generated: /Users/cobain/robot-build/fw/build/temp_build/config.json
All files have been merged into assets.bin
Successfully generated assets.bin: /Users/cobain/robot-build/fw/build/generated_assets.bin
Assets file size: 5295.11K (5422195 bytes)
Build completed successfully!

Project build complete. To flash, run:
 idf.py flash
or
 idf.py -p PORT flash
or
 python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0xd000 build/ota_data_initial.bin 0x20000 build/stack-chan.bin 0xa00000 build/generated_assets.bin
or from the "/Users/cobain/robot-build/fw/build" directory
 python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash "@flash_args"
```
build exit code: 0

### 产物
```
stack-chan.bin: mtime=Aug  6 16:18:21 2026 size=3727328
generated_assets.bin: mtime=Aug  6 16:54:01 2026 size=5422195
```

## [2026-08-06 17:30:06] 阶段 1.2 build（修 LV_TIMER_REPEAT_INFINITE → 默认无限循环）
```
Executing action: all (aliases: build)
Running ninja in directory /Users/cobain/robot-build/fw/build
Executing "ninja all"...
[0/2] Re-checking globbed directories...
[1/6] cd /Users/cobain/robot-build/fw/build/esp-idf/esptool_py && /Users/cobain/.espressif/python_env/idf5.5_py3.14_env/bin/python /Users/cobain/esp/esp-idf/components/partition_table/check_sizes.py --offset 0x8000 partition --type app /Users/cobain/robot-build/fw/build/partition_table/partition-table.bin /Users/cobain/robot-build/fw/build/stack-chan.bin
stack-chan.bin binary size 0x38dfe0 bytes. Smallest app partition is 0x4f0000 bytes. 0x162020 bytes (28%) free.
[2/6] Performing build step for 'bootloader'
[1/1] cd /Users/cobain/robot-build/fw/build/bootloader/esp-idf/esptool_py && /Users/cobain/.espressif/python_env/idf5.5_py3.14_env/bin/python /Users/cobain/esp/esp-idf/components/partition_table/check_sizes.py --offset 0x8000 bootloader 0x0 /Users/cobain/robot-build/fw/build/bootloader/bootloader.bin
Bootloader binary size 0x5cf0 bytes. 0x2310 bytes (27%) free.
[3/6] No install step for 'bootloader'
[4/6] Completed 'bootloader'
[5/6] Building default assets.bin based on configuration
Building default assets...
  sdkconfig: /Users/cobain/robot-build/fw/sdkconfig
  builtin_text_font: font_puhui_basic_20_4
  emoji_collection: twemoji_64
  output: /Users/cobain/robot-build/fw/build/generated_assets.bin
  Note: Found wakenet models ['wn9_histackchan_tts3'] but wake word type is not ESP/AFE, skipping
  multinet models: mn7_cn, fst (will be packaged)
  custom wake word: ni hao mi bao (你好，米宝)
  wake word language: cn
  wake word threshold: 0.2
Starting to build assets...
Copied directory: /Users/cobain/robot-build/fw/managed_components/espressif__esp-sr/model/multinet_model/mn7_cn -> /Users/cobain/robot-build/fw/build/temp_build/srmodels/mn7_cn
Added multinet model: mn7_cn
Copied directory: /Users/cobain/robot-build/fw/managed_components/espressif__esp-sr/model/multinet_model/fst -> /Users/cobain/robot-build/fw/build/temp_build/srmodels/fst
Added multinet model: fst
Generated: /Users/cobain/robot-build/fw/build/temp_build/srmodels/srmodels.bin
Copied: /Users/cobain/robot-build/fw/build/temp_build/srmodels/srmodels.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/srmodels.bin
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/cbin/font_puhui_common_20_4.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/font_puhui_common_20_4.bin
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/happy.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/happy.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/delicious.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/delicious.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/neutral.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/neutral.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/crying.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/crying.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/silly.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/silly.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/sad.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/sad.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/surprised.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/surprised.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/confident.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/confident.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/winking.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/winking.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/relaxed.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/relaxed.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/embarrassed.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/embarrassed.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/confused.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/confused.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/loving.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/loving.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/kissy.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/kissy.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/sleepy.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/sleepy.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/angry.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/angry.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/thinking.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/thinking.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/funny.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/funny.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/cool.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/cool.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/shocked.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/shocked.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/laughing.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/laughing.png
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_wifi_high.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_wifi_high.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_iot_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_iot_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_wifi_slash.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_wifi_slash.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_wifi_medium.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_wifi_medium.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_look_close_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_look_close_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_setup.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_setup.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_look_open_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_look_open_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_wifi_low.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_wifi_low.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_personal_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_personal_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_controller.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_controller.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/app_center_bg.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/app_center_bg.png
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_work_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_work_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_indicator_left.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_indicator_left.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_bat_lightning.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_bat_lightning.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_app_center.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_app_center.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_ezdata.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_ezdata.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_meeting_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_meeting_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_sentinel.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_sentinel.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_bell.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_bell.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_chat_60.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_chat_60.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_indicator_right.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_indicator_right.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_dance.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_dance.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_meeting.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_meeting.png
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/setup_stackchan_front_view.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/setup_stackchan_front_view.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/meeting_bg.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/meeting_bg.png
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_ai_agent.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_ai_agent.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_home.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_home.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_iot_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_iot_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_look_close_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_look_close_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_look_open_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_look_open_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_personal_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_personal_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_work_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_work_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_meeting_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_meeting_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_chat_60.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_chat_60.bin
Processed 34 extra files from: /Users/cobain/robot-build/fw/main/assets/assets_bin
Generated: /Users/cobain/robot-build/fw/build/temp_build/assets/index.json
Generated: /Users/cobain/robot-build/fw/build/temp_build/config.json
All files have been merged into assets.bin
Successfully generated assets.bin: /Users/cobain/robot-build/fw/build/generated_assets.bin
Assets file size: 5295.11K (5422195 bytes)
Build completed successfully!

Project build complete. To flash, run:
 idf.py flash
or
 idf.py -p PORT flash
or
 python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0xd000 build/ota_data_initial.bin 0x20000 build/stack-chan.bin 0xa00000 build/generated_assets.bin
or from the "/Users/cobain/robot-build/fw/build" directory
 python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash "@flash_args"
```
build exit code: 0

### 产物
```
stack-chan.bin: mtime=Aug  6 16:18:21 2026 size=3727328
generated_assets.bin: mtime=Aug  6 17:30:13 2026 size=5422195
```

## [2026-08-06 18:06:10] 阶段 1.3 build（launcher 9→4 + 米宝图标 + 屏保闭眼图更新）
```
Executing action: all (aliases: build)
Running ninja in directory /Users/cobain/robot-build/fw/build
Executing "ninja all"...
[0/2] Re-checking globbed directories...
-- GLOB mismatch!
The following files were added:
  +/Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_chat_150.bin
-- GLOB mismatch!
The following files were added:
  +/Users/cobain/robot-build/fw/main/assets/mibao_bin/mibao_chat_150.bin
[1/2] Re-running CMake...
-- Minimal build - OFF
-- Building ESP-IDF components for target esp32s3
NOTICE: Skipping optional dependency: espressif/cjson
NOTICE: Skipping optional dependency: espressif/esp_h264
NOTICE: Skipping optional dependency: espressif/esp_ipa
NOTICE: Skipping optional dependency: espressif/usb
NOTICE: Skipping optional dependency: espressif/esp_lcd_jd9365
NOTICE: Skipping optional dependency: waveshare/esp_lcd_st7703
NOTICE: Skipping optional dependency: espressif/esp32_p4_function_ev_board
NOTICE: Skipping optional dependency: espressif/esp_lcd_ili9881c
NOTICE: Skipping optional dependency: espressif/esp_lcd_ek79007
NOTICE: Skipping optional dependency: espressif/esp_hosted
NOTICE: Skipping optional dependency: espressif/esp_wifi_remote
NOTICE: Skipping optional dependency: espfriends/servo_dog_ctrl
NOTICE: Skipping optional dependency: llgok/cpp_bus_driver
NOTICE: Skipping optional dependency: espressif/mqtt
NOTICE: Processing 60 dependencies:
NOTICE: [1/60] 78/esp-ml307 (3.6.5)
NOTICE: [2/60] 78/esp-wifi-connect (3.1.5)
NOTICE: [3/60] 78/esp_lcd_nv3023 (1.0.1)
NOTICE: [4/60] 78/uart-eth-modem (0.3.5)
NOTICE: [5/60] 78/uart-uhci (0.2.2)
NOTICE: [6/60] 78/xiaozhi-fonts (1.6.0)
NOTICE: [7/60] espressif/adc_battery_estimation (0.2.2)
NOTICE: [8/60] espressif/adc_mic (0.2.3)
NOTICE: [9/60] espressif/bmi270_sensor (0.1.2)
NOTICE: [10/60] espressif/button (4.1.7)
NOTICE: [11/60] espressif/cmake_utilities (0.5.3)
NOTICE: [12/60] espressif/dl_fft (0.6.0)
NOTICE: [13/60] espressif/esp-dsp (1.7.0)
NOTICE: [14/60] espressif/esp-sr (2.3.1)
NOTICE: [15/60] espressif/esp32-camera (2.1.7)
NOTICE: [16/60] espressif/esp_audio_codec (2.4.1)
NOTICE: [17/60] espressif/esp_audio_effects (1.2.1)
NOTICE: [18/60] espressif/esp_cam_sensor (1.5.2)
NOTICE: [19/60] espressif/esp_codec_dev (1.5.4)
NOTICE: [20/60] espressif/esp_image_effects (1.1.0)
NOTICE: [21/60] espressif/esp_io_expander (1.2.1)
NOTICE: [22/60] espressif/esp_io_expander_tca9554 (2.0.0)
NOTICE: [23/60] espressif/esp_io_expander_tca95xx_16bit (2.0.2)
NOTICE: [24/60] espressif/esp_jpeg (1.3.1)
NOTICE: [25/60] espressif/esp_lcd_axs15231b (1.0.1~1)
NOTICE: [26/60] espressif/esp_lcd_co5300 (2.1.0)
NOTICE: [27/60] espressif/esp_lcd_gc9a01 (2.0.1)
NOTICE: [28/60] espressif/esp_lcd_ili9341 (1.2.0)
NOTICE: [29/60] espressif/esp_lcd_panel_io_additions (1.0.1~1)
NOTICE: [30/60] espressif/esp_lcd_spd2010 (1.0.2)
NOTICE: [31/60] espressif/esp_lcd_st7701 (1.1.5)
NOTICE: [32/60] espressif/esp_lcd_st77916 (1.0.1)
NOTICE: [33/60] espressif/esp_lcd_st7796 (1.3.5)
NOTICE: [34/60] espressif/esp_lcd_touch (1.2.1)
NOTICE: [35/60] espressif/esp_lcd_touch_cst816s (1.1.1~2)
NOTICE: [36/60] espressif/esp_lcd_touch_ft5x06 (1.0.7)
NOTICE: [37/60] espressif/esp_lcd_touch_gt1151 (1.1.0~2)
NOTICE: [38/60] espressif/esp_lcd_touch_gt911 (1.2.0~3)
NOTICE: [39/60] espressif/esp_lcd_touch_st7123 (1.0.2)
NOTICE: [40/60] espressif/esp_lvgl_port (2.7.2)
NOTICE: [41/60] espressif/esp_mmap_assets (2.0.0)
NOTICE: [42/60] espressif/esp_new_jpeg (0.6.1)
NOTICE: [43/60] espressif/esp_sccb_intf (0.0.8)
NOTICE: [44/60] espressif/esp_video (1.3.1)
NOTICE: [45/60] espressif/i2c_bus (1.5.2)
NOTICE: [46/60] espressif/iot_eth (0.1.0)
NOTICE: [47/60] espressif/iot_usbh_cdc (3.1.0)
NOTICE: [48/60] espressif/iot_usbh_rndis (0.3.1)
NOTICE: [49/60] espressif/knob (1.1.0)
NOTICE: [50/60] espressif/led_strip (3.0.3)
NOTICE: [51/60] espressif/usb_host_uvc (2.3.1)
NOTICE: [52/60] espressif2022/image_player (1.1.1)
NOTICE: [53/60] lvgl/lvgl (9.4.0)
NOTICE: [54/60] tny-robotics/sh1106-esp-idf (1.0.1)
NOTICE: [55/60] txp666/otto-emoji-gif-component (1.3.0)
NOTICE: [56/60] waveshare/custom_io_expander_ch32v003 (1.0.2)
NOTICE: [57/60] waveshare/esp_lcd_sh8601 (1.0.2)
NOTICE: [58/60] waveshare/esp_lcd_touch_cst9217 (1.0.4)
NOTICE: [59/60] wvirgil123/sscma_client (1.0.2)
NOTICE: [60/60] idf (5.5.3)
-- Using en-US fallback for missing audio: 0.ogg
-- Using en-US fallback for missing audio: 1.ogg
-- Using en-US fallback for missing audio: 2.ogg
-- Using en-US fallback for missing audio: 3.ogg
-- Using en-US fallback for missing audio: 4.ogg
-- Using en-US fallback for missing audio: 5.ogg
-- Using en-US fallback for missing audio: 6.ogg
-- Using en-US fallback for missing audio: 7.ogg
-- Using en-US fallback for missing audio: 8.ogg
-- Using en-US fallback for missing audio: 9.ogg
-- Using en-US fallback for missing audio: activation.ogg
-- Using en-US fallback for missing audio: err_pin.ogg
-- Using en-US fallback for missing audio: err_reg.ogg
-- Using en-US fallback for missing audio: upgrade.ogg
-- Using en-US fallback for missing audio: welcome.ogg
-- Using en-US fallback for missing audio: wificonfig.ogg
-- ESP-TEE is currently supported only on the esp32c6;esp32h2;esp32c5 SoCs
NOTICE: Skipping optional dependency: espressif/esp_lcd_jd9365
NOTICE: Skipping optional dependency: waveshare/esp_lcd_st7703
NOTICE: Skipping optional dependency: espressif/esp32_p4_function_ev_board
NOTICE: Skipping optional dependency: espressif/esp_lcd_ili9881c
NOTICE: Skipping optional dependency: espressif/esp_lcd_ek79007
NOTICE: Skipping optional dependency: espressif/esp_hosted
NOTICE: Skipping optional dependency: espressif/esp_wifi_remote
NOTICE: Skipping optional dependency: espfriends/servo_dog_ctrl
NOTICE: Skipping optional dependency: llgok/cpp_bus_driver
NOTICE: Skipping optional dependency: espressif/cjson
NOTICE: Skipping optional dependency: espressif/esp_h264
NOTICE: Skipping optional dependency: espressif/esp_ipa
NOTICE: Skipping optional dependency: espressif/usb
-- Project sdkconfig file /Users/cobain/robot-build/fw/sdkconfig
Loading defaults file /Users/cobain/robot-build/fw/sdkconfig.defaults...
-- Compiler supported targets: xtensa-esp-elf
-- USING O3
-- App "stack-chan" version: 1.4.3
-- Adding linker script /Users/cobain/robot-build/fw/build/esp-idf/esp_system/ld/memory.ld
-- Adding linker script /Users/cobain/robot-build/fw/build/esp-idf/esp_system/ld/sections.ld.in
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.api.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.bt_funcs.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.libgcc.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.wdt.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.version.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.ble_cca.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.ble_test.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.libc.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.newlib.ld
-- Adding linker script /Users/cobain/esp/esp-idf/components/soc/esp32s3/ld/esp32s3.peripherals.ld
-- ESP_NOW: 2.5.2
78/esp_wifi_connect: IDF_VER is v5.5.3
78/esp_wifi_connect: idf_component_register for v5 used
-- ESP_LCD_NV3023: 1.0.1
-- IOT_ETH: 0.1.0
-- ADC_BATTERY_ESTIMATION: 0.2.2
-- ADC_MIC: 0.2.3
-- I2C_BUS: 1.5.2
-- BUTTON: 4.1.7
-- ESP_LCD_AXS15231B: 1.0.1
-- ESP_LCD_CO5300: 2.1.0
-- ESP_LCD_GC9A01: 2.0.1
-- ESP_LCD_ILI9341: 1.2.0
-- ESP_LCD_PANEL_IO_ADDITIONS: 1.0.1
-- ESP_LCD_SPD2010: 1.0.2
-- ESP_LCD_ST7701: 1.1.5
-- ESP_LCD_ST77916: 1.0.1
-- ESP_LCD_ST7796: 1.3.5
-- ESP_LCD_TOUCH_ST7123: 1.0.2
-- LVGL version: 9.4.0
-- ESP_MMAP_ASSETS: 2.0.0
-- ESP_CAM_SENSOR: 1.5.2
-- ESP_VIDEO: 1.3.1
-- IOT_USBH_CDC: 3.1.0
-- IOT_USBH_RNDIS: 0.3.1
-- KNOB: 1.1.0
-- Otto Emoji GIF Component: gifs only at /Users/cobain/robot-build/fw/managed_components/txp666__otto-emoji-gif-component/gifs
-- ESP_LCD_SH8601: 1.0.2
-- 米宝资源: 已同步 /Users/cobain/robot-build/fw/main/assets/mibao_bin/mibao_chat_150.bin;/Users/cobain/robot-build/fw/main/assets/mibao_bin/mibao_chat_60.bin;/Users/cobain/robot-build/fw/main/assets/mibao_bin/mibao_iot_150.bin;/Users/cobain/robot-build/fw/main/assets/mibao_bin/mibao_look_close_150.bin;/Users/cobain/robot-build/fw/main/assets/mibao_bin/mibao_look_open_150.bin;/Users/cobain/robot-build/fw/main/assets/mibao_bin/mibao_meeting_150.bin;/Users/cobain/robot-build/fw/main/assets/mibao_bin/mibao_personal_150.bin;/Users/cobain/robot-build/fw/main/assets/mibao_bin/mibao_work_150.bin 个 bin 到 /Users/cobain/robot-build/fw/main/assets/assets_bin
-- Default assets build configured: /Users/cobain/robot-build/fw/build/generated_assets.bin
-- Generated default assets flash configured: /Users/cobain/robot-build/fw/build/generated_assets.bin -> assets partition
-- Component idf::main will be linked with -Wl,--whole-archive
-- Components: 78__esp-ml307 78__esp-wifi-connect 78__esp_lcd_nv3023 78__uart-eth-modem 78__uart-uhci 78__xiaozhi-fonts ArduinoJson app_trace app_update bootloader bootloader_support bt cmock console cxx driver efuse esp-now esp-tls esp_adc esp_app_format esp_bootloader_format esp_coex esp_common esp_driver_ana_cmpr esp_driver_bitscrambler esp_driver_cam esp_driver_dac esp_driver_gpio esp_driver_gptimer esp_driver_i2c esp_driver_i2s esp_driver_isp esp_driver_jpeg esp_driver_ledc esp_driver_mcpwm esp_driver_parlio esp_driver_pcnt esp_driver_ppa esp_driver_rmt esp_driver_sdio esp_driver_sdm esp_driver_sdmmc esp_driver_sdspi esp_driver_spi esp_driver_touch_sens esp_driver_tsens esp_driver_twai esp_driver_uart esp_driver_usb_serial_jtag esp_eth esp_event esp_gdbstub esp_hal_ieee802154 esp_hid esp_http_client esp_http_server esp_https_ota esp_https_server esp_hw_support esp_lcd esp_local_ctrl esp_mm esp_netif esp_netif_stack esp_partition esp_phy esp_pm esp_psram esp_ringbuf esp_rom esp_security esp_system esp_timer esp_vfs_console esp_wifi espcoredump espressif2022__image_player espressif__adc_battery_estimation espressif__adc_mic espressif__bmi270_sensor espressif__button espressif__cmake_utilities espressif__dl_fft espressif__esp-dsp espressif__esp-sr espressif__esp32-camera espressif__esp_audio_codec espressif__esp_audio_effects espressif__esp_cam_sensor espressif__esp_codec_dev espressif__esp_image_effects espressif__esp_io_expander espressif__esp_io_expander_tca9554 espressif__esp_io_expander_tca95xx_16bit espressif__esp_jpeg espressif__esp_lcd_axs15231b espressif__esp_lcd_co5300 espressif__esp_lcd_gc9a01 espressif__esp_lcd_ili9341 espressif__esp_lcd_panel_io_additions espressif__esp_lcd_spd2010 espressif__esp_lcd_st7701 espressif__esp_lcd_st77916 espressif__esp_lcd_st7796 espressif__esp_lcd_touch espressif__esp_lcd_touch_cst816s espressif__esp_lcd_touch_ft5x06 espressif__esp_lcd_touch_gt1151 espressif__esp_lcd_touch_gt911 espressif__esp_lcd_touch_st7123 espressif__esp_lvgl_port espressif__esp_mmap_assets espressif__esp_new_jpeg espressif__esp_sccb_intf espressif__esp_video espressif__i2c_bus espressif__iot_eth espressif__iot_usbh_cdc espressif__iot_usbh_rndis espressif__knob espressif__led_strip espressif__usb_host_uvc esptool_py fatfs freertos hal heap http_parser idf_test ieee802154 json log lvgl__lvgl lwip main mbedtls mooncake mooncake_log mqtt newlib nvs_flash nvs_sec_provider openthread partition_table perfmon protobuf-c protocomm pthread rt sdmmc smooth_ui_toolkit soc spi_flash spiffs tcp_transport tny-robotics__sh1106-esp-idf touch_element txp666__otto-emoji-gif-component ulp unity usb vfs waveshare__custom_io_expander_ch32v003 waveshare__esp_lcd_sh8601 waveshare__esp_lcd_touch_cst9217 wear_levelling wifi_provisioning wpa_supplicant wvirgil123__sscma_client xtensa
-- Component paths: /Users/cobain/robot-build/fw/managed_components/78__esp-ml307 /Users/cobain/robot-build/fw/managed_components/78__esp-wifi-connect /Users/cobain/robot-build/fw/managed_components/78__esp_lcd_nv3023 /Users/cobain/robot-build/fw/managed_components/78__uart-eth-modem /Users/cobain/robot-build/fw/managed_components/78__uart-uhci /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts /Users/cobain/robot-build/fw/components/ArduinoJson /Users/cobain/esp/esp-idf/components/app_trace /Users/cobain/esp/esp-idf/components/app_update /Users/cobain/esp/esp-idf/components/bootloader /Users/cobain/esp/esp-idf/components/bootloader_support /Users/cobain/esp/esp-idf/components/bt /Users/cobain/esp/esp-idf/components/cmock /Users/cobain/esp/esp-idf/components/console /Users/cobain/esp/esp-idf/components/cxx /Users/cobain/esp/esp-idf/components/driver /Users/cobain/esp/esp-idf/components/efuse /Users/cobain/robot-build/fw/components/esp-now /Users/cobain/esp/esp-idf/components/esp-tls /Users/cobain/esp/esp-idf/components/esp_adc /Users/cobain/esp/esp-idf/components/esp_app_format /Users/cobain/esp/esp-idf/components/esp_bootloader_format /Users/cobain/esp/esp-idf/components/esp_coex /Users/cobain/esp/esp-idf/components/esp_common /Users/cobain/esp/esp-idf/components/esp_driver_ana_cmpr /Users/cobain/esp/esp-idf/components/esp_driver_bitscrambler /Users/cobain/esp/esp-idf/components/esp_driver_cam /Users/cobain/esp/esp-idf/components/esp_driver_dac /Users/cobain/esp/esp-idf/components/esp_driver_gpio /Users/cobain/esp/esp-idf/components/esp_driver_gptimer /Users/cobain/esp/esp-idf/components/esp_driver_i2c /Users/cobain/esp/esp-idf/components/esp_driver_i2s /Users/cobain/esp/esp-idf/components/esp_driver_isp /Users/cobain/esp/esp-idf/components/esp_driver_jpeg /Users/cobain/esp/esp-idf/components/esp_driver_ledc /Users/cobain/esp/esp-idf/components/esp_driver_mcpwm /Users/cobain/esp/esp-idf/components/esp_driver_parlio /Users/cobain/esp/esp-idf/components/esp_driver_pcnt /Users/cobain/esp/esp-idf/components/esp_driver_ppa /Users/cobain/esp/esp-idf/components/esp_driver_rmt /Users/cobain/esp/esp-idf/components/esp_driver_sdio /Users/cobain/esp/esp-idf/components/esp_driver_sdm /Users/cobain/esp/esp-idf/components/esp_driver_sdmmc /Users/cobain/esp/esp-idf/components/esp_driver_sdspi /Users/cobain/esp/esp-idf/components/esp_driver_spi /Users/cobain/esp/esp-idf/components/esp_driver_touch_sens /Users/cobain/esp/esp-idf/components/esp_driver_tsens /Users/cobain/esp/esp-idf/components/esp_driver_twai /Users/cobain/esp/esp-idf/components/esp_driver_uart /Users/cobain/esp/esp-idf/components/esp_driver_usb_serial_jtag /Users/cobain/esp/esp-idf/components/esp_eth /Users/cobain/esp/esp-idf/components/esp_event /Users/cobain/esp/esp-idf/components/esp_gdbstub /Users/cobain/esp/esp-idf/components/esp_hal_ieee802154 /Users/cobain/esp/esp-idf/components/esp_hid /Users/cobain/esp/esp-idf/components/esp_http_client /Users/cobain/esp/esp-idf/components/esp_http_server /Users/cobain/esp/esp-idf/components/esp_https_ota /Users/cobain/esp/esp-idf/components/esp_https_server /Users/cobain/esp/esp-idf/components/esp_hw_support /Users/cobain/esp/esp-idf/components/esp_lcd /Users/cobain/esp/esp-idf/components/esp_local_ctrl /Users/cobain/esp/esp-idf/components/esp_mm /Users/cobain/esp/esp-idf/components/esp_netif /Users/cobain/esp/esp-idf/components/esp_netif_stack /Users/cobain/esp/esp-idf/components/esp_partition /Users/cobain/esp/esp-idf/components/esp_phy /Users/cobain/esp/esp-idf/components/esp_pm /Users/cobain/esp/esp-idf/components/esp_psram /Users/cobain/esp/esp-idf/components/esp_ringbuf /Users/cobain/esp/esp-idf/components/esp_rom /Users/cobain/esp/esp-idf/components/esp_security /Users/cobain/esp/esp-idf/components/esp_system /Users/cobain/esp/esp-idf/components/esp_timer /Users/cobain/esp/esp-idf/components/esp_vfs_console /Users/cobain/esp/esp-idf/components/esp_wifi /Users/cobain/esp/esp-idf/components/espcoredump /Users/cobain/robot-build/fw/managed_components/espressif2022__image_player /Users/cobain/robot-build/fw/managed_components/espressif__adc_battery_estimation /Users/cobain/robot-build/fw/managed_components/espressif__adc_mic /Users/cobain/robot-build/fw/managed_components/espressif__bmi270_sensor /Users/cobain/robot-build/fw/managed_components/espressif__button /Users/cobain/robot-build/fw/managed_components/espressif__cmake_utilities /Users/cobain/robot-build/fw/managed_components/espressif__dl_fft /Users/cobain/robot-build/fw/managed_components/espressif__esp-dsp /Users/cobain/robot-build/fw/managed_components/espressif__esp-sr /Users/cobain/robot-build/fw/managed_components/espressif__esp32-camera /Users/cobain/robot-build/fw/managed_components/espressif__esp_audio_codec /Users/cobain/robot-build/fw/managed_components/espressif__esp_audio_effects /Users/cobain/robot-build/fw/managed_components/espressif__esp_cam_sensor /Users/cobain/robot-build/fw/managed_components/espressif__esp_codec_dev /Users/cobain/robot-build/fw/managed_components/espressif__esp_image_effects /Users/cobain/robot-build/fw/managed_components/espressif__esp_io_expander /Users/cobain/robot-build/fw/managed_components/espressif__esp_io_expander_tca9554 /Users/cobain/robot-build/fw/managed_components/espressif__esp_io_expander_tca95xx_16bit /Users/cobain/robot-build/fw/managed_components/espressif__esp_jpeg /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_axs15231b /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_co5300 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_gc9a01 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_ili9341 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_panel_io_additions /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_spd2010 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_st7701 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_st77916 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_st7796 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_touch /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_touch_cst816s /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_touch_ft5x06 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_touch_gt1151 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_touch_gt911 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lcd_touch_st7123 /Users/cobain/robot-build/fw/managed_components/espressif__esp_lvgl_port /Users/cobain/robot-build/fw/managed_components/espressif__esp_mmap_assets /Users/cobain/robot-build/fw/managed_components/espressif__esp_new_jpeg /Users/cobain/robot-build/fw/managed_components/espressif__esp_sccb_intf /Users/cobain/robot-build/fw/managed_components/espressif__esp_video /Users/cobain/robot-build/fw/managed_components/espressif__i2c_bus /Users/cobain/robot-build/fw/managed_components/espressif__iot_eth /Users/cobain/robot-build/fw/managed_components/espressif__iot_usbh_cdc /Users/cobain/robot-build/fw/managed_components/espressif__iot_usbh_rndis /Users/cobain/robot-build/fw/managed_components/espressif__knob /Users/cobain/robot-build/fw/managed_components/espressif__led_strip /Users/cobain/robot-build/fw/managed_components/espressif__usb_host_uvc /Users/cobain/esp/esp-idf/components/esptool_py /Users/cobain/esp/esp-idf/components/fatfs /Users/cobain/esp/esp-idf/components/freertos /Users/cobain/esp/esp-idf/components/hal /Users/cobain/esp/esp-idf/components/heap /Users/cobain/esp/esp-idf/components/http_parser /Users/cobain/esp/esp-idf/components/idf_test /Users/cobain/esp/esp-idf/components/ieee802154 /Users/cobain/esp/esp-idf/components/json /Users/cobain/esp/esp-idf/components/log /Users/cobain/robot-build/fw/managed_components/lvgl__lvgl /Users/cobain/esp/esp-idf/components/lwip /Users/cobain/robot-build/fw/main /Users/cobain/esp/esp-idf/components/mbedtls /Users/cobain/robot-build/fw/components/mooncake /Users/cobain/robot-build/fw/components/mooncake_log /Users/cobain/esp/esp-idf/components/mqtt /Users/cobain/esp/esp-idf/components/newlib /Users/cobain/esp/esp-idf/components/nvs_flash /Users/cobain/esp/esp-idf/components/nvs_sec_provider /Users/cobain/esp/esp-idf/components/openthread /Users/cobain/esp/esp-idf/components/partition_table /Users/cobain/esp/esp-idf/components/perfmon /Users/cobain/esp/esp-idf/components/protobuf-c /Users/cobain/esp/esp-idf/components/protocomm /Users/cobain/esp/esp-idf/components/pthread /Users/cobain/esp/esp-idf/components/rt /Users/cobain/esp/esp-idf/components/sdmmc /Users/cobain/robot-build/fw/components/smooth_ui_toolkit /Users/cobain/esp/esp-idf/components/soc /Users/cobain/esp/esp-idf/components/spi_flash /Users/cobain/esp/esp-idf/components/spiffs /Users/cobain/esp/esp-idf/components/tcp_transport /Users/cobain/robot-build/fw/managed_components/tny-robotics__sh1106-esp-idf /Users/cobain/esp/esp-idf/components/touch_element /Users/cobain/robot-build/fw/managed_components/txp666__otto-emoji-gif-component /Users/cobain/esp/esp-idf/components/ulp /Users/cobain/esp/esp-idf/components/unity /Users/cobain/esp/esp-idf/components/usb /Users/cobain/esp/esp-idf/components/vfs /Users/cobain/robot-build/fw/managed_components/waveshare__custom_io_expander_ch32v003 /Users/cobain/robot-build/fw/managed_components/waveshare__esp_lcd_sh8601 /Users/cobain/robot-build/fw/managed_components/waveshare__esp_lcd_touch_cst9217 /Users/cobain/esp/esp-idf/components/wear_levelling /Users/cobain/esp/esp-idf/components/wifi_provisioning /Users/cobain/esp/esp-idf/components/wpa_supplicant /Users/cobain/robot-build/fw/managed_components/wvirgil123__sscma_client /Users/cobain/esp/esp-idf/components/xtensa
-- Configuring done (35.8s)
-- Generating done (1.4s)
-- Build files have been written to: /Users/cobain/robot-build/fw/build
[0/4] Re-checking globbed directories...
[1/19] Performing build step for 'bootloader'
[1/1] cd /Users/cobain/robot-build/fw/build/bootloader/esp-idf/esptool_py && /Users/cobain/.espressif/python_env/idf5.5_py3.14_env/bin/python /Users/cobain/esp/esp-idf/components/partition_table/check_sizes.py --offset 0x8000 bootloader 0x0 /Users/cobain/robot-build/fw/build/bootloader/bootloader.bin
Bootloader binary size 0x5cf0 bytes. 0x2310 bytes (27%) free.
[2/19] No install step for 'bootloader'
[3/19] Completed 'bootloader'
[4/19] Building default assets.bin based on configuration
Building default assets...
  sdkconfig: /Users/cobain/robot-build/fw/sdkconfig
  builtin_text_font: font_puhui_basic_20_4
  emoji_collection: twemoji_64
  output: /Users/cobain/robot-build/fw/build/generated_assets.bin
  Note: Found wakenet models ['wn9_histackchan_tts3'] but wake word type is not ESP/AFE, skipping
  multinet models: mn7_cn, fst (will be packaged)
  custom wake word: ni hao mi bao (你好，米宝)
  wake word language: cn
  wake word threshold: 0.2
Starting to build assets...
Copied directory: /Users/cobain/robot-build/fw/managed_components/espressif__esp-sr/model/multinet_model/mn7_cn -> /Users/cobain/robot-build/fw/build/temp_build/srmodels/mn7_cn
Added multinet model: mn7_cn
Copied directory: /Users/cobain/robot-build/fw/managed_components/espressif__esp-sr/model/multinet_model/fst -> /Users/cobain/robot-build/fw/build/temp_build/srmodels/fst
Added multinet model: fst
Generated: /Users/cobain/robot-build/fw/build/temp_build/srmodels/srmodels.bin
Copied: /Users/cobain/robot-build/fw/build/temp_build/srmodels/srmodels.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/srmodels.bin
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/cbin/font_puhui_common_20_4.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/font_puhui_common_20_4.bin
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/happy.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/happy.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/delicious.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/delicious.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/neutral.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/neutral.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/crying.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/crying.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/silly.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/silly.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/sad.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/sad.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/surprised.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/surprised.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/confident.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/confident.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/winking.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/winking.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/relaxed.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/relaxed.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/embarrassed.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/embarrassed.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/confused.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/confused.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/loving.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/loving.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/kissy.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/kissy.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/sleepy.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/sleepy.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/angry.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/angry.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/thinking.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/thinking.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/funny.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/funny.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/cool.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/cool.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/shocked.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/shocked.png
Copied: /Users/cobain/robot-build/fw/managed_components/78__xiaozhi-fonts/png/twemoji_64/laughing.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/laughing.png
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_wifi_high.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_wifi_high.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_iot_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_iot_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_wifi_slash.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_wifi_slash.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_wifi_medium.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_wifi_medium.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_look_close_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_look_close_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_setup.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_setup.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_look_open_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_look_open_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_wifi_low.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_wifi_low.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_personal_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_personal_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_controller.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_controller.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/app_center_bg.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/app_center_bg.png
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_chat_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_chat_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_work_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_work_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_indicator_left.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_indicator_left.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_bat_lightning.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_bat_lightning.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_app_center.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_app_center.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_ezdata.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_ezdata.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_meeting_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_meeting_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_sentinel.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_sentinel.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_bell.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_bell.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_chat_60.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_chat_60.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_indicator_right.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_indicator_right.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_dance.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_dance.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_meeting.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_meeting.png
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/setup_stackchan_front_view.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/setup_stackchan_front_view.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/meeting_bg.png -> /Users/cobain/robot-build/fw/build/temp_build/assets/meeting_bg.png
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_ai_agent.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_ai_agent.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/icon_home.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/icon_home.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_iot_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_iot_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_look_close_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_look_close_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_look_open_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_look_open_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_personal_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_personal_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_chat_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_chat_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_work_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_work_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_meeting_150.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_meeting_150.bin
Copied: /Users/cobain/robot-build/fw/main/assets/assets_bin/mibao_bin/mibao_chat_60.bin -> /Users/cobain/robot-build/fw/build/temp_build/assets/mibao_chat_60.bin
Processed 36 extra files from: /Users/cobain/robot-build/fw/main/assets/assets_bin
Generated: /Users/cobain/robot-build/fw/build/temp_build/assets/index.json
Generated: /Users/cobain/robot-build/fw/build/temp_build/config.json
All files have been merged into assets.bin
Successfully generated assets.bin: /Users/cobain/robot-build/fw/build/generated_assets.bin
Assets file size: 5383.12K (5512313 bytes)
Build completed successfully!
[5/19] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_mibao_iot/app_mibao_iot.cpp.obj
[6/19] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/main.cpp.obj
[7/19] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_mibao_personal/app_mibao_personal.cpp.obj
[8/19] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_chat/app_chat.cpp.obj
[9/19] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_meeting/app_meeting.cpp.obj
[10/19] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_launcher/app_launcher.cpp.obj
[11/19] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_launcher/view/view.cpp.obj
/Users/cobain/robot-build/fw/main/apps/app_launcher/view/view.cpp: In member function 'void view::LauncherView::handle_state_normal()':
/Users/cobain/robot-build/fw/main/apps/app_launcher/view/view.cpp:578:9: warning: unused variable 'center_set_start_x' [-Wunused-variable]
  578 |     int center_set_start_x = _center_copy_index * set_width_px;
      |         ^~~~~~~~~~~~~~~~~~
[12/19] Linking C static library esp-idf/main/libmain.a
[13/19] Generating esp-idf/esp_system/ld/sections.ld
[14/19] Linking CXX executable stack-chan.elf
[15/19] Generating binary image from built executable
esptool.py v4.12.dev2
Creating esp32s3 image...
Merged 2 ELF sections
Successfully created esp32s3 image.
Generated /Users/cobain/robot-build/fw/build/stack-chan.bin
[16/19] cd /Users/cobain/robot-build/fw/build/esp-idf/esptool_py && /Users/cobain/.espressif/python_env/idf5.5_py3.14_env/bin/python /Users/cobain/esp/esp-idf/components/partition_table/check_sizes.py --offset 0x8000 partition --type app /Users/cobain/robot-build/fw/build/partition_table/partition-table.bin /Users/cobain/robot-build/fw/build/stack-chan.bin
stack-chan.bin binary size 0x3802f0 bytes. Smallest app partition is 0x4f0000 bytes. 0x16fd10 bytes (29%) free.

Project build complete. To flash, run:
 idf.py flash
or
 idf.py -p PORT flash
or
 python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0xd000 build/ota_data_initial.bin 0x20000 build/stack-chan.bin 0xa00000 build/generated_assets.bin
or from the "/Users/cobain/robot-build/fw/build" directory
 python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash "@flash_args"
```
build exit code: 0

### 产物
```
stack-chan.bin: mtime=Aug  6 18:07:53 2026 size=3670768
generated_assets.bin: mtime=Aug  6 18:06:51 2026 size=5512313
```

## [2026-08-10 10:29] AI 对话功能修复 build（增量）
### 改动
- mibao_config.h/.cpp：新增 syncOtaUrlToXiaozhi()，把 ota_url 同步到 xiaozhi 的 NVS "wifi"/ota_url
- mibao_config.cpp：pullDeviceConfigFromServer 拉取 ota_url 后调用 syncOtaUrlToXiaozhi()
- mibao_url.cpp + workers.h：设置页加 OTA URL 输入框 + AI Chat 开关（写 mibao/ai_chat）
- app_launcher.cpp：重启后自动连接已保存的 Wi-Fi（WifiManager::StartStation）
### 产物
stack-chan.bin: 4.0M (2026-08-10 10:29)，分区剩余 18%
### 待验证
- [ ] 设置页 Mibao Server URLs 出现 OTA URL 输入框 + AI Chat 开关
- [ ] 开启 AI Chat 后进对话 app 进入 xiaozhi
- [ ] 重启后 WiFi 自动连接（不再丢网）
