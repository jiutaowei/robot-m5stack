#!/bin/bash
# 阶段 1.2：直接 build（修一处编译错误，屏保文件改动 → 增量编译）
# 修：'LV_TIMER_REPEAT_INFINITE' was not declared → lv_timer_create 默认无限循环

SRC="/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw"
DST="/Users/cobain/robot-build/fw"
LOG="/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/BUILD_LOG.md"
TS=$(date '+%Y-%m-%d %H:%M:%S')

cd "$DST" || exit 1
source ~/esp/esp-idf/export.sh

# 同步 screensaver.cpp 修复
cp "$SRC/main/apps/app_launcher/view/screensaver.cpp" "$DST/main/apps/app_launcher/view/screensaver.cpp"

{
    echo ""
    echo "## [$TS] 阶段 1.2 build（修 LV_TIMER_REPEAT_INFINITE → 默认无限循环）"
    echo '```'
    idf.py build 2>&1
    RC=$?
    echo '```'
    echo "build exit code: $RC"
    echo ""
    echo "### 产物"
    echo '```'
    stat -f "stack-chan.bin: mtime=%Sm size=%z" build/stack-chan.bin 2>/dev/null
    stat -f "generated_assets.bin: mtime=%Sm size=%z" build/generated_assets.bin 2>/dev/null
    echo '```'
} | tee -a "$LOG"
