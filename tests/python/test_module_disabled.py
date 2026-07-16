import pytest

from ws_client import WSTestClient

pytestmark = pytest.mark.asyncio


async def test_without_module_loaded(nginx_disabled_server):
    """nginx on port 8091 has no load_module directive."""
    client = WSTestClient("ws://127.0.0.1:8091/ws", compress=False)
    await client.connect()
    await client.send_text("no module")
    response = await client.recv_text()
    assert response == "no module"
    await client.close()


async def test_binary_without_module(nginx_disabled_server):
    client = WSTestClient("ws://127.0.0.1:8091/ws-binary", compress=False)
    await client.connect()
    await client.send_bytes(b"\xca\xfe")
    response = await client.recv_bytes()
    assert response == b"\xca\xfe"
    await client.close()


async def test_multiple_messages_disabled(nginx_disabled_server):
    client = WSTestClient("ws://127.0.0.1:8091/ws", compress=False)
    await client.connect()
    for i in range(5):
        await client.send_text(f"d{i}")
        assert await client.recv_text() == f"d{i}"
    await client.close()
