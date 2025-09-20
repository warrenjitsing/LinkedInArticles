#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <csignal>
#include <fcntl.h>
#include <cerrno>

extern "C" {
#include <httpc/unix_transport.h>
}

class UnixTransportTest : public ::testing::Test {
protected:
    TransportInterface* transport;
    UnixClient* client;
    HttpcSyscalls mock_syscalls;

    std::thread listener_thread;
    std::atomic<bool> listener_should_stop{false};
    int listener_fd = -1;
    std::string socket_path;
    const char* test_message = "hello world";

    void SetUp() override {
        signal(SIGPIPE, SIG_IGN);

        transport = unix_transport_new(nullptr);
        ASSERT_NE(transport, nullptr);
        client = (UnixClient*)transport;
        httpc_syscalls_init_default(&mock_syscalls);

        start_listener();
    }

    void TearDown() override {
        stop_listener();
        unix_transport_destroy(transport);
    }

    void start_listener() {
        socket_path = std::string("/tmp/httpc_test_") + std::to_string(getpid());
        unlink(socket_path.c_str());

        listener_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        ASSERT_NE(listener_fd, -1);

        struct sockaddr_un serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sun_family = AF_UNIX;
        strncpy(serv_addr.sun_path, socket_path.c_str(), sizeof(serv_addr.sun_path) - 1);

        ASSERT_EQ(bind(listener_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)), 0);
        ASSERT_EQ(listen(listener_fd, 1), 0);

