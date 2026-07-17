#!/bin/bash
set -euo pipefail

echo "=== BUILDING MODULE ==="
cd /opt/nginx-src
./auto/configure --with-compat --with-debug \
  --with-cc-opt="-I/usr/local/include" \
  --with-ld-opt="-L/usr/local/lib" \
  --add-dynamic-module=/workspace/ngx_http_ws_deflate_module > /dev/null
make modules -j$(nproc) > /dev/null
make install > /dev/null
echo "BUILD OK"

echo "=== SETUP VENV ==="
python3 -m venv /tmp/venv
source /tmp/venv/bin/activate
pip install -q fastapi uvicorn pytest httpx playwright --break-system-packages 2>/dev/null
echo "VENV OK"

echo "=== START BACKEND ==="
cd /workspace/tests/python
/tmp/venv/bin/python -m uvicorn ws_backend:app --host 127.0.0.1 --port 9001 &
sleep 2

echo "=== START NGINX ==="
mkdir -p /tmp/nginx-ws-test/logs
/usr/local/nginx/sbin/nginx -c /workspace/tests/python/nginx.conf -p /tmp/nginx-ws-test
sleep 1

echo "=== RUN COMPRESSION TEST ==="
/tmp/venv/bin/python -m pytest /workspace/tests/python/test_compression_active.py -v -s 2>&1
echo "EXIT CODE: $?"

echo "=== NGINX ERROR LOG (last 40 lines) ==="
tail -40 /tmp/nginx-ws-test/logs/error.log 2>/dev/null || echo "(no log)"
