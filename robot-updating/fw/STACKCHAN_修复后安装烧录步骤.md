# StackChan 修复版环境安装与烧录步骤

本文用于在关闭当前 PowerShell 和 VS Code 后，继续完成 StackChan 固件的安装、编译和烧录。

项目目录：

```text
C:\esp\robot\fw
```

ESP-IDF 目录：

```text
C:\esp\robot\esp-idf-env\v5.5.4\esp-idf
```

目标芯片：ESP32-S3

## 1. 关闭旧终端和 VS Code

关闭当前 PowerShell、VS Code，以及其他可能正在使用 ESP-IDF 的终端。

本次安装中断时，部分工具已经写入：

```text
C:\Users\15286\.espressif
```

## 2. 清理中断安装产生的文件

重新打开 PowerShell，先检查目录：

```powershell
Get-ChildItem C:\Users\15286\.espressif -Force
```

如果没有其他 ESP-IDF 版本需要保留，可以删除本次残留：

```powershell
Remove-Item -LiteralPath "C:\Users\15286\.espressif" -Recurse -Force
```

如果只想删除未完成的下载缓存，可以使用：

```powershell
Remove-Item -LiteralPath "C:\Users\15286\.espressif\dist" -Recurse -Force -ErrorAction SilentlyContinue
```

暂时不要删除：

```text
C:\Espressif
```

等 D 盘环境成功编译后，再决定是否清理旧工具。

## 3. 设置 D 盘安装路径

建议使用以下目录：

```text
D:\tmp\espressif
```

在 PowerShell 中执行：

```powershell
$env:IDF_TOOLS_PATH="D:\tmp\espressif"
$env:IDF_PYTHON_ENV_PATH="D:\tmp\espressif\python_env"
```

确认输出为 D 盘路径：

```powershell
$env:IDF_TOOLS_PATH
```

应该显示：

```text
D:\tmp\espressif
```

## 4. 安装 ESP-IDF 工具

```powershell
Set-Location C:\esp\robot\esp-idf-env\v5.5.4\esp-idf
.\install.ps1 esp32s3
```

安装过程中确认日志中的工具路径位于：

```text
D:\tmp\espressif
```

如果网络中断，重新执行上面的安装命令即可。

## 5. 加载并验证 ESP-IDF 环境

安装成功后执行：

```powershell
. .\export.ps1
idf.py --version
xtensa-esp-elf-gcc --version
```

两条版本命令都能正常输出，才继续下一步。

如果新开了 PowerShell，需要再次执行：

```powershell
$env:IDF_TOOLS_PATH="D:\tmp\espressif"
$env:IDF_PYTHON_ENV_PATH="D:\tmp\espressif\python_env"
Set-Location C:\esp\robot\esp-idf-env\v5.5.4\esp-idf
. .\export.ps1
```

## 6. 编译修复后的固件

将构建目录放到 D 盘，减少 C 盘占用：

```powershell
Set-Location C:\esp\robot\fw
idf.py set-target esp32s3
idf.py -B D:\tmp\stackchan-build build
```

编译成功后，应存在：

```text
D:\tmp\stackchan-build\stack-chan.bin
D:\tmp\stackchan-build\bootloader\bootloader.bin
D:\tmp\stackchan-build\partition_table\partition-table.bin
```

如果构建目录异常，可以删除后重新编译：

```powershell
Remove-Item -LiteralPath "D:\tmp\stackchan-build" -Recurse -Force
idf.py -B D:\tmp\stackchan-build build
```

## 7. 查找 StackChan 串口

插入 StackChan 后执行：

```powershell
Get-CimInstance Win32_SerialPort | Select-Object DeviceID,Description
```

假设查到的串口是 `COM7`，后续命令都使用 `COM7`。如果实际是其他串口，请替换。

## 8. 烧录固件

```powershell
Set-Location C:\esp\robot\fw
idf.py -B D:\tmp\stackchan-build -p COM7 flash
```

如果设备没有自动进入下载模式：

1. 按住 BOOT 键。
2. 短按 RESET 键。
3. 松开 BOOT 键。
4. 再次执行烧录命令。

## 9. 查看启动日志

```powershell
idf.py -B D:\tmp\stackchan-build -p COM7 monitor
```

退出监视器：

```text
Ctrl + ]
```

启动日志中应看到：

```text
SD card mounted at /sdcard
```

如果看到：

```text
Failed to mount SD card
```

请检查 microSD 卡是否为 FAT32、卡是否插反，以及卡是否接触良好。

## 10. 测试录音

1. 插入 FAT32 microSD 卡。
2. 启动 StackChan。
3. 进入 Meeting 页面。
4. 确认显示 `SD: ready`。
5. 按下 `Start`。
6. 确认显示 `SD: recording`。
7. 录音几秒。
8. 按 `End`，再按一次确认。
9. 检查 SD 卡中的：

```text
/meetings/meeting_xxx.wav
```

如果仍然失败，屏幕会显示具体错误码，例如：

```text
SD: open e2
```

同时从串口日志中查找：

```text
open recording failed:
```

记录完整的 `errno` 和 `error` 内容。

## 11. 本次代码修复内容

修复文件：

```text
main/hal/board/hal_bridge.h
main/hal/board/stackchan.cc
main/hal/board/config.h
main/apps/app_meeting/app_meeting.cpp
```

修复内容：

- 检查 SD 卡是否真正挂载。
- SD 未挂载时不进入录音流程。
- SD 访问结束后恢复共享 GPIO35 的 LCD 状态。
- 显示文件打开失败的 errno。
- 启动界面准确显示 SD 卡状态。
- SD 卡改用 SPI2，LCD 保持使用 SPI3，避免录音写入时发生 SPI 总线竞争。

## 12. 重要：FATFS 长文件名配置

录音文件名使用了：

```text
/sdcard/meetings/meeting_8035.wav
```

FAT 8.3 文件名限制要求文件主体最多 8 个字符，但 `meeting_8035` 超过了这个限制。如果关闭长文件名支持，`fopen()` 会返回 `errno=22` / `Invalid argument`。

项目已经改为启用：

```text
CONFIG_FATFS_LFN_HEAP=y
CONFIG_FATFS_MAX_LFN=255
```

修改 `sdkconfig` 后必须重新执行 `reconfigure` 和 `build`，再重新烧录，旧固件不会自动更新：

```powershell
Set-Location C:\esp\robot\fw
idf.py -B D:\tmp\stackchan-build reconfigure
idf.py -B D:\tmp\stackchan-build build
idf.py -B D:\tmp\stackchan-build -p COM3 flash
```
