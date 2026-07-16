# Task 3: Handshake Handler — Report

## What was implemented

1. **`ngx_http_ws_deflate_handshake.h`** — Exports the loc_conf struct so both handshake.c and module.c share the same definition; declares `ngx_http_ws_deflate_module` as extern; declares `ngx_http_ws_deflate_handshake_handler`.

2. **`ngx_http_ws_deflate_handshake.c`** — Full handshake handler implementation that:
   - Gets loc_conf and returns `NGX_DECLINED` if module is disabled
   - Checks `r->headers_out.status == NGX_HTTP_SWITCHING_PROTOCOLS` (101)
   - Checks `r->headers_in.upgrade` for `websocket` (exact 9-char case-insensitive match)
   - Iterates `r->headers_in.headers` list to find `Sec-WebSocket-Extensions` (nginx 1.31.3 has no pre-parsed field for this)
   - Strips `permessage-deflate` (including parameters and comma separators) from the request header; sets `hash=0` to clear if it was the only extension
   - Adds `Sec-WebSocket-Extensions: permessage-deflate` to `r->headers_out.headers` via `ngx_list_push`
   - Sets request context (`ngx_http_ws_deflate_ctx_t`) via `ngx_http_set_ctx` for future tunnel interception
   - Returns `NGX_OK` on success, `NGX_ERROR` on allocation failure, `NGX_DECLINED` for pass-through

3. **`ngx_http_ws_deflate_tunnel.h`** — Defines `ngx_http_ws_deflate_ctx_t` with `initialized:1` bitfield for context marking.

4. **`ngx_http_ws_deflate_module.c`** — Removed duplicate loc_conf struct definition (now sourced from handshake.h).

## Build verification

- `./auto/configure` — success
- `make modules` — success, no warnings (compiled with `-Werror`)
- `ngx_http_ws_deflate_module.so` — 93816 bytes, valid ELF 64-bit shared object

## Deviations from brief

- `sec_websocket_extensions` does not exist as a pre-parsed field in nginx 1.31.3's `ngx_http_headers_in_t`. Instead, the handler iterates `r->headers_in.headers` to find the header by lowercase name `sec-websocket-extensions`. This is functionally equivalent and more portable.
- The loc_conf struct was moved to the header (`handshake.h`) to avoid ODR violations when both `module.c` and `handshake.c` reference it.

## Files changed

```
M  ngx_http_ws_deflate_module/ngx_http_ws_deflate_handshake.c
M  ngx_http_ws_deflate_module/ngx_http_ws_deflate_handshake.h
M  ngx_http_ws_deflate_module/ngx_http_ws_deflate_module.c
M  ngx_http_ws_deflate_module/ngx_http_ws_deflate_tunnel.h
```

Ready for Task 4 (tunnel interception).
