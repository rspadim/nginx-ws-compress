#!/bin/bash
set -euo pipefail

NGINX_SRC="${1:-$HOME/nginx-src}"
cd "$NGINX_SRC"

echo "=== Configuring nginx with ws_deflate module ==="
./auto/configure \
  --with-compat \
  --with-cc-opt="-I/usr/local/include" \
  --with-ld-opt="-L/usr/local/lib" \
  --add-dynamic-module=ngx_http_ws_deflate_module

echo "=== Building (modules only) ==="
make modules -j$(nproc)

echo "=== Module built ==="
ls -la objs/ngx_http_ws_deflate_module.so
