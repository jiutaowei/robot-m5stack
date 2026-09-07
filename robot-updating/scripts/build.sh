#!/usr/bin/env bash
# 米宝一号固件编译脚本（Mac 适配版）
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
#
#   Windows 用户：请勿在 Git Bash 里跑本脚本（路径/环境为 Mac 专属），
#   改用官方「ESP-IDF 5.5 CMD」，在 fw/ 目录执行：
#       idf.py reconfigure && idf.py build
#   详见仓库根目录 WINDOWS_使用指南.md

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FW_SRC="$SCRIPT_DIR/../fw"
IDF_PATH_HOME="$HOME/esp/esp-idf"
IDF_EXPORT="$IDF_PATH_HOME/export.sh"

# 检测是否 Windows（MSYS/Git Bash 下 uname 含 MINGW/MSYS）
detect_windows() {
    case "$(uname -s 2>/dev/null)" in
        MINGW*|MSYS*|CYGWIN*) return 0 ;;
        *) return 1 ;;
    esac
}

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
    if detect_windows; then
        cat >&2 <<'EOF'
[build.sh] ✗ 检测到 Windows 环境。
本脚本为 Mac 适配（依赖 ~/esp/esp-idf 与 export.sh），Windows 请勿运行。

Windows 正确做法：
  1. 开始菜单打开「ESP-IDF 5.5 CMD」
  2. cd 到固件目录（本仓库 robot-updating/fw）
  3. 执行：idf.py reconfigure && idf.py build
  4. 烧录：  idf.py -p COM3 flash

详见仓库根目录 WINDOWS_使用指南.md
EOF
        exit 1
    fi

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
