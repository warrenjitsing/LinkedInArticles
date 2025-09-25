#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>

extern "C" {
#include <httpc/http1_protocol.h>
}

namespace {
struct MockTransportState {
    HttpcSyscalls* syscalls = nullptr;

    std::vector<char> write_buffer;
    std::vector<char> read_buffer;
    size_t read_pos = 0;

    bool connect_called = false;
    std::string host_received;
    int port_received = -1;

    bool write_called = false;
    bool read_called = false;
    bool close_called = false;

    bool should_fail_connect = false;
    bool should_fail_write = false;
    bool should_fail_read = false;
    bool should_fail_close = false;
};

Error mock_transport_connect(void* context, const char* host, int port) {
    auto* state = static_cast<MockTransportState*>(context);
    state->connect_called = true;
    state->host_received = host;
    state->port_received = port;
    if (state->should_fail_connect) {
        return {ErrorType.TRANSPORT, TransportErrorCode.SOCKET_CONNECT_FAILURE};
    }
    return {ErrorType.NONE, 0};
}

Error mock_transport_write(void* context, const void* buffer, size_t len, ssize_t* bytes_written) {
    auto* state = static_cast<MockTransportState*>(context);
    state->write_called = true;
    if (state->should_fail_write) {
        *bytes_written = -1;
        return {ErrorType.TRANSPORT, TransportErrorCode.SOCKET_WRITE_FAILURE};
    }
    const auto* char_buffer = static_cast<const char*>(buffer);
    state->write_buffer.insert(state->write_buffer.end(), char_buffer, char_buffer + len);
    *bytes_written = len;
    return {ErrorType.NONE, 0};
}

Error mock_transport_read(void* context, void* buffer, size_t len, ssize_t* bytes_read) {
    auto* state = static_cast<MockTransportState*>(context);
    state->read_called = true;
    if (state->should_fail_read) {
        *bytes_read = -1;
        return {ErrorType.TRANSPORT, TransportErrorCode.SOCKET_READ_FAILURE};
    }

    size_t remaining = state->read_buffer.size() - state->read_pos;
    size_t to_read = std::min(len, remaining);

    if (to_read == 0) {
        *bytes_read = 0;
        return {ErrorType.TRANSPORT, TransportErrorCode.CONNECTION_CLOSED};
    }

    state->syscalls->memcpy(buffer, state->read_buffer.data() + state->read_pos, to_read);
    state->read_pos += to_read;
    *bytes_read = to_read;
    return {ErrorType.NONE, 0};
}

Error mock_transport_close(void* context) {
    auto* state = static_cast<MockTransportState*>(context);
    state->close_called = true;
    if (state->should_fail_close) {
        return {ErrorType.TRANSPORT, TransportErrorCode.SOCKET_CLOSE_FAILURE};
    }
    return {ErrorType.NONE, 0};
}
}

class HttpProtocolTest : public ::testing::Test {
protected:
    HttpProtocolInterface* protocol;
    Http1Protocol* protocol_impl;

    MockTransportState mock_transport_state;
    TransportInterface mock_transport_interface;
    HttpcSyscalls mock_syscalls;

    void SetUp() override {
        httpc_syscalls_init_default(&mock_syscalls);

        mock_transport_state = {};
        mock_transport_state.syscalls = &mock_syscalls;

        mock_transport_interface = {};
        mock_transport_interface.context = &mock_transport_state;
        mock_transport_interface.connect = mock_transport_connect;
        mock_transport_interface.write = mock_transport_write;
        mock_transport_interface.read = mock_transport_read;
        mock_transport_interface.close = mock_transport_close;

        protocol = http1_protocol_new(&mock_transport_interface, &mock_syscalls, HTTP_RESPONSE_UNSAFE_ZERO_COPY);
        ASSERT_NE(protocol, nullptr);
        protocol_impl = (Http1Protocol*)protocol;
    }

    void TearDown() override {
        http1_protocol_destroy(protocol);
    }
};

