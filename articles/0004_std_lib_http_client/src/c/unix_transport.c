#include <httpc/unix_transport.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/un.h>

static Error unix_transport_connect(void* context, const char* host, int port) {
    (void)port;
    UnixClient* self = (UnixClient*)context;
    int sfd;
    struct sockaddr_un addr;

    sfd = self->syscalls->socket(AF_UNIX, SOCK_STREAM, 0);
    if (sfd == -1) {
        return (Error){ErrorType.TRANSPORT, TransportErrorCode.SOCKET_CREATE_FAILURE};
    }

    self->syscalls->memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    self->syscalls->strncpy(addr.sun_path, host, sizeof(addr.sun_path) - 1);

    if (self->syscalls->connect(sfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        self->syscalls->close(sfd);
        return (Error){ErrorType.TRANSPORT, TransportErrorCode.SOCKET_CONNECT_FAILURE};
    }

    self->fd = sfd;
    return (Error){ErrorType.NONE, 0};
}

static Error unix_transport_write(void* context, const void* buffer, size_t len, ssize_t* bytes_written) {
    UnixClient* self = (UnixClient*)context;
    *bytes_written = self->syscalls->write(self->fd, buffer, len);
    if (*bytes_written == -1) {
        return (Error){ErrorType.TRANSPORT, TransportErrorCode.SOCKET_WRITE_FAILURE};
    }
    return (Error){ErrorType.NONE, 0};
}

static Error unix_transport_read(void* context, void* buffer, size_t len, ssize_t* bytes_read) {
    UnixClient* self = (UnixClient*)context;
    *bytes_read = self->syscalls->read(self->fd, buffer, len);
    if (*bytes_read == -1) {
        return (Error){ErrorType.TRANSPORT, TransportErrorCode.SOCKET_READ_FAILURE};
    }
    if (*bytes_read == 0) {
        return (Error){ErrorType.TRANSPORT, TransportErrorCode.CONNECTION_CLOSED};
    }
    return (Error){ErrorType.NONE, 0};
}

static Error unix_transport_close(void* context) {
    UnixClient* self = (UnixClient*)context;
    if (self->fd > 0) {
        int result = self->syscalls->close(self->fd);
        self->fd = 0;
        if (result == -1) {
            return (Error){ErrorType.TRANSPORT, TransportErrorCode.SOCKET_CLOSE_FAILURE};
        }
    }
    return (Error){ErrorType.NONE, 0};
}

TransportInterface* unix_transport_new(const HttpcSyscalls* syscalls_override) {
    if (!default_syscalls_initialized) {
        httpc_syscalls_init_default(&DEFAULT_SYSCALLS);
        default_syscalls_initialized = 1;
    }

    if (!syscalls_override)
    {
        syscalls_override = &DEFAULT_SYSCALLS;
    }

    UnixClient* self = syscalls_override->malloc(sizeof(UnixClient));
    if (!self) {
        return nullptr;
    }
    syscalls_override->memset(self, 0, sizeof(UnixClient));
    self->syscalls = syscalls_override;

    self->interface.context = self;
    self->interface.connect = unix_transport_connect;
    self->interface.write = unix_transport_write;
    self->interface.read = unix_transport_read;
    self->interface.close = unix_transport_close;

    return &self->interface;
}

void unix_transport_destroy(TransportInterface* transport) {
    if (!transport) {
        return;
    }
    UnixClient* self = (UnixClient*)transport;
    self->syscalls->free(self);
}