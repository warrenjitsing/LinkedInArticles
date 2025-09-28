#pragma once

#include <httpcpp/transport.hpp>
#include <httpcpp/error.hpp>
#include <vector>
#include <string>
#include <string_view>
#include <utility>
#include <span>
#include <optional>

namespace httpcpp {

    enum class HttpMethod {
        Get,
        Post,
    };

    using HttpHeaderView = std::pair<std::string_view, std::string_view>;
    using HttpOwnedHeader = std::pair<std::string, std::string>;

    struct HttpRequest {
        HttpMethod method = HttpMethod::Get;
        std::string_view path;
        std::span<const std::byte> body;
        std::vector<HttpHeaderView> headers;
    };

    struct UnsafeHttpResponse {
        int status_code;
        std::string_view status_message;
        std::span<const std::byte> body;
        std::vector<HttpHeaderView> headers;
        std::optional<size_t> content_length = std::nullopt;
    };

    struct SafeHttpResponse {
        int status_code;
        std::string status_message;
        std::vector<std::byte> body;
        std::vector<HttpOwnedHeader> headers;
        std::optional<size_t> content_length = std::nullopt;
    };

    template<typename T>
    concept HttpProtocol = requires(T proto, const HttpRequest& req, const char* host, uint16_t port) {

        { proto.connect(host, port) } noexcept -> std::same_as<std::expected<void, Error>>;
        { proto.disconnect() } noexcept -> std::same_as<std::expected<void, Error>>;
        { proto.perform_request_safe(req) } noexcept -> std::same_as<std::expected<SafeHttpResponse, Error>>;
        { proto.perform_request_unsafe(req) } noexcept -> std::same_as<std::expected<UnsafeHttpResponse, Error>>;
    };

    enum class HttpStatusCode : int {
        Continue = 100,
        Ok = 200,
        Created = 201,
        Accepted = 202,
        Found = 302,
        BadRequest = 400,
        Unauthorized = 401,
        Forbidden = 403,
        NotFound = 404,
        InternalServerError = 500,
        BadGateway = 502,
    };

} // namespace httpcpp