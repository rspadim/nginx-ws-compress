# Task 3: Handshake Handler (Header Manipulation)

**Files:**
- Modify: `ngx_http_ws_deflate_module/ngx_http_ws_deflate_handshake.h`
- Modify: `ngx_http_ws_deflate_module/ngx_http_ws_deflate_handshake.c`
- Modify: `ngx_http_ws_deflate_module/ngx_http_ws_deflate_module.c` (if needed — already has header filter hook)

**Interfaces:**
- Consumes: Task 2 (loc_conf with enabled flag, header filter already registered)
- Produces: `ngx_http_ws_deflate_handshake_handler(r)` — header filter that manipulates Sec-WebSocket-Extensions

## Acceptance Criteria

- Handshake handler detects WebSocket upgrade (status 101 + Upgrade: websocket)
- Removes `permessage-deflate` from request `Sec-WebSocket-Extensions` before forwarding to backend
- Adds `Sec-WebSocket-Extensions: permessage-deflate` to the response sent to the client
- Marks the request context for tunnel interception (via `ngx_http_set_ctx`)
- Does NOT modify non-WebSocket requests (pass-through for normal HTTP)
- Compiles cleanly with no warnings

## Implementation

### ngx_http_ws_deflate_handshake.h

```c
#ifndef _NGX_HTTP_WS_DEFLATE_HANDSHAKE_H_
#define _NGX_HTTP_WS_DEFLATE_HANDSHAKE_H_

#include <ngx_core.h>
#include <ngx_http.h>

ngx_int_t ngx_http_ws_deflate_handshake_handler(ngx_http_request_t *r);

#endif
```

### ngx_http_ws_deflate_handshake.c

This is the main implementation. The handler must:

1. Check if the module is enabled for this location (conf->enabled or conf->auto_detect)
2. Check for status == 101 Switching Protocols
3. Check for Upgrade: websocket header
4. Remove `permessage-deflate` from the request's Sec-WebSocket-Extensions header
5. Add `Sec-WebSocket-Extensions: permessage-deflate` to the response headers
6. Mark the connection for tunnel interception

**Key details:**
- Access request headers via `r->headers_in.upgrade` and `r->headers_in.sec_websocket_extensions`
- Response headers via `r->headers_out.headers` (ngx_list_push)
- For removing the extension: if the header value is exactly "permessage-deflate", remove the entire header. If there are multiple extensions, strip just "permessage-deflate" and its parameters.
- Use `ngx_strncasecmp` for case-insensitive matching
- Use `ngx_pnalloc(r->pool, ...)` for allocating new header values
- The WS_DEFLATE_EXT_HEADER constant is not needed — nginx already has Sec-WebSocket-Extensions indexed
- For the response, use:
  ```c
  h = ngx_list_push(&r->headers_out.headers);
  h->hash = 1;
  ngx_str_set(&h->key, "Sec-WebSocket-Extensions");
  ngx_str_set(&h->value, "permessage-deflate");
  ```

The stub `ngx_http_ws_deflate_handshake.c` from Task 2 has minimal content. Replace it with the full implementation.

### Build

```powershell
wsl -d Ubuntu -u root -- bash -c "cd ~/nginx-src && ./auto/configure --with-compat --with-cc-opt='-I/usr/local/include' --with-ld-opt='-L/usr/local/lib' --add-dynamic-module=ngx_http_ws_deflate_module && make modules -j\$(nproc)"
```

Should compile cleanly. The resulting `.so` still won't do anything at runtime (no tunnel yet), but the handshake code is now live.
