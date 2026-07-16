import pytest

from ws_client import WSTestClient

pytestmark = pytest.mark.asyncio


async def test_text_message(nginx_server):
    client = WSTestClient("ws://127.0.0.1:8090/ws", compress=True)
    await client.connect()
    await client.send_text("hello world")
    response = await client.recv_text()
    assert response == "hello world"
    await client.close()


async def test_binary_message(nginx_server):
    client = WSTestClient("ws://127.0.0.1:8090/ws-binary", compress=True)
    payload = bytes(range(256))
    await client.connect()
    await client.send_bytes(payload)
    response = await client.recv_bytes()
    assert response == payload
    await client.close()


async def test_large_payload(nginx_server):
    client = WSTestClient("ws://127.0.0.1:8090/ws", compress=True)
    large = "A" * 100_000
    await client.connect()
    await client.send_text(large)
    response = await client.recv_text()
    assert response == large
    assert len(response) == 100_000
    await client.close()


async def test_no_compress_location(nginx_server):
    client = WSTestClient("ws://127.0.0.1:8090/no-compress", compress=False)
    await client.connect()
    await client.send_text("passthrough")
    response = await client.recv_text()
    assert response == "passthrough"
    await client.close()


async def test_sequential_messages(nginx_server):
    client = WSTestClient("ws://127.0.0.1:8090/ws", compress=True)
    await client.connect()
    for i in range(10):
        msg = f"seq-{i}"
        await client.send_text(msg)
        response = await client.recv_text()
        assert response == msg
    await client.close()


async def test_mixed_text_and_binary(nginx_server):
    client_text = WSTestClient("ws://127.0.0.1:8090/ws", compress=True)
    client_bin = WSTestClient("ws://127.0.0.1:8090/ws-binary", compress=True)
    await client_text.connect()
    await client_bin.connect()
    await client_text.send_text("ping")
    await client_bin.send_bytes(b"\xde\xad")
    assert await client_text.recv_text() == "ping"
    assert await client_bin.recv_bytes() == b"\xde\xad"
    await client_text.close()
    await client_bin.close()
