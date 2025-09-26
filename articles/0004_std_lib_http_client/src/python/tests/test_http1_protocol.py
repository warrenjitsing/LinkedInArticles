import pytest
import socket
import threading
import time
import os
from dataclasses import dataclass
from typing import Callable, Generator, Type
from contextlib import contextmanager
from queue import Queue


from httppy.transport import Transport
from httppy.tcp_transport import TcpTransport
from httppy.unix_transport import UnixTransport
from httppy.http1_protocol import Http1Protocol
from httppy.http_protocol import HttpRequest, HttpMethod
from httppy.errors import TransportError, HttpParseError


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
            details.path = f"/tmp/httppy_test_{os.getpid()}_{time.time_ns()}.sock"
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
def test_connect_and_disconnect_succeeds(server_factory, transport_class):

    def handler(client_sock: socket.socket):
        pass

    with server_factory(transport_class, handler) as details:
        transport = transport_class()

        try:
            if transport_class is TcpTransport:
                transport.connect(details.host, details.port)
            else:
                transport.connect(details.path, 0)
        finally:
            transport.close()


@pytest.mark.parametrize("transport_class", [TcpTransport, UnixTransport])
def test_perform_request_fails_if_not_connected(transport_class):
    transport = transport_class()
    protocol = Http1Protocol(transport)

    req = HttpRequest(method=HttpMethod.GET, path="/")

    with pytest.raises(TransportError, match="Cannot write on a disconnected transport."):
        protocol.perform_request_unsafe(req)


@pytest.mark.parametrize("transport_class", [TcpTransport, UnixTransport])
def test_correctly_serializes_get_request(server_factory, transport_class):
    request_queue = Queue()

    def handler(client_sock: socket.socket):
        data = client_sock.recv(1024)
        request_queue.put(data)
        client_sock.sendall(b"HTTP/1.1 204 No Content\r\n\r\n")

    with server_factory(transport_class, handler) as details:
        transport = transport_class()
        protocol = Http1Protocol(transport)

        if transport_class is TcpTransport:
            protocol.connect(details.host, details.port)
        else:
            protocol.connect(details.path, 0)

        req = HttpRequest(
            method=HttpMethod.GET,
            path="/test",
            headers=[("Host", "example.com")]
        )

        protocol.perform_request_unsafe(req)
        protocol.disconnect()

    captured_request = request_queue.get(timeout=1.0)
    expected_request = (
        b"GET /test HTTP/1.1\r\n"
        b"Host: example.com\r\n"
        b"\r\n"
    )

    assert captured_request == expected_request


@pytest.mark.parametrize("transport_class", [TcpTransport, UnixTransport])
def test_correctly_serializes_post_request(server_factory, transport_class):
    request_queue = Queue()
    body_content = b"key=value&data=true"

    def handler(client_sock: socket.socket):
        data = client_sock.recv(1024)
        request_queue.put(data)
        client_sock.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n")

    with server_factory(transport_class, handler) as details:
        transport = transport_class()
        protocol = Http1Protocol(transport)

        if transport_class is TcpTransport:
            protocol.connect(details.host, details.port)
        else:
            protocol.connect(details.path, 0)

        req = HttpRequest(
            method=HttpMethod.POST,
            path="/api/submit",
            body=body_content,
            headers=[
                ("Host", "test-server"),
                ("Content-Length", str(len(body_content)))
            ]
        )

        protocol.perform_request_unsafe(req)
        protocol.disconnect()

    captured_request = request_queue.get(timeout=1.0)
    expected_request = (
        b"POST /api/submit HTTP/1.1\r\n"
        b"Host: test-server\r\n"
        b"Content-Length: 19\r\n"
        b"\r\n"
        b"key=value&data=true"
    )

    assert captured_request == expected_request


@pytest.mark.parametrize("transport_class", [TcpTransport, UnixTransport])
def test_parses_response_with_content_length(server_factory, transport_class):
    canned_response = (
        b"HTTP/1.1 200 OK\r\n"
        b"Content-Length: 12\r\n"
        b"Content-Type: text/plain\r\n"
        b"\r\n"
        b"Hello Client"
    )

    def handler(client_sock: socket.socket):
        client_sock.recv(1024)
        client_sock.sendall(canned_response)

    with server_factory(transport_class, handler) as details:
        transport = transport_class()
        protocol = Http1Protocol(transport)

        if transport_class is TcpTransport:
            protocol.connect(details.host, details.port)
        else:
            protocol.connect(details.path, 0)

        req = HttpRequest()
        res = protocol.perform_request_unsafe(req)

        assert res.status_code == 200
        assert res.status_message.tobytes() == b"OK"
        assert len(res.headers) == 2
        assert res.headers[0][0].tobytes() == b"Content-Length"
        assert res.headers[0][1].tobytes() == b"12"
        assert res.headers[1][0].tobytes() == b"Content-Type"
        assert res.headers[1][1].tobytes() == b"text/plain"
        assert res.body.tobytes() == b"Hello Client"

        protocol.disconnect()

