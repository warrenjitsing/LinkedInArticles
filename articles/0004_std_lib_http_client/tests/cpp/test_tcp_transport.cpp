#include <gtest/gtest.h>
#include <httpcpp/tcp_transport.hpp>

#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <csignal>
#include <functional>
#include <future>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

class TcpTransportTest : public ::testing::Test {
protected:
    std::thread server_thread_;
    std::atomic<bool> should_stop_{false};
    int listener_fd_ = -1;

    // Server state
    int port_ = 0;
    std::string captured_message_;
    std::function<void(int)> server_logic_;

    httpcpp::TcpTransport transport_;

    void SetUp() override {
        signal(SIGPIPE, SIG_IGN);
    }

    void TearDown() override {
        StopServer();
    }

    void StartServer(std::function<void(int)> server_logic) {
        server_logic_ = std::move(server_logic);

        listener_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_NE(listener_fd_, -1);

        sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        serv_addr.sin_port = 0;
        ASSERT_EQ(bind(listener_fd_, (struct sockaddr*)&serv_addr, sizeof(serv_addr)), 0);

        socklen_t len = sizeof(serv_addr);
        ASSERT_EQ(getsockname(listener_fd_, (struct sockaddr*)&serv_addr, &len), 0);
        port_ = ntohs(serv_addr.sin_port);
        ASSERT_GT(port_, 0);

        // The listen() call now correctly happens here, on the main thread.
        ASSERT_EQ(listen(listener_fd_, 1), 0);

        server_thread_ = std::thread(&TcpTransportTest::AcceptLoop, this);
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
            }
        }
    }

    std::promise<void> server_read_promise_;

private:
    void AcceptLoop() {
        int client_fd = accept(listener_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            return;
        }

        if (server_logic_) {
            server_logic_(client_fd);
        }

        close(client_fd);
    }
};

TEST(TcpTransportLifecycle, ConstructionSucceeds) {
    httpcpp::TcpTransport transport;
    SUCCEED();
}

TEST_F(TcpTransportTest, ConnectSucceeds) {
    StartServer([](int client_fd){
        (void)client_fd;
    });

    auto result = transport_.connect("127.0.0.1", port_);
    ASSERT_TRUE(result.has_value()) << "Connect failed with: "
        << static_cast<int>(result.error());
}

TEST_F(TcpTransportTest, WriteSucceeds) {
    server_read_promise_ = std::promise<void>();
    auto server_read_future = server_read_promise_.get_future();

    StartServer([this](int client_fd){
        std::vector<char> buffer(1024, 0);
        ssize_t bytes_read = read(client_fd, buffer.data(), buffer.size() - 1);
        if (bytes_read > 0) {
            captured_message_.assign(buffer.data(), bytes_read);
        }
        server_read_promise_.set_value();
    });

    auto connect_result = transport_.connect("127.0.0.1", port_);
    ASSERT_TRUE(connect_result.has_value());

    const std::string message_to_send = "hello server";
    std::span<const std::byte> write_buffer(
        reinterpret_cast<const std::byte*>(message_to_send.data()),
        message_to_send.size()
    );
    auto write_result = transport_.write(write_buffer);
    ASSERT_TRUE(write_result.has_value());

    server_read_future.wait();

    ASSERT_EQ(captured_message_, message_to_send);
}

TEST_F(TcpTransportTest, ReadSucceeds) {
    // 1. Define server behavior: write a message to the connected client.
    const std::string message_from_server = "hello from server";
    StartServer([&message_from_server](int client_fd){
        write(client_fd, message_from_server.c_str(), message_from_server.length());
    });

    // 2. Connect the client.
    auto connect_result = transport_.connect("127.0.0.1", port_);
    ASSERT_TRUE(connect_result.has_value());

    // 3. Prepare a buffer and call read.
    std::vector<std::byte> read_buffer(1024);
    auto read_result = transport_.read(read_buffer);

    // 4. Assert the read was successful and the byte count is correct.
    ASSERT_TRUE(read_result.has_value());
    ASSERT_EQ(*read_result, message_from_server.size());

    // 5. Assert the content of the buffer is correct.
    std::string received_message(
        reinterpret_cast<const char*>(read_buffer.data()),
        *read_result
    );
    ASSERT_EQ(received_message, message_from_server);
}

TEST_F(TcpTransportTest, CloseSucceeds) {
    StartServer([](int client_fd){
        (void)client_fd;
    });

    auto connect_result = transport_.connect("127.0.0.1", port_);
    ASSERT_TRUE(connect_result.has_value());

    auto close_result = transport_.close();

    ASSERT_TRUE(close_result.has_value()) << "Close failed with: "
        << static_cast<int>(close_result.error());
}

TEST_F(TcpTransportTest, CloseIsIdempotent) {
    StartServer([](int client_fd){
        (void)client_fd;
    });

    auto connect_result = transport_.connect("127.0.0.1", port_);
    ASSERT_TRUE(connect_result.has_value());

    auto close_result1 = transport_.close();
    ASSERT_TRUE(close_result1.has_value()) << "First close failed with: "
        << static_cast<int>(close_result1.error());

    auto close_result2 = transport_.close();
    ASSERT_TRUE(close_result2.has_value()) << "Second close failed with: "
        << static_cast<int>(close_result2.error());
}

TEST_F(TcpTransportTest, ConnectFailsOnUnresponsivePort) {
    const uint16_t unresponsive_port = 65531;

    auto result = transport_.connect("127.0.0.1", unresponsive_port);

    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), httpcpp::TransportError::SocketConnectFailure);
}

TEST_F(TcpTransportTest, ConnectFailsOnDnsFailure) {
    auto result = transport_.connect("a-hostname-that-does-not-exist.invalid", 80);

    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), httpcpp::TransportError::DnsFailure);
}

TEST_F(TcpTransportTest, WriteFailsOnClosedConnection) {
    std::promise<void> server_closed_promise;
    auto server_closed_future = server_closed_promise.get_future();

    StartServer([&server_closed_promise](int client_fd){
        // Set SO_LINGER to force a TCP RST (reset) on close, making the
        // connection failure immediate and deterministic for the client.
        struct linger so_linger;
        so_linger.l_onoff = 1;
        so_linger.l_linger = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));

        // The fixture's AcceptLoop will close the socket after this lambda.
        server_closed_promise.set_value();
    });

    auto connect_result = transport_.connect("127.0.0.1", port_);
    ASSERT_TRUE(connect_result.has_value());

    server_closed_future.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const std::string message = "this will fail";
    std::span<const std::byte> write_buffer(
        reinterpret_cast<const std::byte*>(message.data()),
        message.size()
    );
    auto write_result = transport_.write(write_buffer);

    ASSERT_FALSE(write_result.has_value());
    ASSERT_EQ(write_result.error(), httpcpp::TransportError::SocketWriteFailure);
}

TEST_F(TcpTransportTest, ReadFailsOnPeerShutdown) {
    std::promise<void> server_accepted_promise;
    auto server_accepted_future = server_accepted_promise.get_future();

    StartServer([&server_accepted_promise](int client_fd){
        (void)client_fd;
        server_accepted_promise.set_value();
    });

    auto connect_result = transport_.connect("127.0.0.1", port_);
    ASSERT_TRUE(connect_result.has_value());

    server_accepted_future.wait();

    std::vector<std::byte> read_buffer(1024);
    auto read_result = transport_.read(read_buffer);

    ASSERT_FALSE(read_result.has_value());
    ASSERT_EQ(read_result.error(), httpcpp::TransportError::ConnectionClosed);
}