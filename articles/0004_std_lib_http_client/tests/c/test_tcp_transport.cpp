#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <csignal>
#include <cerrno>
#include <httpc/httpc.h>

extern "C" {
#include <httpc/tcp_transport.h>
}

class TcpTransportTest : public ::testing::Test {
protected:
    TransportInterface* transport;
    TcpClient* client;
    HttpcSyscalls mock_syscalls;

    std::thread listener_thread;
    std::atomic<bool> listener_should_stop{false};
    int listener_fd = -1;
    int listener_port = 0;
    const char* test_message = "hello world";

    void SetUp() override {
        signal(SIGPIPE, SIG_IGN);

        transport = tcp_transport_new(nullptr);
        ASSERT_NE(transport, nullptr);
        client = (TcpClient*)transport;
        httpc_syscalls_init_default(&mock_syscalls);

        start_listener();
    }

    void TearDown() override {
        stop_listener();
        transport->destroy(transport->context);
    }

    void start_listener() {
        listener_fd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_NE(listener_fd, -1);

        struct sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        serv_addr.sin_port = 0;

        ASSERT_EQ(bind(listener_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)), 0);

        socklen_t len = sizeof(serv_addr);
        ASSERT_EQ(getsockname(listener_fd, (struct sockaddr*)&serv_addr, &len), 0);
        listener_port = ntohs(serv_addr.sin_port);

        ASSERT_EQ(listen(listener_fd, 1), 0);

        listener_thread = std::thread([this]() {
            while (!listener_should_stop) {
                int client_fd = accept(listener_fd, nullptr, nullptr);
                if (client_fd < 0) {
                    continue;
                }
                write(client_fd, test_message, strlen(test_message));
                close(client_fd);
            }
        });
    }

    void stop_listener() {
        listener_should_stop = true;
        shutdown(listener_fd, SHUT_RDWR);
        if (listener_thread.joinable()) {
            listener_thread.join();
        }
        close(listener_fd);
    }

    void ReinitializeWithMocks() {
        transport->destroy(transport->context);
        transport = tcp_transport_new(&mock_syscalls);
        ASSERT_NE(transport, nullptr);
        client = (TcpClient*)transport;
    }
};


TEST_F(TcpTransportTest, NewSucceedsWithDefaultSyscalls) {
    ASSERT_NE(transport, nullptr);
    ASSERT_NE(client, nullptr);
    ASSERT_EQ(transport->context, client);
    ASSERT_NE(client->syscalls, nullptr);
    ASSERT_EQ(client->syscalls->socket, socket);
}

TEST(TcpTransportLifecycle, NewSucceedsWithOverrideSyscalls) {
    HttpcSyscalls mock_syscalls;
    memset(&mock_syscalls, 0, sizeof(mock_syscalls));
    httpc_syscalls_init_default(&mock_syscalls);

    TransportInterface* transport = tcp_transport_new(&mock_syscalls);
    ASSERT_NE(transport, nullptr);

    TcpClient* client = (TcpClient*)transport;
    ASSERT_EQ(client->syscalls, &mock_syscalls);

    transport->destroy(transport->context);
}

TEST(TcpTransportLifecycle, DestroyHandlesNullGracefully) {
    TransportInterface* transport = tcp_transport_new(nullptr);
    TcpClient* client = (TcpClient*)transport;
    transport->destroy(nullptr);
    transport->destroy(transport->context);
    SUCCEED();
}

static int mock_socket_creation_fails(int, int, int) {
    return -1;
}

TEST_F(TcpTransportTest, ConnectSucceedsOnLocalListener) {
    Error err = transport->connect(transport->context, "127.0.0.1", listener_port);

    ASSERT_EQ(err.type, ErrorType.NONE);
    ASSERT_GT(client->fd, 0);
}

static struct sockaddr_in MOCK_FAIL_ADDR;
static struct sockaddr_in MOCK_SUCCESS_ADDR;
static struct addrinfo MOCK_ADDRINFO_FAIL;
static struct addrinfo MOCK_ADDRINFO_SUCCESS;

