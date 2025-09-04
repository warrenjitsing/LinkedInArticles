import asyncio
import ssl
import json
import argparse
import websockets

# Include the same auth payload generator from our tests
import hmac
import time
import base64


DUMMY_API_KEY = "abc"
DUMMY_API_SECRET = "def"


def get_auth_payload(api_key, api_secret) -> dict:
    timestamp = int(time.time() * 1000)
    message = str(timestamp) + 'GET' + '/users/self/verify'
    mac = hmac.new(bytes(api_secret, encoding='utf8'), bytes(message, encoding='utf-8'), digestmod='sha256')
    sign = base64.b64encode(mac.digest()).decode()
    return {"op": "login", "args": [{"apiKey": api_key, "passphrase": "pw", "timestamp": str(timestamp), "sign": sign}]}


async def main():
    parser = argparse.ArgumentParser(description="Manual WebSocket client.")
    parser.add_argument("endpoint", choices=["echo", "stream", "private"], help="The endpoint to connect to.")
    args = parser.parse_args()

    # Create an SSL context that trusts our self-signed certificate
    ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ssl_context.check_hostname = False
    ssl_context.verify_mode = ssl.CERT_NONE

    uri = f"wss://localhost:8000/ws/{args.endpoint}"
    if args.endpoint == 'private':
        uri = "wss://localhost:8000/ws/v5/private"

    async with websockets.connect(uri, ssl=ssl_context) as websocket:
        print(f"Connected to {uri}")

        if args.endpoint == "echo":
            while True:
                message = input("Message to send (or 'exit'): ")
                if message.lower() == 'exit':
                    break
                await websocket.send(message)
                response = await websocket.recv()
                print(f"< Received: {response}")

        elif args.endpoint == "stream":
            try:
                while True:
                    response = await websocket.recv()
                    print(f"< Received: {response}")
            except websockets.ConnectionClosed:
                print("Stream connection closed by server.")

        elif args.endpoint == "private":
            payload = get_auth_payload(DUMMY_API_KEY, DUMMY_API_SECRET)
            print(f"> Sending login request: {json.dumps(payload)}")
            await websocket.send(json.dumps(payload))
            response = await websocket.recv()
            print(f"< Received: {response}")


if __name__ == "__main__":
    asyncio.run(main())