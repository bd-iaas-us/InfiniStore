import infinity
import asyncio
import uvloop
import fastapi
from fastapi import FastAPI, Request
import uvicorn



app = FastAPI()

@app.get("/")
async def read_root():
    return {"Hello": "World"}

if __name__ == "__main__":
    loop = uvloop.new_event_loop()
    asyncio.set_event_loop(loop)
    infinity.lib.register_server(loop)
    config = uvicorn.Config(
    app,
    host="0.0.0.0",
    port=8080,
    loop="uvloop",
    )

    server = uvicorn.Server(config)

    # 运行服务器
    loop.run_until_complete(server.serve())