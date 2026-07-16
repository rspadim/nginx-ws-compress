import asyncio
import websockets

async def main():
    async with websockets.connect("ws://127.0.0.1:8090/ws") as ws:
        await ws.send("hello deflate")
        r = await ws.recv()
        print(f"OK: {r}")

asyncio.run(main())
