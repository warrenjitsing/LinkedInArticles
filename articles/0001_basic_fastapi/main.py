import asyncio
import hmac
import base64


from contextlib import asynccontextmanager
from pathlib import Path
from datetime import datetime, UTC


import uvicorn


from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse
from pydantic import BaseModel


# Define the path for our static content
STATIC_DIR = Path("static")
DATA_FILE = STATIC_DIR / "data.txt"

# --- DUMMY CREDENTIALS FOR DEMONSTRATION ---
# In a real application, these would be stored securely in a database.
DUMMY_API_KEY = "abc"
DUMMY_API_SECRET = "def"


class CreateItemRequest(BaseModel):
    name: str
    is_offer: bool | None = None


@asynccontextmanager
async def lifespan(app: FastAPI):
    """
    Manages the creation and cleanup of a static file for demonstration.
    """
    print("Server is starting up...")
    STATIC_DIR.mkdir(exist_ok=True)
    DATA_FILE.write_text("This is a test file served by FastAPI.")
    yield
    print("Server is shutting down...")
    DATA_FILE.unlink()
    STATIC_DIR.rmdir()


app = FastAPI(lifespan=lifespan)


def _verify_signature(payload: dict) -> bool:
    """Verifies the signature from a client login message."""
    # Extract the arguments from the payload
    try:
        args = payload["args"][0]
        api_key = args["apiKey"]
        timestamp = args["timestamp"]
        client_sign = args["sign"]
    except (KeyError, IndexError):
        return False

    # Check if the API key is valid
    if api_key != DUMMY_API_KEY:
        return False

    # Re-create the signature on the server side
    message = str(timestamp) + 'GET' + '/users/self/verify'
    mac = hmac.new(
        bytes(DUMMY_API_SECRET, encoding='utf8'),
        bytes(message, encoding='utf-8'),
        digestmod='sha256'
    )
    server_sign = base64.b64encode(mac.digest()).decode()

    # Securely compare the client's signature with the server's
    return hmac.compare_digest(server_sign, client_sign)



@app.get("/")
async def read_root():
    """A simple root endpoint to confirm the server is running."""
    return {"message": "Server is running"}


@app.get("/status")
async def get_status():
    """Returns the current status of the server."""
    return {"status": "ok"}


@app.get("/static/data.txt")
async def get_data_file():
    """Serves the static data.txt file."""
    return FileResponse(DATA_FILE)


@app.get("/items/{item_id}")
async def get_item(item_id: int):
    """
    Returns a dummy item if the ID is valid, otherwise raises a 404.
    """
    if item_id != 1:
        raise HTTPException(
            status_code=404,
            detail=f"Item with ID {item_id} not found."
        )
    return {"item_id": item_id, "name": "The One Item"}


@app.post("/items", status_code=201)
async def create_item(item: CreateItemRequest):
    """Creates a new item from a request body."""
    return {"message": "Item created successfully", "item_data": item.model_dump()}


@app.websocket("/ws/echo")
async def websocket_echo(websocket: WebSocket):
    """
    A simple WebSocket endpoint that echoes back any message it receives.
    """
    await websocket.accept()
    try:
        while True:
            message = await websocket.receive_text()
            await websocket.send_text(f"Echo: {message}")
    except WebSocketDisconnect:
        print("Client disconnected from echo endpoint.")


@app.websocket("/ws/stream")
async def websocket_stream(websocket: WebSocket):
    """
    Streams the current server time to the client every second.
    """
    await websocket.accept()
    try:
        while True:
            # Generate the data to send
            payload = {
                "timestamp": datetime.now(UTC).isoformat(),
                "message": "This is a periodic update from the server."
            }
            # Send the data as JSON
            await websocket.send_json(payload)
            # Wait for one second
            await asyncio.sleep(1)
    except WebSocketDisconnect:
        print("Client disconnected from stream endpoint.")


@app.websocket("/ws/v5/private")
async def websocket_private(websocket: WebSocket):
    """
    A simulated private endpoint requiring signature authentication.
    """
    await websocket.accept()
    try:
        # Wait for the initial login message
        login_payload = await websocket.receive_json()

        if login_payload.get("op") == "login" and _verify_signature(login_payload):
            await websocket.send_json({"event": "login", "success": True})
            # --- AUTHENTICATED LOOP ---
            # The client is now authenticated.
            # You could enter another loop here to handle private data.
            # For this example, we will just close after login.
            await websocket.close()
        else:
            # If login fails, send an error and close the connection
            await websocket.send_json({"event": "login", "success": False, "message": "Authentication failed"})
            await websocket.close()

    except WebSocketDisconnect:
        print("Client disconnected from private endpoint.")
    except Exception as e:
        # Handle potential errors like malformed JSON
        print(f"An error occurred in the private endpoint: {e}")


if __name__ == "__main__":
    uvicorn.run(
        "main:app",
        host="0.0.0.0",
        port=8000,
        reload=True,
        ssl_keyfile="key.pem",
        ssl_certfile="cert.pem",
    )