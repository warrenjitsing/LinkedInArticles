add_executable(httpc_tests
        test_main.cpp
        c/test_syscalls.cpp
        c/test_tcp_transport.cpp
        c/test_unix_transport.cpp
        c/test_http1_protocol.cpp
        c/test_httpc.cpp
)

add_executable(httpcpp_tests
        test_main.cpp
        cpp/test_tcp_transport.cpp
        cpp/test_unix_transport.cpp
)

target_link_libraries(httpc_tests PRIVATE
        httpc_lib
        GTest::gmock
        GTest::gtest_main
)

target_link_libraries(httpcpp_tests PRIVATE
        httpcpp_lib
        GTest::gmock
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(httpc_tests)
gtest_discover_tests(httpcpp_tests)
