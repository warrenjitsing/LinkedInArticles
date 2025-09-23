import pytest
import socket
import threading
import time
import os
from queue import Queue
from unittest.mock import patch

from httppy.unix_transport import UnixTransport
from httppy.errors import (
    SocketConnectError,
    SocketWriteError,
    TransportError,
    SocketReadError
)

class UnixServerFixture:
    def __init__(self, handler):
        self._handler = handler
        self._should_stop = threading.Event()

        self.socket_path = f"/tmp/httppy_test_{os.getpid()}_{time.time_ns()}"
        # Clean up any old socket file
        if os.path.exists(self.socket_path):
            os.remove(self.socket_path)

        self.listener_sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.listener_sock.settimeout(0.5) # To prevent hanging on accept
        self.listener_sock.bind(self.socket_path)
        self.thread = threading.Thread(target=self._accept_loop)

    def start(self):
        self.thread.start()

    def stop(self):
        if not self._should_stop.is_set():
            self._should_stop.set()
            self.thread.join()
            self.listener_sock.close()
            if os.path.exists(self.socket_path):
                os.remove(self.socket_path)

    def _accept_loop(self):
        self.listener_sock.listen()
        while not self._should_stop.is_set():
            try:
                client_sock, _ = self.listener_sock.accept()
                with client_sock:
                    if self._handler:
                        self._handler(client_sock)
                # Only handle one connection for simplicity in tests
                break
            except socket.timeout:
                continue
            except OSError:
                break

@pytest.fixture
def test_server(request):
    server = UnixServerFixture(request.param if hasattr(request, "param") else None)
    server.start()
    yield server
    server.stop()


def test_construction_succeeds():
    try:
        _ = UnixTransport()
        assert True
    except Exception:
        pytest.fail("UnixTransport construction failed")

@pytest.mark.parametrize("test_server", [lambda sock: None], indirect=True)
def test_connect_succeeds(test_server):
    transport = UnixTransport()
    transport.connect(test_server.socket_path, 0)
    transport.close()

def test_write_succeeds(test_server):
    message_queue = Queue()
    def server_logic(sock):
        data = sock.recv(1024)
        message_queue.put(data)
    test_server._handler = server_logic

    transport = UnixTransport()
    transport.connect(test_server.socket_path, 0)
    message_to_send = b"hello from client"
    bytes_written = transport.write(message_to_send)

    assert bytes_written == len(message_to_send)
    captured_message = message_queue.get(timeout=1)
    assert captured_message == message_to_send
    transport.close()

@pytest.mark.parametrize("test_server", [lambda sock: sock.sendall(b"hello from server")], indirect=True)
def test_read_into_succeeds(test_server):
    transport = UnixTransport()
    transport.connect(test_server.socket_path, 0)

    buffer = bytearray(1024)
    bytes_read = transport.read_into(buffer)

    assert bytes_read == len(b"hello from server")
    assert buffer[:bytes_read] == b"hello from server"
    transport.close()

@pytest.mark.parametrize("test_server", [lambda sock: None], indirect=True)
def test_close_succeeds(test_server):
    transport = UnixTransport()
    transport.connect(test_server.socket_path, 0)
    transport.close()

@pytest.mark.parametrize("test_server", [lambda sock: None], indirect=True)
def test_close_is_idempotent(test_server):
    transport = UnixTransport()
    transport.connect(test_server.socket_path, 0)
    transport.close()
    transport.close()

def test_connect_fails_on_unresponsive_path():
    transport = UnixTransport()
    path = "/tmp/this-is-a-non-existent-socket.sock"
    with pytest.raises(SocketConnectError):
        transport.connect(path, 0)

def test_write_fails_on_closed_connection():
    server_closed_event = threading.Event()
    def server_logic(sock):
        import struct
        l_onoff = 1
        l_linger = 0
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', l_onoff, l_linger))
        server_closed_event.set()

    server = UnixServerFixture(server_logic)
    server.start()

    transport = UnixTransport()
    transport.connect(server.socket_path, 0)

    server_closed_event.wait(timeout=1)
    time.sleep(0.05)

    with pytest.raises(SocketWriteError):
        transport.write(b"this should fail")

    server.stop()

@pytest.mark.parametrize("test_server", [lambda sock: None], indirect=True)
def test_read_into_fails_on_peer_shutdown(test_server):
    transport = UnixTransport()
    transport.connect(test_server.socket_path, 0)

    test_server.thread.join(timeout=1)

    buffer = bytearray(1024)
    bytes_read = transport.read_into(buffer)
    assert bytes_read == 0
    transport.close()

def test_write_fails_if_not_connected():
    transport = UnixTransport()
    with pytest.raises(TransportError, match="Cannot write on a disconnected transport."):
        transport.write(b"some data")

def test_read_into_fails_if_not_connected():
    transport = UnixTransport()
    buffer = bytearray(1024)
    with pytest.raises(TransportError, match="Cannot read from a disconnected transport."):
        transport.read_into(buffer)

@pytest.mark.parametrize("test_server", [lambda sock: None], indirect=True)
def test_connect_fails_if_already_connected(test_server):
    transport = UnixTransport()
    transport.connect(test_server.socket_path, 0)

    with pytest.raises(TransportError, match="Transport is already connected."):
        transport.connect(test_server.socket_path, 0)

    transport.close()

@pytest.mark.parametrize("test_server", [lambda sock: None], indirect=True)
def test_read_into_raises_socket_read_error_on_os_error(test_server):
    transport = UnixTransport()
    transport.connect(test_server.socket_path, 0)

    buffer = bytearray(1024)

    with patch('socket.socket.recv_into') as mock_recv_into:
        mock_recv_into.side_effect = OSError("Mock OS-level read error")

        with pytest.raises(SocketReadError, match="Mock OS-level read error"):
            transport.read_into(buffer)

    transport.close()