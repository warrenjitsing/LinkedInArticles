#pragma once
#include <httpc/syscalls.h>
#include <httpc/http_protocol.h>
#include <httpc/growable_buffer.h>

typedef struct {
    HttpProtocolInterface interface;
    TransportInterface* transport;
    GrowableBuffer buffer;
    const HttpcSyscalls* syscalls;
} Http1Protocol;


HttpProtocolInterface* http1_protocol_new(
    TransportInterface* transport,
    const HttpcSyscalls* syscalls_override,
    HttpResponseMemoryPolicy policy // New parameter
);
void http1_protocol_destroy(HttpProtocolInterface* protocol);