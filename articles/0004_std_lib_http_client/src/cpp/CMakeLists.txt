add_library(
        httpcpp_lib
        SHARED
        tcp_transport.cpp
        unix_transport.cpp
)

target_include_directories(httpcpp_lib
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)


add_executable(
        httpcpp
        main.cpp
)

target_link_libraries(
        httpcpp
        PRIVATE
        httpcpp_lib
)

