#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <unistd.h>

#include <thread>
#include <atomic>
#include <future>
#include <functional>
#include <sstream>

#include <gtest/gtest.h>

#include <httpcpp/http1_protocol.hpp>
#include <httpcpp/tcp_transport.hpp>
#include <httpcpp/unix_transport.hpp>

using TransportTypes = ::testing::Types<httpcpp::TcpTransport, httpcpp::UnixTransport>;

template <typename T>
class Http1ProtocolIntegrationTest : public ::testing::Test {
protected:
    using TransportType = T;

    void TearDown() override {
        StopServer();
    }

    void StartServer(std::function<void(int)> server_logic) {
        server_logic_ = std::move(server_logic);

        if constexpr (std::is_same_v<TransportType, httpcpp::TcpTransport>) {
            SetupTcpServer();
        } else if constexpr (std::is_same_v<TransportType, httpcpp::UnixTransport>) {
            SetupUnixServer();
        }

        server_thread_ = std::thread(&Http1ProtocolIntegrationTest::AcceptLoop, this);
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
                if constexpr (std::is_same_v<TransportType, httpcpp::UnixTransport>) {
                    unlink(socket_path_.c_str());
                }
            }
        }
    }

    uint16_t port_ = 0;
    std::string socket_path_;
    httpcpp::Http1Protocol<TransportType> protocol_;
    std::string captured_request_;

