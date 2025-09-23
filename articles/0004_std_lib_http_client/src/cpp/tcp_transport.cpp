#include <httpcpp/tcp_transport.hpp>
#include <string>

namespace httpcpp {

TcpTransport::TcpTransport() noexcept : socket_(io_context_) {}

TcpTransport::~TcpTransport() noexcept {
    if (socket_.is_open()) {
        std::error_code ec;
        socket_.close(ec);
    }
}

auto TcpTransport::connect(const char* host, uint16_t port) noexcept -> std::expected<void, TransportError> {
    std::error_code ec;

    net::ip::tcp::resolver resolver(io_context_);
    auto endpoints = resolver.resolve(host, std::to_string(port), ec);

    if (ec) {
        return std::unexpected(TransportError::DnsFailure);
    }

    // Buggy
    // net::connect(socket_, endpoints, ec);

    // Manual connection loop to bypass the bug in the net::connect free function.
    for (const auto& endpoint : endpoints) {
        std::error_code close_ec;
        socket_.close(close_ec);

        socket_.connect(endpoint.endpoint(), ec);

        if (!ec) {
            break;
        }
    }

    if (ec) {
        return std::unexpected(TransportError::SocketConnectFailure);
    }

    socket_.set_option(net::ip::tcp::no_delay(true), ec);
    if (ec) {
        if (!close())
        {
            return std::unexpected(TransportError::SocketCloseFailure);
        }
        return std::unexpected(TransportError::SocketConnectFailure);
    }

    return {};
}

auto TcpTransport::close() noexcept -> std::expected<void, TransportError> {
    if (!socket_.is_open()) {
        return {};
    }
    std::error_code ec;
    socket_.close(ec);
    if (ec) {
        return std::unexpected(TransportError::SocketCloseFailure);
    }
    return {};
}

auto TcpTransport::write(std::span<const std::byte> data) noexcept -> std::expected<size_t, TransportError> {
    std::error_code ec;
    size_t bytes_written = socket_.write_some(net::buffer(data.data(), data.size()), ec);
    if (ec) {
        return std::unexpected(TransportError::SocketWriteFailure);
    }
    return bytes_written;
}

auto TcpTransport::read(std::span<std::byte> buffer) noexcept -> std::expected<size_t, TransportError> {
    std::error_code ec;
    size_t bytes_read = socket_.read_some(net::buffer(buffer.data(), buffer.size()), ec);

    if (ec == net::stream_errc::eof) {
        return std::unexpected(TransportError::ConnectionClosed);
    }
    if (ec) {
        return std::unexpected(TransportError::SocketReadFailure);
    }

    if (bytes_read == 0 && !buffer.empty()) {
        return std::unexpected(TransportError::ConnectionClosed);
    }

    return bytes_read;
}

} // namespace httpcpp