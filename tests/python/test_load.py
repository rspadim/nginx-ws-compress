import asyncio

import pytest

from ws_client import WSTestClient

pytestmark = pytest.mark.asyncio

CONCURRENT = 50
MESSAGES_PER_CONN = 10


async def _session(client_id: int):
    client = WSTestClient(f"ws://127.0.0.1:8090/ws")
    await client.connect()
    for i in range(MESSAGES_PER_CONN):
        msg = f"load-{client_id}-{i}"
        await client.send_text(msg)
        resp = await client.recv_text()
        assert resp == msg
    await client.close()


async def test_concurrent_connections(nginx_server):
    tasks = [_session(i) for i in range(CONCURRENT)]
    await asyncio.gather(*tasks)
