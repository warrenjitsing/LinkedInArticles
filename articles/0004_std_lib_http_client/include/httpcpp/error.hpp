#pragma once

#include <variant>

namespace httpcpp {

    enum class TransportError {
        None,
        DnsFailure,
        SocketCreateFailure,
        SocketConnectFailure,
        SocketWriteFailure,
        SocketReadFailure,
        ConnectionClosed,
        SocketCloseFailure,
        InitFailure,
    };

    enum class HttpClientError {
        None,
        UrlParseFailure,
        HttpParseFailure,
        InitFailure,
    };

    using Error = std::variant<TransportError, HttpClientError>;

} // namespace httpcpp