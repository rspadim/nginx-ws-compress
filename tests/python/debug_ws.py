import asyncio
import websockets

async def debug():
    async with websockets.connect("ws://127.0.0.1:8090/ws") as ws:
        req = ws.request
        print(f"Request headers type: {type(req.headers)}")
        for h in req.headers:
            print(f"  {h}: {req.headers[h]}")

asyncio.run(debug())
