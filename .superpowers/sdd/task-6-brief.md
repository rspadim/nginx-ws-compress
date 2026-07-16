# Task 6: Tunnel Integration (Event Handler Replacement)

**Files:**
- Modify: `ngx_http_ws_deflate_module/ngx_http_ws_deflate_tunnel.h` (replace stub)
- Modify: `ngx_http_ws_deflate_module/ngx_http_ws_deflate_tunnel.c` (replace stub)
- May need: `ngx_http_ws_deflate_handshake.c` (verify tunnel_install is called)
- May need: `ngx_http_ws_deflate_module.c` (if ctx_t struct changed)

**Interfaces:**
- Consumes: Task 3 (handshake calls tunnel_install), Task 4 (frame parser), Task 5 (compression)
- Produces: complete frame-processing tunnel that intercepts WebSocket connections

## Acceptance Criteria

- `ngx_http_ws_deflate_tunnel_install(r)` is called from the handshake handler
- Custom read event handlers installed on client and upstream connections
- Client→upstream path: parse → decompress (if RSV1) → serialize raw → write to upstream
- Upstream→client path: parse → compress (if text/binary) → serialize with RSV1 → write to client
- Control frames (close/ping/pong) pass through unchanged
- Buffer management: handles partial frames, multiple frames in one read
- Error handling: close connection on parse/compress failure
- Module compiles cleanly

## Implementation

### ngx_http_ws_deflate_tunnel.h

Define the tunnel context struct with all fields needed:

```c
typedef struct {
    ngx_ws_deflate_ctx_t         compress_ctx;    /* compression context */
    ngx_buf_t                   *client_buf;       /* buffer for client data */
    ngx_buf_t                   *upstream_buf;     /* buffer for upstream data */
    ngx_http_ws_deflate_loc_conf_t *conf;          /* module config */
    ngx_flag_t                   handshake_done;   /* tunnel active */
    /* Pool for large allocations during frame processing */
    ngx_pool_t                  *pool;
    /* Temp buffers for compressed/decompressed payloads */
    u_char                      *tmp_compress_buf;
    size_t                       tmp_compress_len;
    u_char                      *tmp_decompress_buf;
    size_t                       tmp_decompress_len;
} ngx_http_ws_deflate_tunnel_ctx_t;
```

### ngx_http_ws_deflate_tunnel.c

Must implement:

1. `ngx_http_ws_deflate_tunnel_install(r)` — installs custom handlers
2. `client_read_handler(ev)` — reads from client, processes frames
3. `upstream_read_handler(ev)` — reads from upstream, processes frames
4. Helper: `process_client_frame(tctx)` — decompress + forward to upstream
5. Helper: `process_upstream_frame(tctx)` — compress + forward to client
6. Helper: `process_upstream_frame_nocompress(tctx)` — pass-through for control frames
7. `ngx_http_ws_deflate_tunnel_close(r)` — cleanup

**Key implementation flow:**

```
client_read_handler:
  read data → append to client_buf
  loop: parse frame → decompress if RSV1 → serialize raw → write to upstream
  → advance buffer → repeat until no more complete frames

upstream_read_handler:
  read data → append to upstream_buf
  loop: parse frame → if text/binary: compress → serialize with RSV1
                    → if control: pass through
  → write to client
  → advance buffer → repeat
```

**Write helper:**
Use `c->send_chain(c, &chain, 0)` to send data. Create a temporary ngx_chain_t with the buffer.

```c
static ngx_int_t
ngx_http_ws_deflate_write(ngx_connection_t *c, u_char *data, size_t len)
{
    ngx_buf_t    *b;
    ngx_chain_t   chain;
    
    b = ngx_create_temp_buf(c->pool, len);
    if (b == NULL) return NGX_ERROR;
    ngx_memcpy(b->start, data, len);
    b->last = b->start + len;
    b->memory = 1;
    chain.buf = b;
    chain.next = NULL;
    
    if (c->send_chain(c, &chain, 0) == NGX_CHAIN_ERROR) {
        return NGX_ERROR;
    }
    return NGX_OK;
}
```

**Handle close frames:**
When a close frame is detected (opcode 0x8), forward it to the other side and close both connections.

**IMPORTANT: Stack buffers vs Pool allocation**
The initial implementation can use stack buffers for compressed/decompressed data (up to 64KB), with a fallback to pool allocation for larger payloads. Document this with a TODO comment.

## Verification

Build the module:
```powershell
wsl -d Ubuntu -u root -- bash -c "cd ~/nginx-src && ./auto/configure --with-compat --with-cc-opt='-I/usr/local/include' --with-ld-opt='-L/usr/local/lib' --add-dynamic-module=ngx_http_ws_deflate_module && make modules -j\$(nproc)"
```

The module should compile cleanly. The tunnel won't be functionally tested until Task 7 (Python integration tests) but compilation proves all interfaces are wired correctly.
