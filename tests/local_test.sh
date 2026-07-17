#!/bin/bash
# Local test: build module + run compression test with debug logs
set -euxo pipefail

NGINX_VERSION="release-1.31.3"
WORKDIR="/tmp/nginx-ws-test-local"
rm -rf "$WORKDIR"

# Start minimal ubuntu container with everything needed
docker run --rm -it \
  -v "$PWD":/src \
  -w /src \
  ubuntu:24.04 bash -c '
set -euxo pipefail
apt-get update -qq
apt-get install -y -qq build-essential git libpcre3-dev libssl-dev \
    python3 python3-pip python3-venv curl wget sudo libnss3 libnspr4 libatk1.0-0 libatk-bridge2.0-0 \
    libcups2 libdrm2 libxkbcommon0 libxcomposite1 libxdamage1 libxrandr2 libgbm1 libpango-1.0-0 \
    libcairo2 libasound2 libatspi2.0-0 2>&1 | tail -5

# Install zlib-ng
cd /tmp
git clone --depth 1 --branch develop https://github.com/zlib-ng/zlib-ng.git
cd zlib-ng
./configure --prefix=/usr/local --zlib-compat
make -j$(nproc)
make install
ldconfig

# Build module
cd /src
git clone https://github.com/nginx/nginx.git /tmp/nginx-src
cd /tmp/nginx-src
git checkout '"$NGINX_VERSION"'
./auto/configure \
    --with-compat --with-debug \
    --with-cc-opt="-I/usr/local/include" \
    --with-ld-opt="-L/usr/local/lib" \
    --add-dynamic-module=/src/ngx_http_ws_deflate_module
make modules -j$(nproc)
make install

# Install Python deps
cd /src/tests/python
python3 -m venv venv
source venv/bin/activate
pip install -q -r requirements.txt
playwright install chromium 2>/dev/null || true
playwright install-deps chromium 2>/dev/null || true

# Run test with verbose logging
echo "=== Running compression test ==="
python -m pytest test_compression_active.py -v -s 2>&1 || true

# Show nginx error log
echo "=== Nginx error log ==="
cat /tmp/nginx-ws-test/logs/error.log 2>/dev/null || echo "(no error log)"
'
