#pragma once

#include <httpcpp/error.hpp>
#include <httpcpp/http_protocol.hpp>
#include <httpcpp/http1_protocol.hpp>
#include <httpcpp/tcp_transport.hpp>
#include <httpcpp/unix_transport.hpp>

namespace httpcpp {

    template<HttpProtocol P>
    class HttpClient {
    public:
        HttpClient() = default;
        ~HttpClient() = default;

        HttpClient(const HttpClient&) = delete;
        HttpClient& operator=(const HttpClient&) = delete;
        HttpClient(HttpClient&&) = delete;
        HttpClient& operator=(HttpClient&&) = delete;

        [[nodiscard]] auto connect(const char* host, uint16_t port) noexcept -> std::expected<void, Error> {
            return protocol_.connect(host, port);
        }

        [[nodiscard]] auto disconnect() noexcept -> std::expected<void, Error> {
            return protocol_.disconnect();
        }

        [[nodiscard]] auto get_safe(std::string_view path) noexcept -> std::expected<SafeHttpResponse, Error> {
            // Stub
            return std::unexpected(HttpClientError::InitFailure);
        }

        [[nodiscard]] auto get_unsafe(std::string_view path) noexcept -> std::expected<UnsafeHttpResponse, Error> {
            // Stub
            return std::unexpected(HttpClientError::InitFailure);
        }

        [[nodiscard]] auto post_safe(std::string_view path, std::span<const std::byte> body) noexcept -> std::expected<SafeHttpResponse, Error> {
            // Stub
            return std::unexpected(HttpClientError::InitFailure);
        }

        [[nodiscard]] auto post_unsafe(std::string_view path, std::span<const std::byte> body) noexcept -> std::expected<UnsafeHttpResponse, Error> {
            // Stub
            return std::unexpected(HttpClientError::InitFailure);
        }

    private:
        P protocol_;
    };
} // namespace httpcpp