class HttpcError(Exception):
    """Base exception for the httppy library."""
    pass

# --- Transport Errors ---

class TransportError(HttpcError):
    """A generic error occurred in the transport layer."""
    pass

class DnsFailureError(TransportError): pass
class SocketCreateError(TransportError): pass
class SocketConnectError(TransportError): pass
class SocketWriteError(TransportError): pass
class SocketReadError(TransportError): pass
class ConnectionClosedError(TransportError): pass
class SocketCloseFailure(TransportError): pass
class TransportInitError(TransportError): pass

# --- HTTP Client Errors ---

class HttpClientError(HttpcError):
    """A generic error occurred in the HTTP client logic."""
    pass

class UrlParseError(HttpClientError): pass
class HttpParseError(HttpClientError): pass
class InvalidRequestError(HttpClientError): pass
class HttpClientInitError(HttpClientError): pass