#!/usr/bin/env python3
"""
Raw WebSocket test: manually do HTTP upgrade, then send/receive a frame.
Tests that the tunnel proxy works without compression dependency.
"""
import asyncio
import struct
import os

WS_HOST = "127.0.0.1"
WS_PORT = 8090
WS_PATH = "/ws"


async def raw_ws_test():
    reader, writer = await asyncio.open_connection(WS_HOST, WS_PORT)

    # 1. HTTP Upgrade handshake
    import secrets
    key = secrets.token_bytes(16)
    import base64
    key_b64 = base64.b64encode(key).decode()

    request = (
        f"GET {WS_PATH} HTTP/1.1\r\n"
        f"Host: {WS_HOST}:{WS_PORT}\r\n"
        f"Upgrade: websocket\r\n"
        f"Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key_b64}\r\n"
        f"Sec-WebSocket-Version: 13\r\n"
        f"Sec-WebSocket-Extensions: permessage-deflate\r\n"
        f"\r\n"
    )
    writer.write(request.encode())
    await writer.drain()

    # Read response
    response = b""
    while b"\r\n\r\n" not in response:
        chunk = await asyncio.wait_for(reader.read(4096), timeout=5)
        if not chunk:
            break
        response += chunk

    print("=== RESPONSE HEADERS ===")
    print(response.decode(errors="replace"))

    if b"101 Switching Protocols" not in response:
        print("FAIL: Not a 101 response")
        writer.close()
        return False

    # Check for extensions header
    if b"Sec-WebSocket-Extensions" in response:
        print("PASS: Sec-WebSocket-Extensions found in response")
    else:
        print("NOTE: No Sec-WebSocket-Extensions in response")

    # 2. Send a WebSocket text frame (unmasked since client→server should be masked,
    # but the tunnel might handle it either way)
    # Build a masked text frame
    payload = b"hello-tunnel"
    mask_key = os.urandom(4)

    frame = bytearray()
    frame.append(0x81)  # FIN + text opcode
    # Masked, length = len(payload)
    if len(payload) < 126:
        frame.append(0x80 | len(payload))  # MASK bit + length
    frame.extend(mask_key)
    # Mask payload
    masked = bytes(b ^ mask_key[i % 4] for i, b in enumerate(payload))
    frame.extend(masked)

    writer.write(bytes(frame))
    await writer.drain()
    print(f"\nSent: {payload}")

    # 3. Read response frame
    resp_header = await asyncio.wait_for(reader.read(2), timeout=10)
    if len(resp_header) < 2:
        print("FAIL: No response")
        writer.close()
        return False

    opcode = resp_header[0] & 0x0F
    masked = resp_header[1] & 0x80
    length = resp_header[1] & 0x7F

    if length == 126:
        ext = await reader.read(2)
        length = struct.unpack("!H", ext)[0]
    elif length == 127:
        ext = await reader.read(8)
        length = struct.unpack("!Q", ext)[0]

    if masked:
        mask = await reader.read(4)

    payload_resp = await asyncio.wait_for(reader.read(length), timeout=5)

    if masked:
        payload_resp = bytes(b ^ mask[i % 4] for i, b in enumerate(payload_resp))

    print(f"Received: {payload_resp}")
    print(f"Opcode: {opcode}, Length: {length}")

    if payload_resp == b"hello-tunnel":
        print("\n=== PASS: Echo matches! Tunnel works! ===")
        writer.close()
        return True
    else:
        # Could be unmasked echo from backend
        print(f"\nMismatch: got {payload_resp!r}, expected {b'hello-tunnel'!r}")
        writer.close()
        return False


async def main():
    result = await raw_ws_test()
    return 0 if result else 1


if __name__ == "__main__":
    exit(asyncio.run(main()))
