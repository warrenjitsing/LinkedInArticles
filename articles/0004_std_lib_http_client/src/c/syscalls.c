#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <httpc/syscalls.h>


void httpc_syscalls_init_default(HttpcSyscalls* syscalls) {
    if (!syscalls) {
        return;
    }

    syscalls->getaddrinfo = getaddrinfo;
    syscalls->freeaddrinfo = freeaddrinfo;
    syscalls->socket = socket;
    syscalls->setsockopt = setsockopt;
    syscalls->connect = connect;
    syscalls->write = write;
    syscalls->read = read;
    syscalls->close = close;

    syscalls->malloc = malloc;
    syscalls->realloc = realloc;
    syscalls->free = free;
    syscalls->memset = memset;
    syscalls->memcpy = memcpy;

    syscalls->strchr = strchr;
    syscalls->strncpy = strncpy;
    syscalls->strlen = strlen;
    syscalls->strstr = strstr;
    syscalls->strtok_r = strtok_r;
    syscalls->snprintf = snprintf;
    syscalls->strcasecmp = strcasecmp;
    syscalls->atoi = atoi;
    syscalls->sscanf = sscanf;
}