#!/bin/bash
# 一键启动米宝 AI 对话服务器（xiaozhi-server）
# 用法：./start_server.sh
# 如果提示 Permission denied，先执行：chmod +x start_server.sh

cd "$(dirname "$0")/xiaozhi-server"

# 如果端口已被占用，说明服务器已在运行
if lsof -iTCP:8001 -sTCP:LISTEN >/dev/null 2>&1; then
    echo "✅ 服务器已经在运行（端口 8001 正在监听），无需重复启动"
    exit 0
fi

echo "🚀 正在启动 AI 对话服务器..."
nohup .venv/bin/python app.py > /tmp/xiaozhi_server.log 2>&1 &

# 等待端口就绪（最多等 20 秒）
for i in $(seq 1 20); do
    sleep 1
    if lsof -iTCP:8001 -sTCP:LISTEN >/dev/null 2>&1; then
        echo "✅ 服务器启动成功！"
        echo "   WebSocket: ws://$(ifconfig en1 2>/dev/null | grep 'inet ' | awk '{print $2}' || echo '?') :8001"
        echo "   OTA 更新:  http://$(ifconfig en1 2>/dev/null | grep 'inet ' | awk '{print $2}' || echo '?') :8003"
        echo ""
        echo "   👉 现在去设备上退出 AI 对话再重新进入，即可连上"
        exit 0
    fi
done

echo "❌ 服务器启动失败，请查看日志：tail -20 /tmp/xiaozhi_server.log"
exit 1
