import pytest
import socket
import threading
import time
import os
import random
import functools
import operator
import socketserver


from dataclasses import dataclass
from typing import Callable, Generator, Type
from contextlib import contextmanager
from queue import Queue


from httppy.httppy import HttpClient
from httppy.http1_protocol import Http1Protocol
from httppy.tcp_transport import TcpTransport
from httppy.unix_transport import UnixTransport
from httppy.transport import Transport
from httppy.errors import InvalidRequestError
from httppy.http_protocol import HttpRequest, HttpMethod, SafeHttpResponse, UnsafeHttpResponse


@dataclass
class ServerDetails:
    host: str = ""
    port: int = 0
    path: str = ""


@pytest.fixture
def server_factory() -> Callable[[Type[Transport], Callable[[socket.socket], None]], Generator[ServerDetails, None, None]]:
    @contextmanager
    def _factory(transport_class: Type[Transport], handler: Callable[[socket.socket], None]):
        server_thread = None
        listener_sock = None
        details = ServerDetails()
        stop_event = threading.Event()

        def server_loop(listener: socket.socket):
            try:
                client_sock, _ = listener.accept()
                with client_sock:
                    handler(client_sock)
            except (socket.timeout, OSError):
                pass

        if transport_class is TcpTransport:
            listener_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            listener_sock.bind(("127.0.0.1", 0))
            details.host, details.port = listener_sock.getsockname()
        elif transport_class is UnixTransport:
            details.path = f"/tmp/httppy_client_test_{os.getpid()}_{time.time_ns()}.sock"
            if os.path.exists(details.path):
                os.remove(details.path)
            listener_sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            listener_sock.bind(details.path)
        else:
            pytest.fail(f"Unknown transport type for server_factory: {transport_class}")

        try:
            listener_sock.settimeout(1.0)
            listener_sock.listen()
            server_thread = threading.Thread(target=server_loop, args=(listener_sock,))
            server_thread.start()
            yield details
        finally:
            stop_event.set()
            if transport_class is TcpTransport:
                with socket.create_connection((details.host, details.port), timeout=0.1):
                    pass
            elif transport_class is UnixTransport:
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
                    try:
                        sock.connect(details.path)
                    except (ConnectionRefusedError, FileNotFoundError):
                        pass

            if server_thread:
                server_thread.join(timeout=1.0)
            if listener_sock:
                listener_sock.close()
            if transport_class is UnixTransport and os.path.exists(details.path):
                os.remove(details.path)

    return _factory


@pytest.mark.parametrize("transport_class", [TcpTransport, UnixTransport])
def test_get_request_safe_succeeds(server_factory, transport_class):
    canned_response = b"HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\nsuccess"
    request_queue = Queue()

    def handler(client_sock: socket.socket):
        data = client_sock.recv(1024)
        request_queue.put(data)
        client_sock.sendall(canned_response)

    with server_factory(transport_class, handler) as details:
        transport = transport_class()
        protocol = Http1Protocol(transport)
        client = HttpClient(protocol)

        if transport_class is TcpTransport:
            client.connect(details.host, details.port)
        else:
            client.connect(details.path, 0)

        request = HttpRequest(path="/test")
        res = client.get_safe(request)

        assert isinstance(res, SafeHttpResponse)
        assert res.status_code == 200
        assert res.body == b"success"

        captured_request = request_queue.get(timeout=1.0)
        assert b"GET /test HTTP/1.1" in captured_request

        client.disconnect()


@pytest.mark.parametrize("transport_class", [TcpTransport, UnixTransport])
def test_get_request_unsafe_succeeds(server_factory, transport_class):
    canned_response = b"HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\nsuccess"
    request_queue = Queue()

    def handler(client_sock: socket.socket):
        data = client_sock.recv(1024)
        request_queue.put(data)
        client_sock.sendall(canned_response)

    with server_factory(transport_class, handler) as details:
        transport = transport_class()
        protocol = Http1Protocol(transport)
        client = HttpClient(protocol)

        if transport_class is TcpTransport:
            client.connect(details.host, details.port)
        else:
            client.connect(details.path, 0)

        request = HttpRequest(path="/test")
        res = client.get_unsafe(request)

        assert isinstance(res, UnsafeHttpResponse)
        assert res.status_code == 200
        assert isinstance(res.body, memoryview)
        assert res.body.tobytes() == b"success"

        captured_request = request_queue.get(timeout=1.0)
        assert b"GET /test HTTP/1.1" in captured_request

        client.disconnect()