static int mock_getaddrinfo_two_results(const char*, const char*,
                                        const struct addrinfo*,
                                        struct addrinfo** res) {
    memset(&MOCK_FAIL_ADDR, 0, sizeof(MOCK_FAIL_ADDR));
    MOCK_FAIL_ADDR.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &MOCK_FAIL_ADDR.sin_addr);
    MOCK_FAIL_ADDR.sin_port = htons(9999); // A port we know will fail

    memset(&MOCK_ADDRINFO_FAIL, 0, sizeof(MOCK_ADDRINFO_FAIL));
    MOCK_ADDRINFO_FAIL.ai_family = AF_INET;
    MOCK_ADDRINFO_FAIL.ai_socktype = SOCK_STREAM;
    MOCK_ADDRINFO_FAIL.ai_addr = (struct sockaddr*)&MOCK_FAIL_ADDR;
    MOCK_ADDRINFO_FAIL.ai_addrlen = sizeof(MOCK_FAIL_ADDR);
    MOCK_ADDRINFO_FAIL.ai_next = &MOCK_ADDRINFO_SUCCESS;

    *res = &MOCK_ADDRINFO_FAIL;
    return 0;
}

static void mock_freeaddrinfo(struct addrinfo*) {
    // Do nothing, as our addrinfo structs are static.
}

static int mock_connect_first_fails(int sockfd, const struct sockaddr* addr, socklen_t) {
    auto* sin = (const struct sockaddr_in*)addr;
    if (sin->sin_port == MOCK_FAIL_ADDR.sin_port) {
        return -1; // The first address fails.
    }
    // For the second address, call the real connect.
    return connect(sockfd, addr, sizeof(MOCK_SUCCESS_ADDR));
}

TEST_F(TcpTransportTest, ConnectSucceedsOnSecondAddressIfFirstFails) {
    memset(&MOCK_SUCCESS_ADDR, 0, sizeof(MOCK_SUCCESS_ADDR));
    MOCK_SUCCESS_ADDR.sin_family = AF_INET;
    MOCK_SUCCESS_ADDR.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    MOCK_SUCCESS_ADDR.sin_port = htons(listener_port);

    memset(&MOCK_ADDRINFO_SUCCESS, 0, sizeof(MOCK_ADDRINFO_SUCCESS));
    MOCK_ADDRINFO_SUCCESS.ai_family = AF_INET;
    MOCK_ADDRINFO_SUCCESS.ai_socktype = SOCK_STREAM;
    MOCK_ADDRINFO_SUCCESS.ai_addr = (struct sockaddr*)&MOCK_SUCCESS_ADDR;
    MOCK_ADDRINFO_SUCCESS.ai_addrlen = sizeof(MOCK_SUCCESS_ADDR);
    MOCK_ADDRINFO_SUCCESS.ai_next = nullptr;

    mock_syscalls.getaddrinfo = mock_getaddrinfo_two_results;
    mock_syscalls.freeaddrinfo = mock_freeaddrinfo;
    mock_syscalls.connect = mock_connect_first_fails;
    ReinitializeWithMocks();

    Error err = transport->connect(transport->context, "example.com", listener_port);

    ASSERT_EQ(err.type, ErrorType.NONE);
    ASSERT_GT(client->fd, 0);
}