TEST_F(HttpProtocolTest, NewSucceedsAndInitializesInterface) {
    ASSERT_NE(protocol, nullptr);
    ASSERT_NE(protocol_impl, nullptr);
    ASSERT_EQ(protocol_impl->interface.context, protocol_impl);
    ASSERT_NE(protocol_impl->interface.perform_request, nullptr);
    ASSERT_EQ(protocol_impl->transport, &mock_transport_interface);
    ASSERT_EQ(protocol_impl->syscalls, &mock_syscalls);
}

TEST(HttpProtocolLifecycle, DestroyHandlesNullGracefully) {
    http1_protocol_destroy(nullptr);
    SUCCEED();
}


static void* mock_malloc_fails(size_t size) {
    (void)size;
    return nullptr;
}

TEST(HttpProtocolLifecycle, NewFailsWhenMallocFails) {
    HttpcSyscalls mock_syscalls;
    httpc_syscalls_init_default(&mock_syscalls);

    mock_syscalls.malloc = mock_malloc_fails;

    HttpProtocolInterface* protocol = http1_protocol_new(nullptr, &mock_syscalls, HTTP_RESPONSE_UNSAFE_ZERO_COPY);

    ASSERT_EQ(protocol, nullptr);
}

TEST_F(HttpProtocolTest, ConnectCallsTransportConnect) {
    const char* expected_host = "example.com";
    const int expected_port = 80;

    Error err = protocol->connect(protocol->context, expected_host, expected_port);

    ASSERT_EQ(err.type, ErrorType.NONE);
    ASSERT_TRUE(mock_transport_state.connect_called);
    ASSERT_EQ(mock_transport_state.host_received, expected_host);
    ASSERT_EQ(mock_transport_state.port_received, expected_port);
}

TEST_F(HttpProtocolTest, DisconnectCallsTransportClose) {
    Error err = protocol->disconnect(protocol->context);

    ASSERT_EQ(err.type, ErrorType.NONE);
    ASSERT_TRUE(mock_transport_state.close_called);
}

