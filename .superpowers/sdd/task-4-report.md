# Task 4 Report: WebSocket Frame Parser

## Status: COMPLETE

### Files Created/Modified

| File | Action |
|------|--------|
| `ngx_http_ws_deflate_module/ngx_http_ws_deflate_frame.h` | Replaced stub — full header with RFC 6455 defines, `ngx_ws_frame_t` struct, 3 function prototypes |
| `ngx_http_ws_deflate_module/ngx_http_ws_deflate_frame.c` | Replaced stub — 3 functions implemented (parse, serialize, apply_mask) |
| `tests/c/test_frame.c` | Created — 6 unit tests |
| `tests/c/test_main.c` | Created — minimal main declaration |
| `tests/c/Makefile` | Created — standalone compilation |

### Functions Implemented

- **`ngx_ws_frame_parse`** — Parses RFC 6455 frames from raw bytes. Handles basic 2-byte header, 16-bit extended length (126), 64-bit extended length (127), masking key. Returns `NGX_OK`, `NGX_AGAIN` (partial data), or `NGX_ERROR` (payload > 16MB).
- **`ngx_ws_frame_serialize`** — Serializes `ngx_ws_frame_t` back to wire format (header + payload).
- **`ngx_ws_frame_apply_mask`** — XOR-masks/unmasks payload with 4-byte key.

### Test Results

All 6 tests pass:

```
WebSocket Frame Parser Tests
============================
  parse basic text frame ... ok
  parse masked frame ... ok
  extended 16-bit length ... ok
  partial frame returns NGX_AGAIN ... ok
  serialize/parse roundtrip ... ok
  max payload exceeded ... ok

6 tests run, 0 failed
```

### Build Verification

- Standalone C tests compile and run cleanly via `tests/c/Makefile` (gcc, no warnings)
- Module compiles within nginx build: `make modules` succeeds (shared object `objs/ngx_http_ws_deflate_module.so`)

### Notes

- The frame header (`ngx_http_ws_deflate_frame.h`) depends only on `<ngx_config.h>` and `<ngx_core.h>` (no `<ngx_http.h>` dependency), keeping it suitable for standalone use
- Max payload set to `NGX_WS_MAX_PAYLOAD = 16 * 1024 * 1024` (16 MB)
