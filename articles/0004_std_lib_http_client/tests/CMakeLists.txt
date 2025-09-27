include(GoogleTest)

# --- C Language Tests ---
add_executable(httpc_tests
        test_main.cpp
        c/test_syscalls.cpp
        c/test_tcp_transport.cpp
        c/test_unix_transport.cpp
        c/test_http1_protocol.cpp
        c/test_httpc.cpp
)

target_link_libraries(httpc_tests PRIVATE
        httpc_lib
        GTest::gmock
        GTest::gtest_main
)
gtest_discover_tests(httpc_tests)


# Compiling separately because of experimental::net linkage bug

# --- C++ Transport Tests ---
add_executable(httpcpp_transport_tests
        test_main.cpp
        cpp/test_tcp_transport.cpp
        cpp/test_unix_transport.cpp
)

target_link_libraries(httpcpp_transport_tests PRIVATE
        httpcpp_lib
        GTest::gmock
        GTest::gtest_main
)
gtest_discover_tests(httpcpp_transport_tests)


# --- C++ Protocol Tests ---
add_executable(httpcpp_protocol_tests
        test_main.cpp
        cpp/test_http1_protocol.cpp
)

target_link_libraries(httpcpp_protocol_tests PRIVATE
        httpcpp_lib
        GTest::gmock
        GTest::gtest_main
)
gtest_discover_tests(httpcpp_protocol_tests)

# --- C++ Client Tests ---

add_executable(httpcpp_client_tests
        test_main.cpp
        cpp/test_httpcpp.cpp
)

target_link_libraries(httpcpp_client_tests PRIVATE
        httpcpp_lib
        GTest::gmock
        GTest::gtest_main
)
gtest_discover_tests(httpcpp_client_tests)