TEST_F(HttpProtocolTest, ConnectPropagatesTransportError) {
    mock_transport_state.should_fail_connect = true;

    Error err = protocol->connect(protocol->context, "example.com", 80);

    ASSERT_TRUE(mock_transport_state.connect_called);
    ASSERT_EQ(err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(err.code, TransportErrorCode.SOCKET_CONNECT_FAILURE);
}

TEST_F(HttpProtocolTest, DisconnectPropagatesTransportError) {
    mock_transport_state.should_fail_close = true;

    Error err = protocol->disconnect(protocol->context);

    ASSERT_TRUE(mock_transport_state.close_called);
    ASSERT_EQ(err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(err.code, TransportErrorCode.SOCKET_CLOSE_FAILURE);
}

TEST_F(HttpProtocolTest, PerformRequestBuildsCorrectGetRequestString) {
    HttpRequest request = {};
    request.method = HTTP_GET;
    request.path = "/test";
    request.headers[0] = {"Host", "api.example.com"};
    request.num_headers = 1;

    HttpResponse response = {};
    protocol->perform_request(protocol->context, &request, &response);

    ASSERT_TRUE(mock_transport_state.write_called);

    const std::string expected = "GET /test HTTP/1.1\r\n"
                                 "Host: api.example.com\r\n"
                                 "\r\n";

    const std::string actual(mock_transport_state.write_buffer.begin(),
                             mock_transport_state.write_buffer.end());

    ASSERT_EQ(actual, expected);
}

TEST_F(HttpProtocolTest, PerformRequestBuildsCorrectPostRequestString) {
    HttpRequest request = {};
    request.method = HTTP_POST;
    request.path = "/api/v1/submit";
    request.body = "{\"data\":true}";
    request.headers[0] = {"Host", "localhost"};
    request.headers[1] = {"Content-Type", "application/json"};
    request.headers[2] = {"Content-Length", "13"};
    request.num_headers = 3;

    HttpResponse response = {};
    protocol->perform_request(protocol->context, &request, &response);

    ASSERT_TRUE(mock_transport_state.write_called);

    const std::string expected = "POST /api/v1/submit HTTP/1.1\r\n"
                                 "Host: localhost\r\n"
                                 "Content-Type: application/json\r\n"
                                 "Content-Length: 13\r\n"
                                 "\r\n"
                                 "{\"data\":true}";

    const std::string actual(mock_transport_state.write_buffer.begin(),
                             mock_transport_state.write_buffer.end());

    ASSERT_EQ(actual, expected);
}

TEST_F(HttpProtocolTest, PerformRequestSucceedsWithContentLength) {
    const std::string mock_response_str =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "Test Response";
    mock_transport_state.read_buffer.assign(
        mock_response_str.begin(), mock_response_str.end());

    HttpRequest request = {};
    request.method = HTTP_GET;
    request.path = "/";

    HttpResponse response = {};
    Error err = protocol->perform_request(protocol->context, &request, &response);

    ASSERT_EQ(err.type, ErrorType.NONE);

    ASSERT_EQ(response.status_code, 200);
    ASSERT_EQ(response.num_headers, 2);

    ASSERT_STREQ(response.headers[0].key, "Content-Type");
    ASSERT_STREQ(response.headers[0].value, "text/plain");
    ASSERT_STREQ(response.headers[1].key, "Content-Length");
    ASSERT_STREQ(response.headers[1].value, "13");

    ASSERT_EQ(response.body_len, 13);
    ASSERT_NE(response.body, nullptr);
    const std::string actual_body(response.body, response.body_len);
    ASSERT_EQ(actual_body, "Test Response");
}

TEST_F(HttpProtocolTest, PerformRequestSucceedsWithoutContentLengthOnConnectionClose) {
    const std::string mock_response_str =
        "HTTP/1.1 200 OK\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Body until close";
    mock_transport_state.read_buffer.assign(
        mock_response_str.begin(), mock_response_str.end());

    HttpRequest request = {};
    HttpResponse response = {};
    Error err = protocol->perform_request(protocol->context, &request, &response);

    ASSERT_EQ(err.type, ErrorType.NONE);

    ASSERT_EQ(response.status_code, 200);
    ASSERT_EQ(response.num_headers, 1);
    ASSERT_STREQ(response.headers[0].key, "Connection");
    ASSERT_STREQ(response.headers[0].value, "close");

    ASSERT_NE(response.body, nullptr);
    const std::string actual_body(response.body, response.body_len);
    ASSERT_EQ(actual_body, "Body until close");
}


std::vector<std::string> g_response_chunks;
size_t g_read_chunk_index = 0;

Error mock_read_in_chunks(void* context, void* buffer, size_t len, ssize_t* bytes_read) {
    auto* state = static_cast<MockTransportState*>(context);
    state->read_called = true;

    if (g_read_chunk_index >= g_response_chunks.size()) {
        *bytes_read = 0;
        return {ErrorType.TRANSPORT, TransportErrorCode.CONNECTION_CLOSED};
    }

    const std::string& chunk = g_response_chunks[g_read_chunk_index];
    size_t to_copy = std::min(len, chunk.size());
    state->syscalls->memcpy(buffer, chunk.data(), to_copy);
    *bytes_read = to_copy;

    g_read_chunk_index++;
    return {ErrorType.NONE, 0};
}


TEST_F(HttpProtocolTest, PerformRequestHandlesResponseSplitAcrossMultipleReads) {
    g_response_chunks = {
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n",
        "Content-Length: 4\r\n\r\n",
        "Body"
    };
    g_read_chunk_index = 0;

    mock_transport_interface.read = mock_read_in_chunks;

    HttpRequest request = {};
    HttpResponse response = {};
    Error err = protocol->perform_request(protocol->context, &request, &response);

    ASSERT_EQ(err.type, ErrorType.NONE);

    ASSERT_EQ(response.status_code, 200);
    ASSERT_EQ(response.num_headers, 2);
    ASSERT_STREQ(response.headers[1].key, "Content-Length");
    ASSERT_STREQ(response.headers[1].value, "4");

    ASSERT_EQ(response.body_len, 4);
    ASSERT_NE(response.body, nullptr);
    const std::string actual_body(response.body, response.body_len);
    ASSERT_EQ(actual_body, "Body");
}

TEST_F(HttpProtocolTest, PerformRequestCorrectlyParsesHeadersAndStatusLine) {
    const std::string mock_response_str =
        "HTTP/1.1 404 Not Found\r\n"
        "Connection: close\r\n"
        "Content-Type: application/json\r\n"
        "X-Custom-Header: some_value\r\n"
        "Content-Length: 0\r\n"
        "\r\n";
    mock_transport_state.read_buffer.assign(
        mock_response_str.begin(), mock_response_str.end());

    HttpRequest request = {};
    HttpResponse response = {};
    Error err = protocol->perform_request(protocol->context, &request, &response);

    ASSERT_EQ(err.type, ErrorType.NONE);

    ASSERT_EQ(response.status_code, 404);
    ASSERT_STREQ(response.status_message, "Not Found");

    ASSERT_EQ(response.num_headers, 4);
    ASSERT_STREQ(response.headers[0].key, "Connection");
    ASSERT_STREQ(response.headers[0].value, "close");
    ASSERT_STREQ(response.headers[1].key, "Content-Type");
    ASSERT_STREQ(response.headers[1].value, "application/json");
    ASSERT_STREQ(response.headers[2].key, "X-Custom-Header");
    ASSERT_STREQ(response.headers[2].value, "some_value");
    ASSERT_STREQ(response.headers[3].key, "Content-Length");
    ASSERT_STREQ(response.headers[3].value, "0");
}

TEST_F(HttpProtocolTest, PerformRequestHandlesZeroContentLength) {
    const std::string mock_response_str =
        "HTTP/1.1 204 No Content\r\n"
        "Content-Length: 0\r\n"
        "\r\n";
    mock_transport_state.read_buffer.assign(
        mock_response_str.begin(), mock_response_str.end());

    HttpRequest request = {};
    HttpResponse response = {};
    Error err = protocol->perform_request(protocol->context, &request, &response);

    ASSERT_EQ(err.type, ErrorType.NONE);
    ASSERT_EQ(response.status_code, 204);
    ASSERT_EQ(response.num_headers, 1);
    ASSERT_STREQ(response.headers[0].key, "Content-Length");
    ASSERT_STREQ(response.headers[0].value, "0");
    ASSERT_EQ(response.body_len, 0);
}

TEST_F(HttpProtocolTest, PerformRequestFailsIfTransportWriteFails) {
    mock_transport_state.should_fail_write = true;

    HttpRequest request = {};
    HttpResponse response = {};
    Error err = protocol->perform_request(protocol->context, &request, &response);

    ASSERT_TRUE(mock_transport_state.write_called);
    ASSERT_EQ(err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(err.code, TransportErrorCode.SOCKET_WRITE_FAILURE);
}

TEST_F(HttpProtocolTest, PerformRequestFailsIfTransportReadFails) {
    mock_transport_state.should_fail_read = true;

    HttpRequest request = {};
    HttpResponse response = {};
    Error err = protocol->perform_request(protocol->context, &request, &response);

    ASSERT_TRUE(mock_transport_state.read_called);
    ASSERT_EQ(err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(err.code, TransportErrorCode.SOCKET_READ_FAILURE);
}

TEST_F(HttpProtocolTest, PerformRequestFailsIfConnectionClosedDuringHeaders) {
    const std::string mock_response_str = "HTTP/1.1 200 OK\r\nContent-Type: text/plain";
    mock_transport_state.read_buffer.assign(
        mock_response_str.begin(), mock_response_str.end());

    HttpRequest request = {};
    HttpResponse response = {};
    Error err = protocol->perform_request(protocol->context, &request, &response);

    ASSERT_EQ(err.type, ErrorType.HTTPC);
    ASSERT_EQ(err.code, HttpClientErrorCode.HTTP_PARSE_FAILURE);
}

static void* mock_realloc_fails(void* ptr, size_t size) {
    (void)ptr;
    (void)size;
    return nullptr;
}

TEST_F(HttpProtocolTest, PerformRequestFailsIfInitialBufferAllocationFails) {
    mock_syscalls.realloc = mock_realloc_fails;

    HttpRequest request = {};
    HttpResponse response = {};
    Error err = protocol->perform_request(protocol->context, &request, &response);

    ASSERT_EQ(err.type, ErrorType.HTTPC);
    ASSERT_EQ(err.code, HttpClientErrorCode.HTTP_PARSE_FAILURE);
}

TEST_F(HttpProtocolTest, PerformRequestSucceedsWhenResponseIsLargerThanInitialBuffer) {
    std::string large_body(3000, 'a');
    std::string mock_response_str =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 3000\r\n"
        "\r\n" + large_body;

    g_response_chunks = {
        mock_response_str.substr(0, 1024),
        mock_response_str.substr(1024, 1024),
        mock_response_str.substr(2048)
    };
    g_read_chunk_index = 0;

    mock_transport_interface.read = mock_read_in_chunks;

    HttpRequest request = {};
    HttpResponse response = {};
    Error err = protocol->perform_request(protocol->context, &request, &response);

    ASSERT_EQ(err.type, ErrorType.NONE);
    ASSERT_EQ(response.status_code, 200);
    ASSERT_EQ(response.body_len, 3000);

    const std::string actual_body(response.body, response.body_len);
    ASSERT_EQ(actual_body, large_body);
    size_t req_len = mock_response_str.length();
    ASSERT_EQ(protocol_impl->buffer.capacity, req_len + 1);
    ASSERT_EQ(protocol_impl->buffer.len, req_len);
}

TEST_F(HttpProtocolTest, SafeResponseSucceedsAndIsOwning) {
    HttpProtocolInterface* safe_protocol = http1_protocol_new(
        &mock_transport_interface, &mock_syscalls, HTTP_RESPONSE_SAFE_OWNING);
    ASSERT_NE(safe_protocol, nullptr);
    auto* safe_protocol_impl = (Http1Protocol*)safe_protocol;

    std::string large_header_val(1024, 'a');
    std::string mock_response_str =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "X-Large-Header: " + large_header_val + "\r\n"
        "Content-Length: 4\r\n"
        "\r\n"
        "body";
    mock_transport_state.read_buffer.assign(
        mock_response_str.begin(), mock_response_str.end());

    HttpRequest request = {};
    HttpResponse response = {};
    Error err = safe_protocol->perform_request(safe_protocol->context, &request, &response);

    ASSERT_EQ(err.type, ErrorType.NONE);
    ASSERT_EQ(response.status_code, 200);
    ASSERT_EQ(response.body_len, 4);
    ASSERT_STREQ(response.body, "body");
    ASSERT_STREQ(response.headers[1].key, "X-Large-Header");

    ASSERT_NE(response._owned_buffer, nullptr);
    ASSERT_NE(response.body, safe_protocol_impl->buffer.data);

    http_response_destroy(&response);
    http1_protocol_destroy(safe_protocol);
}

TEST_F(HttpProtocolTest, SafeResponseDestroyCleansUp) {
    HttpProtocolInterface* safe_protocol = http1_protocol_new(
        &mock_transport_interface, &mock_syscalls, HTTP_RESPONSE_SAFE_OWNING);
    ASSERT_NE(safe_protocol, nullptr);

    const std::string mock_response_str = "HTTP/1.1 204 No Content\r\n\r\n";
    mock_transport_state.read_buffer.assign(
        mock_response_str.begin(), mock_response_str.end());

    HttpRequest request = {};
    HttpResponse response = {};
    safe_protocol->perform_request(safe_protocol->context, &request, &response);

    ASSERT_NE(response._owned_buffer, nullptr);
    http_response_destroy(&response);
    ASSERT_EQ(response._owned_buffer, nullptr);

    http1_protocol_destroy(safe_protocol);
}

TEST_F(HttpProtocolTest, SafeResponseHandlesReadFailureAndCleansUp) {
    HttpProtocolInterface* safe_protocol = http1_protocol_new(
        &mock_transport_interface, &mock_syscalls, HTTP_RESPONSE_SAFE_OWNING);
    ASSERT_NE(safe_protocol, nullptr);

    mock_transport_state.should_fail_read = true;

    HttpRequest request = {};
    HttpResponse response = {};
    Error err = safe_protocol->perform_request(safe_protocol->context, &request, &response);

    ASSERT_EQ(err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(err.code, TransportErrorCode.SOCKET_READ_FAILURE);

    http1_protocol_destroy(safe_protocol);
}