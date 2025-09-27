#pragma once
#include <httpc/syscalls.h>
#include <httpc/http_protocol.h>
#include <httpc/growable_buffer.h>

typedef struct {
    HttpProtocolInterface interface;
    TransportInterface* transport;
    GrowableBuffer buffer;
    const HttpcSyscalls* syscalls;
    HttpResponseMemoryPolicy policy;
    Error (*parse_response)(void* context, HttpResponse* response);
} Http1Protocol;


HttpProtocolInterface* http1_protocol_new(
    TransportInterface* transport,
    const HttpcSyscalls* syscalls_override,
    HttpResponseMemoryPolicy policy
);
