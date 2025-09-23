import socket

from .errors import TransportError, SocketConnectError, SocketWriteError, SocketReadError
from .transport import Transport


class UnixTransport(Transport):
    def __init__(self) -> None:
        self._sock: socket.socket | None = None

    def connect(self, path: str, port: int) -> None:
        if self._sock is not None:
            raise TransportError("Transport is already connected.")

        # The 'port' argument is ignored for Unix sockets.
        _ = port

        try:
            self._sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self._sock.connect(path)
        except OSError as e:
            self._sock = None
            raise SocketConnectError(f"Socket connection failed for path '{path}': {e}") from e

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