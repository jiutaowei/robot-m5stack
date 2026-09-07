#!/bin/bash
# 阶段：本地文件管理 app 编译（#8 落地）
#
# 改动：
#   - fw/main/apps/app_mibao_files/{app_mibao_files.h,cpp}（新增）
#   - fw/main/apps/apps.h（加 app_mibao_files include）
#   - fw/main/main.cpp（installApp AppMibaoFiles，launcher 显示 6 个 app）
#   - fw/main/apps/app_mibao_files/app_mibao_files.h 增补 _close_requested 字段
#     （home indicator 关闭请求，cpp 已使用但头文件漏写）
#
# 资源：
#   - 占位图标 mibao_work_150.bin（已存在 assets_bin/）
#
# 注意：镜像构建目录 /Users/cobain/robot-build/fw/main 是 symlink → 源码，
# 改源码后镜像自动同步，无需手动 cp。

SRC="/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw"
DST="/Users/cobain/robot-build/fw"
LOG="/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/BUILD_LOG.md"
TS=$(date '+%Y-%m-%d %H:%M:%S')

cd "$DST" || exit 1
source ~/esp/esp-idf/export.sh

BUILD_OUT=/tmp/robot_build_stage_files.log
idf.py build > "$BUILD_OUT" 2>&1
RC=$?
tail -10 "$BUILD_OUT"

{
    echo ""
    echo "## [$TS] 阶段 files build（#8 本地文件管理 app：列表/播放/删除/重命名）"
    echo ""
    echo "### 改动清单"
    echo "- 新增 fw/main/apps/app_mibao_files/{h,cpp}（文件管理 app）"
    echo "- apps.h：include app_mibao_files.h"
    echo "- main.cpp：installApp(AppMibaoFiles)，launcher 共 6 个 app"
    echo "- app_mibao_files.h：补 _close_requested 字段（修复编译错误）"
    echo ""
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
