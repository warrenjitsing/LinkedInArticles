#pragma once

#include <experimental/net>

#include <httpcpp/transport.hpp>


namespace net = std::experimental::net;

namespace httpcpp {

    class TcpTransport {
    public:
        TcpTransport() noexcept;
        ~TcpTransport() noexcept;

        TcpTransport(const TcpTransport&) = delete;
        TcpTransport& operator=(const TcpTransport&) = delete;
        TcpTransport(TcpTransport&&) noexcept = default;
        TcpTransport& operator=(TcpTransport&&) noexcept = default;

        [[nodiscard]] auto connect(const char* host, uint16_t port) noexcept -> std::expected<void, TransportError>;
        [[nodiscard]] auto close() noexcept -> std::expected<void, TransportError>;
        [[nodiscard]] auto write(std::span<const std::byte> data) noexcept -> std::expected<size_t, TransportError>;
        [[nodiscard]] auto read(std::span<std::byte> buffer) noexcept -> std::expected<size_t, TransportError>;

    private:
        net::io_context io_context_;
        net::ip::tcp::socket socket_;
    };

    static_assert(Transport<TcpTransport>);


} // namespace httpcpp