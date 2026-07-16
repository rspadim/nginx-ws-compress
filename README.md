# nginx WebSocket per-message Deflate Module

[![CI](https://github.com/rspadim/nginx-ws-compress/actions/workflows/ci.yml/badge.svg)](https://github.com/rspadim/nginx-ws-compress/actions/workflows/ci.yml)
[![Platforms](https://img.shields.io/badge/platform-linux%20%7C%20macOS%20%7C%20windows%20%7C%20arm64-blue)](https://nginx.org/en/#tested_os_and_platforms)
[![License](https://img.shields.io/badge/license-BSD--2--Clause-blue.svg)](LICENSE)

**ngx_http_ws_deflate_module** — Dynamic nginx module that enables WebSocket
per-message deflate compression (RFC 7692) in a reverse proxy setup where the
backend does NOT support compression.

```
Client (browser)  ──[compressed]──▶  nginx  ──[raw]──▶  Legacy Backend
                  ◀──[compressed]──       ◀──[raw]──
```

The module acts as a **compression bridge**: it negotiates `permessage-deflate`
with modern clients, compresses backend→client messages, and decompresses
client→backend messages, while the backend only sees raw (uncompressed)
WebSocket frames.

---

## Features

- **Dynamic module** — loaded via `load_module`, no nginx recompilation needed
- **Transparent compression bridge** — client gets compression, backend stays raw
- **Full RFC 7692 compliance** — raw deflate (windowBits=-15), sync flush tail
  stripping, context takeover support
- **Configurable compression level** — 1 (fast) to 9 (best ratio)
- **Configurable context takeover** — keep zlib context between messages for
  better compression, or reset per message for lower memory
- **Auto-detect mode** — automatically apply compression to all WebSocket
  connections with exclusion patterns
- **No overhead when disabled** — when `ws_deflate off`, nginx uses its native
  WebSocket proxy (zero module overhead)

---

## How It Works

### Handshake Flow

```
1. Client sends:  GET /ws + Upgrade: websocket
                   + Sec-WebSocket-Extensions: permessage-deflate

2. Module strips permessage-deflate from request (backend never sees it)
   and forwards to backend

3. Backend responds: 101 Switching Protocols (no extensions)

4. Module adds: Sec-WebSocket-Extensions: permessage-deflate
   to the response and installs the compression tunnel

5. From now on: all frames are compressed to/from client,
   raw to/from backend
```

### Frame Processing

```
 Client → nginx → Backend (decompression):
   [frame: RSV1=1, compressed payload]
     → decompress with zlib inflate
     → clear RSV1
     → forward raw to backend

 Backend → nginx → Client (compression):
   [frame: RSV1=0, raw payload]
     → compress with zlib deflate (if text/binary)
     → set RSV1=1
     → forward compressed to client
```

---

## Building

### Prerequisites

- nginx 1.9.11+ (dynamic module support)
- zlib-ng (recommended) or zlib

### Compile

```bash
# Clone nginx source
git clone https://github.com/nginx/nginx.git
cd nginx

# Clone the module
git clone https://github.com/rspadim/nginx-ws-compress.git

# Configure with the module
./auto/configure \
  --with-compat \
  --add-dynamic-module=path/to/nginx-ws-compress/ngx_http_ws_deflate_module \
  --with-cc-opt="-I/usr/local/include" \
  --with-ld-opt="-L/usr/local/lib"

# Build only the module
make modules
```

The compiled module is at `objs/ngx_http_ws_deflate_module.so`.

### Install

```bash
sudo cp objs/ngx_http_ws_deflate_module.so /etc/nginx/modules/
```

---

## Configuration

### Basic Setup

```nginx
load_module /etc/nginx/modules/ngx_http_ws_deflate_module.so;

http {
    server {
        listen 80;

        location /ws {
            proxy_pass http://backend:9001;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection "upgrade";

            # Enable WebSocket compression
            ws_deflate on;
        }
    }
}
```

### All Directives

| Directive | Scope | Default | Description |
|---|---|---|---|
| `ws_deflate` | http, server, location | `off` | Enable/disable the module |
| `ws_deflate_auto` | http, server | `off` | Auto-detect WebSocket connections |
| `ws_deflate_except` | http, server | — | Exclude paths from auto-detect |
| `ws_deflate_compression_level` | http, server, location | `6` | zlib level (1-9) |
| `ws_deflate_context_takeover` | http, server, location | `on` | Keep zlib context between messages |
| `ws_deflate_chunk_size` | http, server, location | `4096` | Internal buffer size |

### Full Example

```nginx
load_module modules/ngx_http_ws_deflate_module.so;

http {
    # Auto-detect mode: compress all WebSocket connections
    ws_deflate_auto on;
    ws_deflate_except /legacy-ws/;
    ws_deflate_except ~ /no-compress/.*;

    server {
        listen 80;

        # Explicit per-location configuration
        location /api/ws/ {
            proxy_pass http://backend:9001;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection "upgrade";

            ws_deflate on;
            ws_deflate_compression_level 9;       # max compression
            ws_deflate_context_takeover on;        # keep context
        }

        location /chat/ {
            proxy_pass http://backend:9002;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection "upgrade";

            ws_deflate on;
            ws_deflate_compression_level 1;        # low latency
            ws_deflate_context_takeover off;        # reset context per message
        }

        # No compression (passthrough)
        location /legacy-ws/ {
            proxy_pass http://backend:9003;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection "upgrade";
            ws_deflate off;
        }
    }
}
```

---

## Configuration Examples

### 1. Minimal — just enable compression

```nginx
load_module modules/ngx_http_ws_deflate_module.so;

http {
    server {
        listen 80;

        location /ws {
            proxy_pass http://backend:9001;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection "upgrade";
            ws_deflate on;                   # that's all you need
        }
    }
}
```

### 2. Multiple endpoints with different compression levels

```nginx
load_module modules/ngx_http_ws_deflate_module.so;

http {
    upstream prod_api  { server 10.0.1.10:9001; }
    upstream chat_api  { server 10.0.1.11:9002; }
    upstream iot_api   { server 10.0.1.12:9003; }

    server {
        listen 80;

        # Critical API: maximum compression (more CPU)
        location /api/ws/ {
            proxy_pass http://prod_api;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection "upgrade";
            proxy_set_header Host $host;

            ws_deflate on;
            ws_deflate_compression_level 9;
            ws_deflate_context_takeover on;
        }

        # Real-time chat: latency first
        location /chat/ws/ {
            proxy_pass http://chat_api;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection "upgrade";
            proxy_set_header Host $host;

            ws_deflate on;
            ws_deflate_compression_level 1;      # fast compression
            ws_deflate_context_takeover off;      # less memory
        }

        # IoT: small binary payloads, compression barely helps
        location /iot/ws/ {
            proxy_pass http://iot_api;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection "upgrade";
            proxy_set_header Host $host;

            ws_deflate on;
            ws_deflate_compression_level 1;
        }
    }
}
```

### 3. Auto-detect + exceptions

Useful when you have many WebSocket endpoints and don't want to configure each one:

```nginx
load_module modules/ngx_http_ws_deflate_module.so;

http {
    # Enable compression for ANY WebSocket connection
    ws_deflate_auto on;

    # But disable it for specific paths
    ws_deflate_except /healthcheck/;
    ws_deflate_except /legacy/;
    ws_deflate_except ~ /no-compress/;

    server {
        listen 80;

        # These locations DON'T need ws_deflate on —
        # auto mode detects WebSocket and compresses automatically
        location /chat/ {
            proxy_pass http://chat_backend;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection "upgrade";
        }

        location /notifications/ {
            proxy_pass http://notif_backend;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection "upgrade";
        }

        # This one is excluded by ws_deflate_except
        location /legacy/ {
            proxy_pass http://old_backend;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection "upgrade";
        }
    }
}
```

### 4. Reverse proxy with multiple backends

```nginx
load_module modules/ngx_http_ws_deflate_module.so;

http {
    upstream backend_modern  { server 10.0.0.1:8080; }
    upstream backend_legacy  { server 10.0.0.2:8080; }

    server {
        listen 443 ssl;
        ssl_certificate     /etc/ssl/certs/nginx.crt;
        ssl_certificate_key /etc/ssl/private/nginx.key;

        # Modern backend: it handles compression itself, nginx just proxies
        location /modern/ws/ {
            proxy_pass http://backend_modern;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection "upgrade";
            # ws_deflate off — the backend manages compression
        }

        # Legacy backend: doesn't support compression, nginx bridges it
        location /legacy/ws/ {
            proxy_pass http://backend_legacy;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection "upgrade";

            ws_deflate on;
            ws_deflate_compression_level 6;
        }
    }
}
```

### 5. Docker Compose

```yaml
version: "3.8"
services:
  nginx:
    image: nginx:latest
    volumes:
      - ./nginx.conf:/etc/nginx/nginx.conf:ro
      - ./ngx_http_ws_deflate_module.so:/etc/nginx/modules/ngx_http_ws_deflate_module.so:ro
    ports:
      - "8080:80"

  backend:
    image: python:3.12-slim
    command: >
      sh -c "pip install fastapi uvicorn &&
             python -m uvicorn ws_backend:app --host 0.0.0.0 --port 9001"
    ports:
      - "9001:9001"
```

---

## Testing

### Unit Tests (C)

```bash
cd tests/c
make test
```

Tests the WebSocket frame parser and zlib compression/decompression
independently of nginx.

### Integration Tests (Python)

```bash
cd tests/python
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt

# Run all tests
python -m pytest -v --asyncio-mode=auto
```

Requires:
- nginx with the module installed
- FastAPI + uvicorn (for the test backend)
- playwright + chromium (for browser test)

### Test Matrix

| Test | What it verifies |
|---|---|
| `test_frame.c` | RFC 6455 frame parsing, serialization, masking |
| `test_compress.c` | zlib roundtrip, context takeover, empty payload |
| `test_roundtrip.py` | Text, binary, large payload, sequential messages |
| `test_module_disabled.py` | nginx works without module loaded |
| `test_browser.py` | Chrome WebSocket through nginx |
| `test_load.py` | 50 concurrent connections × 10 messages |

---

## Architecture

```
ngx_http_ws_deflate_module/
├── config                                    # nginx build config
├── ngx_http_ws_deflate_module.c              # Module registration + directives
├── ngx_http_ws_deflate_handshake.h/.c        # Sec-WebSocket-Extensions negotiation
├── ngx_http_ws_deflate_frame.h/.c            # RFC 6455 frame parser/serializer
├── ngx_http_ws_deflate_compress.h/.c         # zlib compression (RFC 7692)
└── ngx_http_ws_deflate_tunnel.h/.c           # Bidirectional frame tunnel
```

### Module Phases

1. **PRECONTENT phase**: `request_handler` — removes `permessage-deflate`
   from request headers before upstream sees them
2. **Header filter**: `handshake_handler` — adds `Sec-WebSocket-Extensions`
   to response; triggers tunnel installation
3. **Event handlers**: `client_read_handler` / `upstream_read_handler` —
   frame processing after WebSocket upgrade

---

## Known Limitations

- **Large payloads**: Buffers are request-pool allocated. Very large messages
  (>100KB) may cause connection resets. Increase `ws_deflate_chunk_size` or
  implement streaming compression for production use.
- **Per-frame allocation**: Each frame allocates temporary buffers from the
  request pool without freeing until connection close. For long-lived
  connections with thousands of frames, memory usage grows linearly. A
  sub-pool or reusable scratch buffer optimization is planned.

---

## Platforms

The module is written in portable C (no assembly) and compiles on **any
architecture nginx supports**. CI-tested:

| OS | Arch | Status |
|---|---|---|
| Linux | amd64 | ✅ Full (build + C tests + Python + browser + load) |
| Linux | arm64 | ✅ Build + C tests + Python |
| macOS | amd64 | ✅ Build + C tests |
| Windows | amd64 | 🔄 Experimental |

To build on any platform:

```bash
./auto/configure --add-dynamic-module=path/to/ngx_http_ws_deflate_module
make modules
```

---

## License

This module is distributed under the same license as nginx — 2-clause BSD-like.
