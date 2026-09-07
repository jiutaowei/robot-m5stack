#!/bin/bash
set -e
cd "$(dirname "$0")/.."
# 本机为 Intel Mac，系统 python3 = 3.10.2（/usr/local/bin/python3）
# 若需要特定版本可改为: /usr/local/opt/python@3.10/bin/python3.10
PYTHON_BIN="${PYTHON_BIN:-python3}"
"$PYTHON_BIN" -m venv .venv
./.venv/bin/pip install -q --upgrade pip
./.venv/bin/pip install -q -r requirements-mibao.txt  # 裁剪版：去掉 vosk（arm64 无 wheel，且未启用 VoskASR）
echo INSTALL_OK
