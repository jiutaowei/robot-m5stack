#!/usr/bin/env bash
# 米宝一号固件烧录脚本（本机适配版）
# 用法：
#   ./scripts/flash.sh                  # 用默认串口
#   ./scripts/flash.sh /dev/cu.usbmodemXXXXX   # 指定串口
#   ./scripts/flash.sh erase            # 先擦 flash 再烧（换版本必用）
#   ./scripts/flash.sh <PORT> erase     # 指定串口 + 擦除
#
# 说明（2026-08 本机适配）：
#   1) esptool 从 ESP-IDF 的 python_env 中动态查找，不再硬编码 cobain 路径。
#   2) 固件在源码目录 build/ 下（本机路径无特殊字符，无需镜像目录）。
#   3) bootloader offset 必须 0x0（ESP-IDF v5.5+），换版本前先 erase。

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FW="$SCRIPT_DIR/../fw"

PORT=""
DO_ERASE=0
for arg in "$@"; do
    case "$arg" in
        erase) DO_ERASE=1 ;;
        /dev/*) PORT="$arg" ;;
    esac
done
PORT="${PORT:-/dev/cu.usbmodem141301}"

# 动态查找 esptool.py（ESP-IDF python_env 内）
ESPTOOL=""
for p in "$HOME"/.espressif/python_env/*/bin/esptool.py; do
    if [[ -f "$p" ]]; then ESPTOOL="$p"; break; fi
done
if [[ -z "$ESPTOOL" ]]; then
    echo "[flash.sh] ✗ 未找到 esptool.py（请先安装 ESP-IDF）" >&2
    exit 1
fi

# 校验串口
if [[ ! -e "$PORT" ]]; then
    echo "[flash.sh] ✗ 串口 $PORT 不存在" >&2
    echo "[flash.sh] 当前可用的串口：" >&2
    ls -1 /dev/cu.usb* 2>/dev/null | sed 's/^/    /' >&2
    exit 1
fi

# 校验固件
for f in bootloader/bootloader.bin partition_table/partition-table.bin ota_data_initial.bin stack-chan.bin generated_assets.bin; do
    if [[ ! -f "$FW/build/$f" ]]; then
        echo "[flash.sh] ✗ 缺少 $FW/build/$f（请先编译）" >&2
        exit 1
    fi
done

# 可选：先擦 flash
if [[ $DO_ERASE -eq 1 ]]; then
    echo "[flash.sh] 擦除 flash..."
    "$ESPTOOL" --chip esp32s3 -p "$PORT" -b 460800 erase_flash
fi

# 烧录 5 个文件
echo "[flash.sh] 烧录到 $PORT ..."
"$ESPTOOL" --chip esp32s3 -p "$PORT" -b 460800 \
    --before default_reset --after hard_reset \
    write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
    0x0       "$FW/build/bootloader/bootloader.bin" \
    0x8000    "$FW/build/partition_table/partition-table.bin" \
    0xd000    "$FW/build/ota_data_initial.bin" \
    0x20000   "$FW/build/stack-chan.bin" \
    0xA00000  "$FW/build/generated_assets.bin"

echo ""
echo "[flash.sh] ✓ 烧录完成，设备已自动 reset"
echo "[flash.sh] 看串口日志："
echo "  $ESPTOOL --chip esp32s3 -p $PORT monitor"
