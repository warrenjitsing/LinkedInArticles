from typing import Protocol

class Transport(Protocol):
    def connect(self, host: str, port: int) -> None:
        ...

    def write(self, data: bytes) -> int:
        ...

    def read_into(self, buffer: bytearray) -> int:
        ...

    def close(self) -> None:
        ...