import pytest
import socket
import threading
import time
from queue import Queue


from unittest.mock import patch


from httppy.tcp_transport import TcpTransport
from httppy.errors import (
    SocketConnectError,
    DnsFailureError,
    SocketWriteError,
    TransportError,
    SocketReadError
)

class ServerFixture:
    def __init__(self, handler):
        self._handler = handler
        self._should_stop = threading.Event()
        self.listener_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener_sock.bind(("127.0.0.1", 0))
        self.port = self.listener_sock.getsockname()[1]
        self.thread = threading.Thread(target=self._accept_loop)

    def start(self):
        self.thread.start()

    def stop(self):
        if not self._should_stop.is_set():
            self._should_stop.set()
            # Connect to unblock the accept() call
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=0.1)
            except OSError:
                pass
            self.thread.join()
            self.listener_sock.close()

    def _accept_loop(self):
        self.listener_sock.listen()
        try:
            client_sock, _ = self.listener_sock.accept()
            with client_sock:
                if self._handler:
                    self._handler(client_sock)
        except OSError:
            pass


@pytest.fixture
def test_server(request):
    server = ServerFixture(request.param if hasattr(request, "param") else None)
    server.start()
    yield server
    server.stop()


def test_construction_succeeds():
    try:
        _ = TcpTransport()
        assert True
    except Exception:
        pytest.fail("TcpTransport construction failed")


@pytest.mark.parametrize("test_server", [lambda sock: None], indirect=True)
def test_connect_succeeds(test_server):
    transport = TcpTransport()
    transport.connect("127.0.0.1", test_server.port)
    transport.close()


def test_write_succeeds(test_server):
    message_queue = Queue()
    def server_logic(sock):
        data = sock.recv(1024)
        message_queue.put(data)

    test_server._handler = server_logic

    transport = TcpTransport()
    transport.connect("127.0.0.1", test_server.port)
    message_to_send = b"hello from client"
    bytes_written = transport.write(message_to_send)

    assert bytes_written == len(message_to_send)

    captured_message = message_queue.get(timeout=1)
    assert captured_message == message_to_send
    transport.close()


@pytest.mark.parametrize("test_server", [lambda sock: sock.sendall(b"hello from server")], indirect=True)
def test_read_into_succeeds(test_server):
    transport = TcpTransport()
    transport.connect("127.0.0.1", test_server.port)

    buffer = bytearray(1024)
    bytes_read = transport.read_into(buffer)

    assert bytes_read == len(b"hello from server")
    assert buffer[:bytes_read] == b"hello from server"
    transport.close()


@pytest.mark.parametrize("test_server", [lambda sock: None], indirect=True)
def test_close_succeeds(test_server):
    transport = TcpTransport()
    transport.connect("127.0.0.1", test_server.port)
    transport.close()


@pytest.mark.parametrize("test_server", [lambda sock: None], indirect=True)
def test_close_is_idempotent(test_server):
    transport = TcpTransport()
    transport.connect("127.0.0.1", test_server.port)
    transport.close()
    transport.close()


def test_connect_fails_on_unresponsive_port():
    transport = TcpTransport()
    unresponsive_port = 65531
    with pytest.raises(SocketConnectError):
        transport.connect("127.0.0.1", unresponsive_port)

def test_connect_fails_on_dns_failure():
    transport = TcpTransport()
    with pytest.raises(DnsFailureError):
        transport.connect("a-hostname-that-will-not-resolve.invalid", 80)

def test_write_fails_on_closed_connection():
    server_closed_event = threading.Event()
    def server_logic(sock):
        # Force an abrupt RST shutdown with SO_LINGER
        import struct
        l_onoff = 1
        l_linger = 0
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', l_onoff, l_linger))
        server_closed_event.set()

    server = ServerFixture(server_logic)
    server.start()

    transport = TcpTransport()
    transport.connect("127.0.0.1", server.port)

    server_closed_event.wait(timeout=1)
    time.sleep(0.05) # Give OS time to process RST

    with pytest.raises(SocketWriteError):
        transport.write(b"this should fail")

    server.stop()


@pytest.mark.parametrize("test_server", [lambda sock: None], indirect=True)
def test_read_into_fails_on_peer_shutdown(test_server):
    transport = TcpTransport()
    transport.connect("127.0.0.1", test_server.port)

    # Wait for the server thread to accept and close the connection
    test_server.thread.join(timeout=1)

    buffer = bytearray(1024)
    bytes_read = transport.read_into(buffer)
    assert bytes_read == 0
    transport.close()


def test_write_fails_if_not_connected():
    transport = TcpTransport()
    with pytest.raises(TransportError, match="Cannot write on a disconnected transport."):
        transport.write(b"some data")


def test_read_into_fails_if_not_connected():
    transport = TcpTransport()
    buffer = bytearray(1024)
    with pytest.raises(TransportError, match="Cannot read from a disconnected transport."):
        transport.read_into(buffer)


@pytest.mark.parametrize("test_server", [lambda sock: None], indirect=True)
def test_connect_fails_if_already_connected(test_server):
    transport = TcpTransport()
    transport.connect("127.0.0.1", test_server.port)

    with pytest.raises(TransportError, match="Transport is already connected."):
        transport.connect("127.0.0.1", test_server.port)

    transport.close()


@pytest.mark.parametrize("test_server", [lambda sock: None], indirect=True)
def test_read_into_raises_socket_read_error_on_os_error(test_server):
    transport = TcpTransport()
    transport.connect("127.0.0.1", test_server.port)

    buffer = bytearray(1024)

    with patch('socket.socket.recv_into') as mock_recv_into:
        mock_recv_into.side_effect = OSError("Mock OS-level read error")

        with pytest.raises(SocketReadError, match="Mock OS-level read error"):
            transport.read_into(buffer)

    transport.close()