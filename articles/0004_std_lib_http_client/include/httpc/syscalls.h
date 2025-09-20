#pragma once

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

typedef struct {
    // Network Syscalls
    int (*getaddrinfo)(const char* node, const char* service,
                       const struct addrinfo* hints,
                       struct addrinfo** res);
    void (*freeaddrinfo)(struct addrinfo* res);
    int (*socket)(int domain, int type, int protocol);
    int (*setsockopt) (int fd, int level, int optname, const void *optval, socklen_t optlen);
    int (*connect)(int sockfd, const struct sockaddr* addr, socklen_t addrlen);
    ssize_t (*write)(int fd, const void* buf, size_t count);
    ssize_t (*read)(int fd, void* buf, size_t count);
    int (*close)(int fd);

    // Memory Syscalls
    void* (*malloc)(size_t size);
    void* (*realloc)(void* ptr, size_t size);
    void (*free)(void* ptr);
    void* (*memset)(void* s, int c, size_t n);
    void* (*memcpy)(void* dest, const void* src, size_t n);

    // String Syscalls
    char* (*strchr)(const char* s, int c);
    char* (*strncpy) (char* d, const char* s, size_t n);
    size_t (*strlen)(const char* s);
    char* (*strstr)(const char* haystack, const char* needle);
    char* (*strtok_r)(char* str, const char* delim, char** saveptr);
    int (*snprintf)(char* str, size_t size, const char* format, ...);
    int (*strcasecmp)(const char* s1, const char* s2);
    int (*atoi)(const char* nptr);
    int (*sscanf) (const char* s, const char* format, ...);

} HttpcSyscalls;

void httpc_syscalls_init_default(HttpcSyscalls* syscalls);
static int default_syscalls_initialized = 0;
static HttpcSyscalls DEFAULT_SYSCALLS;