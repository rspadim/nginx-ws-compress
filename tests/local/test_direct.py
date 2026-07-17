#!/usr/bin/env python3
"""
Raw WebSocket test: manually do HTTP upgrade, then send/receive a frame.
Tests compression by decompressing RSV1=1 frames.
"""
import asyncio
import struct
import os
import zlib

WS_HOST = "127.0.0.1"
WS_PORT = 8090
WS_PATH = "/ws"


async def raw_ws_test():
    reader, writer = await asyncio.open_connection(WS_HOST, WS_PORT)

    # 1. HTTP Upgrade handshake
    import secrets
    import base64
    key = secrets.token_bytes(16)
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

    print("=== RESPONSE ===")
    # Show headers
    header_part = response.split(b"\r\n\r\n")[0]
    print(header_part.decode(errors="replace"))

    if b"101 Switching Protocols" not in response:
        print("FAIL: Not a 101 response")
        writer.close()
        return False

    # 2. Send a masked text frame
    payload = b"hello-tunnel"
    mask_key = os.urandom(4)

    frame = bytearray()
    frame.append(0x81)  # FIN + text
    frame.append(0x80 | len(payload))  # MASK + length
    frame.extend(mask_key)
    masked = bytes(b ^ mask_key[i % 4] for i, b in enumerate(payload))
    frame.extend(masked)

    writer.write(bytes(frame))
    await writer.drain()
    print(f"\nSent: {len(payload)} bytes: {payload!r}")

    # 3. Read response frame  
    resp_header = await asyncio.wait_for(reader.read(2), timeout=10)
    if len(resp_header) < 2:
        print("FAIL: No response")
        writer.close()
        return False

    opcode = resp_header[0] & 0x0F
    rsv1 = bool(resp_header[0] & 0x40)
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

    print(f"Received frame: fin={bool(resp_header[0]&0x80)} rsv1={rsv1} "
          f"opcode={opcode} masked={bool(masked)} length={length}")
    print(f"Raw payload ({len(payload_resp)} bytes): {payload_resp.hex()}")

    if rsv1:
        print("Frame is compressed (RSV1=1)")
        # Raw deflate decompress (permessage-deflate uses -15 window bits)
        try:
            decompressor = zlib.decompressobj(-zlib.MAX_WBITS)
            decompressed = decompressor.decompress(payload_resp)
            decompressed += decompressor.flush()
            print(f"Decompressed: {decompressed}")
            payload_resp = decompressed
        except Exception as e:
            print(f"Decompress FAILED: {e}")
            # Let's try with zlib.wbits instead
            try:
                d2 = zlib.decompressobj(zlib.MAX_WBITS)
                r2 = d2.decompress(payload_resp)
                r2 += d2.flush()
                print(f"Decompress (zlib mode): {r2}")
            except Exception as e2:
                print(f"Decompress (zlib mode) also failed: {e2}")

    writer.close()

    if payload_resp == b"hello-tunnel":
        print("\n=== PASS! Echo matches. ===")
        return True
    else:
        print(f"\n=== FAIL: got {payload_resp!r} ===")
        return False


async def main():
    result = await raw_ws_test()
    return 0 if result else 1


if __name__ == "__main__":
    exit(asyncio.run(main()))
