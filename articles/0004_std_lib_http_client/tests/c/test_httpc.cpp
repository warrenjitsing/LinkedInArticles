#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <csignal>
#include <functional>
#include <random>
#include <iomanip>
#include <cinttypes>
#include <charconv>

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

    // New member to hold custom server logic
    std::function<void(int)> server_logic_;

    void SetUp() override {
        signal(SIGPIPE, SIG_IGN);
    }

    void TearDown() override {
        StopServer();
    }

    // Overloaded functions to start the server
    void StartTcpServer() {
        StartTcpServer(nullptr);
    }
    void StartTcpServer(std::function<void(int)> logic) {
        server_logic_ = std::move(logic);
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

    void StartUnixServer()
    {
        StartUnixServer(nullptr);;
    }

    void StartUnixServer(std::function<void(int)> logic) {
        server_logic_ = std::move(logic);
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

    void RunMultiRequestLoop(HttpIoPolicy io_policy, HttpResponseMemoryPolicy res_policy);

private:
    void AcceptLoop() {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            return;
        }

        if (server_logic_) {
            server_logic_(client_fd);
        } else {
            std::vector<char> buffer(4096, 0);
            ssize_t bytes_read = read(client_fd, buffer.data(), buffer.size() - 1);
            if (bytes_read > 0) {
                captured_request.assign(buffer.data(), bytes_read);
            }
            write(client_fd, canned_response.c_str(), canned_response.length());
        }

        close(client_fd);
    }
};

uint64_t client_xor_checksum(const char* data, size_t len) {
    uint64_t checksum = 0;
    const unsigned char* p = (const unsigned char*)data;
    for (size_t i = 0; i < len; ++i) {
        checksum ^= p[i];
    }
    return checksum;
}


std::string uint64_to_hex(uint64_t val) {
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << val;
    return ss.str();
}

