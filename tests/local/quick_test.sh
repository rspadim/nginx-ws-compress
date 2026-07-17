#!/bin/bash
# Quick test: build + run websockets test in container
set -euo pipefail
cd /opt/nginx-src
./auto/configure --with-compat --with-debug --with-cc-opt="-I/usr/local/include" --with-ld-opt="-L/usr/local/lib" --add-dynamic-module=/workspace/ngx_http_ws_deflate_module > /dev/null 2>&1
make -j$(nproc) > /dev/null 2>&1
make install > /dev/null 2>&1
python3 -m venv /tmp/v22
source /tmp/v22/bin/activate
pip install -q fastapi uvicorn[standard] httpx websockets --break-system-packages 2>/dev/null
cd /workspace/tests/python
/tmp/v22/bin/python -m uvicorn ws_backend:app --host 127.0.0.1 --port 9001 &
sleep 2
mkdir -p /tmp/nginx-ws-test/logs
/usr/local/nginx/sbin/nginx -c /workspace/tests/python/nginx.conf -p /tmp/nginx-ws-test
sleep 1
echo "=== TEST ==="
/tmp/v22/bin/python -c "
import asyncio, websockets
async def t():
    async with websockets.connect('ws://127.0.0.1:8090/ws', max_size=65536) as ws:
        print('CONNECTED', dict(ws.response_headers))
        await ws.send('hello')
        r = await asyncio.wait_for(ws.recv(), timeout=5)
        print('ECHO:', r)
asyncio.run(t())
" 2>&1
echo "=== NGINX LOG ==="
grep -i "ws_deflate\|tunnel\|compressed\|deferred\|process_data" /tmp/nginx-ws-test/logs/error.log 2>/dev/null | tail -5
