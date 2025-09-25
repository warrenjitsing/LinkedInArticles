from dataclasses import dataclass, field
from enum import Enum
from typing import Protocol, TypeVar, Generic


class HttpMethod(Enum):
    GET = "GET"
    POST = "POST"

@dataclass
class HttpRequest:
    method: HttpMethod = HttpMethod.GET
    path: str = "/"
    body: bytes = b""
    headers: list[tuple[str, str]] = field(default_factory=list)


@dataclass
class SafeHttpResponse:
    status_code: int
    status_message: str
    body: bytes
    headers: list[tuple[str, str]]

@dataclass
class UnsafeHttpResponse:
    status_code: int
    status_message: memoryview
    body: memoryview
    headers: list[tuple[memoryview, memoryview]]

ResponseType = TypeVar("ResponseType", SafeHttpResponse, UnsafeHttpResponse)

class HttpProtocol(Generic[ResponseType], Protocol):
    def perform_request(self, request: HttpRequest) -> ResponseType:
        ...

# --- Status Codes ---

class HttpStatusCode(Enum):
    CONTINUE = 100
    OK = 200
    CREATED = 201
    ACCEPTED = 202
    FOUND = 302
    BAD_REQUEST = 400
    UNAUTHORIZED = 401
    FORBIDDEN = 403
    NOT_FOUND = 404
    INTERNAL_SERVER_ERROR = 500
    BAD_GATEWAY = 502