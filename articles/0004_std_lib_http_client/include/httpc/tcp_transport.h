#pragma once

#include <httpc/transport.h>
#include <httpc/syscalls.h>

typedef struct {
    TransportInterface interface;
    int fd;
    const HttpcSyscalls* syscalls;
} TcpClient;

TransportInterface* tcp_transport_new(const HttpcSyscalls* syscalls_override);
void tcp_transport_destroy(TransportInterface* transport);