TEST_F(TcpTransportTest, ConnectFailsIfAllSocketCreationsFail) {
    mock_syscalls.socket = mock_socket_creation_fails;
    ReinitializeWithMocks();

    Error err = transport->connect(transport->context, "example.com", 80);

    ASSERT_EQ(err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(err.code, TransportErrorCode.SOCKET_CONNECT_FAILURE);
}

static int mock_getaddrinfo_fails(const char*, const char*,
                                  const struct addrinfo*,
                                  struct addrinfo**) {
    return EAI_FAIL;
}

TEST_F(TcpTransportTest, ConnectFailsOnDnsFailure) {
    mock_syscalls.getaddrinfo = mock_getaddrinfo_fails;
    ReinitializeWithMocks();

    Error err = transport->connect(transport->context, "example.com", 80);

    ASSERT_EQ(err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(err.code, TransportErrorCode.DNS_FAILURE);
}


static int mock_connect_fails(int, const struct sockaddr*, socklen_t) {
    return -1;
}

TEST_F(TcpTransportTest, ConnectFailsIfAllSocketConnectionsFail) {
    mock_syscalls.connect = mock_connect_fails;
    ReinitializeWithMocks();

    Error err = transport->connect(transport->context, "example.com", 80);

    ASSERT_EQ(err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(err.code, TransportErrorCode.SOCKET_CONNECT_FAILURE);
}


TEST_F(TcpTransportTest, ReadSucceedsAndReturnsBytesRead) {
    Error connect_err = transport->connect(transport->context, "127.0.0.1", listener_port);
    ASSERT_EQ(connect_err.type, ErrorType.NONE);

    char buffer[32] = {0};
    ssize_t bytes_read = 0;

    Error read_err = transport->read(transport->context, buffer, sizeof(buffer) - 1, &bytes_read);

    ASSERT_EQ(read_err.type, ErrorType.NONE);
    ASSERT_EQ(bytes_read, strlen(test_message));
    ASSERT_STREQ(buffer, test_message);
}

TEST_F(TcpTransportTest, ReadReturnsConnectionClosedOnPeerShutdown) {
    int svr_sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(svr_sock, -1);

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serv_addr.sin_port = 0;
    ASSERT_EQ(bind(svr_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)), 0);

    socklen_t len = sizeof(serv_addr);
    ASSERT_EQ(getsockname(svr_sock, (struct sockaddr*)&serv_addr, &len), 0);
    int port = ntohs(serv_addr.sin_port);
    ASSERT_EQ(listen(svr_sock, 1), 0);

    std::thread server_thread([svr_sock]() {
        int client_fd = accept(svr_sock, nullptr, nullptr);
        if (client_fd >= 0) {
            close(client_fd);
        }
    });

    Error connect_err = transport->connect(transport->context, "127.0.0.1", port);
    ASSERT_EQ(connect_err.type, ErrorType.NONE);

    char buffer[32];
    ssize_t bytes_read = 0;
    Error read_err = transport->read(transport->context, buffer, sizeof(buffer), &bytes_read);

    ASSERT_EQ(read_err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(read_err.code, TransportErrorCode.CONNECTION_CLOSED);

    shutdown(svr_sock, SHUT_RDWR);
    server_thread.join();
    close(svr_sock);
}

static ssize_t mock_read_fails(int, void*, size_t) {
    return -1;
}

TEST_F(TcpTransportTest, ReadFailsOnSocketReadError) {
    mock_syscalls.read = mock_read_fails;
    ReinitializeWithMocks();

    Error connect_err = transport->connect(transport->context, "127.0.0.1", listener_port);
    ASSERT_EQ(connect_err.type, ErrorType.NONE);

    char buffer[32];
    ssize_t bytes_read = 0;
    Error read_err = transport->read(transport->context, buffer, sizeof(buffer), &bytes_read);

    ASSERT_EQ(read_err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(read_err.code, TransportErrorCode.SOCKET_READ_FAILURE);
}

TEST_F(TcpTransportTest, WriteSucceedsAndReturnsBytesWritten) {
    int svr_sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(svr_sock, -1);

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serv_addr.sin_port = 0;
    ASSERT_EQ(bind(svr_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)), 0);

    socklen_t len = sizeof(serv_addr);
    ASSERT_EQ(getsockname(svr_sock, (struct sockaddr*)&serv_addr, &len), 0);
    int port = ntohs(serv_addr.sin_port);
    ASSERT_EQ(listen(svr_sock, 1), 0);

    std::thread server_thread([svr_sock]() {
        int client_fd = accept(svr_sock, nullptr, nullptr);
        if (client_fd >= 0) {
            char buffer[64];
            read(client_fd, buffer, sizeof(buffer));
            close(client_fd);
        }
    });

    Error connect_err = transport->connect(transport->context, "127.0.0.1", port);
    ASSERT_EQ(connect_err.type, ErrorType.NONE);

    const char* message = "test data";
    ssize_t bytes_written = 0;
    Error write_err = transport->write(transport->context, message, strlen(message), &bytes_written);

    ASSERT_EQ(write_err.type, ErrorType.NONE);
    ASSERT_EQ(bytes_written, strlen(message));

    shutdown(svr_sock, SHUT_RDWR);
    server_thread.join();
    close(svr_sock);
}

static ssize_t mock_write_fails(int, const void*, size_t) {
    return -1;
}

TEST_F(TcpTransportTest, WriteFailsOnSocketWriteError) {
    mock_syscalls.write = mock_write_fails;
    ReinitializeWithMocks();

    Error connect_err = transport->connect(transport->context, "127.0.0.1", listener_port);
    ASSERT_EQ(connect_err.type, ErrorType.NONE);

    const char* message = "test data";
    ssize_t bytes_written = 0;
    Error write_err = transport->write(transport->context, message, strlen(message), &bytes_written);

    ASSERT_EQ(write_err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(write_err.code, TransportErrorCode.SOCKET_WRITE_FAILURE);
    ASSERT_EQ(bytes_written, -1);
}

static bool setsockopt_called_with_nodelay = false;
static int mock_setsockopt_capture(int, int level, int optname, const void* optval, socklen_t) {
    if (level == IPPROTO_TCP && optname == TCP_NODELAY) {
        if (*(const int*)optval == 1) {
            setsockopt_called_with_nodelay = true;
        }
    }
    return 0;
}

TEST_F(TcpTransportTest, ConnectSetsTcpNoDelay) {
    setsockopt_called_with_nodelay = false;
    mock_syscalls.setsockopt = mock_setsockopt_capture;
    ReinitializeWithMocks();

    Error err = transport->connect(transport->context, "127.0.0.1", listener_port);

    ASSERT_EQ(err.type, ErrorType.NONE);
    ASSERT_TRUE(setsockopt_called_with_nodelay);
}

static ssize_t mock_write_fails_with_epipe(int, const void*, size_t) {
    errno = EPIPE; // Set the specific "Broken Pipe" error
    return -1;
}

TEST_F(TcpTransportTest, WriteFailsOnClosedConnection) {
    mock_syscalls.write = mock_write_fails_with_epipe;
    ReinitializeWithMocks();

    Error connect_err = transport->connect(transport->context, "127.0.0.1", listener_port);
    ASSERT_EQ(connect_err.type, ErrorType.NONE);

    const char* message = "test data";
    ssize_t bytes_written = 0;
    Error write_err = transport->write(transport->context, message, strlen(message), &bytes_written);

    ASSERT_EQ(write_err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(write_err.code, TransportErrorCode.SOCKET_WRITE_FAILURE);
    ASSERT_EQ(bytes_written, -1);
}

TEST_F(TcpTransportTest, CloseSucceedsAndInvalidatesFd) {
    Error connect_err = transport->connect(transport->context, "127.0.0.1", listener_port);
    ASSERT_EQ(connect_err.type, ErrorType.NONE);
    ASSERT_GT(client->fd, 0);

    Error close_err = transport->close(transport->context);

    ASSERT_EQ(close_err.type, ErrorType.NONE);
    ASSERT_EQ(client->fd, 0);
}

TEST_F(TcpTransportTest, CloseIsIdempotent) {
    Error connect_err = transport->connect(transport->context, "127.0.0.1", listener_port);
    ASSERT_EQ(connect_err.type, ErrorType.NONE);
    ASSERT_GT(client->fd, 0);

    Error err1 = transport->close(transport->context);
    Error err2 = transport->close(transport->context);

    ASSERT_EQ(err1.type, ErrorType.NONE);
    ASSERT_EQ(err2.type, ErrorType.NONE);
    ASSERT_EQ(client->fd, 0);
}

static int mock_close_fails(int) {
    return -1;
}

TEST_F(TcpTransportTest, CloseFailsOnSyscallError) {
    mock_syscalls.close = mock_close_fails;
    ReinitializeWithMocks();

    Error connect_err = transport->connect(transport->context, "127.0.0.1", listener_port);
    ASSERT_EQ(connect_err.type, ErrorType.NONE);

    Error close_err = transport->close(transport->context);

    ASSERT_EQ(close_err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(close_err.code, TransportErrorCode.SOCKET_CLOSE_FAILURE);
}

TEST_F(TcpTransportTest, WriteFailsIfNotConnected) {
    const char* message = "test";
    ssize_t bytes_written = 0;
    Error err = transport->write(transport->context, message, strlen(message), &bytes_written);

    ASSERT_EQ(err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(err.code, TransportErrorCode.SOCKET_WRITE_FAILURE);
    ASSERT_EQ(bytes_written, -1);
}

TEST_F(TcpTransportTest, ReadFailsIfNotConnected) {
    char buffer[32] = {};
    ssize_t bytes_read = 0;
    Error err = transport->read(transport->context, buffer, sizeof(buffer), &bytes_read);

    ASSERT_EQ(err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(err.code, TransportErrorCode.SOCKET_READ_FAILURE);
    ASSERT_EQ(bytes_read, -1);
}