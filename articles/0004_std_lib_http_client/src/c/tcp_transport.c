#include <httpc/tcp_transport.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

static Error tcp_transport_connect(void* context, const char* host, int port) {
    TcpClient* self = (TcpClient*)context;
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int sfd = -1, s;
    char port_str[6];

    self->syscalls->memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    self->syscalls->snprintf(port_str, sizeof(port_str), "%d", port);

    s = self->syscalls->getaddrinfo(host, port_str, &hints, &result);
    if (s != 0) {
        return (Error){ErrorType.TRANSPORT, TransportErrorCode.DNS_FAILURE};
    }

    for (rp = result; rp != nullptr; rp = rp->ai_next) {
        sfd = self->syscalls->socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) {
            continue;
        }

        if (self->syscalls->connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            int flag = 1;
            if (self->syscalls->setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) == -1) {
                self->syscalls->close(sfd);
                sfd = -1;
                continue;
            }
            break;
        }

        self->syscalls->close(sfd);
        sfd = -1;
    }

    self->syscalls->freeaddrinfo(result);

    if (rp == nullptr || sfd == -1) {
        return (Error){ErrorType.TRANSPORT, TransportErrorCode.SOCKET_CONNECT_FAILURE};
    }

    self->fd = sfd;
    return (Error){ErrorType.NONE, 0};
}

static Error tcp_transport_write(void* context, const void* buffer, size_t len, ssize_t* bytes_written) {
    TcpClient* self = (TcpClient*)context;
    if (self->fd <= 0) {
        *bytes_written = -1;
        return (Error){ErrorType.TRANSPORT, TransportErrorCode.SOCKET_WRITE_FAILURE};
    }
    *bytes_written = self->syscalls->write(self->fd, buffer, len);
    if (*bytes_written == -1) {
        return (Error){ErrorType.TRANSPORT, TransportErrorCode.SOCKET_WRITE_FAILURE};
    }
    return (Error){ErrorType.NONE, 0};
}

static Error tcp_transport_writev(void* context, const struct iovec* iov, int iovcnt, ssize_t* bytes_written) {
    TcpClient* self = (TcpClient*)context;
    if (self->fd <= 0) {
        *bytes_written = -1;
        return (Error){ErrorType.TRANSPORT, TransportErrorCode.SOCKET_WRITE_FAILURE};
    }
    *bytes_written = self->syscalls->writev(self->fd, iov, iovcnt);
    if (*bytes_written == -1) {
        return (Error){ErrorType.TRANSPORT, TransportErrorCode.SOCKET_WRITE_FAILURE};
    }
    return (Error){ErrorType.NONE, 0};
}

static Error tcp_transport_read(void* context, void* buffer, size_t len, ssize_t* bytes_read) {
    TcpClient* self = (TcpClient*)context;
    if (self->fd <= 0) {
        *bytes_read = -1;
        return (Error){ErrorType.TRANSPORT, TransportErrorCode.SOCKET_READ_FAILURE};
    }
    *bytes_read = self->syscalls->read(self->fd, buffer, len);
    if (*bytes_read == -1) {
        return (Error){ErrorType.TRANSPORT, TransportErrorCode.SOCKET_READ_FAILURE};
    }
    if (*bytes_read == 0) {
        return (Error){ErrorType.TRANSPORT, TransportErrorCode.CONNECTION_CLOSED};
    }
    return (Error){ErrorType.NONE, 0};
}

static Error tcp_transport_close(void* context) {
    TcpClient* self = (TcpClient*)context;
    if (self->fd > 0) {
        int result = self->syscalls->close(self->fd);
        self->fd = 0;
        if (result == -1) {
            return (Error){ErrorType.TRANSPORT, TransportErrorCode.SOCKET_CLOSE_FAILURE};
        }
    }
    return (Error){ErrorType.NONE, 0};
}

void tcp_transport_destroy(void* context) {
    if (!context) {
        return;
    }
    TcpClient* self = (TcpClient*)context;
    self->syscalls->free(self);
}

TransportInterface* tcp_transport_new(const HttpcSyscalls* syscalls_override) {
    if (!default_syscalls_initialized) {
        httpc_syscalls_init_default(&DEFAULT_SYSCALLS);
        default_syscalls_initialized = 1;
    }

    if (!syscalls_override)
    {
        syscalls_override = &DEFAULT_SYSCALLS;
    }

    TcpClient* self = syscalls_override->malloc(sizeof(TcpClient));
    if (!self) {
        return nullptr;
    }
    syscalls_override->memset(self, 0, sizeof(TcpClient));
    self->syscalls = syscalls_override;

    self->interface.context = self;
    self->interface.connect = tcp_transport_connect;
    self->interface.write = tcp_transport_write;
    self->interface.writev = tcp_transport_writev;
    self->interface.read = tcp_transport_read;
    self->interface.close = tcp_transport_close;
    self->interface.destroy = tcp_transport_destroy;

    return &self->interface;
}


