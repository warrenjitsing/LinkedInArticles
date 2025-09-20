add_library(
        httpc_lib
        lib.c
        syscalls.c
        tcp_transport.c
        unix_transport.c
        http1_protocol.c
)

target_include_directories(httpc_lib
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

add_executable(
        httpc
        main.c
)

target_link_libraries(
        httpc
        PRIVATE
        httpc_lib
)