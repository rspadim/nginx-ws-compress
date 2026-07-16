# WebSocket per-message Deflate Proxy Module — Design Spec

## 1. Summary

Dynamic nginx module that acts as a compression bridge for WebSocket reverse
proxy. Compresses backend→client messages (permessage-deflate, RFC 7692) and
decompresses client→backend messages, allowing a legacy backend (without
compression support) to benefit from WebSocket compression.

The module is built as a dynamic loadable module (`.so`) — no core nginx
patching required. It hooks into the proxy tunnel after the WebSocket
handshake and replaces the raw byte tunnel with a frame-aware processor.

## 2. Architecture

```
Client (browser)
    │  WebSocket compressed (permessage-deflate, RFC 7692)
    ▼
┌──────────────────────────────────────┐
│  nginx                                │
│  ┌────────────────────────────────┐   │
│  │ ngx_http_ws_deflate_module     │   │
│  │  ┌──────────────────────┐      │   │
│  │  │ Handshake Handler    │      │   │  → manipulates Sec-WebSocket-Extensions
│  │  └──────┬───────────────┘      │   │
│  │  ┌──────▼───────────────┐      │   │
│  │  │ Frame Processor      │      │   │  → processes frames bidirectionally
│  │  │  ┌────┐ ┌─────────┐  │      │   │
│  │  │  │ RX │ │ Inflate │  │      │   │  client→backend: decompress
│  │  │  └────┘ └─────────┘  │      │   │
│  │  │  ┌────┐ ┌─────────┐  │      │   │
│  │  │  │ TX │ │ Deflate │  │      │   │  backend→client: compress
│  │  │  └────┘ └─────────┘  │      │   │
│  │  └──────────────────────┘      │   │
│  └────────────────────────────────┘   │
└──────────────────────────────────────┘
    │  WebSocket raw (no compression)
    ▼
Legacy Backend
```

## 3. Configuration Directives

### 3.1 `ws_deflate`

- **Scope:** http, server, location
- **Default:** off
- **Description:** Enables/disables the module for the location.

### 3.2 `ws_deflate_auto`

- **Scope:** http, server
- **Default:** off
- **Description:** Automatically detects WebSocket connections
  (`Upgrade: websocket`) and applies compression without needing per-location
  configuration.

### 3.3 `ws_deflate_except`

- **Scope:** http, server
- **Default:** — (empty list)
- **Description:** List of prefix/regex patterns to exclude from auto mode.
  Syntax follows nginx `location` semantics:
  - `/path` — prefix match
  - `~ regex` — case-sensitive regex
  - `~* regex` — case-insensitive regex
  - `^~ /path` — priority prefix
  Multiple entries allowed, evaluated in order.

### 3.4 `ws_deflate_compression_level`

- **Scope:** http, server, location
- **Default:** 6
- **Description:** zlib compression level (1-9). 1 = fastest, 9 = best ratio.

### 3.5 `ws_deflate_context_takeover`

- **Scope:** http, server, location
- **Default:** on
- **Description:** Keeps the zlib context between messages (better compression).
  Disabling reduces memory but worsens compression ratio.

### 3.6 `ws_deflate_chunk_size`

- **Scope:** http, server, location
- **Default:** 4096
- **Description:** Compression/decompression buffer size in bytes.

## 4. Handshake Flow

1. Client sends `GET /path` with headers:
   - `Upgrade: websocket`
   - `Sec-WebSocket-Version: 13`
   - `Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits`

2. Module intercepts the request and:
   - Removes `permessage-deflate` from `Sec-WebSocket-Extensions` (or strips
     the entire header)
   - Forwards the modified request to the backend

3. Backend responds `101 Switching Protocols` (no extensions — it doesn't
   understand compression)

4. Module intercepts the response and:
   - Adds `Sec-WebSocket-Extensions: permessage-deflate` to the response
   - Takes control of the tunnel (replaces the raw proxy pass-through)

5. Connection established: compressed mode with client, raw mode with backend

## 5. Frame Processing (Post-Upgrade)

