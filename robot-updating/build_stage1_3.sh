#!/bin/bash
# 阶段 1.3：launcher 9→4 改造 + 屏保闭眼图更新（增量 build）
#
# 改动范围（main 是 symlink → 源码目录，改动自动生效，无需手动 cp）：
#   - fw/main/apps/app_chat/（新增：对话入口 app）
#   - fw/main/main.cpp（install 4 app：对话/会议/个人灵感/物联网）
#   - fw/main/apps/apps.h（加 app_chat include）
#   - fw/main/apps/app_meeting/app_meeting.cpp（图标 → mibao_meeting_150.bin）
#   - fw/main/apps/app_mibao_{personal,iot}/*.cpp（启用米宝图标）
#   - fw/main/apps/app_launcher/view/view.cpp + app_launcher.cpp（主题色 #2DBE8D）
#   - fw/main/assets/mibao/看你_闭眼.png（替换为用户新提供透明底图，旧图备份 .bak_floodfill.png）
#   - fw/main/assets/mibao_bin/*.bin（png_to_mibao_bin.py 重新生成，新增 mibao_chat_150.bin）
#   - fw/main/assets/assets_bin/（脚本已同步 bin，含嵌套 mibao_bin/ 副本）

SRC="/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw"
DST="/Users/cobain/robot-build/fw"
LOG="/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/BUILD_LOG.md"
TS=$(date '+%Y-%m-%d %H:%M:%S')

cd "$DST" || exit 1
source ~/esp/esp-idf/export.sh

# 保险起见：显式同步关键改动文件（main 为 symlink 时等价于自身复制，无副作用）
cp "$SRC/main/apps/app_launcher/view/screensaver.cpp" "$DST/main/apps/app_launcher/view/screensaver.cpp"

BUILD_OUT=/tmp/robot_build_stage1_3.log
idf.py build > "$BUILD_OUT" 2>&1
RC=$?
tail -5 "$BUILD_OUT"

{
    echo ""
    echo "## [$TS] 阶段 1.3 build（launcher 9→4 + 米宝图标 + 屏保闭眼图更新）"
    echo '```'
    cat "$BUILD_OUT"
    echo '```'
    echo "build exit code: $RC"
    echo ""
    echo "### 产物"
    echo '```'
    stat -f "stack-chan.bin: mtime=%Sm size=%z" build/stack-chan.bin 2>/dev/null
    stat -f "generated_assets.bin: mtime=%Sm size=%z" build/generated_assets.bin 2>/dev/null
    echo '```'
} >> "$LOG"

exit $RC
