import pytest

from ws_client import WSTestClient

pytestmark = pytest.mark.asyncio


async def test_text_message(nginx_server):
    client = WSTestClient("ws://127.0.0.1:8090/ws")
    await client.connect()
    await client.send_text("hello world")
    response = await client.recv_text()
    assert response == "hello world"
    await client.close()


async def test_binary_message(nginx_server):
    client = WSTestClient("ws://127.0.0.1:8090/ws-binary")
    payload = bytes(range(256))
    await client.connect()
    await client.send_bytes(payload)
    response = await client.recv_bytes()
    assert response == payload
    await client.close()


async def test_large_payload(nginx_server):
    client = WSTestClient("ws://127.0.0.1:8090/ws")
    large = "A" * 10_000  # 10KB
    await client.connect()
    await client.send_text(large)
    response = await client.recv_text()
    assert response == large
    assert len(response) == 10_000
    await client.close()


async def test_no_compress_location(nginx_server):
    client = WSTestClient("ws://127.0.0.1:8090/no-compress")
    await client.connect()
    await client.send_text("passthrough")
    response = await client.recv_text()
    assert response == "passthrough"
    await client.close()


async def test_sequential_messages(nginx_server):
    client = WSTestClient("ws://127.0.0.1:8090/ws")
    await client.connect()
    for i in range(10):
        msg = f"seq-{i}"
        await client.send_text(msg)
        response = await client.recv_text()
        assert response == msg
    await client.close()


async def test_mixed_text_and_binary(nginx_server):
    client_text = WSTestClient("ws://127.0.0.1:8090/ws")
    client_bin = WSTestClient("ws://127.0.0.1:8090/ws-binary")
    await client_text.connect()
    await client_bin.connect()
    await client_text.send_text("ping")
    await client_bin.send_bytes(b"\xde\xad")
    assert await client_text.recv_text() == "ping"
    assert await client_bin.recv_bytes() == b"\xde\xad"
    await client_text.close()
    await client_bin.close()


async def test_empty_message(nginx_server):
    """Empty string must roundtrip correctly."""
    client = WSTestClient("ws://127.0.0.1:8090/ws")
    await client.connect()
    await client.send_text("")
    response = await client.recv_text()
    assert response == "", f"Expected empty string, got '{response}'"
    await client.close()


async def test_single_byte(nginx_server):
    """Single byte payload must roundtrip correctly."""
    client = WSTestClient("ws://127.0.0.1:8090/ws")
    await client.connect()
    await client.send_text("Z")
    response = await client.recv_text()
    assert response == "Z"
    await client.close()


async def test_125_126_127_boundary(nginx_server):
    """
    RFC 6455 frame length boundaries:
    0-125: 7-bit inline
    126: 16-bit extended
    127: 16-bit extended
    """
    client = WSTestClient("ws://127.0.0.1:8090/ws")
    await client.connect()
    for size in [125, 126, 127]:
        payload = "B" * size
        await client.send_text(payload)
        response = await client.recv_text()
        assert response == payload, f"Failed at size={size}"
    await client.close()


async def test_65535_65536_boundary(nginx_server):
    """
    RFC 6455 frame length boundaries:
    65535: max 16-bit extended length
    65536: min 64-bit extended length
    """
    client = WSTestClient("ws://127.0.0.1:8090/ws")
    await client.connect()
    for size in [65535, 65536]:
        payload = "C" * size
        await client.send_text(payload)
        response = await client.recv_text()
        assert response == payload, f"Failed at size={size}"
    await client.close()
