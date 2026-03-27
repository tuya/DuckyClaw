
# pip install websockets
import asyncio
import json

import websockets

TOKEN = "my-secret-token-2026"
URI = f"ws://xxx.xxx.xxx.xxx:18789/?token={TOKEN}"
CHAT_ID = "user1"
CONTENT = "Hello"

async def chat():
    async with websockets.connect(URI) as ws:
        await ws.send(json.dumps({
            "type": "message",
            "content": CONTENT,
            "chat_id": CHAT_ID
        }))
        print(f"sent: chat_id={CHAT_ID} content={CONTENT}")
        print("waiting for device messages, press Ctrl+C to stop")

        try:
            async for resp in ws:
                try:
                    print(json.loads(resp))
                except json.JSONDecodeError:
                    print(resp)
        except websockets.ConnectionClosed as exc:
            print(f"connection closed: code={exc.code} reason={exc.reason}")

asyncio.run(chat())
