# Task 4: WebSocket Frame Parser (RFC 6455)

**Files:**
- Modify: `ngx_http_ws_deflate_module/ngx_http_ws_deflate_frame.h` (replace stub)
- Modify: `ngx_http_ws_deflate_module/ngx_http_ws_deflate_frame.c` (replace stub)
- Create: `tests/c/test_frame.c`
- Create: `tests/c/Makefile`
- Create: `tests/c/test_main.c`

**Interfaces:**
- Consumes: nothing (standalone parser, no nginx dependency beyond ngx_core.h types)
- Produces:
  - `ngx_ws_frame_t` struct
  - `ngx_ws_frame_parse(data, len, frame) → NGX_OK/NGX_AGAIN/NGX_ERROR`
  - `ngx_ws_frame_serialize(frame, buf, len) → NGX_OK/NGX_ERROR`
  - `ngx_ws_frame_apply_mask(payload, len, masking_key)`

## Acceptance Criteria

- Implements WebSocket frame parsing per RFC 6455
- Extended payload length (16-bit and 64-bit) supported
- Masking/unmasking supported
- Partial frame handling (returns NGX_AGAIN)
- Payload size limit (NGX_WS_MAX_PAYLOAD = 16MB)
- Roundtrip test: serialize → parse produces identical frame
- C unit tests compile and pass standalone (no nginx build needed)
- Module compiles cleanly within nginx build

## Implementation

### ngx_http_ws_deflate_frame.h

Defines:
```c
#define NGX_WS_OPCODE_CONTINUATION 0x0
#define NGX_WS_OPCODE_TEXT         0x1
#define NGX_WS_OPCODE_BINARY       0x2
#define NGX_WS_OPCODE_CLOSE        0x8
#define NGX_WS_OPCODE_PING         0x9
#define NGX_WS_OPCODE_PONG         0xA

#define NGX_WS_FLAG_FIN   0x80
#define NGX_WS_FLAG_RSV1  0x40
#define NGX_WS_FLAG_RSV2  0x20
#define NGX_WS_FLAG_RSV3  0x10
#define NGX_WS_FLAG_MASK  0x80

#define NGX_WS_MAX_PAYLOAD (16 * 1024 * 1024)  // 16 MB

typedef struct {
    ngx_uint_t   fin;
    ngx_uint_t   rsv1, rsv2, rsv3;
    ngx_uint_t   opcode;
    ngx_uint_t   masked;
    uint32_t     masking_key;
    u_char      *payload;
    size_t       payload_len;
    size_t       header_len;
} ngx_ws_frame_t;
```

### ngx_http_ws_deflate_frame.c

Three functions:
1. `ngx_ws_frame_parse` — parse bytes into frame struct
2. `ngx_ws_frame_serialize` — serialize frame struct to bytes
3. `ngx_ws_frame_apply_mask` — XOR mask/unmask

Full code is in the implementation plan (Task 4 Step 2). Key details:
- Minimum 2 bytes for frame header
- Payload length 0-125: inline, 126: 2-byte extended, 127: 8-byte extended
- Masking key follows extended length (if present)
- Returns NGX_AGAIN if incomplete data
- Returns NGX_ERROR if payload > NGX_WS_MAX_PAYLOAD

### tests/c/test_frame.c

Tests (from plan Task 4 Step 3):
1. Parse basic text frame ("Hello")
2. Parse masked frame
3. Extended 16-bit length (256 bytes)
4. Partial frame returns NGX_AGAIN
5. Serialize/parse roundtrip
6. Max payload exceeded

### tests/c/Makefile

Must compile test_frame.c + ngx_http_ws_deflate_frame.c standalone (no nginx).

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -g -I../../ngx_http_ws_deflate_module
CFLAGS += -DNGX_OK=0 -DNGX_AGAIN=-2 -DNGX_ERROR=-1
CFLAGS += -DNGX_CONF_UNSET=-1 -DNGX_CONF_UNSET_SIZE=(size_t)-1

test_runner: test_main.c test_frame.c ../../ngx_http_ws_deflate_module/ngx_http_ws_deflate_frame.c
	$(CC) $(CFLAGS) -o $@ $^

test: test_runner
	./test_runner
```

### Note: ngx_int_t

The frame code uses `ngx_int_t` (defined in nginx headers as `long`). For standalone testing, we define `NGX_OK=0`, `NGX_AGAIN=-2`, `NGX_ERROR=-1`. The standalone compile needs nginx header access. On WSL with nginx source cloned, we can use `-I ~/nginx-src/src/core -I ~/nginx-src/src/os/unix -I ~/nginx-src/objs`.

Actually simpler: compile with gcc using the nginx include paths from the WSL build:
```bash
wsl -d Ubuntu -u root -- bash -c "cd ~/nginx-src && gcc -Wall -Wextra -g -I src/core -I src/os/unix -I objs -I /usr/local/include -o /tmp/test_frame tests/c/test_main.c tests/c/test_frame.c ngx_http_ws_deflate_module/ngx_http_ws_deflate_frame.c"
```

## Build & Test

```powershell
# Step 1: Write the files in Windows
# Step 2: Compile and run tests in WSL
wsl -d Ubuntu -u root -- bash -c "cd ~/nginx-src && gcc -Wall -Wextra -g -I src/core -I src/os/unix -I objs -I /usr/local/include -o /tmp/test_frame tests/c/test_main.c tests/c/test_frame.c ngx_http_ws_deflate_module/ngx_http_ws_deflate_frame.c && /tmp/test_frame"

# Step 3: Build the module
wsl -d Ubuntu -u root -- bash -c "cd ~/nginx-src && ./auto/configure --with-compat --with-cc-opt='-I/usr/local/include' --with-ld-opt='-L/usr/local/lib' --add-dynamic-module=ngx_http_ws_deflate_module && make modules -j\$(nproc)"
```
