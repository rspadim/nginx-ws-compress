# Task 2: Module Skeleton (config + module registration + directives)

**Files:**
- Create: `ngx_http_ws_deflate_module/config`
- Create: `ngx_http_ws_deflate_module/ngx_http_ws_deflate_module.c`
- Create: `tests/python/nginx.conf`

**Interfaces:**
- Consumes: Task 1 (nginx source setup in WSL at ~/nginx-src)
- Produces: loadable `.so` module with config directives that parse correctly

## Acceptance Criteria

- `ngx_http_ws_deflate_module/config` exists and references all 5 source files
- `ngx_http_ws_deflate_module/ngx_http_ws_deflate_module.c` implements:
  - All configuration directives (ws_deflate, ws_deflate_auto, ws_deflate_except, ws_deflate_compression_level, ws_deflate_context_takeover, ws_deflate_chunk_size)
  - Module registration with NGX_HTTP_MODULE type
  - Create/merge config functions for both server and location contexts
  - Postconfiguration hook that registers the header filter (calling ngx_http_ws_deflate_postconfiguration)
- `tests/python/nginx.conf` contains test nginx config with load_module and two locations
- Module compiles successfully: `make modules` produces `objs/ngx_http_ws_deflate_module.so`

## Implementation

### config script

```bash
ngx_addon_name=ngx_http_ws_deflate_module
HTTP_MODULES="$HTTP_MODULES ngx_http_ws_deflate_module"
NGX_ADDON_SRCS="$NGX_ADDON_SRCS \
    $ngx_addon_dir/ngx_http_ws_deflate_module.c \
    $ngx_addon_dir/ngx_http_ws_deflate_handshake.c \
    $ngx_addon_dir/ngx_http_ws_deflate_frame.c \
    $ngx_addon_dir/ngx_http_ws_deflate_compress.c \
    $ngx_addon_dir/ngx_http_ws_deflate_tunnel.c"
CORE_LIBS="$CORE_LIBS -lz-ng"
```

### ngx_http_ws_deflate_module.c

Must include:
- `#include <ngx_config.h>`, `<ngx_core.h>`, `<ngx_http.h>`
- `ngx_http_ws_deflate_loc_conf_t` struct with fields: `enabled` (ngx_flag_t), `auto_detect` (ngx_flag_t), `except_patterns` (ngx_array_t*), `compression_level` (ngx_int_t), `context_takeover` (ngx_flag_t), `chunk_size` (size_t)
- `ngx_http_ws_deflate_srv_conf_t` struct with fields: `auto_detect` (ngx_flag_t), `except_patterns` (ngx_array_t*)
- Commands array: ws_deflate, ws_deflate_auto, ws_deflate_except, ws_deflate_compression_level, ws_deflate_context_takeover, ws_deflate_chunk_size
- Module context with create_srv_conf, merge_srv_conf, create_loc_conf, merge_loc_conf
- Postconfiguration function that registers the handshake handler as a header filter in NGX_HTTP_UPSTREAM_HEADER_FILTER phase
- Module declaration (ngx_module_t ngx_http_ws_deflate_module)
- Include "ngx_http_ws_deflate_handshake.h" and call ngx_http_ws_deflate_handshake_handler

The complete code is in the plan at Task 2 Step 2. Use it as template.

### tests/python/nginx.conf

```nginx
worker_processes  1;
error_log  /tmp/nginx-ws-test-error.log;
pid        /tmp/nginx-ws-test.pid;

load_module /usr/local/nginx/modules/ngx_http_ws_deflate_module.so;

events {
    worker_connections  1024;
}

http {
    access_log  /tmp/nginx-ws-test-access.log;

    server {
        listen       8090;

        location /ws {
            proxy_pass http://127.0.0.1:9001;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection "upgrade";
            ws_deflate on;
            ws_deflate_compression_level 6;
            ws_deflate_context_takeover on;
        }

        location /no-compress {
            proxy_pass http://127.0.0.1:9001;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection "upgrade";
            ws_deflate off;
        }
    }
}
```

## Build Steps

After creating the files, build the module:

```bash
cd ~/nginx-src
./auto/configure \
  --with-compat \
  --with-cc-opt="-I/usr/local/include" \
  --with-ld-opt="-L/usr/local/lib" \
  --add-dynamic-module=ngx_http_ws_deflate_module
make modules -j$(nproc)
```

This should succeed. The resulting `.so` file must exist:
```bash
ls -la objs/ngx_http_ws_deflate_module.so
```
