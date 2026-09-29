#!/usr/bin/env bash
# 米宝一号 —— macOS 开机自动启动 AI 服务器 + 防止睡眠
#
# 作用：
#   1. 生成并安装 launchd 配置，登录 Mac 后自动运行 xiaozhi-server
#      （进程意外退出还会自动拉起）
#   2. 安装 caffeinate 保活，避免 Mac 睡眠导致机器人断连
#
# 用法：
#   ./scripts/macos/install_autostart.sh          # 安装并启动
#   ./scripts/macos/install_autostart.sh uninstall # 卸载
#
# 说明：plist 里的路径按本机实际位置生成，因此换电脑/换目录重新跑一次即可。

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
SERVER_DIR="$REPO_ROOT/robot-updating/xiaozhi-server"
VENV_PY="$SERVER_DIR/.venv/bin/python"
AGENTS_DIR="$HOME/Library/LaunchAgents"
AI_PLIST="$AGENTS_DIR/com.mibao.ai-server.plist"
AWAKE_PLIST="$AGENTS_DIR/com.mibao.keep-awake.plist"

if [[ "${1:-install}" == "uninstall" ]]; then
    echo "[mibao] 卸载自动启动..."
    launchctl unload -w "$AI_PLIST" 2>/dev/null || true
    launchctl unload -w "$AWAKE_PLIST" 2>/dev/null || true
    rm -f "$AI_PLIST" "$AWAKE_PLIST"
    echo "[mibao] ✓ 已卸载（服务器如仍在运行：pkill -f app.py）"
    exit 0
fi

if [[ ! -x "$VENV_PY" ]]; then
    echo "[mibao] ✗ 未找到虚拟环境：$VENV_PY"
    echo "        请先在 xiaozhi-server 下创建 .venv 并安装依赖："
    echo "          python3 -m venv .venv && .venv/bin/pip install -r requirements.txt"
    exit 1
fi

if [[ ! -f "$SERVER_DIR/data/.config.yaml" ]]; then
    echo "[mibao] ⚠️  缺少 $SERVER_DIR/data/.config.yaml（含 API 密钥），服务器会启动失败"
fi

mkdir -p "$AGENTS_DIR"

# ---------- 1) AI 服务器 ----------
cat > "$AI_PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.mibao.ai-server</string>

    <key>ProgramArguments</key>
    <array>
        <string>$VENV_PY</string>
        <string>$SERVER_DIR/app.py</string>
    </array>

    <key>WorkingDirectory</key>
    <string>$SERVER_DIR</string>

    <!-- launchd 的 PATH 很短，手动补上 ffmpeg 等命令所在目录 -->
    <key>EnvironmentVariables</key>
    <dict>
        <key>PATH</key>
        <string>/usr/local/bin:/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin</string>
    </dict>

    <!-- 登录后自动启动 -->
    <key>RunAtLoad</key>
    <true/>

    <!-- 意外退出自动拉起 -->
    <key>KeepAlive</key>
    <true/>

    <key>StandardOutPath</key>
    <string>/tmp/xiaozhi_server_daemon.log</string>
    <key>StandardErrorPath</key>
    <string>/tmp/xiaozhi_server_daemon.log</string>
</dict>
</plist>
EOF

# ---------- 2) 防睡眠 ----------
cat > "$AWAKE_PLIST" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.mibao.keep-awake</string>

    <key>ProgramArguments</key>
    <array>
        <string>/usr/bin/caffeinate</string>
        <string>-s</string>
    </array>

    <key>RunAtLoad</key>
    <true/>

    <key>KeepAlive</key>
    <true/>
</dict>
</plist>
EOF

echo "[mibao] 已生成："
echo "        $AI_PLIST"
echo "        $AWAKE_PLIST"

# ---------- 3) 加载 ----------
launchctl unload -w "$AI_PLIST" 2>/dev/null || true
launchctl unload -w "$AWAKE_PLIST" 2>/dev/null || true
launchctl load -w "$AI_PLIST"
launchctl load -w "$AWAKE_PLIST"

sleep 10
echo
echo "[mibao] 端口检查："
for p in 8001 8003; do
    if lsof -iTCP:$p -sTCP:LISTEN -P >/dev/null 2>&1; then
        echo "        ✓ TCP $p 监听中"
    else
        echo "        ✗ TCP $p 未监听（看日志 /tmp/xiaozhi_server_daemon.log）"
    fi
done
lsof -iUDP:8004 -P >/dev/null 2>&1 && echo "        ✓ UDP 8004 自动发现服务" || echo "        ✗ UDP 8004 未监听"

echo
echo "[mibao] ✓ 完成。以后登录 Mac 会自动启动服务器并保持不休眠。"
echo "        查看日志：tail -f /tmp/xiaozhi_server_daemon.log"
echo "        卸载：    $0 uninstall"
