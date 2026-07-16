import subprocess
import sys
import time
import socket
import shutil
from pathlib import Path

import pytest

NGINX_BIN = shutil.which("nginx") or "/usr/local/nginx/sbin/nginx"
TEST_DIR = Path(__file__).parent


def _wait_for_port(host: str, port: int, timeout: float = 15) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=1):
                return True
        except (ConnectionRefusedError, OSError):
            time.sleep(0.5)
    return False


def _start_nginx(config_name: str):
    config_path = TEST_DIR / config_name
    subprocess.run(
        [NGINX_BIN, "-c", str(config_path)],
        check=True,
        capture_output=True,
    )


def _stop_nginx(config_name: str):
    config_path = TEST_DIR / config_name
    subprocess.run(
        [NGINX_BIN, "-s", "quit", "-c", str(config_path)],
        capture_output=True,
        timeout=10,
    )


@pytest.fixture(scope="session")
def backend_server():
    proc = subprocess.Popen(
        [sys.executable, "-m", "uvicorn", "ws_backend:app",
         "--host", "127.0.0.1", "--port", "9001",
         "--log-level", "error"],
        cwd=TEST_DIR,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if not _wait_for_port("127.0.0.1", 9001):
        proc.terminate()
        proc.wait()
        raise RuntimeError("Backend server failed to start on port 9001")
    yield
    proc.terminate()
    proc.wait()


@pytest.fixture(scope="session")
def nginx_server(backend_server):
    _stop_nginx("nginx.conf")
    _start_nginx("nginx.conf")
    if not _wait_for_port("127.0.0.1", 8090):
        raise RuntimeError("nginx failed to start on port 8090")
    yield
    _stop_nginx("nginx.conf")


@pytest.fixture(scope="session")
def nginx_disabled_server(backend_server):
    _stop_nginx("nginx-disabled.conf")
    _start_nginx("nginx-disabled.conf")
    if not _wait_for_port("127.0.0.1", 8091):
        raise RuntimeError("nginx (disabled) failed to start on port 8091")
    yield
    _stop_nginx("nginx-disabled.conf")
