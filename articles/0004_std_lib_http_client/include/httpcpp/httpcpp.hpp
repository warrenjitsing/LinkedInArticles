#pragma once
#include <algorithm>
#include <cctype>


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

        [[nodiscard]] auto get_safe(HttpRequest& request) noexcept -> std::expected<SafeHttpResponse, Error> {
            if (!request.body.empty()) {
                return std::unexpected(HttpClientError::InvalidRequest);
            }
            request.method = HttpMethod::Get;
            return protocol_.perform_request_safe(request);
        }

        [[nodiscard]] auto get_unsafe(HttpRequest& request) noexcept -> std::expected<UnsafeHttpResponse, Error> {
            if (!request.body.empty()) {
                return std::unexpected(HttpClientError::InvalidRequest);
            }
            request.method = HttpMethod::Get;
            return protocol_.perform_request_unsafe(request);
        }

        [[nodiscard]] auto post_safe(HttpRequest& request) noexcept -> std::expected<SafeHttpResponse, Error> {
            if (auto validation_result = validate_post_request(request); !validation_result) {
                return std::unexpected(validation_result.error());
            }
            request.method = HttpMethod::Post;
            return protocol_.perform_request_safe(request);
        }


        [[nodiscard]] auto post_unsafe(HttpRequest& request) noexcept -> std::expected<UnsafeHttpResponse, Error> {
            if (auto validation_result = validate_post_request(request); !validation_result) {
                return std::unexpected(validation_result.error());
            }
            request.method = HttpMethod::Post;
            return protocol_.perform_request_unsafe(request);
        }

    private:
        P protocol_;

        [[nodiscard]] auto validate_post_request(const HttpRequest& request) noexcept -> std::expected<void, Error> {
            if (request.body.empty()) {
                return std::unexpected(HttpClientError::InvalidRequest);
            }

            bool content_length_found = false;
            std::string_view cl_key = "Content-Length";
            for (const auto& [key, value] : request.headers) {
                if (key.size() == cl_key.size() &&
                    std::equal(key.begin(), key.end(), cl_key.begin(),
                               [](char a, char b) { return std::tolower(a) == std::tolower(b); })) {
                                   content_length_found = true;
                                   break;
                               }
            }

            if (!content_length_found) {
                return std::unexpected(HttpClientError::InvalidRequest);
            }

            return {};
        }
    };
} // namespace httpcpp