@pytest.mark.parametrize("transport_class", [TcpTransport, UnixTransport])
def test_post_request_safe_succeeds(server_factory, transport_class):
    canned_response = b"HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\nsuccess"
    request_queue = Queue()

    def handler(client_sock: socket.socket):
        data = client_sock.recv(1024)
        request_queue.put(data)
        client_sock.sendall(canned_response)

    with server_factory(transport_class, handler) as details:
        transport = transport_class()
        protocol = Http1Protocol(transport)
        client = HttpClient(protocol)

        if transport_class is TcpTransport:
            client.connect(details.host, details.port)
        else:
            client.connect(details.path, 0)

        body_content = b"key=value"
        request = HttpRequest(
            path="/submit",
            body=body_content,
            headers=[("Content-Length", str(len(body_content)))]
        )
        res = client.post_safe(request)

        assert isinstance(res, SafeHttpResponse)
        assert res.status_code == 200
        assert res.body == b"success"

        captured_request = request_queue.get(timeout=1.0)
        assert b"POST /submit HTTP/1.1" in captured_request
        assert captured_request.endswith(body_content)

        client.disconnect()


@pytest.mark.parametrize("transport_class", [TcpTransport, UnixTransport])
def test_post_request_unsafe_succeeds(server_factory, transport_class):
    canned_response = b"HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\nsuccess"
    request_queue = Queue()

    def handler(client_sock: socket.socket):
        data = client_sock.recv(1024)
        request_queue.put(data)
        client_sock.sendall(canned_response)

    with server_factory(transport_class, handler) as details:
        transport = transport_class()
        protocol = Http1Protocol(transport)
        client = HttpClient(protocol)

        if transport_class is TcpTransport:
            client.connect(details.host, details.port)
        else:
            client.connect(details.path, 0)

        body_content = b"key=value"
        request = HttpRequest(
            path="/submit",
            body=body_content,
            headers=[("Content-Length", str(len(body_content)))]
        )
        res = client.post_unsafe(request)

        assert isinstance(res, UnsafeHttpResponse)
        assert res.status_code == 200
        assert isinstance(res.body, memoryview)
        assert res.body.tobytes() == b"success"

        captured_request = request_queue.get(timeout=1.0)
        assert b"POST /submit HTTP/1.1" in captured_request
        assert captured_request.endswith(body_content)

        client.disconnect()


@pytest.mark.parametrize("transport_class", [TcpTransport, UnixTransport])
def test_get_request_with_body_returns_error(transport_class):
    transport = transport_class()
    protocol = Http1Protocol(transport)
    client = HttpClient(protocol)

    request = HttpRequest(
        path="/test",
        body=b"this body is not allowed"
    )

    with pytest.raises(InvalidRequestError, match="GET requests cannot have a body."):
        client.get_safe(request)

    with pytest.raises(InvalidRequestError, match="GET requests cannot have a body."):
        client.get_unsafe(request)


@pytest.mark.parametrize("transport_class", [TcpTransport, UnixTransport])
def test_post_request_without_body_returns_error(transport_class):
    transport = transport_class()
    protocol = Http1Protocol(transport)
    client = HttpClient(protocol)

    request = HttpRequest(
        path="/test",
        body=b"",
        headers=[("Content-Length", "0")]
    )

    with pytest.raises(InvalidRequestError, match="POST requests must have a body."):
        client.post_safe(request)

    with pytest.raises(InvalidRequestError, match="POST requests must have a body."):
        client.post_unsafe(request)


@pytest.mark.parametrize("transport_class", [TcpTransport, UnixTransport])
def test_post_request_without_content_length_returns_error(transport_class):
    transport = transport_class()
    protocol = Http1Protocol(transport)
    client = HttpClient(protocol)

    request = HttpRequest(
        path="/test",
        body=b"some body",
        headers=[]
    )

    with pytest.raises(InvalidRequestError, match="POST requests must include a Content-Length header."):
        client.post_safe(request)

    with pytest.raises(InvalidRequestError, match="POST requests must include a Content-Length header."):
        client.post_unsafe(request)


def xor_checksum(data: bytes) -> int:
    return functools.reduce(operator.xor, data, 0)


