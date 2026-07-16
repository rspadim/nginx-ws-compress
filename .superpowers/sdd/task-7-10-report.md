# Tasks 7-10: Python Test Suite — Complete

## Files Created/Updated

| File | Action | Description |
|---|---|---|
| `tests/python/ws_backend.py` | created | FastAPI server: `/ws` (text echo), `/ws-binary` (binary echo) |
| `tests/python/ws_client.py` | created | `WSTestClient` helper wrapping `websockets` library with optional `compress` param |
| `tests/python/conftest.py` | created | 3 session-scoped fixtures: `backend_server`, `nginx_server`, `nginx_disabled_server` |
| `tests/python/test_roundtrip.py` | created | 6 tests: text, binary, large payload, no-compress location, sequential, mixed |
| `tests/python/test_module_disabled.py` | created | 3 tests: text, binary, multiple messages — all via nginx on port 8091 (no `load_module`) |
| `tests/python/test_browser.py` | created | Playwright test; auto-skipped if Chromium not installed |
| `tests/python/test_load.py` | created | 50 concurrent connections × 20 messages, memory diff assertion (< 100 MiB) |
| `tests/python/nginx.conf` | **updated** | Added `/ws-binary` location, added `rewrite ^ /ws break` for `/no-compress` |
| `tests/python/nginx-disabled.conf` | created | Separate config without `load_module`, listens on port 8091 |
| `scripts/run-tests.sh` | created | Copies module .so, installs deps, runs `pytest --asyncio-mode=auto` |

## Files Unchanged

| File | Notes |
|---|---|
| `tests/python/requirements.txt` | Already complete (fastapi, uvicorn, websockets, pytest, pytest-asyncio, playwright, psutil) |

## How to Run

```bash
# From WSL:
/mnt/e/Trabalho/RS/nginx-ws-compress/scripts/run-tests.sh

# Or step by step:
cp ~/nginx-src/objs/ngx_http_ws_deflate_module.so /usr/local/nginx/modules/
cd /mnt/e/Trabalho/RS/nginx-ws-compress
python -m pytest tests/python/ -v --asyncio-mode=auto

# Browser test only (after playwright install chromium):
python -m pytest tests/python/test_browser.py -v --asyncio-mode=auto
```

## Design Decisions

- **Sanity shutdown before start**: `_stop_nginx()` runs before each fixture setup to clean any leftover nginx from a previous run.
- **Session-scoped fixtures**: `backend_server`, `nginx_server`, `nginx_disabled_server` all share session scope so they start once per `pytest` invocation.
- **Port isolation**: 9001 (backend), 8090 (nginx w/ module), 8091 (nginx w/o module).
- **No-compress rewrite**: The `/no-compress` location uses `rewrite ^ /ws break` so the backend receives a path it knows.
- **Playwright guard**: `_chromium_available()` tries to launch Chromium; if unavailable, all browser tests skip automatically.
- **Load test memory check**: Compares RSS before/after 1000 total messages; asserts growth < 100 MiB as a leak signal.
