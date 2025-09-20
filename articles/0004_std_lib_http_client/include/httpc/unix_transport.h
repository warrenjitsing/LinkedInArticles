#pragma once

#include <httpc/transport.h>
#include <httpc/syscalls.h>

typedef struct {
    TransportInterface interface;
    int fd;
    const HttpcSyscalls* syscalls;
} UnixClient;

TransportInterface* unix_transport_new(const HttpcSyscalls* syscalls_override);
void unix_transport_destroy(TransportInterface* transport);