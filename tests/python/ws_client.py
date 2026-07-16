import websockets


class WSTestClient:
    def __init__(self, uri: str):
        self.uri = uri
        self.conn = None

    async def connect(self):
        self.conn = await websockets.connect(self.uri, compression=None)
        return self.conn

    async def send_text(self, text: str):
        await self.conn.send(text)

    async def send_bytes(self, data: bytes):
        await self.conn.send(data)

    async def recv_text(self) -> str:
        msg = await self.conn.recv()
        if isinstance(msg, bytes):
            return msg.decode("utf-8")
        return str(msg)

    async def recv_bytes(self) -> bytes:
        msg = await self.conn.recv()
        if isinstance(msg, str):
            return msg.encode("utf-8")
        return bytes(msg)

    async def close(self):
        if self.conn:
            await self.conn.close()