@pytest.mark.parametrize("transport_class", [TcpTransport, UnixTransport])
def test_reads_body_on_connection_close(server_factory, transport_class):
    canned_response = (
        b"HTTP/1.1 200 OK\r\n"
        b"Connection: close\r\n"
        b"\r\n"
        b"Full body."
    )

    def handler(client_sock: socket.socket):
        client_sock.recv(1024)
        client_sock.sendall(canned_response)

    with server_factory(transport_class, handler) as details:
        transport = transport_class()
        protocol = Http1Protocol(transport)

        if transport_class is TcpTransport:
            protocol.connect(details.host, details.port)
        else:
            protocol.connect(details.path, 0)

        req = HttpRequest()
        res = protocol.perform_request_unsafe(req)

        assert res.status_code == 200
        assert res.body.tobytes() == b"Full body."

        protocol.disconnect()


@pytest.mark.parametrize("transport_class", [TcpTransport, UnixTransport])
def test_handles_response_split_across_multiple_reads(server_factory, transport_class):
    response_chunks = [
        b"HTTP/1.1 200 OK\r\n",
        b"Content-Type: text/plain\r\n",
        b"Content-Length: 4\r\n",
        b"\r\n",
        b"Body"
    ]

    def handler(client_sock: socket.socket):
        client_sock.recv(1024)
        for chunk in response_chunks:
            client_sock.sendall(chunk)
            time.sleep(0.01)

    with server_factory(transport_class, handler) as details:
        transport = transport_class()
        protocol = Http1Protocol(transport)

        if transport_class is TcpTransport:
            protocol.connect(details.host, details.port)
        else:
            protocol.connect(details.path, 0)

        req = HttpRequest()
        res = protocol.perform_request_unsafe(req)

        assert res.status_code == 200
        assert len(res.headers) == 2
        assert res.headers[1][0].tobytes() == b"Content-Length"
        assert res.headers[1][1].tobytes() == b"4"
        assert res.body.tobytes() == b"Body"

        protocol.disconnect()


@pytest.mark.parametrize("transport_class", [TcpTransport, UnixTransport])
def test_parses_complex_status_line_and_headers(server_factory, transport_class):
    canned_response = (
        b"HTTP/1.1 404 Not Found\r\n"
        b"Connection: close\r\n"
        b"Content-Type: application/json\r\n"
        b"X-Request-ID: abc-123\r\n"
        b"Content-Length: 0\r\n"
        b"\r\n"
    )

    def handler(client_sock: socket.socket):
        client_sock.recv(1024)
        client_sock.sendall(canned_response)

    with server_factory(transport_class, handler) as details:
        transport = transport_class()
        protocol = Http1Protocol(transport)

        if transport_class is TcpTransport:
            protocol.connect(details.host, details.port)
        else:
            protocol.connect(details.path, 0)

        req = HttpRequest()
        res = protocol.perform_request_unsafe(req)

        assert res.status_code == 404
        assert res.status_message.tobytes() == b"Not Found"
        assert len(res.headers) == 4
        assert res.headers[0][0].tobytes() == b"Connection"
        assert res.headers[0][1].tobytes() == b"close"
        assert res.headers[1][0].tobytes() == b"Content-Type"
        assert res.headers[1][1].tobytes() == b"application/json"
        assert res.headers[2][0].tobytes() == b"X-Request-ID"
        assert res.headers[2][1].tobytes() == b"abc-123"
        assert res.headers[3][0].tobytes() == b"Content-Length"
        assert res.headers[3][1].tobytes() == b"0"
        assert res.body.tobytes() == b""

        protocol.disconnect()


