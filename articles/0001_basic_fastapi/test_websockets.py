import hmac
import time
import base64
from fastapi.testclient import TestClient
from main import app


# --- Test Data for Private Endpoint ---
DUMMY_API_KEY = "abc"
DUMMY_API_SECRET = "def"


def get_auth_payload(api_key, api_secret) -> dict:
    """Generates a valid login payload for the private endpoint."""
    timestamp = int(time.time() * 1000)
    message = str(timestamp) + 'GET' + '/users/self/verify'
    mac = hmac.new(
        bytes(api_secret, encoding='utf8'),
        bytes(message, encoding='utf-8'),
        digestmod='sha256'
    )
    sign = base64.b64encode(mac.digest()).decode()

    return {
        "op": "login",
        "args": [{
            "apiKey": api_key,
            "passphrase": "dummy_passphrase", # Not used in our server logic
            "timestamp": str(timestamp),
            "sign": sign
        }]
    }


def test_websocket_echo():
    """Tests the /ws/echo endpoint."""
    with TestClient(app) as client:
        with client.websocket_connect("/ws/echo") as websocket:
            test_message = "Hello, WebSocket!"
            websocket.send_text(test_message)
            response = websocket.receive_text()
            assert response == f"Echo: {test_message}"


def test_websocket_stream():
    """Tests the /ws/stream endpoint for a few messages."""
    with TestClient(app) as client:
        with client.websocket_connect("/ws/stream") as websocket:
            # Receive the first 3 messages from the stream
            for _ in range(3):
                response = websocket.receive_json()
                assert "timestamp" in response
                assert "message" in response
            # The test client automatically closes the connection here.


def test_websocket_private_auth_success():
    """Tests a successful authentication on the private endpoint."""
    with TestClient(app) as client:
        with client.websocket_connect("/ws/v5/private") as websocket:
            payload = get_auth_payload(DUMMY_API_KEY, DUMMY_API_SECRET)
            websocket.send_json(payload)
            response = websocket.receive_json()
            assert response == {"event": "login", "success": True}


def test_websocket_private_auth_failure():
    """Tests a failed authentication with a bad secret."""
    with TestClient(app) as client:
        with client.websocket_connect("/ws/v5/private") as websocket:
            payload = get_auth_payload(DUMMY_API_KEY, "BAD_SECRET")
            websocket.send_json(payload)
            response = websocket.receive_json()
            assert response["event"] == "login"
            assert response["success"] is False
