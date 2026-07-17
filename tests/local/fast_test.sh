#!/bin/bash
set -uo pipefail

# Use pre-built module from workspace (build with build_module.sh first)
MODULE=/workspace/ngx_http_ws_deflate_module.so

# Copy module to nginx
cp "$MODULE" /usr/local/nginx/modules/ngx_http_ws_deflate_module.so

echo "=== STARTING ==="

# Backend (using uvicorn with websockets)
python3 -m venv /tmp/fv 2>/dev/null
source /tmp/fv/bin/activate
pip install -q fastapi "uvicorn[standard]" httpx websockets --break-system-packages 2>/dev/null

cd /workspace/tests/python
/tmp/fv/bin/python -m uvicorn ws_backend:app --host 127.0.0.1 --port 9001 &
sleep 2

# Nginx with debug
mkdir -p /tmp/nginx-ws-test/logs
/usr/local/nginx/sbin/nginx -c /workspace/tests/python/nginx.conf -p /tmp/nginx-ws-test
sleep 1
echo "=== READY ==="

# Quick test with websockets (no compression negotiation)
/tmp/fv/bin/python -c "
import asyncio, websockets, sys
async def t():
    try:
        async with websockets.connect('ws://127.0.0.1:8090/ws', compression=None) as ws:
            print('BASIC: CONNECTED')
            await ws.send('hello')
            r = await asyncio.wait_for(ws.recv(), timeout=5)
            print('BASIC: ECHO:', r)
    except Exception as e:
        print('BASIC FAIL:', e)
asyncio.run(t())
" 2>&1

# Test with compression negotiation (will test the tunnel)
/tmp/fv/bin/python -c "
import asyncio, websockets, sys
async def t():
    try:
        async with websockets.connect('ws://127.0.0.1:8090/ws') as ws:
            print('COMPRESS: CONNECTED', dict(ws.response_headers))
            await ws.send('hello')
            r = await asyncio.wait_for(ws.recv(), timeout=5)
            print('COMPRESS: ECHO:', r)
    except Exception as e:
        print('COMPRESS FAIL:', e)
asyncio.run(t())
" 2>&1

echo "=== NGINX LOG ==="
grep -i "ws_deflate\|NOTICE" /tmp/nginx-ws-test/logs/error.log 2>/dev/null | tail -20
