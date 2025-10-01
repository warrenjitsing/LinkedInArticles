#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <csignal>

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

extern "C" {
#include <httpc/httpc.h>
}

class HttpClientIntegrationTest : public ::testing::Test {
protected:
    std::thread server_thread;
    std::atomic<bool> should_stop{false};
    int server_fd = -1;

    std::string captured_request;
    const std::string canned_response = "HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\nsuccess";

    int tcp_port = 0;
    std::string unix_socket_path;

    void SetUp() override {
        signal(SIGPIPE, SIG_IGN);
    }

    void TearDown() override {
        StopServer();
    }

    void StartTcpServer() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_NE(server_fd, -1);

        sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        serv_addr.sin_port = 0;
        ASSERT_EQ(bind(server_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)), 0);

        socklen_t len = sizeof(serv_addr);
        ASSERT_EQ(getsockname(server_fd, (struct sockaddr*)&serv_addr, &len), 0);
        tcp_port = ntohs(serv_addr.sin_port);

        ASSERT_EQ(listen(server_fd, 1), 0);
        server_thread = std::thread(&HttpClientIntegrationTest::AcceptLoop, this);
    }

    void StartUnixServer() {
        unix_socket_path = "/tmp/httpc_integ_test_" + std::to_string(getpid());
        unlink(unix_socket_path.c_str());

        server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        ASSERT_NE(server_fd, -1);

        sockaddr_un serv_addr{};
        serv_addr.sun_family = AF_UNIX;
        strncpy(serv_addr.sun_path, unix_socket_path.c_str(), sizeof(serv_addr.sun_path) - 1);

        ASSERT_EQ(bind(server_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)), 0);
        ASSERT_EQ(listen(server_fd, 1), 0);
        server_thread = std::thread(&HttpClientIntegrationTest::AcceptLoop, this);
    }

    void StopServer() {
        if (!should_stop.exchange(true)) {
            shutdown(server_fd, SHUT_RDWR);
            if (server_thread.joinable()) {
                server_thread.join();
            }
            close(server_fd);
            if (!unix_socket_path.empty()) {
                unlink(unix_socket_path.c_str());
            }
        }
    }

private:
    void AcceptLoop() {
        while (!should_stop) {
            int client_fd = accept(server_fd, nullptr, nullptr);
            if (client_fd < 0) {
                continue;
            }

            std::vector<char> buffer(4096, 0);
            ssize_t bytes_read = read(client_fd, buffer.data(), buffer.size() - 1);
            if (bytes_read > 0) {
                captured_request.assign(buffer.data(), bytes_read);
            }

            write(client_fd, canned_response.c_str(), canned_response.length());
            close(client_fd);
        }
    }
};


