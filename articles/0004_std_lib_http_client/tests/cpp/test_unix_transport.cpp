#include <gtest/gtest.h>
#include <httpcpp/unix_transport.hpp>

#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <csignal>
#include <functional>
#include <future>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

class UnixTransportTest : public ::testing::Test {
protected:
    std::thread server_thread_;
    std::atomic<bool> should_stop_{false};
    int listener_fd_ = -1;

    std::string socket_path_;
    std::string captured_message_;
    std::function<void(int)> server_logic_;

    httpcpp::UnixTransport transport_;

    void SetUp() override {
        signal(SIGPIPE, SIG_IGN);
    }

    void TearDown() override {
        StopServer();
    }

    void StartServer(std::function<void(int)> server_logic) {
        server_logic_ = std::move(server_logic);

        socket_path_ = "/tmp/httpcpp_test_" + std::to_string(getpid());
        unlink(socket_path_.c_str());

        listener_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        ASSERT_NE(listener_fd_, -1);

        sockaddr_un serv_addr{};
        serv_addr.sun_family = AF_UNIX;
        strncpy(serv_addr.sun_path, socket_path_.c_str(), sizeof(serv_addr.sun_path) - 1);

        ASSERT_EQ(bind(listener_fd_, (struct sockaddr*)&serv_addr, sizeof(serv_addr)), 0);
        ASSERT_EQ(listen(listener_fd_, 1), 0);

        server_thread_ = std::thread(&UnixTransportTest::AcceptLoop, this);
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
                unlink(socket_path_.c_str());
            }
        }
    }

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

TEST(UnixTransportLifecycle, ConstructionSucceeds) {
    httpcpp::UnixTransport transport;
    SUCCEED();
}

TEST_F(UnixTransportTest, ConnectSucceeds) {
    StartServer([](int){});
    auto result = transport_.connect(socket_path_.c_str(), 0);
    ASSERT_TRUE(result.has_value());
}

TEST_F(UnixTransportTest, WriteSucceeds) {
    std::promise<void> server_read_promise;
    auto server_read_future = server_read_promise.get_future();

    StartServer([this, &server_read_promise](int client_fd){
        std::vector<char> buffer(1024, 0);
        ssize_t bytes_read = read(client_fd, buffer.data(), buffer.size() - 1);
        if (bytes_read > 0) {
            captured_message_.assign(buffer.data(), bytes_read);
        }
        server_read_promise.set_value();
    });

    auto connect_result = transport_.connect(socket_path_.c_str(), 0);
    ASSERT_TRUE(connect_result.has_value());

    const std::string message = "hello unix server";
    auto write_result = transport_.write({
        reinterpret_cast<const std::byte*>(message.data()), message.size()
    });
    ASSERT_TRUE(write_result.has_value());

    server_read_future.wait();
    ASSERT_EQ(captured_message_, message);
}

TEST_F(UnixTransportTest, ReadSucceeds) {
    const std::string server_message = "hello unix client";
    StartServer([&server_message](int client_fd){
        write(client_fd, server_message.c_str(), server_message.length());
    });

    auto connect_result = transport_.connect(socket_path_.c_str(), 0);
    ASSERT_TRUE(connect_result.has_value());

    std::vector<std::byte> buffer(1024);
    auto read_result = transport_.read(buffer);
    ASSERT_TRUE(read_result.has_value());
    ASSERT_EQ(*read_result, server_message.size());

    std::string received_message(reinterpret_cast<const char*>(buffer.data()), *read_result);
    ASSERT_EQ(received_message, server_message);
}

TEST_F(UnixTransportTest, CloseSucceeds) {
    StartServer([](int){});
    auto connect_result = transport_.connect(socket_path_.c_str(), 0);
    ASSERT_TRUE(connect_result.has_value());
    auto close_result = transport_.close();
    ASSERT_TRUE(close_result.has_value());
}

TEST_F(UnixTransportTest, CloseIsIdempotent) {
    StartServer([](int){});
    auto connect_result = transport_.connect(socket_path_.c_str(), 0);
    ASSERT_TRUE(connect_result.has_value());
    ASSERT_TRUE(transport_.close().has_value());
    ASSERT_TRUE(transport_.close().has_value());
}

TEST_F(UnixTransportTest, ConnectFailsOnUnresponsiveSocket) {
    auto result = transport_.connect("/tmp/non-existent-socket-path.sock", 0);
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), httpcpp::TransportError::SocketConnectFailure);
}

TEST_F(UnixTransportTest, ReadFailsOnPeerShutdown) {
    std::promise<void> server_accepted_promise;
    auto server_accepted_future = server_accepted_promise.get_future();
    StartServer([&server_accepted_promise](int){
        server_accepted_promise.set_value();
    });

    auto connect_result = transport_.connect(socket_path_.c_str(), 0);
    ASSERT_TRUE(connect_result.has_value());
    server_accepted_future.wait();

    std::vector<std::byte> buffer(1024);
    auto read_result = transport_.read(buffer);
    ASSERT_FALSE(read_result.has_value());
    ASSERT_EQ(read_result.error(), httpcpp::TransportError::ConnectionClosed);
}

TEST_F(UnixTransportTest, WriteFailsIfNotConnected) {
    const std::string message = "test";
    auto write_result = transport_.write({
        reinterpret_cast<const std::byte*>(message.data()), message.size()
    });

    ASSERT_FALSE(write_result.has_value());
    ASSERT_EQ(write_result.error(), httpcpp::TransportError::SocketWriteFailure);
}

TEST_F(UnixTransportTest, ReadFailsIfNotConnected) {
    std::vector<std::byte> buffer(1024);
    auto read_result = transport_.read(buffer);

    ASSERT_FALSE(read_result.has_value());
    ASSERT_EQ(read_result.error(), httpcpp::TransportError::SocketReadFailure);
}

TEST_F(UnixTransportTest, ConnectFailsIfAlreadyConnected) {
    StartServer([](int){});

    auto connect_result1 = transport_.connect(socket_path_.c_str(), 0);
    ASSERT_TRUE(connect_result1.has_value());

    auto connect_result2 = transport_.connect(socket_path_.c_str(), 0);
    ASSERT_FALSE(connect_result2.has_value());
    ASSERT_EQ(connect_result2.error(), httpcpp::TransportError::SocketConnectFailure);
}

TEST_F(UnixTransportTest, WriteFailsOnClosedConnection) {
    std::promise<void> server_closed_promise;
    auto server_closed_future = server_closed_promise.get_future();

    StartServer([&server_closed_promise](int client_fd){
        struct linger so_linger{};
        so_linger.l_onoff = 1;
        so_linger.l_linger = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
        server_closed_promise.set_value();
    });

    auto connect_result = transport_.connect(socket_path_.c_str(), 0);
    ASSERT_TRUE(connect_result.has_value());

    server_closed_future.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const std::string message = "this will fail";
    auto write_result = transport_.write({
        reinterpret_cast<const std::byte*>(message.data()), message.size()
    });

    ASSERT_FALSE(write_result.has_value());
    ASSERT_EQ(write_result.error(), httpcpp::TransportError::SocketWriteFailure);
}