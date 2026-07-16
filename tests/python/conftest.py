import subprocess
import sys
import time
import socket
import shutil
from pathlib import Path

import pytest

NGINX_BIN = "/usr/local/nginx/sbin/nginx"
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


def _force_kill_nginx():
    """Kill all nginx processes forcefully."""
    subprocess.run(["pkill", "-9", "nginx"], capture_output=True, timeout=5)
    subprocess.run(["pkill", "-9", "uvicorn"], capture_output=True, timeout=5)


def _stop_nginx(prefix: str = "/tmp/nginx-ws-test"):
    _force_kill_nginx()
    subprocess.run(
        [NGINX_BIN, "-s", "stop", "-p", prefix],
        capture_output=True, timeout=5,
    )
    _force_kill_nginx()


def _prepare_prefix(prefix: str):
    """Create nginx prefix directories (logs, temp dirs)."""
    Path(prefix).mkdir(parents=True, exist_ok=True)
    for d in ["logs", "client_body_temp", "proxy_temp", "fastcgi_temp",
              "uwsgi_temp", "scgi_temp"]:
        Path(prefix, d).mkdir(parents=True, exist_ok=True)


def _start_nginx(config_name: str, prefix: str = "/tmp/nginx-ws-test",
                 port: int = 8090):
    config_path = TEST_DIR / config_name
    _prepare_prefix(prefix)
    subprocess.run(
        [NGINX_BIN, "-c", str(config_path), "-p", prefix],
        check=True,
        capture_output=True,
    )
    if not _wait_for_port("127.0.0.1", port):
        _stop_nginx(prefix)
        raise RuntimeError(f"nginx failed to start on port {port}")


@pytest.fixture(scope="session")
def backend_server():
    venv_python = TEST_DIR / "venv" / "bin" / "python3"
    proc = subprocess.Popen(
        [str(venv_python), "-m", "uvicorn", "ws_backend:app",
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
    _stop_nginx()
    _start_nginx("nginx.conf", port=8090)
    yield
    _stop_nginx()


@pytest.fixture(scope="session")
def nginx_disabled_server(backend_server):
    _stop_nginx("/tmp/nginx-ws-test-disabled")
    _start_nginx("nginx-disabled.conf", "/tmp/nginx-ws-test-disabled", 8091)
    yield
    _stop_nginx("/tmp/nginx-ws-test-disabled")


@pytest.fixture(scope="session")
def nginx_auto_server(backend_server):
    _stop_nginx("/tmp/nginx-ws-test-auto")
    _start_nginx("nginx-auto.conf", "/tmp/nginx-ws-test-auto", 8092)
    yield
    _stop_nginx("/tmp/nginx-ws-test-auto")


@pytest.fixture(scope="session")
def nginx_debug_server(backend_server):
    _stop_nginx("/tmp/nginx-ws-test-debug")
    _start_nginx("nginx-debug.conf", "/tmp/nginx-ws-test-debug", 8093)
    yield
    _stop_nginx("/tmp/nginx-ws-test-debug")
