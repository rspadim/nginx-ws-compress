#!/bin/bash
set -euo pipefail

MODULE_SRC="$HOME/nginx-src/objs/ngx_http_ws_deflate_module.so"
MODULE_DST="/usr/local/nginx/modules/"
mkdir -p "$MODULE_DST"
cp "$MODULE_SRC" "$MODULE_DST"

TEST_DIR="/mnt/e/Trabalho/RS/nginx-ws-compress/tests/python"
cd "$TEST_DIR"

pip install -q -r requirements.txt 2>/dev/null || true

cd /mnt/e/Trabalho/RS/nginx-ws-compress
python -m pytest tests/python/ -v --asyncio-mode=auto "$@"