        listener_thread = std::thread([this]() {
            while (!listener_should_stop) {
                int client_fd = accept(listener_fd, nullptr, nullptr);
                if (client_fd >= 0) {
                    write(client_fd, test_message, strlen(test_message));
                    close(client_fd);
                }
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
        unlink(socket_path.c_str());
    }

    void ReinitializeWithMocks() {
        unix_transport_destroy(transport);
        transport = unix_transport_new(&mock_syscalls);
        ASSERT_NE(transport, nullptr);
        client = (UnixClient*)transport;
    }
};

TEST_F(UnixTransportTest, NewSucceedsWithDefaultSyscalls) {
    ASSERT_NE(transport, nullptr);
    ASSERT_NE(client, nullptr);
    ASSERT_EQ(transport->context, client);
    ASSERT_NE(client->syscalls, nullptr);
    ASSERT_EQ(client->syscalls->socket, socket);
}

TEST(UnixTransportLifecycle, NewSucceedsWithOverrideSyscalls) {
    HttpcSyscalls mock_syscalls;
    memset(&mock_syscalls, 0, sizeof(mock_syscalls));
    httpc_syscalls_init_default(&mock_syscalls);
    TransportInterface* transport = unix_transport_new(&mock_syscalls);
    ASSERT_NE(transport, nullptr);
    UnixClient* client = (UnixClient*)transport;
    ASSERT_EQ(client->syscalls, &mock_syscalls);
    unix_transport_destroy(transport);
}

TEST(UnixTransportLifecycle, DestroyHandlesNullGracefully) {
    unix_transport_destroy(nullptr);
    SUCCEED();
}

TEST_F(UnixTransportTest, ConnectSucceedsOnLocalListener) {
    Error err = transport->connect(transport->context, socket_path.c_str(), 0);
    ASSERT_EQ(err.type, ErrorType.NONE);
    ASSERT_GT(client->fd, 0);
}

static int mock_socket_creation_fails(int, int, int) {
    return -1;
}

TEST_F(UnixTransportTest, ConnectFailsOnSocketCreation) {
    mock_syscalls.socket = mock_socket_creation_fails;
    ReinitializeWithMocks();
    Error err = transport->connect(transport->context, "/tmp/dummy.sock", 0);
    ASSERT_EQ(err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(err.code, TransportErrorCode.SOCKET_CREATE_FAILURE);
}

static int mock_connect_fails(int, const struct sockaddr*, socklen_t) {
    return -1;
}

TEST_F(UnixTransportTest, ConnectFailsOnConnectionFailure) {
    mock_syscalls.connect = mock_connect_fails;
    ReinitializeWithMocks();
    Error err = transport->connect(transport->context, "/tmp/dummy.sock", 0);
    ASSERT_EQ(err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(err.code, TransportErrorCode.SOCKET_CONNECT_FAILURE);
}

TEST_F(UnixTransportTest, ReadSucceedsAndReturnsBytesRead) {
    Error connect_err = transport->connect(transport->context, socket_path.c_str(), 0);
    ASSERT_EQ(connect_err.type, ErrorType.NONE);

    char buffer[32] = {0};
    ssize_t bytes_read = 0;
    Error read_err = transport->read(transport->context, buffer, sizeof(buffer) - 1, &bytes_read);

    ASSERT_EQ(read_err.type, ErrorType.NONE);
    ASSERT_EQ(bytes_read, strlen(test_message));
    ASSERT_STREQ(buffer, test_message);
}

TEST_F(UnixTransportTest, ReadReturnsConnectionClosedOnPeerShutdown) {
    std::string test_socket_path = "/tmp/test_shutdown.sock";
    unlink(test_socket_path.c_str());

    int svr_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT_NE(svr_sock, -1);

    struct sockaddr_un serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sun_family = AF_UNIX;
    strncpy(serv_addr.sun_path, test_socket_path.c_str(), sizeof(serv_addr.sun_path) - 1);

    ASSERT_EQ(bind(svr_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)), 0);
    ASSERT_EQ(listen(svr_sock, 1), 0);

    std::thread server_thread([svr_sock]() {
        int client_fd = accept(svr_sock, nullptr, nullptr);
        if (client_fd >= 0) {
            close(client_fd);
        }
    });

    Error connect_err = transport->connect(transport->context, test_socket_path.c_str(), 0);
    ASSERT_EQ(connect_err.type, ErrorType.NONE);

    char buffer[32];
    ssize_t bytes_read = 0;
    Error read_err = transport->read(transport->context, buffer, sizeof(buffer), &bytes_read);

    ASSERT_EQ(read_err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(read_err.code, TransportErrorCode.CONNECTION_CLOSED);

    server_thread.join();
    close(svr_sock);
    unlink(test_socket_path.c_str());
}

static ssize_t mock_read_fails(int, void*, size_t) {
    return -1;
}

TEST_F(UnixTransportTest, ReadFailsOnSocketReadError) {
    mock_syscalls.read = mock_read_fails;
    ReinitializeWithMocks();
    Error connect_err = transport->connect(transport->context, socket_path.c_str(), 0);
    ASSERT_EQ(connect_err.type, ErrorType.NONE);

    char buffer[32];
    ssize_t bytes_read = 0;
    Error read_err = transport->read(transport->context, buffer, sizeof(buffer), &bytes_read);

    ASSERT_EQ(read_err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(read_err.code, TransportErrorCode.SOCKET_READ_FAILURE);
}

TEST_F(UnixTransportTest, WriteSucceedsAndReturnsBytesWritten) {
    std::string test_socket_path = "/tmp/test_write.sock";
    unlink(test_socket_path.c_str());

    int svr_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT_NE(svr_sock, -1);

    struct sockaddr_un serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sun_family = AF_UNIX;
    strncpy(serv_addr.sun_path, test_socket_path.c_str(), sizeof(serv_addr.sun_path) - 1);

    ASSERT_EQ(bind(svr_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)), 0);
    ASSERT_EQ(listen(svr_sock, 1), 0);

    std::thread server_thread([svr_sock]() {
        int client_fd = accept(svr_sock, nullptr, nullptr);
        if (client_fd >= 0) {
            char buffer[64];
            read(client_fd, buffer, sizeof(buffer));
            close(client_fd);
        }
    });

    Error connect_err = transport->connect(transport->context, test_socket_path.c_str(), 0);
    ASSERT_EQ(connect_err.type, ErrorType.NONE);

    const char* message = "test data";
    ssize_t bytes_written = 0;
    Error write_err = transport->write(transport->context, message, strlen(message), &bytes_written);

    ASSERT_EQ(write_err.type, ErrorType.NONE);
    ASSERT_EQ(bytes_written, strlen(message));

    server_thread.join();
    close(svr_sock);
    unlink(test_socket_path.c_str());
}

static ssize_t mock_write_fails(int, const void*, size_t) {
    return -1;
}

TEST_F(UnixTransportTest, WriteFailsOnSocketWriteError) {
    mock_syscalls.write = mock_write_fails;
    ReinitializeWithMocks();
    Error connect_err = transport->connect(transport->context, socket_path.c_str(), 0);
    ASSERT_EQ(connect_err.type, ErrorType.NONE);

    const char* message = "test data";
    ssize_t bytes_written = 0;
    Error write_err = transport->write(transport->context, message, strlen(message), &bytes_written);

    ASSERT_EQ(write_err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(write_err.code, TransportErrorCode.SOCKET_WRITE_FAILURE);
    ASSERT_EQ(bytes_written, -1);
}

static ssize_t mock_write_fails_with_epipe(int, const void*, size_t) {
    errno = EPIPE;
    return -1;
}

TEST_F(UnixTransportTest, WriteFailsOnClosedConnection) {
    mock_syscalls.write = mock_write_fails_with_epipe;
    ReinitializeWithMocks();

    Error connect_err = transport->connect(transport->context, socket_path.c_str(), 0);
    ASSERT_EQ(connect_err.type, ErrorType.NONE);

    const char* message = "test data";
    ssize_t bytes_written = 0;
    Error write_err = transport->write(transport->context, message, strlen(message), &bytes_written);

    ASSERT_EQ(write_err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(write_err.code, TransportErrorCode.SOCKET_WRITE_FAILURE);
    ASSERT_EQ(bytes_written, -1);
}

TEST_F(UnixTransportTest, CloseSucceedsAndInvalidatesFd) {
    Error connect_err = transport->connect(transport->context, socket_path.c_str(), 0);
    ASSERT_EQ(connect_err.type, ErrorType.NONE);
    ASSERT_GT(client->fd, 0);
    Error close_err = transport->close(transport->context);
    ASSERT_EQ(close_err.type, ErrorType.NONE);
    ASSERT_EQ(client->fd, 0);
}

TEST_F(UnixTransportTest, CloseIsIdempotent) {
    Error connect_err = transport->connect(transport->context, socket_path.c_str(), 0);
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

TEST_F(UnixTransportTest, CloseFailsOnSyscallError) {
    mock_syscalls.close = mock_close_fails;
    ReinitializeWithMocks();
    Error connect_err = transport->connect(transport->context, socket_path.c_str(), 0);
    ASSERT_EQ(connect_err.type, ErrorType.NONE);
    Error close_err = transport->close(transport->context);
    ASSERT_EQ(close_err.type, ErrorType.TRANSPORT);
    ASSERT_EQ(close_err.code, TransportErrorCode.SOCKET_CLOSE_FAILURE);
}