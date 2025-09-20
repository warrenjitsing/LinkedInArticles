#include <gtest/gtest.h>

extern "C" {
#include <httpc/syscalls.h>
}

#include <gtest/gtest.h>

extern "C" {
#include <httpc/syscalls.h>
}

TEST(Syscalls, DefaultInitialization) {
    HttpcSyscalls syscalls;
    httpc_syscalls_init_default(&syscalls);

    ASSERT_EQ(syscalls.getaddrinfo, getaddrinfo);
    ASSERT_EQ(syscalls.freeaddrinfo, freeaddrinfo);
    ASSERT_EQ(syscalls.socket, socket);
    ASSERT_EQ(syscalls.setsockopt, setsockopt);
    ASSERT_EQ(syscalls.connect, connect);
    ASSERT_EQ(syscalls.write, write);
    ASSERT_EQ(syscalls.read, read);
    ASSERT_EQ(syscalls.close, close);

    ASSERT_EQ(syscalls.malloc, malloc);
    ASSERT_EQ(syscalls.realloc, realloc);
    ASSERT_EQ(syscalls.free, free);
    ASSERT_EQ(syscalls.memset, memset);
    ASSERT_EQ(syscalls.memcpy, memcpy);

    // Due to difference in C/C++ definitions
    ASSERT_NE(syscalls.strchr, nullptr);
    ASSERT_NE(syscalls.strstr, nullptr);

    ASSERT_EQ(syscalls.strlen, strlen);
    ASSERT_EQ(syscalls.strncpy, strncpy);
    ASSERT_EQ(syscalls.strtok_r, strtok_r);
    ASSERT_EQ(syscalls.snprintf, snprintf);
    ASSERT_EQ(syscalls.strcasecmp, strcasecmp);
    ASSERT_EQ(syscalls.atoi, atoi);
    ASSERT_EQ(syscalls.sscanf, sscanf);
}