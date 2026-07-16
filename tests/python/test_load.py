import asyncio

import psutil
import pytest

from ws_client import WSTestClient

pytestmark = pytest.mark.asyncio

CONCURRENT = 50
MESSAGES_PER_CONN = 20


async def _session(client_id: int):
    client = WSTestClient(f"ws://127.0.0.1:8090/ws", compress=True)
    await client.connect()
    for i in range(MESSAGES_PER_CONN):
        msg = f"load-{client_id}-{i}"
        await client.send_text(msg)
        resp = await client.recv_text()
        assert resp == msg
    await client.close()


def _get_nginx_memory() -> int:
    total = 0
    for proc in psutil.process_iter(["name", "cmdline"]):
        try:
            if proc.info["name"] and "nginx" in proc.info["name"]:
                total += proc.memory_info().rss
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return total


async def test_50_concurrent_connections(nginx_server):
    mem_before = _get_nginx_memory()

    tasks = [_session(i) for i in range(CONCURRENT)]
    await asyncio.gather(*tasks)

    mem_after = _get_nginx_memory()
    mem_diff = mem_after - mem_before

    assert mem_diff < 100 * 1024 * 1024, (
        f"nginx memory grew {mem_diff / 1024 / 1024:.1f} MiB — possible leak"
    )
