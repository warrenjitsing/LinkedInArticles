from fastapi.testclient import TestClient
from main import app, DATA_FILE


def test_read_root():
    """Tests the root endpoint."""
    with TestClient(app) as client:
        response = client.get("/")
        assert response.status_code == 200
        assert response.json() == {"message": "Server is running"}


def test_read_status():
    """Tests the /status endpoint."""
    with TestClient(app) as client:
        response = client.get("/status")
        assert response.status_code == 200
        assert response.json() == {"status": "ok"}


def test_read_file():
    """Tests the static file serving endpoint."""
    with TestClient(app) as client:
        response = client.get("/static/data.txt")
        assert response.status_code == 200
        # The file content is read from the original source for the assertion
        assert response.text == DATA_FILE.read_text()


def test_get_item_found():
    """Tests the successful case for finding an item."""
    with TestClient(app) as client:
        response = client.get("/items/1")
        assert response.status_code == 200
        assert response.json() == {"item_id": 1, "name": "The One Item"}


def test_get_item_not_found():
    """Tests the error case for not finding an item."""
    with TestClient(app) as client:
        response = client.get("/items/999")
        assert response.status_code == 404
        assert response.json() == {"detail": "Item with ID 999 not found."}


def test_create_item_success():
    """Tests the successful creation of an item via POST."""
    with TestClient(app) as client:
        item_payload = {"name": "Test Item", "is_offer": True}
        response = client.post("/items", json=item_payload)

        assert response.status_code == 201  # 201 Created
        response_data = response.json()
        assert response_data["message"] == "Item created successfully"
        assert response_data["item_data"]["name"] == item_payload["name"]
        assert response_data["item_data"]["is_offer"] == item_payload["is_offer"]


def test_create_item_validation_error_missing_field():
    """Tests for a validation error when a required field is missing."""
    with TestClient(app) as client:
        # The 'name' field is required by the Pydantic model, so this is invalid.
        item_payload = {"is_offer": False}
        response = client.post("/items", json=item_payload)

        assert response.status_code == 422  # 422 Unprocessable Entity


def test_create_item_validation_error_wrong_type():
    """Tests for a validation error when a field has the wrong data type."""
    with TestClient(app) as client:
        # The 'name' field should be a string, not an integer.
        item_payload = {"name": 123, "is_offer": False}
        response = client.post("/items", json=item_payload)

        assert response.status_code == 422  # 422 Unprocessable Entity
