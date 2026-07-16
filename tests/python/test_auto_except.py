"""
Test ws_deflate_auto and ws_deflate_except directives.
"""
import pytest

from ws_client import WSTestClient

pytestmark = pytest.mark.asyncio


async def test_auto_detects_websocket(nginx_auto_server):
    """ws_deflate_auto on should compress WebSocket without explicit directive."""
    client = WSTestClient("ws://127.0.0.1:8092/ws")
    await client.connect()
    await client.send_text("auto-test")
    response = await client.recv_text()
    assert response == "auto-test"
    await client.close()


async def test_auto_except_prefix(nginx_auto_server):
    """Location matching ws_deflate_except prefix should NOT be compressed."""
    client = WSTestClient("ws://127.0.0.1:8092/no-compress/ws")
    await client.connect()
    await client.send_text("excluded-by-prefix")
    response = await client.recv_text()
    assert response == "excluded-by-prefix"
    await client.close()


async def test_auto_except_regex(nginx_auto_server):
    """Location matching ws_deflate_except regex should NOT be compressed."""
    client = WSTestClient("ws://127.0.0.1:8092/something/legacy/ws")
    await client.connect()
    await client.send_text("excluded-by-regex")
    response = await client.recv_text()
    assert response == "excluded-by-regex"
    await client.close()


async def test_sequential_auto(nginx_auto_server):
    """Multiple messages through auto-detected WebSocket."""
    client = WSTestClient("ws://127.0.0.1:8092/ws")
    await client.connect()
    for i in range(5):
        msg = f"auto-{i}"
        await client.send_text(msg)
        assert await client.recv_text() == msg
    await client.close()
