#pragma once

#include <sys/types.h>
#include <stddef.h>
#include <httpc/error.h>
#include <sys/uio.h>

typedef struct {
    void* context;
    Error (*connect)(void* context, const char* host, int port);
    Error (*write)(void* context, const void* buffer, size_t len, ssize_t* bytes_written);
    Error (*writev)(void* context, const struct iovec* iov, int iovcnt, ssize_t* bytes_written);
    Error (*read)(void* context, void* buffer, size_t len, ssize_t* bytes_read);
    Error (*close)(void* context);
    void (*destroy)(void* context);
} TransportInterface;