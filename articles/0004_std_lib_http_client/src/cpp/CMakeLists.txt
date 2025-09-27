add_library(
        httpcpp_lib
        SHARED
        tcp_transport.cpp
        unix_transport.cpp
        http1_protocol.cpp
        httpcpp.cpp
)

target_include_directories(httpcpp_lib
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

