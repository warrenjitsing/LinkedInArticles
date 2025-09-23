import socket

from .errors import (
    TransportError,
    DnsFailureError,
    SocketConnectError,
    SocketWriteError,
    SocketReadError,
)
from .transport import Transport


class TcpTransport(Transport):
    def __init__(self) -> None:
        self._sock: socket.socket | None = None

    def connect(self, host: str, port: int) -> None:
        if self._sock is not None:
            raise TransportError("Transport is already connected.")

        try:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._sock.connect((host, port))
            self._sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        except socket.gaierror as e:
            self._sock = None
            raise DnsFailureError(f"DNS Failure for host '{host}'") from e
        except OSError as e:
            self._sock = None
            raise SocketConnectError(f"Socket connection failed: {e}") from e

    def write(self, data: bytes) -> int:
        if self._sock is None:
            raise TransportError("Cannot write on a disconnected transport.")

        try:
            return self._sock.send(data)
        except OSError as e:
            raise SocketWriteError(f"Socket write failed: {e}") from e

    def read_into(self, buffer: bytearray) -> int:
        if self._sock is None:
            raise TransportError("Cannot read from a disconnected transport.")

        try:
            return self._sock.recv_into(buffer)
        except OSError as e:
            raise SocketReadError(f"Socket read failed: {e}") from e

    def close(self) -> None:
        if self._sock is not None:
            try:
                self._sock.close()
            finally:
                self._sock = None