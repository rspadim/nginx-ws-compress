# Tasks 7-10: Python Test Suite (Integration, Browser, Disable, Load)

Create the complete Python test infrastructure for the ws_deflate module.

## Files to Create

### tests/python/ws_backend.py
FastAPI WebSocket server simulating a legacy backend (no compression support).
Endpoints: `/ws` (text echo), `/ws-binary` (binary echo).

### tests/python/ws_client.py
WebSocket client helper using the `websockets` library.
Connects through nginx proxy, optionally requests permessage-deflate.

### tests/python/conftest.py
Pytest fixtures:
- `nginx_server`: starts/stops nginx with the module loaded
- `backend_server`: starts/stops the FastAPI backend

### tests/python/test_roundtrip.py
Test message roundtrip through nginx:
- text message with compression
- binary message with compression
- large payload
- no-compression location (ws_deflate off)
- sequential messages (context takeover)

### tests/python/test_module_disabled.py
Test that nginx works correctly when module is not loaded or disabled.
Uses a separate config on port 8091 without `load_module`.

### tests/python/test_browser.py
Playwright test: launch Chrome, open WebSocket through nginx, verify roundtrip.

### tests/python/test_load.py
Load test: many concurrent connections + messages, monitor memory for leaks.
Creates 50 concurrent connections, 20 messages each.

### tests/python/requirements.txt (update if needed)

## Environment

Tests run inside WSL (Ubuntu). The nginx binary is at `/usr/local/nginx/sbin/nginx`
(or wherever built). The module .so is at `~/nginx-src/objs/ngx_http_ws_deflate_module.so`.

Path mapping:
- Windows source: `E:\Trabalho\RS\nginx-ws-compress\tests\python\`
- WSL source: `/mnt/e/Trabalho/RS/nginx-ws-compress/tests/python/`
- Module .so: `/root/nginx-src/objs/ngx_http_ws_deflate_module.so`

Before running tests, copy the module .so:
```bash
mkdir -p /usr/local/nginx/modules
cp ~/nginx-src/objs/ngx_http_ws_deflate_module.so /usr/local/nginx/modules/
```

## Implementation Notes

- Use `pytest-asyncio` with `@pytest.mark.asyncio`
- ws_client.py uses `websockets` library
- conftest.py fixtures should be session-scoped
- nginx config for tests is at `tests/python/nginx.conf` (already created)
- The module .so must be copied to `/usr/local/nginx/modules/` before tests
- For the module-disabled test, create a separate nginx config without load_module
- Use `subprocess` to manage nginx processes
- Backend runs on port 9001, nginx on port 8090 (and 8091 for disable test)

## Quick Test Script

Create `scripts/run-tests.sh`:
```bash
#!/bin/bash
set -euo pipefail

# Copy module
MODULE_SRC="$HOME/nginx-src/objs/ngx_http_ws_deflate_module.so"
MODULE_DST="/usr/local/nginx/modules/"
mkdir -p "$MODULE_DST"
cp "$MODULE_SRC" "$MODULE_DST"

# Install Python deps
cd /mnt/e/Trabalho/RS/nginx-ws-compress/tests/python
pip install -r requirements.txt 2>/dev/null || true

# Run tests
cd /mnt/e/Trabalho/RS/nginx-ws-compress
python -m pytest tests/python/ -v --asyncio-mode=auto
```

Work from: E:\Trabalho\RS\nginx-ws-compress

Report to: E:\Trabalho\RS\nginx-ws-compress\.superpowers\sdd\task-7-10-report.md
