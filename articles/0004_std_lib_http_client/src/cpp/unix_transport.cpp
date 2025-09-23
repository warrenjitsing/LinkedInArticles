#include <httpcpp/unix_transport.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace httpcpp {

UnixTransport::UnixTransport() noexcept = default;

UnixTransport::~UnixTransport() noexcept {
    if (fd_ != -1) {
        ::close(fd_);
    }
}

auto UnixTransport::connect(const char* path, [[maybe_unused]] uint16_t port) noexcept -> std::expected<void, TransportError> {
    if (fd_ != -1) {
        return std::unexpected(TransportError::SocketConnectFailure);
    }

    fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ == -1) {
        return std::unexpected(TransportError::SocketCreateFailure);
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (::connect(fd_, (const sockaddr*)&addr, sizeof(addr)) == -1) {
        ::close(fd_);
        fd_ = -1;
        return std::unexpected(TransportError::SocketConnectFailure);
    }

    return {};
}

auto UnixTransport::close() noexcept -> std::expected<void, TransportError> {
    if (fd_ == -1) {
        return {};
    }

    int result = ::close(fd_);
    if (result == -1) {
        // fd_ is now in an indeterminate state, but we'll reset it.
        fd_ = -1;
        return std::unexpected(TransportError::SocketCloseFailure);
    }

    fd_ = -1;
    return {};
}

auto UnixTransport::write(std::span<const std::byte> data) noexcept -> std::expected<size_t, TransportError> {
    if (fd_ == -1) {
        return std::unexpected(TransportError::SocketWriteFailure);
    }

    ssize_t bytes_written = ::write(fd_, data.data(), data.size());

    if (bytes_written == -1) {
        return std::unexpected(TransportError::SocketWriteFailure);
    }

    return bytes_written;
}

auto UnixTransport::read(std::span<std::byte> buffer) noexcept -> std::expected<size_t, TransportError> {
    if (fd_ == -1) {
        return std::unexpected(TransportError::SocketReadFailure);
    }

    ssize_t bytes_read = ::read(fd_, buffer.data(), buffer.size());

    if (bytes_read == -1) {
        return std::unexpected(TransportError::SocketReadFailure);
    }

    if (bytes_read == 0) {
        return std::unexpected(TransportError::ConnectionClosed);
    }

    return bytes_read;
}

} // namespace httpcpp