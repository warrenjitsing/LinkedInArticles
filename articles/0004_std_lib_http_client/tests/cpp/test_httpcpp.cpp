#include <gtest/gtest.h>
#include <httpcpp/httpcpp.hpp>

using namespace httpcpp;


TEST(a, b)
{
    HttpClient<Http1Protocol<TcpTransport>> client;
}
