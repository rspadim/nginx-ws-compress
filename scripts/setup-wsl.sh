#!/bin/bash
set -euo pipefail

echo "=== Installing build dependencies ==="
sudo apt-get update
sudo apt-get install -y \
  build-essential git libpcre3-dev libssl-dev libz-dev \
  autoconf automake libtool pkg-config \
  python3 python3-pip python3-venv curl wget

echo "=== Installing zlib-ng ==="
if [ ! -f /usr/local/include/zlib.h ]; then
  cd /tmp
  git clone --depth 1 --branch develop https://github.com/zlib-ng/zlib-ng.git
  cd zlib-ng
  ./configure --prefix=/usr/local --zlib-compat
  make -j$(nproc)
  sudo make install
  sudo ldconfig
  cd ~
fi

echo "=== Cloning nginx source ==="
NGINX_SRC="$HOME/nginx-src"
if [ ! -d "$NGINX_SRC" ]; then
  git clone https://github.com/nginx/nginx.git "$NGINX_SRC"
  cd "$NGINX_SRC"
  LATEST_TAG=$(git describe --tags --abbrev=0)
  git checkout -b feat/ws-permessage-deflate "$LATEST_TAG"
fi

# The module source is accessed via /mnt/e/... from WSL
MODULE_WIN_PATH="/mnt/e/Trabalho/RS/nginx-ws-compress/ngx_http_ws_deflate_module"
if [ -d "$MODULE_WIN_PATH" ] && [ ! -L "$NGINX_SRC/ngx_http_ws_deflate_module" ]; then
  ln -sf "$MODULE_WIN_PATH" "$NGINX_SRC/"
fi

cd "$NGINX_SRC"
git config user.email "dev@example.com"
git config user.name "Developer"

echo "=== nginx source ready at $NGINX_SRC (branch: feat/ws-permessage-deflate) ==="
echo "=== Setup complete ==="
