# Interactive Version & Source Code

For the optimal reading experience and access to the complete, version-controlled source code, the definitive version of this article is hosted on GitHub. I highly recommend following along there:

[A First-Principles Guide to HTTP and WebSockets in FastAPI](https://github.com/warrenjitsing/LinkedInArticles/tree/main/articles/0001_basic_fastapi)

This repository also includes an AI assistant bootloader in the `ai.md` file. By providing this bootloader and the article's contents to a capable AI assistant, you can transform this guide into a dynamic, interactive learning tool and project builder. Due to the large context required, a pro-tier subscription to your chosen AI service (such as ChatGPT Plus or Gemini Advanced) is highly recommended for the best results.

# Introduction

In the landscape of Python web frameworks, FastAPI distinguishes itself through its high performance and modern, type-driven design. It is an indispensable tool not only for building production-grade APIs but also for the critical task of verifying the conformance of low-level, handwritten network protocols.

This guide provides a first-principles walkthrough for setting up a combined HTTP and WebSocket server, covering the complete lifecycle from environment setup to automated testing.

# Foundations

## HTTP

The Hypertext Transfer Protocol (HTTP) is the bedrock of data communication on the web, powering everything from browser requests to complex REST APIs. This guide will focus specifically on HTTP/1.1, as its human-readable, text-based format is ideal for understanding the protocol's fundamental mechanics.

HTTP/1.1 operates on a strict request-response cycle. A client always initiates communication by sending a request to a server, and the server's sole job is to return a single response. The server cannot spontaneously send data; it must wait for a client's request. Our examples will concentrate on the two most common request types: GET to retrieve resources and POST to submit data.

A critical feature of HTTP/1.1 is that it is a text-based protocol, unlike the complex binary framing used in HTTP/2. Every message is constructed as a simple string of characters, which is then encoded as a raw sequence of bytes for transmission. At a low level, you can think of this data as a `bytes` object in Python, an `unsigned char*` buffer in C, a `std::vector<std::byte>` in C++, or a `Vec<u8>` in Rust.

### GET request

The HTTP GET method is designed for a single purpose: to retrieve a representation of a specified resource. As a read-only operation, it is considered "safe," meaning it should not alter the state of the resource.

A key characteristic of a GET request is that it contains no message body. While parameters can be sent to the server through the URL's query string (e.g., `GET /search?topic=api&page=2 HTTP/1.1`), the request itself carries no payload.

Like all HTTP/1.1 messages, each line is terminated by a carriage return and newline (`\r\n`). A final blank line (`\r\n` on its own) signals the end of the headers.

```text
GET / HTTP/1.1\r\n
Host: www.example.com\r\n
Connection: close\r\n
User-Agent: SimpleClient/1.0\r\n
\r\n
```

### POST request

The HTTP POST method is used to submit an entity to the specified resource, often causing a change in state or the creation of a new resource on the server. It is the primary method for sending user-generated data to a web server.

Unlike GET, a POST request is defined by its inclusion of a payload in the message body. To ensure the server can correctly process this payload, two headers are critical:

- Content-Type: Specifies the media type of the body (e.g., application/json, multipart/form-data).
- Content-Length: Indicates the exact size of the body in bytes.

Because POST is designed to create or update resources, it is neither "safe" nor "idempotent"—meaning that sending the same request multiple times may have additional side effects, such as creating duplicate entries.

```text
POST /api/users HTTP/1.1\r\n
Host: api.example.com\r\n
Content-Type: application/json\r\n
Content-Length: 25\r\n
Connection: close\r\n
\r\n
{"name": "monday","id": 7}
```

### Headers

HTTP headers are the key-value pairs that carry metadata and control information for both requests and responses. Think of them as the instructions on the outside of a package; they describe the contents, specify the destination, and provide handling directives without altering the package's contents (the message body).

Each header consists of a case-insensitive name, a colon (:), and a value. While there are hundreds of possible headers, they generally fall into a few key roles:

- Describing the Request: Headers like Host specify the server a request is for, while User-Agent identifies the client making the request.
- Describing the Body: For messages with a payload (like POST), Content-Type defines the data format and Content-Length specifies its size.
- Controlling Behavior: Headers can act as directives. For example, Connection: close tells the server to close the socket after the response, and Cache-Control dictates how the response should be cached by browsers and proxies.

Together, this extensible system of headers governs everything from authentication and content negotiation to redirection and cookie management, forming the operational backbone of every HTTP message.

### A Note on HTTPS and Security

HTTPS (Hypertext Transfer Protocol Secure) is not a separate protocol from HTTP. It is simply standard HTTP communication layered on top of a secure TLS socket. Think of it as sending the exact same plaintext HTTP messages you've already seen, but through a private, encrypted tunnel.

A TLS (Transport Layer Security) socket is an enhanced network socket that uses a cryptographic protocol to secure the connection. Before any HTTP data is exchanged, the client and server perform a "TLS handshake." During this handshake, they:

1. Negotiate which encryption ciphers to use.
2. Verify the server's identity using its TLS certificate.
3. Securely generate and exchange session keys for encrypting the data.

The importance of using HTTPS is that it provides three critical security guarantees:

- Confidentiality: All traffic is encrypted, preventing eavesdroppers from reading sensitive information like passwords or credit card numbers.
- Authentication: The server's certificate proves that you are connected to the legitimate website and not an imposter.
- Integrity: TLS ensures that the data sent between the client and server has not been tampered with or altered in transit.

## WebSockets

Where HTTP's request-response model is inefficient for real-time applications, the **WebSocket protocol** provides a persistent, **full-duplex communication channel** over a single TCP connection. This allows both the client and server to send data to each other independently and at any time, making it the ideal standard for applications like live chats, financial data streams, and multiplayer online games.

### The Handshake: Upgrading the Connection

A WebSocket connection does not start on its own. It begins life as a standard HTTP/1.1 GET request, which includes special headers asking the server to "upgrade" the connection from HTTP to the WebSocket protocol.

The client sends an `Upgrade` request with a unique key:
```text
GET /chat HTTP/1.1
Host: server.example.com
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
Sec-WebSocket-Version: 13
```
If the server supports WebSockets and agrees to the upgrade, it responds with a 101 Switching Protocols status. It also computes an acceptance key from the client's key to prove it understood the request.

```text
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
```

### The Data Framing Protocol

Once the handshake is successful, the underlying TCP socket remains open, but the communication is no longer HTTP. Instead, data is exchanged as a sequence of **frames**. Each frame is a small piece of data with a header that describes its content.

The frame header specifies:

- **The Frame Type**: Whether the payload is text, binary data, a ping/pong control message, or a request to close the connection.
- **The Payload Length**: The size of the data in the frame.
- **Masking**: Whether the data is masked (a security feature for client-to-server frames).

This framing mechanism is highly efficient because it removes the overhead of HTTP headers from every message. Instead of a verbose, text-based request-response cycle, you have a lean, continuous stream of data flowing in both directions.

- **FIN (1 bit)**: The "final" bit. It's set to `1` for the final frame of a message, or `0` for intermediate frames of a fragmented message.
- **RSV1, RSV2, RSV3 (1 bit each)**: Three reserved bits. They must be `0` unless an extension is negotiated to define a meaning for them.
- **Opcode (4 bits)**: Defines the interpretation of the payload data. Common opcodes include:
  - `0x1`: Text Frame (the payload is UTF-8 text)
  - `0x2`: Binary Frame (the payload is raw binary data)
  - `0x8`: Connection Close Frame
  - `0x9`: Ping Frame
  - `0xA`: Pong Frame
- **MASK (1 bit)**: Defines whether the payload is "masked" (XORed with a key). All frames sent from a client to a server **must** be masked.
- **Payload Length (7, 7+16, or 7+64 bits)**: A variable-length field describing the payload's size in bytes:
  - **If the value is 0-125**: This is the literal length of the payload.
  - **If the value is 126**: The next 2 bytes (16 bits) are read as an unsigned integer to get the actual payload length.
  - **If the value is 127**: The next 8 bytes (64 bits) are read as an unsigned integer to get the actual payload length.
- **Masking Key (4 bytes / 32 bits)**: *(Optional)* If the `MASK` bit is set to `1`, this field is present and contains the key used to unmask the payload.
- **Payload Data (N bytes)**: The actual application data. If the frame was masked, the recipient must XOR this data with the masking key to retrieve the original content.

### Securing WebSockets with WSS

Much like HTTPS, WebSocket Secure (wss://) is not a fundamentally different protocol. It is simply the standard WebSocket protocol running over a secure TLS connection.

The relationship between ws:// and wss:// is directly analogous to that of http:// and https://. The initial HTTP Upgrade handshake for a wss:// connection must be sent over an established HTTPS connection. Once the handshake is complete, all subsequent WebSocket frames are transmitted through that same secure TLS tunnel.

This provides the same three critical security benefits for your real-time communication. Any production application that sends sensitive information over a WebSocket must use the wss:// scheme to ensure data security.

# Environment Setup

Before writing any code, we need to establish a clean, isolated development environment and install the necessary dependencies.

## Dependencies

First, create a `requirements.txt` file in your project directory with the following contents. These packages provide the core framework, application server, and testing utilities.

```text
fastapi
uvicorn
websockets
httpx
pytest
pytest-cov
```

- **fastapi**: The high-performance web framework we are using to build the API.
- **uvicorn**: A lightning-fast ASGI (Asynchronous Server Gateway Interface) server, required to run our FastAPI application.
- **websockets**: A high-performance WebSocket library that Uvicorn can use for its WebSocket implementation.
- **httpx**: A modern, async-capable HTTP client library. It is a required dependency for FastAPI's integrated `TestClient`.
- **pytest**: The powerful and popular framework we will use to write our automated tests.
- **pytest-cov**: Allows us to calculate the coverage of our application code by our test suite. It is a valuable metric for identifying parts of your codebase that are not exercised by your tests.

## Installation

With the `requirements.txt` file in place, create and activate a Python virtual environment, then install the packages using `pip`.

```shell
# Create a virtual environment named .venv
python3 -m venv .venv

# Activate the virtual environment (syntax for Linux/macOS)
source .venv/bin/activate

# Confirm the venv's python is now on your PATH
which python3
# Expected output: /path/to/your/project/.venv/bin/python3

# Install all packages from the requirements file
python3 -m pip install -r requirements.txt
```

Why do I put `python3 -m` in front of my `pip` command? Because I like to be as `explicit` as a single-argument C++ constructor should be.

## Securing the Local Server with TLS

To enable HTTPS and WSS, our server needs a TLS certificate to handle encrypted traffic. While production servers require certificates issued by a trusted Certificate Authority (CA), we can generate a **self-signed certificate** for local development. This allows us to test our secure `https://` and `wss://` endpoints without purchasing a formal certificate, and crucially, without modern browser warnings.

Run the following `openssl` command in your terminal. This command generates a modern certificate using Elliptic Curve Cryptography and includes a Subject Alternative Name (SAN) for broad browser compatibility. It will create a private key (`key.pem`) and a public certificate (`cert.pem`), valid for one year.

```shell
openssl req -x509 \
  -nodes \
  -newkey ed25519 \
  -keyout key.pem \
  -out cert.pem \
  -sha256 \
  -days 365 \
  -subj '/CN=localhost' \
  -addext "subjectAltName = DNS:localhost,IP:127.0.0.1"
```

Here is a brief breakdown of what these flags do:

- **`req -x509`**: Creates a self-signed certificate, suitable for development.
- **`-nodes`**: "No DES," meaning it creates a private key that is not encrypted with a passphrase.
- **`-newkey ed25519`**: Generates a new, highly efficient **ED25519 Elliptic Curve private key**. This is a modern alternative to RSA with equivalent security at smaller key sizes.
- **`-keyout key.pem`**: Specifies the output file for the private key.
- **`-out cert.pem`**: Specifies the output file for the certificate.
- **`-subj '/CN=localhost'`**: Sets the certificate's subject information non-interactively, with the "Common Name" set to `localhost`.
- **`-addext "subjectAltName = DNS:localhost,IP:127.0.0.1"`**: **Crucially**, this adds a Subject Alternative Name (SAN) extension. Modern browsers rely on SANs rather than the Common Name for hostname validation, ensuring your certificate is trusted for `localhost` and `127.0.0.1` without warnings.

# Implementation

## The HTTP Layer

### The Basic Application Structure

We'll begin by creating the core of our application in a file named `main.py`. This initial version will focus on two things: instantiating the FastAPI application and setting up a "lifespan" event handler to manage resources needed by our server.

### Managing Resources with `lifespan`

A **lifespan handler** is a function that FastAPI runs on startup and shutdown. It's the ideal place to manage resources that the application needs to operate, such as database connections, machine learning models, or, in our case, temporary files for demonstration purposes.

We'll use this handler to create a static file when the server starts and cleanly remove it when the server stops.

```python
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI

# Define the path for our static content
STATIC_DIR = Path("static")
DATA_FILE = STATIC_DIR / "data.txt"


@asynccontextmanager
async def lifespan(app: FastAPI):
    """
    This function runs on application startup and shutdown.
    It creates a dummy file for our FileResponse example.
    """
    print("Server is starting up...")
    # Create the static directory and a test file
    STATIC_DIR.mkdir(exist_ok=True)
    DATA_FILE.write_text("This is a test file served by FastAPI.")

    yield  # The application runs while the lifespan is in this state

    print("Server is shutting down...")
    # Clean up the file and directory
    DATA_FILE.unlink()
    STATIC_DIR.rmdir()


app = FastAPI(lifespan=lifespan)


@app.get("/")
async def read_root():
    """A simple root endpoint to confirm the server is running."""
    return {"message": "Server is running"}

```

This code sets up an `asynccontextmanager` named `lifespan`. Everything **before the `yield` statement** is executed on startup. The server then runs and processes requests. Once the server is shut down (e.g., with `Ctrl+C`), the code **after the `yield`** is executed, ensuring our temporary file and directory are cleanly removed. We connect this handler to our application by passing it to the `FastAPI` constructor.


### Core Concept: HTTP Status Codes

Before we implement our endpoints, it's crucial to understand **HTTP status codes**. Every response a server sends includes a three-digit code that tells the client the outcome of its request. Using the correct code is a fundamental part of building a well-behaved and predictable API.

These codes are grouped into five classes, which are easy to remember by their first digit.

***

#### **`2xx`: Success**

A `2xx` code means the request was successfully received, understood, and accepted.
* **`200 OK`**: The standard response for a successful GET request.
* **`201 Created`**: Indicates that a new resource was successfully created as a result of a POST request.

***

#### **`3xx`: Redirection**

A `3xx` code indicates that the client must take additional action to complete the request, usually by making a new request to a different URL.
* **`307 Temporary Redirect`**: The requested resource has temporarily moved to a new URL.

***

#### **`4xx`: Client Error**

A `4xx` code means there was an error, and it appears to be the client's fault.
* **`400 Bad Request`**: The server could not understand the request due to invalid syntax.
* **`404 Not Found`**: The server could not find the requested resource.
* **`422 Unprocessable Entity`**: The request was well-formed, but contained semantic errors (e.g., a required field was missing in a JSON payload). FastAPI uses this frequently for validation errors.

***

#### **`5xx`: Server Error**

A `5xx` code indicates that the server failed to fulfill a valid request due to an error on its end.
* **`500 Internal Server Error`**: A generic error message given when an unexpected condition was encountered and no more specific message is suitable.

Returning the correct status code is essential. It allows browsers, automated clients, and caching proxies to behave correctly. A common anti-pattern is to return a `200 OK` status with an error message in the body; the correct approach is to use a `4xx` or `5xx` code to signal the error at the protocol level.


### Implementing the HTTP Endpoints

With our basic application structure and `lifespan` handler in place, we can now add the core logic for our HTTP server. We will implement three different endpoints to demonstrate FastAPI's primary capabilities: returning JSON data, serving static files, and handling application errors.

Add the following imports to the top of your `main.py` file:

```python
from fastapi.responses import FileResponse
from fastapi import HTTPException
```

-----

#### Endpoint 1: Returning a JSON Response

The most common use case for a web API is returning structured data. FastAPI makes this incredibly simple. If you return a Python dictionary from an endpoint function, FastAPI will automatically serialize it into a JSON response and set the `Content-Type` header to `application/json`.

Add the following endpoint to `main.py`:

```python
@app.get("/status")
async def get_status():
    """Returns the current status of the server."""
    return {"status": "ok"}
```

-----

#### Endpoint 2: Serving a Static File

To serve a static file, like the one we created in our `lifespan` handler, we use FastAPI's `FileResponse`. This response type is highly efficient as it **streams the file from disk** rather than loading it all into memory first. It also automatically determines the file's `Content-Type` from its extension and sets the `Content-Length` header.

Add this endpoint to `main.py`:

```python
@app.get("/static/data.txt")
async def get_data_file():
    """Serves the static data.txt file."""
    return FileResponse(DATA_FILE)
```

-----

#### Endpoint 3: Handling Errors

Proper error handling is critical for a robust API. When a client requests a resource that doesn't exist, the server should return a `404 Not Found` status code. FastAPI manages this through the `HTTPException` class.

When you **raise an `HTTPException`**, FastAPI stops processing the request and immediately sends an HTTP response with the specified status code and a JSON error body.

Add this endpoint to `main.py` to simulate looking for an item that may not exist:

```python


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
```

#### Endpoint 4: Handling a POST Request

To handle a `POST` request and receive a JSON body, you define an endpoint using the `@app.post()` decorator. By type-hinting the function argument with the Pydantic model we just created (`CreateItemRequest`), you tell FastAPI to expect a body with that structure.

FastAPI will then automatically:

1.  Read the body of the request as JSON.
2.  Validate that it matches the `CreateItemRequest` model.
3.  If validation fails, it returns a `422 Unprocessable Entity` error.
4.  If it succeeds, it passes the parsed data as the `item` argument.

We also specify `status_code=201` in the decorator to return a `201 Created` status, which is the correct semantic response for a successful resource creation.

```python
from pydantic import BaseModel


class CreateItemRequest(BaseModel):
    name: str
    is_offer: bool | None = None

    
@app.post("/items", status_code=201)
async def create_item(item: CreateItemRequest):
    """Creates a new item from a request body."""
    return {"message": "Item created successfully", "item_data": item.model_dump()}
```


### The Complete HTTP Server Code (Revised)

This final version of our `main.py` file includes an entry point to run the server directly using `uvicorn.run()`. This makes the application self-contained.

```python
from contextlib import asynccontextmanager
from pathlib import Path

import uvicorn
from fastapi import FastAPI, HTTPException
from fastapi.responses import FileResponse
from pydantic import BaseModel


# Define the path for our static content
STATIC_DIR = Path("static")
DATA_FILE = STATIC_DIR / "data.txt"


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


if __name__ == "__main__":
    uvicorn.run(
        "main:app",
        host="0.0.0.0",
        port=8000,
        reload=True,
        ssl_keyfile="key.pem",
        ssl_certfile="cert.pem",
    )

```

-----

### Running the Server

Because we have included the `if __name__ == "__main__"` block, which calls `uvicorn.run()` for us, we can now start the server directly using the Python interpreter.

Make sure you are in the same directory as your `main.py`, `key.pem`, and `cert.pem` files, and run the following command:

```shell
python3 main.py
```

Your secure HTTP server will start on port 8000 and is now ready to accept requests.

### Client-Side Testing Strategy

A critical part of developing a robust API is having an automated test suite. FastAPI provides a `TestClient` that makes it easy to test your application using frameworks like `pytest`. The `TestClient` allows you to send requests to your application in memory without needing to run a live server, resulting in fast and reliable tests.

Create a new file named `test_http.py` in your project directory.

```python
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
```

In this script, we import our `app` object from `main.py` and pass it to the `TestClient`. Each function follows the `pytest` convention of being named `test_*`. Inside each function, we use the `client` to make requests to our endpoints and then use `assert` statements to verify that the status code and response body are exactly what we expect.

You will notice in our test suite that the `TestClient` is instantiated using a `with` statement inside each test function, rather than being created once at the top of the file. This is a deliberate and critical pattern.

Using `with TestClient(app) as client:` creates a context manager that properly simulates the application's full **lifespan**. When the `with` block is entered, FastAPI runs the startup portion of our `lifespan` handler (creating the `data.txt` file). When the block is exited, it runs the shutdown portion (cleaning up the file).

This ensures that each test runs within a clean, predictable application state, which is essential for reliable and isolated testing, especially when dealing with resources like files or database connections.

-----

### Running the Tests

To execute the test suite, run `pytest` from your terminal. The `-v` flag provides more verbose output.

```shell
python3 -m pytest -v
```

Pytest will automatically discover and run the functions in `test_http.py`, reporting the success or failure of each assertion.


### Manual and Ad-Hoc Testing

While an automated test suite is essential for validation, it's also useful to interact with your server manually. This is a great way to perform quick, ad-hoc tests or inspect the raw responses from your endpoints while developing.

-----

#### Testing with `curl`

The `curl` command is a powerful, ubiquitous tool for making HTTP requests from the command line. While your `uvicorn` server is running, you can open a new terminal to send `GET`, `POST`, or any other type of request to your endpoints.

Because we are using a **self-signed certificate**, you must include the `-k` (or `--insecure`) flag. This tells `curl` to proceed with the request even though it cannot verify the certificate against a trusted Certificate Authority.

```shell
# Test the status endpoint
$ curl -k https://localhost:8000/status
{"status":"ok"}

# Test the static file endpoint
$ curl -k https://localhost:8000/static/data.txt
This is a test file served by FastAPI.

# Test the item-not-found error case
$ curl -k https://localhost:8000/items/999
{"detail":"Item with ID 999 not found."}

# Test creating a new item via POST
$ curl -k -X POST https://localhost:8000/items \
  -H "Content-Type: application/json" \
  -d '{"name": "My New Item", "is_offer": false}'
```

#### Testing with a Web Browser

You can also test your `GET` endpoints directly in a web browser. When you navigate to the URLs, your browser will display a security warning page because the certificate is self-signed and not trusted by default. You will need to click "Advanced" and then "Proceed to localhost (unsafe)" to view the page.

You can test the following URLs:

  * `https://localhost:8000/status`
  * `https://localhost:8000/static/data.txt`
  * `https://localhost:8000/items/1`


The primary script for generating certificates in the repository, `gen_certs.sh`, uses a modern ED25519 (ECC) key for efficiency. However, some browser and operating system combinations can be overly strict when validating self-signed ECC certificates, which may lead to TLS handshake errors like `PR_END_OF_FILE_ERROR`.

For maximum compatibility, the repository also includes `gen_certs_legacy.sh`. This script generates a traditional RSA certificate, which is universally supported and provides a reliable fallback if you encounter browser connection issues with the primary ECC certificate.


## The WebSocket Layer


### Basic WebSocket Usage

While the HTTP layer is ideal for traditional request-response interactions, it is inefficient for real-time applications that require the server to push data to the client. For this, we turn to WebSockets, which provide a persistent, **bidirectional communication channel** over a single TCP connection.

FastAPI provides first-class support for WebSockets. Unlike an HTTP endpoint that processes a single request and returns a response, a WebSocket endpoint is a long-running asynchronous function that manages the entire lifecycle of a connection.

You define a WebSocket endpoint using the `@app.websocket()` decorator. Instead of returning a value, the endpoint function receives a `WebSocket` object as an argument. This object is the primary interface for interacting with the client: accepting the connection, sending messages, and receiving messages until the connection is closed.


-----

First, add `WebSocket` and `WebSocketDisconnect` to your FastAPI imports at the top of `main.py`:

```python
from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
```

#### Endpoint 1: A Simple Echo Server

The best way to understand the WebSocket connection lifecycle is to build a simple **echo server**. This endpoint will accept a connection, wait for a message from the client, and immediately send that exact same message back.

Add the following code to your `main.py` file:

```python
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
```

##### Deconstructing the Echo Server

This function demonstrates the fundamental pattern for managing a WebSocket connection in FastAPI:

1.  **`await websocket.accept()`**: This is the first and most crucial step. It performs the WebSocket handshake with the client. You must call `accept()` before any `send` or `receive` operations can occur.
2.  **`try...except WebSocketDisconnect`**: This block ensures we handle the client closing the connection gracefully. When the client disconnects, `receive_text()` will raise a `WebSocketDisconnect` exception, allowing us to catch it and cleanly exit the function.
3.  **`while True:`**: This loop keeps the connection alive after the initial handshake, allowing the server to continuously listen for new messages from the same client.
4.  **`await websocket.receive_text()`**: This line pauses execution and waits for a message to arrive from the client. Once a message is received, it's stored in the `message` variable.
5.  **`await websocket.send_text(...)`**: After receiving a message, this line sends a new message back to the client over the same persistent connection.


#### Endpoint 2: A Server-Side Data Streamer

Add the following imports to the top of `main.py`:

```python
import asyncio
from datetime import datetime, UTC
```

The true power of WebSockets is the server's ability to **proactively push data to the client** without waiting for a request. This next endpoint demonstrates this by streaming the server's timestamp to the client every second.

Add the following code to your `main.py` file:

```python
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
```

##### Deconstructing the Data Streamer

This endpoint builds on the pattern of the echo server but with a key difference: the `while True` loop is not blocked waiting for `receive_text()`.

1.  **Server-Initiated Communication**: After accepting the connection, the server immediately enters a loop where it is the one initiating communication. It constructs a JSON payload and sends it to the client using `await websocket.send_json()`.
2.  **Controlled Pacing**: The `await asyncio.sleep(1)` line is crucial. It pauses the loop for one second, creating a steady, paced stream of data. Without this, the server would flood the client with messages as fast as possible.
3.  **Graceful Disconnects**: The `try...except WebSocketDisconnect` block is especially important here. If the client disconnects, the next call to `websocket.send_json()` will raise this exception, allowing the server to gracefully stop the streaming task for that connection.

-----

#### Endpoint 3: A Simulated Authentication Flow

First, add the necessary imports for cryptographic operations to the top of `main.py`:

```python
import hmac
import base64
import hashlib
```


This final WebSocket endpoint demonstrates a more realistic, stateful interaction: a private endpoint that requires clients to authenticate before proceeding. We will simulate the server-side validation of a signature-based login, a common pattern used by cryptocurrency exchanges like OKX.

The server will expect a login message containing an API key, a timestamp, and a signature. It will then re-compute the signature on its end using a secret key and compare the two to verify the client's identity.

Add the following code to your `main.py` file:

```python
# --- DUMMY CREDENTIALS FOR DEMONSTRATION ---
# In a real application, these would be stored securely in a database.
DUMMY_API_KEY = "abc"
DUMMY_API_SECRET = "def"


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
```

##### Deconstructing the Authentication Logic

1.  **Dummy Credentials**: We define a `DUMMY_API_KEY` and `DUMMY_API_SECRET` at the top of the file. In a real system, the server would look up the secret key based on the API key provided by the client.
2.  **Signature Verification**: The `_verify_signature` helper function encapsulates the core logic. It re-creates the exact same `message` string the client used, generates the HMAC-SHA256 signature with the stored secret key, and base64-encodes it.
3.  **Secure Comparison**: It uses `hmac.compare_digest()` to check if the client's signature matches the one generated by the server. This function is essential for preventing timing attacks.
4.  **Handshake Flow**: The main endpoint function waits for a single JSON message. It calls the verification function, sends back a success or failure response, and then closes the connection for this example. In a real application, a successful login would typically lead into another `while True` loop to handle subsequent private messages.

### The Complete WebSocket Code

To finalize our server, we'll add the WebSocket logic to the existing `main.py` file. This involves adding several new imports for WebSockets and cryptography, along with the code for our three distinct WebSocket endpoints.

-----

##### New Imports

Add the following imports to the top of your `main.py` file alongside the existing ones.

```python
import asyncio
from datetime import datetime
import hmac
import base64
from fastapi import WebSocket, WebSocketDisconnect
```

-----

#### WebSocket Endpoints and Logic

Add this entire block of code to the end of your `main.py` file, before the `if __name__ == "__main__"` block.

```python
# --- DUMMY CREDENTIALS FOR DEMONSTRATION ---
DUMMY_API_KEY = "abc"
DUMMY_API_SECRET = "def"


def _verify_signature(payload: dict) -> bool:
    """Verifies the signature from a client login message."""
    try:
        args = payload["args"][0]
        api_key = args["apiKey"]
        timestamp = args["timestamp"]
        client_sign = args["sign"]
    except (KeyError, IndexError):
        return False

    if api_key != DUMMY_API_KEY:
        return False

    message = str(timestamp) + 'GET' + '/users/self/verify'
    mac = hmac.new(
        bytes(DUMMY_API_SECRET, encoding='utf8'),
        bytes(message, encoding='utf-8'),
        digestmod='sha256'
    )
    server_sign = base64.b64encode(mac.digest()).decode()

    return hmac.compare_digest(server_sign, client_sign)


@app.websocket("/ws/echo")
async def websocket_echo(websocket: WebSocket):
    """A simple WebSocket endpoint that echoes back any message it receives."""
    await websocket.accept()
    try:
        while True:
            message = await websocket.receive_text()
            await websocket.send_text(f"Echo: {message}")
    except WebSocketDisconnect:
        print("Client disconnected from echo endpoint.")


@app.websocket("/ws/stream")
async def websocket_stream(websocket: WebSocket):
    """Streams the current server time to the client every second."""
    await websocket.accept()
    try:
        while True:
            payload = {
                "timestamp": datetime.now(UTC).isoformat(),
                "message": "This is a periodic update from the server."
            }
            await websocket.send_json(payload)
            await asyncio.sleep(1)
    except WebSocketDisconnect:
        print("Client disconnected from stream endpoint.")


@app.websocket("/ws/v5/private")
async def websocket_private(websocket: WebSocket):
    """A simulated private endpoint requiring signature authentication."""
    await websocket.accept()
    try:
        login_payload = await websocket.receive_json()

        if login_payload.get("op") == "login" and _verify_signature(login_payload):
            await websocket.send_json({"event": "login", "success": True})
            # In a real app, a loop would follow for private data exchange.
            await websocket.close()
        else:
            await websocket.send_json({"event": "login", "success": False, "message": "Authentication failed"})
            await websocket.close()

    except WebSocketDisconnect:
        print("Client disconnected from private endpoint.")
    except Exception as e:
        print(f"An error occurred in the private endpoint: {e}")

```

### Testing the WebSocket Endpoints

Testing WebSockets requires a different approach than testing HTTP endpoints. We need a client that can establish a persistent connection and exchange messages. We will cover two methods: an automated suite using `pytest` and FastAPI's `TestClient`, and a manual script for ad-hoc interactive testing.

-----

#### Automated Testing with Pytest

FastAPI's `TestClient` provides a `websocket_connect()` context manager that allows us to test our WebSocket endpoints just as easily as our HTTP endpoints.

Create a new file named `test_websockets.py`.

```python
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

```

To run these tests, use the same `pytest` command as before:

```shell
pytest -v
```

-----

#### Manual Testing with a Client Script

For interactive testing, we can create a small client using the `websockets` library. This allows us to connect to our running server and see the message exchange in real-time.

Create a new file named `manual_ws_client.py`.

```python
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
```

To use this script, make sure your FastAPI server is running. Then, from a new terminal, run one of the following commands:

```shell
# Test the echo server interactively
python3 manual_ws_client.py echo

# Connect to the data stream
python3 manual_ws_client.py stream

# Test the private endpoint authentication
python3 manual_ws_client.py private
```


### Measuring Test Coverage

While our test suite validates our endpoints, **test coverage** is a metric that tells us which lines of our application code were actually executed during the test run. It's a valuable tool for identifying parts of your codebase that are not tested at all.

First, you'll need to install the `pytest-cov` plugin (included in requirements.txt:

```shell
python3 -m pip install pytest-cov
```

Now, you can run `pytest` with the `--cov` flag to generate a coverage report. We'll target our `main` module.

```shell
python3 -m pytest --cov=main -v
```

After the tests complete, you will see a report similar to this at the bottom of the output, showing that our tests exercised 100% of our application code.

```text
================================================== test session starts ==================================================
platform linux -- Python 3.11.2, pytest-8.4.1, pluggy-1.6.0 -- xxx
cachedir: .pytest_cache
rootdir: xxx
plugins: cov-6.2.1, anyio-4.10.0
collected 12 items                                                                                                      

test_http.py::test_read_root PASSED                                                                               [  8%]
test_http.py::test_read_status PASSED                                                                             [ 16%]
test_http.py::test_read_file PASSED                                                                               [ 25%]
test_http.py::test_get_item_found PASSED                                                                          [ 33%]
test_http.py::test_get_item_not_found PASSED                                                                      [ 41%]
test_http.py::test_create_item_success PASSED                                                                     [ 50%]
test_http.py::test_create_item_validation_error_missing_field PASSED                                              [ 58%]
test_http.py::test_create_item_validation_error_wrong_type PASSED                                                 [ 66%]
test_websockets.py::test_websocket_echo PASSED                                                                    [ 75%]
test_websockets.py::test_websocket_stream PASSED                                                                  [ 83%]
test_websockets.py::test_websocket_private_auth_success PASSED                                                    [ 91%]
test_websockets.py::test_websocket_private_auth_failure PASSED                                                    [100%]

==================================================== tests coverage =====================================================
____________________________________ coverage: platform linux, python 3.11.2-final-0 ____________________________________

Name      Stmts   Miss  Cover
-----------------------------
main.py      93      9    90%
-----------------------------
TOTAL        93      9    90%
================================================== 12 passed in 2.30s ===================================================
```

-----

# Conclusion

In this guide, we have built a complete, dual-protocol server from the ground up. You have moved from the foundational theory of HTTP and WebSockets to implementing a secure, tested, and robust application using FastAPI. This server is not just a simple example; it is a solid foundation that correctly handles application state, serves different of content, manages errors gracefully, and provides real-time communication capabilities.

This guide presents one approach to building and testing a dual-protocol server, but there are many valid techniques and architectures. I'm keen to hear about your own experiences and preferred stacks. What challenges have you encountered when working with WebSockets in a production environment, and what tools have you found indispensable for testing and validation? Share your thoughts and questions in the comments below.
