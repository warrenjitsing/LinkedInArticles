from .errors import InvalidRequestError
from .http_protocol import (
    HttpProtocol,
    HttpRequest,
    SafeHttpResponse,
    UnsafeHttpResponse,
    HttpMethod
)

class HttpClient:
    def __init__(self, protocol: HttpProtocol):
        self._protocol = protocol

    def connect(self, host: str, port: int) -> None:
        self._protocol.connect(host, port)

    def disconnect(self) -> None:
        self._protocol.disconnect()

    def get_safe(self, request: HttpRequest) -> SafeHttpResponse:
        self._validate_get_request(request)
        request.method = HttpMethod.GET
        return self._protocol.perform_request_safe(request)

    def get_unsafe(self, request: HttpRequest) -> UnsafeHttpResponse:
        self._validate_get_request(request)
        request.method = HttpMethod.GET
        return self._protocol.perform_request_unsafe(request)

    def post_safe(self, request: HttpRequest) -> SafeHttpResponse:
        self._validate_post_request(request)
        request.method = HttpMethod.POST
        return self._protocol.perform_request_safe(request)

    def post_unsafe(self, request: HttpRequest) -> UnsafeHttpResponse:
        self._validate_post_request(request)
        request.method = HttpMethod.POST
        return self._protocol.perform_request_unsafe(request)

    def _validate_get_request(self, request: HttpRequest) -> None:
        if request.body:
            raise InvalidRequestError("GET requests cannot have a body.")

    def _validate_post_request(self, request: HttpRequest) -> None:
        if not request.body:
            raise InvalidRequestError("POST requests must have a body.")

        content_length_found = any(
            key.lower() == "content-length" for key, _ in request.headers
        )

        if not content_length_found:
            raise InvalidRequestError("POST requests must include a Content-Length header.")