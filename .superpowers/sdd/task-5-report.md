# Task 5 Report: Compression Engine

## Summary

Implemented the compression engine for WebSocket permessage-deflate (RFC 7692) using zlib-ng (compat API).

## Files Modified

| File | Action |
|------|--------|
| `ngx_http_ws_deflate_module/ngx_http_ws_deflate_compress.h` | Replaced stub with struct and function declarations |
| `ngx_http_ws_deflate_module/ngx_http_ws_deflate_compress.c` | Implemented init/compress/decompress/destroy |
| `tests/c/test_compress.c` | New file with 4 test cases |
| `tests/c/Makefile` | Added separate targets for compress tests |

## API

```c
ngx_int_t ngx_ws_deflate_ctx_init(ctx, level, takeover);
ngx_int_t ngx_ws_deflate_compress(ctx, in, in_len, out, *out_len);
ngx_int_t ngx_ws_deflate_decompress(ctx, in, in_len, out, *out_len);
void      ngx_ws_deflate_ctx_destroy(ctx);
```

## Implementation Details

### Compression (`ngx_ws_deflate_compress`)

- Uses raw deflate (windowBits = -15) with `Z_SYNC_FLUSH`
- Strips trailing `0x00 0x00 0xFF 0xFF` (sync flush marker) from output per RFC 7692 §7.2.1
- Resets stream via `deflateReset` when context takeover is disabled

### Decompression (`ngx_ws_deflate_decompress`)

- Uses two-pass inflate: first inflates the payload data (`Z_NO_FLUSH`), then inflates the 4-byte marker (`Z_SYNC_FLUSH`)
- This appends the stripped marker back before decompression per RFC 7692 §7.2.2 (step 1: append `0x00 0x00 0xFF 0xFF`)
- Accepts both `Z_OK` and `Z_STREAM_END`
- Resets stream via `inflateReset` when context takeover is disabled

### Key Design Decision

The brief specified `zng_*` API from `<zlib-ng.h>`, but the installed zlib-ng was built with `--zlib-compat` (providing only the traditional `z_` API through `<zlib.h>`). The implementation uses the `z_stream` type, `<zlib.h>`, and traditional API names (`deflate`, `inflate`, etc.). The library linked (`-lz`) is zlib-ng in compat mode.

### RFC 7692 Compliance

The critical insight from §7.2.2 is that the **decompressor must append** the 4-byte marker (`0x00 0x00 0xFF 0xFF`) before decompressing. This ensures the inflate stream properly terminates each deflate block boundary and enables correct context takeover across messages.

## Tests

| Test | Description | Status |
|------|-------------|--------|
| `test_compress_roundtrip` | Compress "Hello World..." → decompress matches | PASS |
| `test_multiple_messages_takeover` | 3 messages with context takeover | PASS |
| `test_empty_payload` | Empty string roundtrip | PASS |
| `test_no_context_takeover` | 3 messages without takeover | PASS |

## Build

```bash
# Standalone tests
gcc -Wall -Wextra -g -I src/core -I src/os/unix -I objs \
    -I /usr/local/include -I ngx_http_ws_deflate_module \
    -DNGX_OK=0 -DNGX_AGAIN=-2 -DNGX_ERROR=-1 \
    -o /tmp/test_compress tests/c/test_compress.c \
    ngx_http_ws_deflate_module/ngx_http_ws_deflate_compress.c -lz

# Frame parser tests (still pass)
gcc ... -o /tmp/test_frame tests/c/test_frame.c \
    ngx_http_ws_deflate_module/ngx_http_ws_deflate_frame.c -lz
```

## Next Steps

Task 6: Wire Tunneling — integrate frame parser + compression engine into the upstream/downstream proxy tunnel.
