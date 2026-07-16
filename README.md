# nginx WebSocket per-message Deflate Module

[![CI](https://github.com/rspadim/nginx-ws-compress/actions/workflows/ci.yml/badge.svg)](https://github.com/rspadim/nginx-ws-compress/actions/workflows/ci.yml)
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

## Fork Info

- **Module repo**: https://github.com/rspadim/nginx-ws-compress
- **nginx fork**: https://github.com/rspadim/nginx (branch `feat/ws-permessage-deflate`)

---

## License

This module is distributed under the same license as nginx — 2-clause BSD-like.