private:
    void SetupTcpServer() {
        listener_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_NE(listener_fd_, -1);

        sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        serv_addr.sin_port = 0; // Bind to any available port
        ASSERT_EQ(bind(listener_fd_, (struct sockaddr*)&serv_addr, sizeof(serv_addr)), 0);
        ASSERT_EQ(listen(listener_fd_, 1), 0);

        socklen_t len = sizeof(serv_addr);
        ASSERT_EQ(getsockname(listener_fd_, (struct sockaddr*)&serv_addr, &len), 0);
        port_ = ntohs(serv_addr.sin_port);
    }

    void SetupUnixServer() {
        socket_path_ = "/tmp/httpcpp_integ_test_" + std::to_string(getpid());
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

        // Run the test-specific logic
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

TYPED_TEST_SUITE(Http1ProtocolIntegrationTest, TransportTypes);

TYPED_TEST(Http1ProtocolIntegrationTest, ConnectAndDisconnectSucceed) {
    this->StartServer([](int){});

    std::expected<void, httpcpp::Error> connect_result;
    if constexpr (std::is_same_v<typename TestFixture::TransportType, httpcpp::TcpTransport>) {
        connect_result = this->protocol_.connect("127.0.0.1", this->port_);
    } else {
        connect_result = this->protocol_.connect(this->socket_path_.c_str(), 0);
    }

    ASSERT_TRUE(connect_result.has_value());

    auto disconnect_result = this->protocol_.disconnect();
    ASSERT_TRUE(disconnect_result.has_value());
}

TYPED_TEST(Http1ProtocolIntegrationTest, PerformRequestFailsIfNotConnected) {
    httpcpp::HttpRequest req{};

    auto result = this->protocol_.perform_request_unsafe(req);

    ASSERT_FALSE(result.has_value());

    auto* err_ptr = std::get_if<httpcpp::TransportError>(&result.error());
    ASSERT_NE(err_ptr, nullptr);
    ASSERT_EQ(*err_ptr, httpcpp::TransportError::SocketWriteFailure);
}

TYPED_TEST(Http1ProtocolIntegrationTest, CorrectlySerializesGetRequest) {
    std::promise<void> server_read_promise;
    auto server_read_future = server_read_promise.get_future();

    this->StartServer([this, &server_read_promise](int client_fd) {
        std::vector<char> buffer(1024, 0);
        ssize_t bytes_read = read(client_fd, buffer.data(), buffer.size() - 1);
        if (bytes_read > 0) {
            this->captured_request_.assign(buffer.data(), bytes_read);
        }
        server_read_promise.set_value();
    });

    if constexpr (std::is_same_v<typename TestFixture::TransportType, httpcpp::TcpTransport>) {
        (void)this->protocol_.connect("127.0.0.1", this->port_);
    } else {
        (void)this->protocol_.connect(this->socket_path_.c_str(), 0);
    }

    httpcpp::HttpRequest req{};
    req.method = httpcpp::HttpMethod::Get;
    req.path = "/test";
    req.headers.emplace_back("Host", "example.com");
    (void)this->protocol_.perform_request_unsafe(req);

    server_read_future.wait();

    const std::string expected_request =
        "GET /test HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "\r\n";

    ASSERT_EQ(this->captured_request_, expected_request);
}

TYPED_TEST(Http1ProtocolIntegrationTest, CorrectlySerializesPostRequest) {
    std::promise<void> server_read_promise;
    auto server_read_future = server_read_promise.get_future();

    this->StartServer([this, &server_read_promise](int client_fd) {
        std::vector<char> buffer(1024, 0);
        ssize_t bytes_read = read(client_fd, buffer.data(), buffer.size() - 1);
        if (bytes_read > 0) {
            this->captured_request_.assign(buffer.data(), bytes_read);
        }
        server_read_promise.set_value();
    });

    if constexpr (std::is_same_v<typename TestFixture::TransportType, httpcpp::TcpTransport>) {
        (void)this->protocol_.connect("127.0.0.1", this->port_);
    } else {
        (void)this->protocol_.connect(this->socket_path_.c_str(), 0);
    }

    const std::string body_content = "key=value";
    std::vector<std::byte> body_data(body_content.size());
    std::transform(body_content.begin(), body_content.end(), body_data.begin(), [](char c){
        return std::byte(c);
    });

    httpcpp::HttpRequest req{};
    req.method = httpcpp::HttpMethod::Post;
    req.path = "/api/submit";
    req.headers.emplace_back("Host", "test-server");
    req.headers.emplace_back("Content-Length", std::to_string(body_content.size()));
    req.body = body_data;

    (void)this->protocol_.perform_request_unsafe(req);
    server_read_future.wait();

    const std::string expected_request =
        "POST /api/submit HTTP/1.1\r\n"
        "Host: test-server\r\n"
        "Content-Length: 9\r\n"
        "\r\n"
        "key=value";

    ASSERT_EQ(this->captured_request_, expected_request);
}


TYPED_TEST(Http1ProtocolIntegrationTest, SuccessfullyParsesResponseWithContentLength) {
    const std::string canned_response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 12\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "Hello Client";

    this->StartServer([&canned_response](int client_fd) {
        char buffer[1024];
        read(client_fd, buffer, sizeof(buffer));
        write(client_fd, canned_response.c_str(), canned_response.length());
    });

    if constexpr (std::is_same_v<typename TestFixture::TransportType, httpcpp::TcpTransport>) {
        (void)this->protocol_.connect("127.0.0.1", this->port_);
    } else {
        (void)this->protocol_.connect(this->socket_path_.c_str(), 0);
    }

    httpcpp::HttpRequest req{};

    auto result = this->protocol_.perform_request_unsafe(req);
    if (!result.has_value()) {
        std::string err_msg = "Test failed with an unknown error.";
        // Check if the error is a TransportError or HttpClientError and format a message.
        if (auto* err_ptr = std::get_if<httpcpp::TransportError>(&result.error())) {
            err_msg = "Test failed with TransportError, code: " + std::to_string(static_cast<int>(*err_ptr));
        } else if (auto* err_ptr = std::get_if<httpcpp::HttpClientError>(&result.error())) {
            err_msg = "Test failed with HttpClientError, code: " + std::to_string(static_cast<int>(*err_ptr));
        }
        FAIL() << err_msg; // This makes gtest fail with our custom message.
    }
    ASSERT_TRUE(result.has_value());
    const auto& res = *result;

    ASSERT_EQ(res.status_code, 200);
    ASSERT_EQ(res.status_message, "OK");
    ASSERT_EQ(res.headers.size(), 2);
    ASSERT_EQ(res.headers[0].first, "Content-Length");
    ASSERT_EQ(res.headers[0].second, "12");
    ASSERT_EQ(res.headers[1].first, "Content-Type");
    ASSERT_EQ(res.headers[1].second, "text/plain");

    std::string body_str(reinterpret_cast<const char*>(res.body.data()), res.body.size());
    ASSERT_EQ(body_str, "Hello Client");
}


TYPED_TEST(Http1ProtocolIntegrationTest, SuccessfullyReadsBodyOnConnectionClose) {
    const std::string canned_response =
        "HTTP/1.1 200 OK\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Full body.";

    this->StartServer([&canned_response](int client_fd) {
        char buffer[1024];
        read(client_fd, buffer, sizeof(buffer));
        write(client_fd, canned_response.c_str(), canned_response.length());
    });

    if constexpr (std::is_same_v<typename TestFixture::TransportType, httpcpp::TcpTransport>) {
        (void)this->protocol_.connect("127.0.0.1", this->port_);
    } else {
        (void)this->protocol_.connect(this->socket_path_.c_str(), 0);
    }

    httpcpp::HttpRequest req{};
    auto result = this->protocol_.perform_request_unsafe(req);

    ASSERT_TRUE(result.has_value());
    const auto& res = *result;

    ASSERT_EQ(res.status_code, 200);
    std::string body_str(reinterpret_cast<const char*>(res.body.data()), res.body.size());
    ASSERT_EQ(body_str, "Full body.");

    ASSERT_FALSE(this->protocol_.get_content_length_for_test().has_value());
}

TYPED_TEST(Http1ProtocolIntegrationTest, CorrectlyParsesComplexStatusLineAndHeaders) {
    const std::string canned_response =
        "HTTP/1.1 404 Not Found\r\n"
        "Connection: close\r\n"
        "Content-Type: application/json\r\n"
        "X-Request-ID: abc-123\r\n"
        "Content-Length: 21\r\n"
        "\r\n"
        "{\"error\":\"not found\"}";

    this->StartServer([&canned_response](int client_fd) {
        char buffer[1024];
        read(client_fd, buffer, sizeof(buffer));
        write(client_fd, canned_response.c_str(), canned_response.length());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });

    if constexpr (std::is_same_v<typename TestFixture::TransportType, httpcpp::TcpTransport>) {
        (void)this->protocol_.connect("127.0.0.1", this->port_);
    } else {
        (void)this->protocol_.connect(this->socket_path_.c_str(), 0);
    }

    httpcpp::HttpRequest req{};
    auto result = this->protocol_.perform_request_unsafe(req);

    ASSERT_TRUE(result.has_value());
    const auto& res = *result;

    ASSERT_EQ(res.status_code, 404);
    ASSERT_EQ(res.status_message, "Not Found");

    ASSERT_EQ(res.headers.size(), 4);
    ASSERT_EQ(res.headers[0].first, "Connection");
    ASSERT_EQ(res.headers[0].second, "close");
    ASSERT_EQ(res.headers[1].first, "Content-Type");
    ASSERT_EQ(res.headers[1].second, "application/json");
    ASSERT_EQ(res.headers[2].first, "X-Request-ID");
    ASSERT_EQ(res.headers[2].second, "abc-123");
    ASSERT_EQ(res.headers[3].first, "Content-Length");
    ASSERT_EQ(res.headers[3].second, "21");

    std::string body_str(reinterpret_cast<const char*>(res.body.data()), res.body.size());
    ASSERT_EQ(body_str, "{\"error\":\"not found\"}");
}


TYPED_TEST(Http1ProtocolIntegrationTest, HandlesZeroContentLengthResponse) {
    const std::string canned_response =
        "HTTP/1.1 204 No Content\r\n"
        "Connection: close\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    this->StartServer([&canned_response](int client_fd) {
        char buffer[1024];
        read(client_fd, buffer, sizeof(buffer));
        write(client_fd, canned_response.c_str(), canned_response.length());
    });

    if constexpr (std::is_same_v<typename TestFixture::TransportType, httpcpp::TcpTransport>) {
        (void)this->protocol_.connect("127.0.0.1", this->port_);
    } else {
        (void)this->protocol_.connect(this->socket_path_.c_str(), 0);
    }

    httpcpp::HttpRequest req{};
    auto result = this->protocol_.perform_request_unsafe(req);

    ASSERT_TRUE(result.has_value());
    const auto& res = *result;

    ASSERT_EQ(res.status_code, 204);
    ASSERT_EQ(res.headers.size(), 2);
    ASSERT_EQ(res.headers[1].first, "Content-Length");
    ASSERT_EQ(res.headers[1].second, "0");

    ASSERT_TRUE(res.body.empty());
}

