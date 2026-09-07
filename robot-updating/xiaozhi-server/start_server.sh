#!/bin/bash
# 启动 xiaozhi-server 并记录日志

cd "/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/xiaozhi-server"
source .venv/bin/activate

LOG_FILE="server.log"
echo "Starting xiaozhi-server, logging to $LOG_FILE"

# 运行 Python 并捕获所有输出
python app.py >> "$LOG_FILE" 2>&1