@pytest.mark.parametrize("transport_class", [TcpTransport, UnixTransport])
def test_handles_zero_content_length_response(server_factory, transport_class):
    canned_response = (
        b"HTTP/1.1 204 No Content\r\n"
        b"Content-Length: 0\r\n"
        b"\r\n"
    )

    def handler(client_sock: socket.socket):
        client_sock.recv(1024)
        client_sock.sendall(canned_response)

    with server_factory(transport_class, handler) as details:
        transport = transport_class()
        protocol = Http1Protocol(transport)

        if transport_class is TcpTransport:
            protocol.connect(details.host, details.port)
        else:
            protocol.connect(details.path, 0)

        req = HttpRequest()
        res = protocol.perform_request_unsafe(req)

        assert res.status_code == 204
        assert len(res.headers) == 1
        assert res.headers[0][0].tobytes() == b"Content-Length"
        assert res.headers[0][1].tobytes() == b"0"
        assert res.body.tobytes() == b""

        protocol.disconnect()


@pytest.mark.parametrize("transport_class", [TcpTransport, UnixTransport])
def test_handles_response_larger_than_initial_buffer(server_factory, transport_class):
    large_body = b'a' * 5000

    canned_response = (
                              b"HTTP/1.1 200 OK\r\n"
                              b"Content-Length: " + str(len(large_body)).encode('ascii') + b"\r\n"
                                                                                           b"\r\n"
                      ) + large_body

    def handler(client_sock: socket.socket):
        client_sock.recv(4096)
        client_sock.sendall(canned_response)

    with server_factory(transport_class, handler) as details:
        transport = transport_class()
        protocol = Http1Protocol(transport)

        if transport_class is TcpTransport:
            protocol.connect(details.host, details.port)
        else:
            protocol.connect(details.path, 0)

        req = HttpRequest()
        res = protocol.perform_request_unsafe(req)

        assert res.status_code == 200
        assert len(res.body) == len(large_body)
        assert res.body.tobytes() == large_body

        protocol.disconnect()


@pytest.mark.parametrize("transport_class", [TcpTransport, UnixTransport])
def test_fails_on_bad_content_length(server_factory, transport_class):
    canned_response = (
        b"HTTP/1.1 200 OK\r\n"
        b"Content-Length: 100\r\n"
        b"\r\n"
        b"short body"
    )

    def handler(client_sock: socket.socket):
        client_sock.recv(1024)
        client_sock.sendall(canned_response)

    with server_factory(transport_class, handler) as details:
        transport = transport_class()
        protocol = Http1Protocol(transport)

        if transport_class is TcpTransport:
            protocol.connect(details.host, details.port)
        else:
            protocol.connect(details.path, 0)

        req = HttpRequest()

        with pytest.raises(HttpParseError, match="Connection closed before full content length"):
            protocol.perform_request_unsafe(req)

        protocol.disconnect()


@pytest.mark.parametrize("transport_class", [TcpTransport, UnixTransport])
def test_fails_if_connection_closed_during_headers(server_factory, transport_class):
    incomplete_response = b"HTTP/1.1 200 OK\r\nContent-Type: text/plain"

    def handler(client_sock: socket.socket):
        client_sock.recv(1024)
        client_sock.sendall(incomplete_response)

    with server_factory(transport_class, handler) as details:
        transport = transport_class()
        protocol = Http1Protocol(transport)

        if transport_class is TcpTransport:
            protocol.connect(details.host, details.port)
        else:
            protocol.connect(details.path, 0)

        req = HttpRequest()

        with pytest.raises(HttpParseError, match="Could not find header separator"):
            protocol.perform_request_unsafe(req)

        protocol.disconnect()


@pytest.mark.parametrize("transport_class", [TcpTransport, UnixTransport])
def test_safe_request_returns_owning_deep_copy(server_factory, transport_class):
    canned_response = (
        b"HTTP/1.1 200 OK\r\n"
        b"Content-Length: 11\r\n"
        b"\r\n"
        b"Safe Buffer"
    )

    def handler(client_sock: socket.socket):
        client_sock.recv(1024)
        client_sock.sendall(canned_response)

    with server_factory(transport_class, handler) as details:
        transport = transport_class()
        protocol = Http1Protocol(transport)

        if transport_class is TcpTransport:
            protocol.connect(details.host, details.port)
        else:
            protocol.connect(details.path, 0)

        req = HttpRequest()
        res = protocol.perform_request_safe(req)

        assert res.status_code == 200
        assert isinstance(res.body, bytes)
        assert res.body == b"Safe Buffer"

        protocol._buffer.clear()

        assert res.body == b"Safe Buffer"

        protocol.disconnect()