TEST(HttpClientLifecycle, ClientInitFailsWithInvalidType) {
    HttpClient client = {};
    Error err = http_client_init(&client, 999, HttpProtocolType.HTTP1, HTTP_RESPONSE_UNSAFE_ZERO_COPY, HTTP_IO_COPY_WRITE);
    ASSERT_EQ(err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(err.code, TransportErrorCode.INIT_FAILURE);

    err = http_client_init(&client, HttpTransportType.TCP, 999, HTTP_RESPONSE_UNSAFE_ZERO_COPY, HTTP_IO_COPY_WRITE);
    ASSERT_EQ(err.type, ErrorType.HTTPC);
    ASSERT_EQ(err.code, HttpClientErrorCode.INIT_FAILURE);

    err = http_client_init(&client, HttpTransportType.UNIX, 999, HTTP_RESPONSE_UNSAFE_ZERO_COPY, HTTP_IO_COPY_WRITE);
    ASSERT_EQ(err.type, ErrorType.HTTPC);
    ASSERT_EQ(err.code, HttpClientErrorCode.INIT_FAILURE);
}

TEST_F(HttpClientIntegrationTest, TcpClientGetRequestSucceeds) {
    StartTcpServer();

    HttpClient client = {};
    Error err = http_client_init(&client, HttpTransportType.TCP, HttpProtocolType.HTTP1, HTTP_RESPONSE_UNSAFE_ZERO_COPY, HTTP_IO_COPY_WRITE);
    ASSERT_EQ(err.type, ErrorType.NONE);

    err = client.connect(&client, "127.0.0.1", tcp_port);
    ASSERT_EQ(err.type, ErrorType.NONE);

    HttpRequest request = {};
    request.path = "/test_path";
    HttpResponse response = {};
    err = client.get(&client, &request, &response);
    ASSERT_EQ(err.type, ErrorType.NONE);

    ASSERT_EQ(response.status_code, 200);
    const std::string body(response.body, response.body_len);
    ASSERT_EQ(body, "success");

    ASSERT_NE(captured_request.find("GET /test_path HTTP/1.1"), std::string::npos);

    http_client_destroy(&client);
}

TEST_F(HttpClientIntegrationTest, TcpClientPostRequestSucceeds) {
    StartTcpServer();

    HttpClient client = {};
    Error err = http_client_init(&client, HttpTransportType.TCP, HttpProtocolType.HTTP1, HTTP_RESPONSE_UNSAFE_ZERO_COPY, HTTP_IO_COPY_WRITE);
    ASSERT_EQ(err.type, ErrorType.NONE);

    err = client.connect(&client, "127.0.0.1", tcp_port);
    ASSERT_EQ(err.type, ErrorType.NONE);


    const char* body_str = "data=value";
    char content_len_str[4];
    snprintf(content_len_str, sizeof(content_len_str), "%zu", strlen(body_str));

    HttpRequest request = {};
    request.path = "/submit";
    request.body = body_str;
    request.headers[0] = {"Content-Length", content_len_str};
    request.num_headers = 1;
    HttpResponse response = {};
    err = client.post(&client, &request, &response);
    ASSERT_EQ(err.type, ErrorType.NONE);

    ASSERT_EQ(response.status_code, 200);
    const std::string body(response.body, response.body_len);
    ASSERT_EQ(body, "success");

    ASSERT_NE(captured_request.find("POST /submit HTTP/1.1"), std::string::npos);
    ASSERT_NE(captured_request.find("\r\n\r\ndata=value"), std::string::npos);

    http_client_destroy(&client);
}

TEST_F(HttpClientIntegrationTest, UnixClientGetRequestSucceeds) {
    StartUnixServer();

    HttpClient client = {};
    Error err = http_client_init(&client, HttpTransportType.UNIX, HttpProtocolType.HTTP1, HTTP_RESPONSE_UNSAFE_ZERO_COPY, HTTP_IO_COPY_WRITE);
    ASSERT_EQ(err.type, ErrorType.NONE);

    err = client.connect(&client, unix_socket_path.c_str(), 0);
    ASSERT_EQ(err.type, ErrorType.NONE);

    HttpRequest request = {};
    request.path = "/local_path";
    HttpResponse response = {};
    err = client.get(&client, &request, &response);
    ASSERT_EQ(err.type, ErrorType.NONE);

    ASSERT_EQ(response.status_code, 200);
    const std::string body(response.body, response.body_len);
    ASSERT_EQ(body, "success");

    ASSERT_NE(captured_request.find("GET /local_path HTTP/1.1"), std::string::npos);

    http_client_destroy(&client);
}

TEST_F(HttpClientIntegrationTest, UnixClientPostRequestSucceeds) {
    StartUnixServer();

    HttpClient client = {};
    Error err = http_client_init(&client, HttpTransportType.UNIX, HttpProtocolType.HTTP1, HTTP_RESPONSE_UNSAFE_ZERO_COPY, HTTP_IO_COPY_WRITE);
    ASSERT_EQ(err.type, ErrorType.NONE);

    err = client.connect(&client, unix_socket_path.c_str(), 0);
    ASSERT_EQ(err.type, ErrorType.NONE);

    const char* body_str = "payload";
    char content_len_str[4];
    snprintf(content_len_str, sizeof(content_len_str), "%zu", strlen(body_str));

    HttpRequest request = {};
    request.path = "/submit_local";
    request.body = body_str;
    request.headers[0] = {"Content-Length", content_len_str};
    request.num_headers = 1;
    HttpResponse response = {};
    err = client.post(&client, &request, &response);
    ASSERT_EQ(err.type, ErrorType.NONE);

    ASSERT_EQ(response.status_code, 200);
    const std::string body(response.body, response.body_len);
    ASSERT_EQ(body, "success");

    ASSERT_NE(captured_request.find("POST /submit_local HTTP/1.1"), std::string::npos);
    ASSERT_NE(captured_request.find("\r\n\r\npayload"), std::string::npos);

    http_client_destroy(&client);
}

TEST_F(HttpClientIntegrationTest, ClientMethodsReturnErrorOnNullptrArgs) {
    HttpClient client = {};
    Error err = http_client_init(&client, HttpTransportType.TCP, HttpProtocolType.HTTP1, HTTP_RESPONSE_UNSAFE_ZERO_COPY, HTTP_IO_COPY_WRITE);
    ASSERT_EQ(err.type, ErrorType.NONE);

    HttpRequest request = {};
    request.path = "/";
    request.body = "body";
    char content_len_str[2];
    snprintf(content_len_str, sizeof(content_len_str), "4");
    request.headers[0] = {"Content-Length", content_len_str};
    request.num_headers = 1;

    HttpResponse response = {};

    err = client.get(nullptr, &request, &response);
    ASSERT_EQ(err.code, HttpClientErrorCode.INVALID_REQUEST_SYNTAX);

    err = client.get(&client, nullptr, &response);
    ASSERT_EQ(err.code, HttpClientErrorCode.INVALID_REQUEST_SYNTAX);

    err = client.get(&client, &request, nullptr);
    ASSERT_EQ(err.code, HttpClientErrorCode.INVALID_REQUEST_SYNTAX);

    HttpRequest bad_req_path = {};
    bad_req_path.path = nullptr;
    err = client.get(&client, &bad_req_path, &response);
    ASSERT_EQ(err.code, HttpClientErrorCode.INVALID_REQUEST_SYNTAX);

    err = client.post(&client, nullptr, &response);
    ASSERT_EQ(err.code, HttpClientErrorCode.INVALID_REQUEST_SYNTAX);

    HttpRequest bad_req_body = {};
    bad_req_body.path = "/";
    bad_req_body.body = nullptr;
    err = client.post(&client, &bad_req_body, &response);
    ASSERT_EQ(err.code, HttpClientErrorCode.INVALID_REQUEST_SYNTAX);

    http_client_destroy(&client);
}

TEST_F(HttpClientIntegrationTest, GetRequestWithBodyReturnsError) {
    HttpClient client = {};
    Error err = http_client_init(&client, HttpTransportType.TCP, HttpProtocolType.HTTP1, HTTP_RESPONSE_UNSAFE_ZERO_COPY, HTTP_IO_COPY_WRITE);
    ASSERT_EQ(err.type, ErrorType.NONE);

    HttpRequest request = {};
    request.path = "/test";
    request.body = "this body is not allowed";

    HttpResponse response = {};
    err = client.get(&client, &request, &response);

    ASSERT_EQ(err.type, ErrorType.HTTPC);
    ASSERT_EQ(err.code, HttpClientErrorCode.INVALID_REQUEST_SYNTAX);

    http_client_destroy(&client);
}

TEST_F(HttpClientIntegrationTest, PostRequestWithoutBodyReturnsError) {
    HttpClient client = {};
    Error err = http_client_init(&client, HttpTransportType.TCP, HttpProtocolType.HTTP1, HTTP_RESPONSE_UNSAFE_ZERO_COPY, HTTP_IO_COPY_WRITE);
    ASSERT_EQ(err.type, ErrorType.NONE);

    HttpRequest request = {};
    request.path = "/test";
    request.body = nullptr;
    request.headers[0] = {"Content-Length", "0"};
    request.num_headers = 1;

    HttpResponse response = {};
    err = client.post(&client, &request, &response);

    ASSERT_EQ(err.type, ErrorType.HTTPC);
    ASSERT_EQ(err.code, HttpClientErrorCode.INVALID_REQUEST_SYNTAX);

    http_client_destroy(&client);
}

TEST_F(HttpClientIntegrationTest, PostRequestWithoutContentLengthReturnsError) {
    HttpClient client = {};
    Error err = http_client_init(&client, HttpTransportType.TCP, HttpProtocolType.HTTP1, HTTP_RESPONSE_UNSAFE_ZERO_COPY, HTTP_IO_COPY_WRITE);
    ASSERT_EQ(err.type, ErrorType.NONE);

    HttpRequest request = {};
    request.path = "/test";
    request.body = "some body";
    request.num_headers = 0;

    HttpResponse response = {};
    err = client.post(&client, &request, &response);

    ASSERT_EQ(err.type, ErrorType.HTTPC);
    ASSERT_EQ(err.code, HttpClientErrorCode.INVALID_REQUEST_SYNTAX);

    http_client_destroy(&client);
}