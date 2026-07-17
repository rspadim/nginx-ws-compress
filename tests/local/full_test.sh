#!/bin/bash
set -uo pipefail

apt-get update -qq 2>/dev/null
apt-get install -y -qq libxfixes3 libasound2t64 2>/dev/null

cd /opt/nginx-src
./auto/configure --with-compat --with-debug \
  --with-cc-opt="-I/usr/local/include" \
  --with-ld-opt="-L/usr/local/lib" \
  --add-dynamic-module=/workspace/ngx_http_ws_deflate_module > /dev/null 2>&1
make modules -j$(nproc) > /dev/null 2>&1
make install > /dev/null 2>&1
echo "BUILD OK"

python3 -m venv /tmp/v9
source /tmp/v9/bin/activate
pip install -q fastapi "uvicorn[standard]" httpx --break-system-packages 2>/dev/null
echo "VENV OK"

cd /workspace/tests/python
/tmp/v9/bin/python -m uvicorn ws_backend:app --host 127.0.0.1 --port 9001 &
sleep 2

mkdir -p /tmp/nginx-ws-test/logs
/usr/local/nginx/sbin/nginx -c /workspace/tests/python/nginx.conf -p /tmp/nginx-ws-test
sleep 1
echo "SERVICES OK"

echo "=== RAW WS TEST ==="
/tmp/v9/bin/python /workspace/tests/local/test_direct.py > /tmp/direct.txt 2>&1 || true
cat /tmp/direct.txt

echo "=== NGINX LOG (WS related) ==="
grep -i "ws_deflate\|WebSocket\|upgrade\|compressed\|write_down\|closing" /tmp/nginx-ws-test/logs/error.log 2>/dev/null | tail -20

echo "=== DONE ==="
