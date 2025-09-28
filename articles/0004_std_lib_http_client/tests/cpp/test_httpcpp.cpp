#include <gtest/gtest.h>
#include <httpcpp/httpcpp.hpp>

#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <string>

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <unistd.h>
#include <csignal>

using namespace httpcpp;

using TransportTypes = ::testing::Types<TcpTransport, UnixTransport>;

template <typename T>
class HttpClientIntegrationTest : public ::testing::Test {
protected:
    using TransportType = T;

    void SetUp() override {
        signal(SIGPIPE, SIG_IGN);
    }

    void TearDown() override {
        StopServer();
    }

    void StartServer(std::function<void(int)> server_logic) {
        server_logic_ = std::move(server_logic);

        if constexpr (std::is_same_v<TransportType, TcpTransport>) {
            SetupTcpServer();
        } else if constexpr (std::is_same_v<TransportType, UnixTransport>) {
            SetupUnixServer();
        }

        server_thread_ = std::thread(&HttpClientIntegrationTest::AcceptLoop, this);
    }

    void StopServer() {
        if (!should_stop_.exchange(true)) {
            if (listener_fd_ != -1) {
                shutdown(listener_fd_, SHUT_RDWR);
            }
            if (server_thread_.joinable()) {
                server_thread_.join();
            }
            if (listener_fd_ != -1) {
                close(listener_fd_);
                if constexpr (std::is_same_v<TransportType, UnixTransport>) {
                    if (!socket_path_.empty()) {
                        unlink(socket_path_.c_str());
                    }
                }
            }
        }
    }

    uint16_t port_ = 0;
    std::string socket_path_;

private:
    void SetupTcpServer() {
        listener_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_NE(listener_fd_, -1);

        sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        serv_addr.sin_port = 0;
        ASSERT_EQ(bind(listener_fd_, (struct sockaddr*)&serv_addr, sizeof(serv_addr)), 0);
        ASSERT_EQ(listen(listener_fd_, 1), 0);

        socklen_t len = sizeof(serv_addr);
        ASSERT_EQ(getsockname(listener_fd_, (struct sockaddr*)&serv_addr, &len), 0);
        port_ = ntohs(serv_addr.sin_port);
    }

    void SetupUnixServer() {
        socket_path_ = "/tmp/httpcpp_client_test_" + std::to_string(getpid());
        unlink(socket_path_.c_str());

        listener_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        ASSERT_NE(listener_fd_, -1);

        sockaddr_un serv_addr{};
        serv_addr.sun_family = AF_UNIX;
        strncpy(serv_addr.sun_path, socket_path_.c_str(), sizeof(serv_addr.sun_path) - 1);

        ASSERT_EQ(bind(listener_fd_, (struct sockaddr*)&serv_addr, sizeof(serv_addr)), 0);
        ASSERT_EQ(listen(listener_fd_, 1), 0);
    }

    void AcceptLoop() {
        int client_fd = accept(listener_fd_, nullptr, nullptr);
        if (client_fd < 0) return;

        if (server_logic_) {
            server_logic_(client_fd);
        }

        close(client_fd);
    }

    std::thread server_thread_;
    std::atomic<bool> should_stop_{false};
    int listener_fd_ = -1;
    std::function<void(int)> server_logic_;
};

TYPED_TEST_SUITE(HttpClientIntegrationTest, TransportTypes);

TYPED_TEST(HttpClientIntegrationTest, GetRequestSafeSucceeds) {
    const std::string canned_response = "HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\nsuccess";
    std::string captured_request;

    this->StartServer([&canned_response, &captured_request](int client_fd) {
        std::vector<char> buffer(1024, 0);
        ssize_t bytes_read = read(client_fd, buffer.data(), buffer.size() - 1);
        if (bytes_read > 0) {
            captured_request.assign(buffer.data(), bytes_read);
        }
        write(client_fd, canned_response.c_str(), canned_response.length());
    });

    HttpClient<Http1Protocol<typename TestFixture::TransportType>> client;

    if constexpr (std::is_same_v<typename TestFixture::TransportType, TcpTransport>) {
        auto connect_result = client.connect("127.0.0.1", this->port_);
        ASSERT_TRUE(connect_result.has_value());
    } else {
        auto connect_result = client.connect(this->socket_path_.c_str(), 0);
        ASSERT_TRUE(connect_result.has_value());
    }

    HttpRequest request{};
    request.path = "/test";

    auto result = client.get_safe(request);
    ASSERT_TRUE(result.has_value());

    const auto& res = *result;
    ASSERT_EQ(res.status_code, 200);

    std::string body_str(reinterpret_cast<const char*>(res.body.data()), res.body.size());
    ASSERT_EQ(body_str, "success");
    ASSERT_NE(captured_request.find("GET /test HTTP/1.1"), std::string::npos);

    (void)client.disconnect();
}

