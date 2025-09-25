add_library(
        httpc_lib
        SHARED
        syscalls.c
        tcp_transport.c
        unix_transport.c
        http1_protocol.c
        http_protocol.c
        httpc.c
)

target_include_directories(httpc_lib
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)
