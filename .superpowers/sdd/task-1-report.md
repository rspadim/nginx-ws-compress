# Task 1: Environment Setup — Report

## What was implemented

1. **`scripts/setup-wsl.sh`** — Bash script that installs build dependencies (build-essential, git, libpcre3-dev, libssl-dev, etc.), compiles and installs zlib-ng from source (develop branch), clones nginx official GitHub mirror to `~/nginx-src` and creates branch `feat/ws-permessage-deflate` at the latest release tag.

2. **`scripts/build-module.sh`** — Bash script that runs `auto/configure` (adjusted for the GitHub mirror layout where configure lives in `auto/`) with `--add-dynamic-module=ngx_http_ws_deflate_module` and then `make modules`.

3. **`tests/python/requirements.txt`** — Python test dependencies (fastapi, uvicorn, websockets, httpx, pytest, playwright, psutil).

## What was tested and test results

- **Setup script**: Ran successfully in WSL (Ubuntu, as root). Installed all apt packages, compiled zlib-ng, cloned nginx at tag `release-1.31.3`, created branch `feat/ws-permessage-deflate`.
- **Build script**: Ran and failed as expected — `configure` reports `error: no ngx_http_ws_deflate_module/config was found` because the module C source does not exist yet (Task 2).
- **Verification**:
  - `~/nginx-src` exists with branch `feat/ws-permessage-deflate` ✓
  - `/usr/local/include/zlib.h` exists (zlib-ng installed) ✓

## Files changed

- `scripts/setup-wsl.sh` — new
- `scripts/build-module.sh` — new
- `tests/python/requirements.txt` — new

## Self-review findings

- The nginx GitHub mirror stores `configure` at `auto/configure`, not at the root as in release tarballs. The `build-module.sh` script was updated to use `./auto/configure` instead of `./configure`.
- The setup script creates a symlink for the module directory if it exists; it will symlink when Task 2 creates `ngx_http_ws_deflate_module/`.

## Issues or concerns

- None. Environment is ready for Task 2 (module implementation).
- WSL Ubuntu runs as `root` by default for this distro — all commands worked without issues.
- zlib-ng was compiled from `develop` branch (zlib-compat mode); nginx source from GitHub mirror at `release-1.31.3`.
