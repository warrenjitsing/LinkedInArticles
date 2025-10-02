find_package(Boost REQUIRED COMPONENTS system thread program_options)
find_package(CURL REQUIRED)

add_executable(data_generator data_generator/main.cpp)
target_link_libraries(data_generator PRIVATE Boost::program_options)

add_executable(benchmark_server server/main.cpp)
target_link_libraries(benchmark_server PRIVATE Boost::system Boost::thread Boost::program_options)


add_executable(httpc_client clients/c/httpc_client.c)
target_link_libraries(httpc_client PRIVATE httpc_lib)
add_executable(libcurl_client clients/c/libcurl_client.c)
target_link_libraries(libcurl_client PRIVATE CURL::libcurl)


add_executable(httpcpp_client clients/cpp/httpcpp_client.cpp)
target_link_libraries(httpcpp_client PRIVATE httpcpp_lib Boost::program_options)
add_executable(boost_client clients/cpp/boost_client.cpp)
target_link_libraries(boost_client PRIVATE Boost::system Boost::thread Boost::program_options)