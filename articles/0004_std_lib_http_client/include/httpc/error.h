#pragma once

static const struct {
    const int NONE;
    const int TRANSPORT;
    const int HTTPC;
} ErrorType = {
    .NONE = 0,
    .TRANSPORT = 1,
    .HTTPC = 2,
};

typedef struct {
    int type;
    int code;
} Error;

static const struct {
    const int NONE;
    const int DNS_FAILURE;
    const int SOCKET_CREATE_FAILURE;
    const int SOCKET_CONNECT_FAILURE;
    const int SOCKET_WRITE_FAILURE;
    const int SOCKET_READ_FAILURE;
    const int CONNECTION_CLOSED;
    const int SOCKET_CLOSE_FAILURE;
    const int INIT_FAILURE;
} TransportErrorCode = {
    .NONE = 0,
    .DNS_FAILURE = 1,
    .SOCKET_CREATE_FAILURE = 2,
    .SOCKET_CONNECT_FAILURE = 3,
    .SOCKET_WRITE_FAILURE = 4,
    .SOCKET_READ_FAILURE = 5,
    .CONNECTION_CLOSED = 6,
    .SOCKET_CLOSE_FAILURE = 7,
    .INIT_FAILURE = 8,
};

static const struct {
    const int NONE;
    const int URL_PARSE_FAILURE;
    const int HTTP_PARSE_FAILURE;
    const int INIT_FAILURE;
} HttpClientErrorCode = {
    .NONE = 0,
    .URL_PARSE_FAILURE = 1,
    .HTTP_PARSE_FAILURE = 2,
    .INIT_FAILURE = 3,
};
