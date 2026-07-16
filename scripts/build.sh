#!/bin/bash
# Build script for Linux / macOS
# Usage: ./scripts/build.sh [nginx-source-dir]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODULE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)/ngx_http_ws_deflate_module"
NGINX_SRC="${1:-$HOME/nginx-src}"

echo "=== ws_deflate module build ==="
echo "Module: $MODULE_DIR"
echo "nginx:  $NGINX_SRC"

# Clone nginx source if not present
if [ ! -d "$NGINX_SRC" ]; then
    echo "Cloning nginx source..."
    git clone https://github.com/nginx/nginx.git "$NGINX_SRC"
    cd "$NGINX_SRC"
    git checkout "$(git describe --tags --abbrev=0)"
    cd -
fi

cd "$NGINX_SRC"

echo "=== Configuring ==="
./auto/configure \
    --with-compat \
    --add-dynamic-module="$MODULE_DIR"

echo "=== Building module ==="
make modules -j"$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)"

echo "=== Done ==="
ls -la objs/ngx_http_ws_deflate_module.so
file objs/ngx_http_ws_deflate_module.so
