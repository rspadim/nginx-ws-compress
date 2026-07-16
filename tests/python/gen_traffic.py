import asyncio
import websockets

async def main():
    for _ in range(5):
        async with websockets.connect("ws://127.0.0.1:8090/ws", compression=None) as ws:
            for i in range(100):
                await ws.send("payload-" + str(i) + "-" + "A"*500)
                await ws.recv()
    print("traffic done")

asyncio.run(main())