@pytest.fixture
def server_factory() -> Callable[[Type[Transport], Callable[[socket.socket], None]], Generator[ServerDetails, None, None]]:
    @contextmanager
    def _factory(transport_class: Type[Transport], handler: Callable[[socket.socket], None]):
        server_thread = None
        listener_sock = None
        details = ServerDetails()
        stop_event = threading.Event()

        def server_loop(listener: socket.socket):
            try:
                client_sock, _ = listener.accept()
                with client_sock:
                    while not stop_event.is_set():
                        if not handler(client_sock):
                            break
            except (socket.timeout, OSError):
                pass

        if transport_class is TcpTransport:
            listener_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            listener_sock.bind(("127.0.0.1", 0))
            details.host, details.port = listener_sock.getsockname()
        elif transport_class is UnixTransport:
            details.path = f"/tmp/httppy_client_test_{os.getpid()}_{time.time_ns()}.sock"
            if os.path.exists(details.path):
                os.remove(details.path)
            listener_sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            listener_sock.bind(details.path)

        try:
            listener_sock.settimeout(2.0)
            listener_sock.listen()
            server_thread = threading.Thread(target=server_loop, args=(listener_sock,))
            server_thread.start()
            yield details
        finally:
            stop_event.set()
            if transport_class is TcpTransport:
                with socket.create_connection((details.host, details.port), timeout=0.1): pass
            elif transport_class is UnixTransport:
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
                    try: sock.connect(details.path)
                    except (ConnectionRefusedError, FileNotFoundError): pass

            if server_thread: server_thread.join(timeout=2.0)
            if listener_sock: listener_sock.close()
            if transport_class is UnixTransport and os.path.exists(details.path):
                os.remove(details.path)

    return _factory


@pytest.mark.parametrize("transport_class", [TcpTransport, UnixTransport])
def test_multi_request_checksum_verification(server_factory, transport_class):
    NUM_CYCLES = 50
    server_rng = random.Random(4321)

    def handler(client_sock: socket.socket) -> bool:
        try:
            headers_raw = bytearray()
            while b'\r\n\r\n' not in headers_raw:
                chunk = client_sock.recv(1024)
                if not chunk: return False
                headers_raw.extend(chunk)

            header_text, body_raw = headers_raw.split(b'\r\n\r\n', 1)
            content_length = 0
            for line in header_text.split(b'\r\n'):
                if line.lower().startswith(b'content-length:'):
                    content_length = int(line.split(b':')[1].strip())
                    break

            while len(body_raw) < content_length:
                chunk = client_sock.recv(content_length - len(body_raw))
                if not chunk: return False
                body_raw.extend(chunk)

            payload = body_raw[:-16]
            checksum_hex = body_raw[-16:].decode('ascii')
            calculated = xor_checksum(payload)
            received = int(checksum_hex, 16)
            if calculated != received:
                error_response = b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"
                client_sock.sendall(error_response)
                return False

            # 3. Send server response
            res_body_len = server_rng.randint(512, 1024)
            res_payload = server_rng.randbytes(res_body_len)
            res_checksum = xor_checksum(res_payload)
            res_checksum_hex = f'{res_checksum:016x}'.encode('ascii')
            full_response_body = res_payload + res_checksum_hex

            response = b"HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n" % len(full_response_body)
            client_sock.sendall(response + full_response_body)
            return True
        except (ConnectionError, UnicodeDecodeError, ValueError):
            return False

    with server_factory(transport_class, handler) as details:
        client = HttpClient(Http1Protocol(transport_class()))

        if transport_class is TcpTransport:
            client.connect(details.host, details.port)
        else:
            client.connect(details.path, 0)

        client_rng = random.Random(1234)

        def run_client_loop(use_safe: bool):
            for i in range(NUM_CYCLES):
                req_payload = client_rng.randbytes(client_rng.randint(512, 1024))
                req_checksum = f'{xor_checksum(req_payload):016x}'.encode('ascii')
                full_body = req_payload + req_checksum

                request = HttpRequest(path="/", body=full_body, headers=[("Content-Length", str(len(full_body)))])
                res = client.post_safe(request) if use_safe else client.post_unsafe(request)

                assert res.status_code == 200

                res_body = res.body if use_safe else res.body.tobytes()
                assert len(res_body) >= 16

                res_payload_bytes = res_body[:-16]
                res_checksum_hex = res_body[-16:].decode('ascii')

                assert xor_checksum(res_payload_bytes) == int(res_checksum_hex, 16)

        try:
            run_client_loop(use_safe=True)
            run_client_loop(use_safe=False)
        finally:
            client.disconnect()