void HttpClientIntegrationTest::RunMultiRequestLoop(HttpIoPolicy io_policy, HttpResponseMemoryPolicy res_policy) {
    const int NUM_CYCLES = 50;

    auto server_logic = [NUM_CYCLES](int client_fd) {
        std::mt19937 gen(4321);
        std::uniform_int_distribution<char> char_dist(32, 126);
        std::uniform_int_distribution<size_t> len_dist(512, 1024);
        std::vector<char> buffer;
        size_t processed_offset = 0;

        for (int i = 0; i < NUM_CYCLES; ++i) {
            const char* headers_end = nullptr;
            size_t content_length = 0;
            size_t request_size = 0;

            while (true) {
                std::string_view search_area(buffer.data() + processed_offset, buffer.size() - processed_offset);
                if (auto pos = search_area.find("\r\n\r\n"); pos != std::string_view::npos) {
                    headers_end = buffer.data() + processed_offset + pos;

                    std::string_view headers(buffer.data() + processed_offset, pos);
                    if (auto cl_pos = headers.find("Content-Length: "); cl_pos != std::string_view::npos) {
                        std::from_chars(headers.data() + cl_pos + 16, headers.data() + headers.length(), content_length);
                    }
                    request_size = (headers_end - (buffer.data() + processed_offset)) + 4 + content_length;
                }

                if (request_size > 0 && (buffer.size() - processed_offset) >= request_size) {
                    break;
                }

                // Read more data
                size_t old_size = buffer.size();
                buffer.resize(old_size + 4096);
                ssize_t bytes_read = read(client_fd, buffer.data() + old_size, 4096);
                if (bytes_read <= 0) {
                    ADD_FAILURE() << "Server failed to read on iteration " << i;
                    return;
                }
                buffer.resize(old_size + bytes_read);
            }

            const char* body_start = headers_end + 4;

            if (content_length >= 16) {
                size_t payload_len = content_length - 16;
                uint64_t calculated = client_xor_checksum(body_start, payload_len);
                uint64_t received = 0;
                sscanf(body_start + payload_len, "%16" SCNx64, &received);
                ASSERT_EQ(calculated, received) << "Server-side checksum mismatch on iteration " << i;
            }

            processed_offset += request_size;

            size_t res_body_len = len_dist(gen);
            std::string res_payload(res_body_len, '\0');
            for (char& c : res_payload) { c = char_dist(gen); }
            uint64_t res_checksum = client_xor_checksum(res_payload.c_str(), res_payload.size());
            std::string res_checksum_hex = uint64_to_hex(res_checksum);
            std::string timestamp_str = "0";
            timestamp_str.resize(19, '0');
            std::string final_body = res_payload + res_checksum_hex + timestamp_str;
            std::string response_str = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(final_body.length()) + "\r\n\r\n" + final_body;
            write(client_fd, response_str.c_str(), response_str.length());
        }
    };

    StartTcpServer(server_logic);

    HttpClient client = {};
    Error err = http_client_init(&client, HttpTransportType.TCP, HttpProtocolType.HTTP1, res_policy, io_policy);
    ASSERT_EQ(err.type, ErrorType.NONE);

    err = client.connect(&client, "127.0.0.1", tcp_port);
    ASSERT_EQ(err.type, ErrorType.NONE);

    std::mt19937 gen(1234);
    std::uniform_int_distribution<char> char_dist(32, 126);
    std::uniform_int_distribution<size_t> len_dist(512, 1024);

    char content_len_str[32];
    char* payload_buffer = NULL;

    for (int i = 0; i < NUM_CYCLES; ++i) {
        size_t req_size = len_dist(gen);
        std::vector<char> body_slice(req_size);
        for(char& c : body_slice) { c = char_dist(gen); }

        uint64_t checksum = client_xor_checksum(body_slice.data(), req_size);
        size_t payload_size = req_size + 16;
        payload_buffer = (char*)realloc(payload_buffer, payload_size + 1);
        memcpy(payload_buffer, body_slice.data(), req_size);

        // Corrected snprintf with PRIx64
        snprintf(payload_buffer + req_size, 17, "%016" PRIx64, checksum);

        HttpRequest request = {};
        request.path = "/";
        request.body = payload_buffer;
        snprintf(content_len_str, sizeof(content_len_str), "%zu", payload_size);
        request.headers[0] = {"Content-Length", content_len_str};
        request.num_headers = 1;

        HttpResponse response = {0};
        err = client.post(&client, &request, &response);
        ASSERT_EQ(err.type, ErrorType.NONE) << "Request failed on iteration " << i;
        ASSERT_EQ(response.status_code, 200);

        if (response.body_len >= 35) {
            size_t res_payload_len = response.body_len - 35;
            uint64_t calculated_checksum = client_xor_checksum(response.body, res_payload_len);
            uint64_t received_checksum = 0;
            // Corrected sscanf with SCNx64
            sscanf(response.body + res_payload_len, "%16" SCNx64, &received_checksum);
            ASSERT_EQ(calculated_checksum, received_checksum) << "Client-side checksum mismatch on iteration " << i;
        } else {
            ADD_FAILURE() << "Response body was too short on iteration " << i;
        }
        http_response_destroy(&response);
    }

    free(payload_buffer);
    http_client_destroy(&client);
}

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

TEST_F(HttpClientIntegrationTest, TcpClientVectoredPostRequestSucceeds) {
    StartTcpServer();

    HttpClient client = {};
    Error err = http_client_init(&client, HttpTransportType.TCP, HttpProtocolType.HTTP1, HTTP_RESPONSE_UNSAFE_ZERO_COPY, HTTP_IO_VECTORED_WRITE);
    ASSERT_EQ(err.type, ErrorType.NONE);

    err = client.connect(&client, "127.0.0.1", tcp_port);
    ASSERT_EQ(err.type, ErrorType.NONE);

    const char* body_str = "data=value";
    char content_len_str[12];
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

TEST_F(HttpClientIntegrationTest, MultiRequest_CopyWrite_SafeResponse) {
    RunMultiRequestLoop(HTTP_IO_COPY_WRITE, HTTP_RESPONSE_SAFE_OWNING);
}

TEST_F(HttpClientIntegrationTest, MultiRequest_CopyWrite_UnsafeResponse) {
    RunMultiRequestLoop(HTTP_IO_COPY_WRITE, HTTP_RESPONSE_UNSAFE_ZERO_COPY);
}

TEST_F(HttpClientIntegrationTest, MultiRequest_VectoredWrite_SafeResponse) {
    RunMultiRequestLoop(HTTP_IO_VECTORED_WRITE, HTTP_RESPONSE_SAFE_OWNING);
}

TEST_F(HttpClientIntegrationTest, MultiRequest_VectoredWrite_UnsafeResponse) {
    RunMultiRequestLoop(HTTP_IO_VECTORED_WRITE, HTTP_RESPONSE_UNSAFE_ZERO_COPY);
}