from .transport import Transport
from .http_protocol import HttpProtocol, HttpRequest, SafeHttpResponse, UnsafeHttpResponse, HttpMethod
from .errors import HttpParseError, ConnectionClosedError


class Http1Protocol(HttpProtocol):
    _HEADER_SEPARATOR = b"\r\n\r\n"
    _HEADER_SEPARATOR_CL = b"Content-Length:"

    def __init__(self, transport: Transport):
        self._transport: Transport = transport
        self._buffer: bytearray = bytearray()
        self._header_size: int = 0
        self._content_length: int | None = None

    def connect(self, host: str, port: int) -> None:
        self._transport.connect(host, port)

    def disconnect(self) -> None:
        self._transport.close()

    def perform_request_safe(self, request: HttpRequest) -> SafeHttpResponse:
        unsafe_res = self.perform_request_unsafe(request)

        status_message = unsafe_res.status_message.tobytes().decode('ascii')
        body = unsafe_res.body.tobytes()

        headers = [
            (key.tobytes().decode('ascii'), value.tobytes().decode('ascii'))
            for key, value in unsafe_res.headers
        ]

        return SafeHttpResponse(
            status_code=unsafe_res.status_code,
            status_message=status_message,
            body=body,
            headers=headers
        )

    def perform_request_unsafe(self, request: HttpRequest) -> UnsafeHttpResponse:
        self._build_request_string(request)
        self._transport.write(self._buffer)
        self._read_full_response()
        return self._parse_unsafe_response()

    def _build_request_string(self, request: HttpRequest) -> None:
        self._buffer.clear()

        request_line = f"{request.method.value} {request.path} HTTP/1.1\r\n"
        self._buffer += request_line.encode('ascii')

        for key, value in request.headers:
            header_line = f"{key}: {value}\r\n"
            self._buffer += header_line.encode('ascii')

        self._buffer += "\r\n".encode('ascii')

        if request.body and request.method == HttpMethod.POST:
            self._buffer += request.body

    def _read_full_response(self) -> None:
        self._buffer.clear()
        self._header_size = 0
        self._content_length = None
        read_chunk_size = 4096

        while True:
            old_len = len(self._buffer)
            try:
                self._buffer.extend(b'\0' * read_chunk_size)
                read_view = memoryview(self._buffer)
                bytes_read = self._transport.read_into(read_view[old_len:])
                del read_view
                del self._buffer[old_len + bytes_read:]

                if bytes_read == 0:
                    if self._content_length is not None and len(self._buffer) < self._header_size + self._content_length:
                        raise HttpParseError("Connection closed before full content length was received.")
                    break

            except ConnectionClosedError:
                if self._content_length is not None and len(self._buffer) < self._header_size + self._content_length:
                    raise HttpParseError("Connection closed before full content length was received.")
                break

            if self._header_size == 0:
                separator_pos = self._buffer.find(self._HEADER_SEPARATOR)
                if separator_pos != -1:
                    self._header_size = separator_pos + len(self._HEADER_SEPARATOR)

                    headers_block_lower = self._buffer[:self._header_size].lower()
                    cl_key_pos = headers_block_lower.find(self._HEADER_SEPARATOR_CL.lower())

                    if cl_key_pos != -1:
                        line_end_pos = self._buffer.find(b'\r\n', cl_key_pos)
                        if line_end_pos != -1:
                            value_start_pos = cl_key_pos + len(self._HEADER_SEPARATOR_CL)
                            value_slice = self._buffer[value_start_pos:line_end_pos]

                            try:
                                self._content_length = int(value_slice.strip())
                            except ValueError:
                                raise HttpParseError("Invalid Content-Length value")

            if self._content_length is not None:
                if len(self._buffer) >= self._header_size + self._content_length:
                    break

        if self._header_size == 0 and self._buffer:
            raise HttpParseError("Could not find header separator in response.")

    def _parse_unsafe_response(self) -> UnsafeHttpResponse:
        if self._header_size == 0:
            raise HttpParseError("Cannot parse response with no headers.")

        buffer_view = memoryview(self._buffer)

        status_line_end = self._buffer.find(b'\r\n')
        if status_line_end == -1:
            raise HttpParseError("Could not find status line terminator.")

        first_space = self._buffer.find(b' ', 0, status_line_end)
        if first_space == -1:
            raise HttpParseError("Could not find space after HTTP version.")

        second_space = self._buffer.find(b' ', first_space + 1, status_line_end)
        if second_space == -1:
            raise HttpParseError("Could not find space after status code.")

        try:
            status_code = int(buffer_view[first_space + 1:second_space])
        except ValueError:
            raise HttpParseError("Invalid status code in status line.")

        status_message = buffer_view[second_space + 1:status_line_end]

        headers = []
        current_pos = status_line_end + 2
        while current_pos < self._header_size:
            line_end = self._buffer.find(b'\r\n', current_pos, self._header_size)
            if line_end == -1:
                break

            if line_end == current_pos:
                break

            header_line_view = buffer_view[current_pos:line_end]
            colon_pos = self._buffer.find(b':', current_pos, line_end)

            if colon_pos != -1:
                key = buffer_view[current_pos:colon_pos]

                value_start = colon_pos + 1
                while value_start < line_end and self._buffer[value_start] in (32, 9):
                    value_start += 1

                value = buffer_view[value_start:line_end]
                headers.append((key, value))

            current_pos = line_end + 2

        if self._content_length is not None:
            body_end = self._header_size + self._content_length
            body = buffer_view[self._header_size:body_end]
        else:
            body = buffer_view[self._header_size:]

        return UnsafeHttpResponse(
            status_code=status_code,
            status_message=status_message,
            headers=headers,
            body=body
        )