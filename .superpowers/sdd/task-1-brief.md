# Task 1: Environment Setup (WSL + Dependencies + nginx Clone)

**Files:**
- Create: `scripts/setup-wsl.sh`
- Create: `scripts/build-module.sh`
- Create: `tests/python/requirements.txt`

**Interfaces:**
- Consumes: nothing (first task)
- Produces: working WSL environment with nginx source cloned at `~/nginx-src`,
  zlib-ng installed, module directory linked; build script that compiles module

**Steps:**

- [ ] Step 1: Create `scripts/setup-wsl.sh`
- [ ] Step 2: Create `scripts/build-module.sh`
- [ ] Step 3: Create `tests/python/requirements.txt`
- [ ] Step 4: Run setup via WSL (wsl -d Ubuntu -u root -- ...)
- [ ] Step 5: Commit

## Step Details

### Step 1: `scripts/setup-wsl.sh`

```bash
#!/bin/bash
set -euo pipefail

echo "=== Installing build dependencies ==="
sudo apt-get update
sudo apt-get install -y \
  build-essential git libpcre3-dev libssl-dev libz-dev \
  autoconf automake libtool pkg-config \
  python3 python3-pip python3-venv curl wget

echo "=== Installing zlib-ng ==="
if [ ! -d /usr/local/include/zlib-ng ]; then
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
```

### Step 2: `scripts/build-module.sh`

```bash
#!/bin/bash
set -euo pipefail

NGINX_SRC="${1:-$HOME/nginx-src}"
cd "$NGINX_SRC"

echo "=== Configuring nginx with ws_deflate module ==="
./configure \
  --with-compat \
  --with-cc-opt="-I/usr/local/include" \
  --with-ld-opt="-L/usr/local/lib" \
  --add-dynamic-module=ngx_http_ws_deflate_module

echo "=== Building (modules only) ==="
make modules -j$(nproc)

echo "=== Module built ==="
ls -la objs/ngx_http_ws_deflate_module.so
```

### Step 3: `tests/python/requirements.txt`

```
fastapi>=0.100.0
uvicorn[standard]>=0.23.0
websockets>=12.0
httpx>=0.25.0
pytest>=8.0
pytest-asyncio>=0.23.0
playwright>=1.40.0
psutil>=5.9.0
```

### Step 4: Run via WSL

```powershell
# From Windows, run:
wsl -d Ubuntu -u root bash /mnt/e/Trabalho/RS/nginx-ws-compress/scripts/setup-wsl.sh
```

## Acceptance Criteria

- `scripts/setup-wsl.sh` exists and is executable
- `scripts/build-module.sh` exists and is executable
- `tests/python/requirements.txt` exists
- nginx source cloned at `~/nginx-src` in WSL on branch `feat/ws-permessage-deflate`
- zlib-ng installed in WSL
- Module compiles: `make modules` produces `objs/ngx_http_ws_deflate_module.so`