TYPED_TEST(Http1ProtocolIntegrationTest, HandlesResponseLargerThanInitialBuffer) {
    const std::string large_body(2000, 'a');

    std::ostringstream oss;
    oss << "HTTP/1.1 200 OK\r\n"
        << "Content-Length: " << large_body.size() << "\r\n"
        << "\r\n"
        << large_body;
    const std::string canned_response = oss.str();

    this->StartServer([&canned_response](int client_fd) {
        char buffer[1024];
        read(client_fd, buffer, sizeof(buffer));

        const char* response_ptr = canned_response.c_str();
        size_t bytes_remaining = canned_response.length();
        while (bytes_remaining > 0) {
            ssize_t bytes_sent = write(client_fd, response_ptr, bytes_remaining);
            if (bytes_sent <= 0) {
                break;
            }
            bytes_remaining -= bytes_sent;
            response_ptr += bytes_sent;
        }
    });

    if constexpr (std::is_same_v<typename TestFixture::TransportType, httpcpp::TcpTransport>) {
        (void)this->protocol_.connect("127.0.0.1", this->port_);
    } else {
        (void)this->protocol_.connect(this->socket_path_.c_str(), 0);
    }

    httpcpp::HttpRequest req{};
    auto result = this->protocol_.perform_request_unsafe(req);

    ASSERT_TRUE(result.has_value());
    const auto& res = *result;

    ASSERT_EQ(res.status_code, 200);
    ASSERT_EQ(res.body.size(), large_body.size());

    std::string body_str(reinterpret_cast<const char*>(res.body.data()), res.body.size());
    ASSERT_EQ(body_str, large_body);
}

