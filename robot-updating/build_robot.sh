#!/bin/bash
# 一键把 robot-updating 映射到无方括号路径，然后跑编译
# 解决：CMake file(GLOB) 把 [0]/[2]/[15] 当字符类通配符导致 ArduinoJson 找不到

set -e

SRC="/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw"
DST="/Users/cobain/robot-build/fw"

echo "=== 1. 创建无方括号的镜像目录 ==="
mkdir -p "$DST"

echo "=== 2. 复制小文件（CMakeLists.txt / sdkconfig 等）==="
for f in CMakeLists.txt sdkconfig.defaults partitions.csv dependencies.lock repos.json .clang-format; do
    if [ -f "$SRC/$f" ]; then
        cp "$SRC/$f" "$DST/$f"
        echo "  cp $f"
    fi
done

echo "=== 3. 软链大目录（components/main/managed_components/xiaozhi-esp32）==="
for d in components main managed_components xiaozhi-esp32; do
    rm -rf "$DST/$d"
    ln -s "$SRC/$d" "$DST/$d"
    echo "  ln -s $d"
done

echo "=== 4. 验证 ==="
ls -la "$DST/CMakeLists.txt" "$DST/components/ArduinoJson/CMakeLists.txt" 2>&1

echo ""
echo "=== 5. 开始编译（用无方括号路径）==="
cd "$DST"
source ~/esp/esp-idf/export.sh
rm -rf build
idf.py set-target esp32s3
idf.py reconfigure
idf.py build 2>&1 | tee /tmp/robot_build.log
RC=${PIPESTATUS[0]}

echo ""
echo "=== 6. 编译结果 ==="
if [ $RC -eq 0 ]; then
    echo "✅ 编译成功"
    tail -10 /tmp/robot_build.log
    echo ""
    echo "烧录命令："
    cat build/flash_args 2>/dev/null | head -10
else
    echo "❌ 编译失败，查看 log："
    echo "  tail -50 /tmp/robot_build.log"
fi
