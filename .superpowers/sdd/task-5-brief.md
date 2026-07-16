# Task 5: Compression Engine (zlib-ng, RFC 7692)

**Files:**
- Modify: `ngx_http_ws_deflate_module/ngx_http_ws_deflate_compress.h` (replace stub)
- Modify: `ngx_http_ws_deflate_module/ngx_http_ws_deflate_compress.c` (replace stub)
- Modify: `tests/c/test_compress.c` (new)
- Modify: `tests/c/Makefile` (add compression test)

**Interfaces:**
- Consumes: nothing (standalone compression, no nginx dependency besides ngx_core.h)
- Produces:
  - `ngx_ws_deflate_ctx_init(ctx, level, takeover) → NGX_OK/NGX_ERROR`
  - `ngx_ws_deflate_compress(ctx, in, in_len, out, *out_len) → NGX_OK/NGX_ERROR`
  - `ngx_ws_deflate_decompress(ctx, in, in_len, out, *out_len) → NGX_OK/NGX_ERROR`
  - `ngx_ws_deflate_ctx_destroy(ctx)`

## Acceptance Criteria

- Uses zlib-ng (zng_* API) with raw deflate (windowBits = -15) per RFC 7692
- Compression with Z_SYNC_FLUSH per RFC 7692 §7.2.1
- Strips trailing 0x00 0x00 0xFF 0xFF from compressed output (zlib sync flush tail)
- Context takeover: keep zlib context between messages if enabled
- Roundtrip test: compress → decompress produces original data
- Multiple messages with/without context takeover
- Empty payload roundtrip
- Links against libz (zlib-ng in --zlib-compat mode)
- C unit tests compile standalone
- Module compiles within nginx build (links -lz)

## Implementation

### ngx_http_ws_deflate_compress.h

```c
typedef struct {
    zng_stream    deflate_stream;
    zng_stream    inflate_stream;
    ngx_flag_t    context_takeover;
    ngx_int_t     compression_level;
    ngx_flag_t    initialized;
} ngx_ws_deflate_ctx_t;
```

### ngx_http_ws_deflate_compress.c

Key implementation details:
- `windowBits = -15` (raw deflate, no gzip/zlib header)
- `mem_level = 8`
- `ngx_ws_deflate_compress`: calls `zng_deflate(strm, Z_SYNC_FLUSH)`, strips tail 0x00 0x00 0xFF 0xFF
- `ngx_ws_deflate_decompress`: calls `zng_inflate(strm, Z_SYNC_FLUSH)`
- Non-takeover mode: resets stream after each message via `zng_deflateReset`/`zng_inflateReset`

### Makefile update (tests/c/Makefile)

Add compression tests:
```makefile
CFLAGS = -Wall -Wextra -g -I../../ngx_http_ws_deflate_module
CFLAGS += -DNGX_OK=0 -DNGX_AGAIN=-2 -DNGX_ERROR=-1

test_runner: test_frame.c test_compress.c \
    ../../ngx_http_ws_deflate_module/ngx_http_ws_deflate_frame.c \
    ../../ngx_http_ws_deflate_module/ngx_http_ws_deflate_compress.c
	$(CC) $(CFLAGS) -o $@ $^ -lz
```

Or run via WSL with nginx include paths:
```bash
wsl -d Ubuntu -u root -- bash -c "cd ~/nginx-src && gcc -Wall -Wextra -g -I src/core -I src/os/unix -I objs -I /usr/local/include -o /tmp/test_ws tests/c/test_compress.c tests/c/test_frame.c ngx_http_ws_deflate_module/ngx_http_ws_deflate_frame.c ngx_http_ws_deflate_module/ngx_http_ws_deflate_compress.c -lz && /tmp/test_ws"
```

## Tests (test_compress.c)

Four tests minimum:
1. `test_compress_roundtrip` — compress "Hello World..." → decompress matches
2. `test_multiple_messages_takeover` — 3 messages with context takeover
3. `test_empty_payload` — empty string roundtrip
4. `test_no_context_takeover` — 3 messages without takeover (streams reset each time)

## Build

```powershell
# Standalone tests
wsl -d Ubuntu -u root -- bash -c "cd ~/nginx-src && gcc -Wall -Wextra -g -I src/core -I src/os/unix -I objs -I /usr/local/include -o /tmp/test_ws tests/c/test_compress.c tests/c/test_frame.c ngx_http_ws_deflate_module/ngx_http_ws_deflate_frame.c ngx_http_ws_deflate_module/ngx_http_ws_deflate_compress.c -lz && /tmp/test_ws"

# nginx module build
wsl -d Ubuntu -u root -- bash -c "cd ~/nginx-src && ./auto/configure --with-compat --with-cc-opt='-I/usr/local/include' --with-ld-opt='-L/usr/local/lib' --add-dynamic-module=ngx_http_ws_deflate_module && make modules -j\$(nproc)"
```