TYPED_TEST(Http1ProtocolIntegrationTest, FailsGracefullyOnBadContentLength) {
    const std::string response_body = "short body";

    std::ostringstream oss;
    oss << "HTTP/1.1 200 OK\r\n"
        << "Content-Length: 100\r\n" // Lie about the content length
        << "\r\n"
        << response_body;
    const std::string canned_response = oss.str();

    this->StartServer([&canned_response](int client_fd) {
        char buffer[1024];
        read(client_fd, buffer, sizeof(buffer));
        write(client_fd, canned_response.c_str(), canned_response.length());
    });

    if constexpr (std::is_same_v<typename TestFixture::TransportType, httpcpp::TcpTransport>) {
        (void)this->protocol_.connect("127.0.0.1", this->port_);
    } else {
        (void)this->protocol_.connect(this->socket_path_.c_str(), 0);
    }

    httpcpp::HttpRequest req{};
    auto result = this->protocol_.perform_request_unsafe(req);

    ASSERT_FALSE(result.has_value());
    auto* err_ptr = std::get_if<httpcpp::HttpClientError>(&result.error());
    ASSERT_NE(err_ptr, nullptr);
    ASSERT_EQ(*err_ptr, httpcpp::HttpClientError::HttpParseFailure);
}

TYPED_TEST(Http1ProtocolIntegrationTest, SafeRequestReturnsOwningDeepCopy) {
    const std::string canned_response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 11\r\n"
        "\r\n"
        "Safe Buffer";

    this->StartServer([&canned_response](int client_fd) {
        char buffer[1024];
        read(client_fd, buffer, sizeof(buffer));
        write(client_fd, canned_response.c_str(), canned_response.length());
    });

    if constexpr (std::is_same_v<typename TestFixture::TransportType, httpcpp::TcpTransport>) {
        (void)this->protocol_.connect("127.0.0.1", this->port_);
    } else {
        (void)this->protocol_.connect(this->socket_path_.c_str(), 0);
    }

    httpcpp::HttpRequest req{};
    auto result = this->protocol_.perform_request_safe(req);

    ASSERT_TRUE(result.has_value());
    const auto& res = *result;

    ASSERT_EQ(res.status_code, 200);
    ASSERT_EQ(this->protocol_.get_content_length_for_test(), 11);
    ASSERT_EQ(res.headers.size(), 1);
    ASSERT_EQ(res.headers[0].first, "Content-Length");
    ASSERT_EQ(res.headers[0].second, "11");
    std::string body_str(reinterpret_cast<const char*>(res.body.data()), res.body.size());
    ASSERT_EQ(body_str, "Safe Buffer");

    // The critical assertion:
    // Verify the response body's memory address is DIFFERENT from the protocol's internal buffer.
    ASSERT_NE(
        reinterpret_cast<const void*>(res.body.data()),
        reinterpret_cast<const void*>(this->protocol_.get_internal_buffer_ptr_for_test())
    );
}