TYPED_TEST(HttpClientIntegrationTest, GetRequestUnsafeSucceeds) {
    const std::string canned_response = "HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\nsuccess";
    std::string captured_request;

    this->StartServer([&canned_response, &captured_request](int client_fd) {
        std::vector<char> buffer(1024, 0);
        ssize_t bytes_read = read(client_fd, buffer.data(), buffer.size() - 1);
        if (bytes_read > 0) {
            captured_request.assign(buffer.data(), bytes_read);
        }
        write(client_fd, canned_response.c_str(), canned_response.length());
    });

    HttpClient<Http1Protocol<typename TestFixture::TransportType>> client;

    if constexpr (std::is_same_v<typename TestFixture::TransportType, TcpTransport>) {
        auto connect_result = client.connect("127.0.0.1", this->port_);
        ASSERT_TRUE(connect_result.has_value());
    } else {
        auto connect_result = client.connect(this->socket_path_.c_str(), 0);
        ASSERT_TRUE(connect_result.has_value());
    }

    HttpRequest request{};
    request.path = "/test";

    auto result = client.get_unsafe(request);
    ASSERT_TRUE(result.has_value());

    const auto& res = *result;
    ASSERT_EQ(res.status_code, 200);

    std::string body_str(reinterpret_cast<const char*>(res.body.data()), res.body.size());
    ASSERT_EQ(body_str, "success");
    ASSERT_NE(captured_request.find("GET /test HTTP/1.1"), std::string::npos);

    (void)client.disconnect();
}

TYPED_TEST(HttpClientIntegrationTest, PostRequestSafeSucceeds) {
    const std::string canned_response = "HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\nsuccess";
    std::string captured_request;

    this->StartServer([&canned_response, &captured_request](int client_fd) {
        std::vector<char> buffer(1024, 0);
        ssize_t bytes_read = read(client_fd, buffer.data(), buffer.size() - 1);
        if (bytes_read > 0) {
            captured_request.assign(buffer.data(), bytes_read);
        }
        write(client_fd, canned_response.c_str(), canned_response.length());
    });

    HttpClient<Http1Protocol<typename TestFixture::TransportType>> client;

    if constexpr (std::is_same_v<typename TestFixture::TransportType, TcpTransport>) {
        (void)client.connect("127.0.0.1", this->port_);
    } else {
        (void)client.connect(this->socket_path_.c_str(), 0);
    }

    const std::string body_content = "key=value";
    std::vector<std::byte> body_data;
    body_data.resize(body_content.size());
    std::transform(body_content.begin(), body_content.end(), body_data.begin(), [](char c) {
        return std::byte(c);
    });

    HttpRequest request{};
    request.path = "/submit";
    request.body = body_data;
    const std::string content_length_str = std::to_string(body_content.size());
    request.headers.emplace_back("Content-Length", content_length_str);

    auto result = client.post_safe(request);
    ASSERT_TRUE(result.has_value());

    const auto& res = *result;
    ASSERT_EQ(res.status_code, 200);

    std::string body_str(reinterpret_cast<const char*>(res.body.data()), res.body.size());
    ASSERT_EQ(body_str, "success");
    ASSERT_NE(captured_request.find("POST /submit HTTP/1.1"), std::string::npos);
    ASSERT_NE(captured_request.find("\r\n\r\nkey=value"), std::string::npos);

    (void)client.disconnect();
}

