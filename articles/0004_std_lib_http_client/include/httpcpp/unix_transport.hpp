#pragma once

#include <httpcpp/transport.hpp>

namespace httpcpp {

    class UnixTransport {
    public:
        UnixTransport() noexcept;
        ~UnixTransport() noexcept;

        UnixTransport(const UnixTransport&) = delete;
        UnixTransport& operator=(const UnixTransport&) = delete;
        UnixTransport(UnixTransport&&) noexcept = default;
        UnixTransport& operator=(UnixTransport&&) noexcept = default;

        [[nodiscard]] auto connect(const char* path, uint16_t port) noexcept -> std::expected<void, TransportError>;
        [[nodiscard]] auto close() noexcept -> std::expected<void, TransportError>;
        [[nodiscard]] auto write(std::span<const std::byte> data) noexcept -> std::expected<size_t, TransportError>;
        [[nodiscard]] auto read(std::span<std::byte> buffer) noexcept -> std::expected<size_t, TransportError>;

    private:
        int fd_ = -1;
    };

    static_assert(Transport<UnixTransport>);

} // namespace httpcpp