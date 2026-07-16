# Contributing

## Project Structure

```
ngx_http_ws_deflate_module/       # C module source
├── config                         # nginx build config
├── ngx_http_ws_deflate_module.c   # directives + module registration
├── ngx_http_ws_deflate_handshake.c  # Sec-WebSocket-Extensions negotiation
├── ngx_http_ws_deflate_frame.c      # RFC 6455 frame parser
├── ngx_http_ws_deflate_compress.c   # zlib compression (RFC 7692)
└── ngx_http_ws_deflate_tunnel.c     # bidirectional frame tunnel

tests/
├── c/                              # C unit tests (standalone)
│   ├── test_frame.c
│   ├── test_compress.c
│   └── Makefile
└── python/                         # Python integration tests
    ├── conftest.py                  # pytest fixtures
    ├── test_roundtrip.py
    ├── test_browser.py
    ├── test_load.py
    ├── test_memory.py
    ├── test_compression_active.py
    ├── test_long_stream.py
    ├── test_auto_except.py
    ├── test_module_disabled.py
    ├── ws_backend.py
    └── ws_client.py

scripts/
├── build.sh          # Linux/macOS build script
├── build.ps1         # Windows build script
└── setup-wsl.sh      # WSL development environment

.github/workflows/
├── ci.yml            # Main CI pipeline
└── long-stress.yml   # Manual long-duration stress test
```

## Development Workflow

### 1. Setup

```bash
# Linux/macOS
git clone https://github.com/rspadim/nginx-ws-compress.git
cd nginx-ws-compress
./scripts/build.sh
```

### 2. Code Changes

- Edit files in `ngx_http_ws_deflate_module/`
- Rebuild: `cd nginx-src && make modules -j$(nproc)`

### 3. Run Tests

```bash
# C unit tests
cd tests/c && make test

# Python integration tests
cd tests/python
python3 -m venv venv && source venv/bin/activate
pip install -r requirements.txt
python -m pytest -v --asyncio-mode=auto
```

### 4. Commit

Use conventional commits:

```
feat: add new feature
fix: correct bug
test: add test coverage
docs: update documentation
ci: CI/CD changes
```

## Pull Request Checklist

- [ ] Module compiles without warnings (`-Werror`)
- [ ] C unit tests pass (frame parser + compression)
- [ ] Python integration tests pass
- [ ] New features have corresponding tests
- [ ] CI pipeline is green

## CI/CD

The CI runs on push/PR to master:

| Job | What it tests |
|---|---|
| build-only | Quick compilation check (30s) |
| build-and-test (ubuntu-latest) | Full suite: C tests + Python + browser + load |
| build-and-test (ubuntu-24.04-arm) | ARM64 compatibility |
| build-and-test (macos-latest) | macOS build + C tests |
| build-windows | Windows static build |

Manual stress test: **Actions > Long Stress Test (manual)**

## Code Style

- Follow nginx coding conventions (similar to nginx's own modules)
- Use `ngx_` prefix for module functions
- Keep functions short and focused
- No trailing whitespace
- 4-space indentation
- Comments in English
