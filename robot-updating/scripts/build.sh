#!/usr/bin/env bash
# 米宝一号固件编译脚本（本机适配版）
# 用法：
#   ./scripts/build.sh              # 增量编译
#   ./scripts/build.sh full         # 全量清理后编译（首次 / 改 CMakeLists 后）
#   ./scripts/build.sh menuconfig   # 打开 menuconfig
#   ./scripts/build.sh clean        # 只清 build/ 目录
#
# 说明（2026-08 本机适配）：
#   1) 本机项目路径不含方括号等特殊字符，CMake file(GLOB) 不会再解析失败，
#      因此直接在源码目录编译，不再需要镜像目录。
#   2) ESP-IDF v5.5.3 装在 ~/esp/esp-idf，用标准 export.sh 激活。
#   3) 保留 mkdir -p build/log，防止 idf.py 写日志目录不存在报错。

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FW_SRC="$SCRIPT_DIR/../fw"
IDF_PATH_HOME="$HOME/esp/esp-idf"
IDF_EXPORT="$IDF_PATH_HOME/export.sh"

activate_idf() {
    if [[ -z "$IDF_PATH" ]] || ! command -v idf.py >/dev/null 2>&1; then
        echo "[build.sh] 激活 ESP-IDF 环境..."
        # shellcheck disable=SC1090
        source "$IDF_EXPORT"
    fi
    if ! command -v idf.py >/dev/null 2>&1; then
        echo "[build.sh] ✗ idf.py 不可用，请确认 ~/esp/esp-idf 已安装" >&2
        exit 1
    fi
    echo "[build.sh] ✓ ESP-IDF ready"
}

main() {
    case "${1:-inc}" in
        full)
            echo "[build.sh] 全量清理 + 编译（首次 5-10 分钟）"
            activate_idf
            cd "$FW_SRC"
            rm -rf build
            mkdir -p build/log
            idf.py reconfigure
            idf.py build
            echo "[build.sh] ✓ 编译完成：$FW_SRC/build/stack-chan.bin"
            ls -lh "$FW_SRC/build/stack-chan.bin"
            ;;
        menuconfig)
            echo "[build.sh] 打开 menuconfig"
            activate_idf
            cd "$FW_SRC"
            mkdir -p build/log
            idf.py menuconfig
            ;;
        clean)
            echo "[build.sh] 仅清理 build 目录"
            cd "$FW_SRC"
            rm -rf build
            echo "[build.sh] ✓ 已清理。下次跑 ./scripts/build.sh 即可。"
            ;;
        inc|*)
            echo "[build.sh] 增量编译"
            activate_idf
            cd "$FW_SRC"
            mkdir -p build/log
            idf.py reconfigure
            idf.py build
            echo "[build.sh] ✓ 编译完成：$FW_SRC/build/stack-chan.bin"
            ls -lh "$FW_SRC/build/stack-chan.bin"
            ;;
    esac
}

main "$@"