TYPED_TEST(HttpClientIntegrationTest, PostRequestUnsafeSucceeds) {
    const std::string canned_response = "HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\nsuccess";
    std::string captured_request;

    this->StartServer([&canned_response, &captured_request](int client_fd) {
        std::vector<char> buffer(1024, 0);
        ssize_t bytes_read = read(client_fd, buffer.data(), buffer.size() - 1);
        if (bytes_read > 0) {
            captured_request.assign(buffer.data(), bytes_read);
        }
        write(client_fd, canned_response.c_str(), canned_response.length());
    });

    HttpClient<Http1Protocol<typename TestFixture::TransportType>> client;

    if constexpr (std::is_same_v<typename TestFixture::TransportType, TcpTransport>) {
        (void)client.connect("127.0.0.1", this->port_);
    } else {
        (void)client.connect(this->socket_path_.c_str(), 0);
    }

    const std::string body_content = "key=value";
    std::vector<std::byte> body_data;
    body_data.resize(body_content.size());
    std::transform(body_content.begin(), body_content.end(), body_data.begin(), [](char c) {
        return std::byte(c);
    });

    HttpRequest request{};
    request.path = "/submit";
    request.body = body_data;
    const std::string content_length_str = std::to_string(body_content.size());
    request.headers.emplace_back("Content-Length", content_length_str);

    auto result = client.post_unsafe(request);
    ASSERT_TRUE(result.has_value());

    const auto& res = *result;
    ASSERT_EQ(res.status_code, 200);

    std::string body_str(reinterpret_cast<const char*>(res.body.data()), res.body.size());
    ASSERT_EQ(body_str, "success");
    ASSERT_NE(captured_request.find("POST /submit HTTP/1.1"), std::string::npos);
    ASSERT_NE(captured_request.find("\r\n\r\nkey=value"), std::string::npos);

    (void)client.disconnect();
}

TYPED_TEST(HttpClientIntegrationTest, GetRequestWithBodyReturnsError) {
    HttpClient<Http1Protocol<typename TestFixture::TransportType>> client;

    const std::string body_content = "this body is not allowed";
    std::vector<std::byte> body_data;
    body_data.resize(body_content.size());
    std::transform(body_content.begin(), body_content.end(), body_data.begin(), [](char c) {
        return std::byte(c);
    });

    HttpRequest request{};
    request.path = "/test";
    request.body = body_data;

    auto result_safe = client.get_safe(request);
    ASSERT_FALSE(result_safe.has_value());
    ASSERT_EQ(std::get<HttpClientError>(result_safe.error()), HttpClientError::InvalidRequest);

    auto result_unsafe = client.get_unsafe(request);
    ASSERT_FALSE(result_unsafe.has_value());
    ASSERT_EQ(std::get<HttpClientError>(result_unsafe.error()), HttpClientError::InvalidRequest);
}

TYPED_TEST(HttpClientIntegrationTest, PostRequestWithoutBodyReturnsError) {
    HttpClient<Http1Protocol<typename TestFixture::TransportType>> client;

    HttpRequest request{};
    request.path = "/test";
    request.headers.emplace_back("Content-Length", "0");

    auto result_safe = client.post_safe(request);
    ASSERT_FALSE(result_safe.has_value());
    ASSERT_EQ(std::get<HttpClientError>(result_safe.error()), HttpClientError::InvalidRequest);

    auto result_unsafe = client.post_unsafe(request);
    ASSERT_FALSE(result_unsafe.has_value());
    ASSERT_EQ(std::get<HttpClientError>(result_unsafe.error()), HttpClientError::InvalidRequest);
}

TYPED_TEST(HttpClientIntegrationTest, PostRequestWithoutContentLengthReturnsError) {
    HttpClient<Http1Protocol<typename TestFixture::TransportType>> client;

    const std::string body_content = "some body";
    std::vector<std::byte> body_data;
    body_data.resize(body_content.size());
    std::transform(body_content.begin(), body_content.end(), body_data.begin(), [](char c) {
        return std::byte(c);
    });

    HttpRequest request{};
    request.path = "/test";
    request.body = body_data;

    auto result_safe = client.post_safe(request);
    ASSERT_FALSE(result_safe.has_value());
    ASSERT_EQ(std::get<HttpClientError>(result_safe.error()), HttpClientError::InvalidRequest);

    auto result_unsafe = client.post_unsafe(request);
    ASSERT_FALSE(result_unsafe.has_value());
    ASSERT_EQ(std::get<HttpClientError>(result_unsafe.error()), HttpClientError::InvalidRequest);
}