### 5.1 WebSocket Frame Format (RFC 6455)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-------+-+-------------+-------------------------------+
|F|R|R|R| opcode|M| Payload len |    Extended payload length    |
|I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
|N|V|V|V|       |S|             |   (if payload len==126/127)   |
| |1|2|3|       |K|             |                               |
+-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
|     Extended payload length continued, if payload len == 127  |
+ - - - - - - - - - - - - - - - +-------------------------------+
|                               |Masking-key, if MASK set to 1  |
+-------------------------------+-------------------------------+
| Masking-key (continued)       |          Payload Data         |
+-------------------------------- - - - - - - - - - - - - - - - +
:                     Payload Data continued ...                :
+ - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
|                     Payload Data (continued)                  |
+---------------------------------------------------------------+
```

- **RSV1 bit**: indicates compressed payload (permessage-deflate, RFC 7692)
- **Opcode 0x1** (text) and **0x2** (binary): only compressible opcodes
- **Opcode 0x8** (close), **0x9** (ping), **0xA** (pong): never compressed

### 5.2 Transformation Pipeline

**Client → nginx → Backend (decompression):**
1. Read complete frame from client
2. If RSV1=1, decompress payload with zlib inflate (contextual)
3. Clear RSV1
4. If payload was masked, unmask
5. Forward raw frame to backend

**Backend → nginx → Client (compression):**
1. Read complete frame from backend
2. If opcode is text (0x1) or binary (0x2):
   a. Compress payload with zlib deflate (contextual)
   b. Set RSV1=1
3. If opcode is close/ping/pong: pass through unchanged
4. If payload was masked, unmask (server frames are never masked)
5. Mask payload for the client
6. Forward compressed frame to client

### 5.3 zlib Context Management

- Two independent contexts: client→backend (inflate), backend→client (deflate)
- Context kept between messages (context takeover) or reset per message,
  configurable via `ws_deflate_context_takeover`
- Window bits = -15 (raw deflate, no gzip/zlib header) per RFC 7692
- Sync flush used after each compressed message per RFC 7692 §7.2.1

### 5.4 Buffer Management

- Frame parser: ring buffer for partial data (frames may arrive fragmented
  over TCP)
- Configurable max payload size limit (OOM protection)
- After frame processed, compact buffer (remove consumed data)
- Compression/decompression uses chunked output (avoid buffering entire
  large payloads in memory)

## 6. Implementation

### 6.1 Module File Structure

```
ngx_http_ws_deflate_module/
├── config                              # nginx module config script
├── ngx_http_ws_deflate_module.c        # main module entry + directives
├── ngx_http_ws_deflate_handshake.c     # handshake header manipulation
├── ngx_http_ws_deflate_handshake.h     # handshake header
├── ngx_http_ws_deflate_frame.c         # WebSocket frame parser/serializer
├── ngx_http_ws_deflate_frame.h         # frame parser header
├── ngx_http_ws_deflate_compress.c      # zlib-ng compress/decompress
├── ngx_http_ws_deflate_compress.h      # compression header
├── ngx_http_ws_deflate_tunnel.c        # tunnel integration (event handlers)
└── ngx_http_ws_deflate_tunnel.h        # tunnel header
```

### 6.2 Dependencies

- **zlib-ng** (libz-ng-dev on Debian/Ubuntu, or compiled alongside)
- **nginx 1.9.11+** (dynamic module support)
- nginx event API (`ngx_event_*`, `ngx_http_*`)
- Standard C build tools (gcc/clang, make, autoconf)

### 6.3 Building

```bash
# As a dynamic module (within nginx source tree)
cd nginx-src
./configure --add-dynamic-module=path/to/ngx_http_ws_deflate_module \
            --with-cc-opt="-I/usr/include/zlib-ng" \
            --with-ld-opt="-L/usr/lib -lz-ng"
make modules
```

In nginx.conf:
```nginx
load_module modules/ngx_http_ws_deflate_module.so;
```

### 6.4 Tunnel Integration Strategy

The module needs to replace nginx's default raw tunnel behavior after the
WebSocket handshake:

1. Register a **header filter handler** (`NGX_HTTP_HEADER_FILTER`) that
   detects `101 Switching Protocols` + WebSocket Upgrade
2. On detection, install custom **read/write event handlers** on the
   connection pair (client ↔ upstream) via
   `c->read->handler` / `c->write->handler` and
   `u->peer.connection->read->handler` / `u->peer.connection->write->handler`
3. The custom handlers run the frame processor instead of the raw
   `ngx_http_upstream_process_non_buffered_*` handlers
4. On error / close, fall back to clean connection teardown

## 7. Error Handling

| Condition | Action |
|---|---|
| Partial frame (fragmented) | Buffer until complete |
| Payload > configured limit | Close with WebSocket close code 1009 |
| Decompression failure | Close with close code 1002 (protocol error) |
| Compression failure | Close with close code 1011 (internal error) |
| Handshake without per-message deflate | Pass raw tunnel (no compression) |
| Upstream connection lost | Forward close to client |
| zlib context init failure | Close with close code 1011 |

## 8. Testing Strategy

### 8.1 Unit Tests (C)
- Frame parser: valid frames, fragmented frames, oversized payloads
- Compression roundtrip: compress→decompress matches original
- Context takeover on/off behavior
- Masking/unmasking correctness
- All compression levels 1-9

### 8.2 Integration Tests (Python + FastAPI)
- FastAPI WebSocket server as legacy backend (no compression)
- Python websockets client connecting through nginx with compression
- Verify messages roundtrip correctly (compressed on wire to client)
- Module disabled: confirm nginx still proxies WebSocket correctly

### 8.3 Browser Test (Playwright)
- Chrome via Playwright Python connects to a test page
- Page opens WebSocket through nginx proxy
- Verify connection uses permessage-deflate (check headers)
- Verify message content preserved

### 8.4 Load / Memory Test
- Flood connection with many messages
- Monitor memory usage (no leak over time)
- Multiple concurrent WebSocket connections

## 9. Implementation Roadmap

1. Environment setup (WSL, deps, clone nginx, create branch)
2. Module skeleton (config, module registration, directives)
3. Handshake handler (header manipulation)
4. Frame parser/serializer (RFC 6455)
5. Compression engine (zlib-ng, RFC 7692)
6. Tunnel integration (event handler replacement)
7. C unit tests (frame + compression)
8. Python integration test suite (FastAPI backend)
9. Playwright browser test
10. Load / memory leak test
11. Module disable compatibility test
12. Performance optimization & code review
