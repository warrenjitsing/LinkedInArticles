#pragma once

#include <httpcpp/transport.hpp>
#include <httpcpp/http_protocol.hpp>

#include <vector>
#include <cstddef> // For std::byte
#include <iostream>
#include <string_view>
#include <algorithm>
#include <charconv>
#include <optional>
#include <cstring>

namespace httpcpp {

    template<Transport T>
    class Http1Protocol {
    public:
        Http1Protocol() noexcept = default;

        ~Http1Protocol() noexcept {
            if (auto result = disconnect(); !result.has_value()) {
                std::cerr << "Warning: Failed to disconnect transport in destructor." << std::endl;
            }
        }

        // --- Connection Management ---
        [[nodiscard]] auto connect(const char* host, uint16_t port) noexcept -> std::expected<void, Error> {
            auto result = transport_.connect(host, port);
            if (!result) {
                return std::unexpected(Error{result.error()});
            }
            return {};
        }

        [[nodiscard]] auto disconnect() noexcept -> std::expected<void, Error> {
            auto result = transport_.close();
            if (!result) {
                return std::unexpected(Error{result.error()});
            }
            return {};
        }

        [[nodiscard]] auto perform_request_safe(const HttpRequest& req) noexcept -> std::expected<SafeHttpResponse, Error> {
            auto unsafe_res_expected = perform_request_unsafe(req);
            if (!unsafe_res_expected) {
                return std::unexpected(unsafe_res_expected.error());
            }
            const auto& unsafe_res = *unsafe_res_expected;

            SafeHttpResponse safe_res;
            safe_res.status_code = unsafe_res.status_code;
            safe_res.status_message = std::string(unsafe_res.status_message);
            safe_res.body = std::vector<std::byte>(unsafe_res.body.begin(), unsafe_res.body.end());

            safe_res.headers.reserve(unsafe_res.headers.size());
            for (const auto& header_view : unsafe_res.headers) {
                safe_res.headers.emplace_back(
                    std::string(header_view.first),
                    std::string(header_view.second)
                );
            }

            return safe_res;
        }

        [[nodiscard]] auto perform_request_unsafe(const HttpRequest& req) noexcept -> std::expected<UnsafeHttpResponse, Error> {
            build_request_string(req);

            if (auto write_res = transport_.write(buffer_); !write_res) {
                return std::unexpected(Error{write_res.error()});
            }

            if (auto read_res = read_full_response(); !read_res) {
                return std::unexpected(read_res.error());
            }

            return parse_unsafe_response();
        }

        // For testing purposes only
        [[nodiscard]] auto get_content_length_for_test() const noexcept {
            return content_length_;
        }

    private:
        void build_request_string(const HttpRequest& req) {
            buffer_.clear();

            // Helper lambda to efficiently append string views to our byte vector.
            auto append = [this](std::string_view sv) {
                buffer_.insert(buffer_.end(),
                    reinterpret_cast<const std::byte*>(sv.data()),
                    reinterpret_cast<const std::byte*>(sv.data()) + sv.size());
            };

            // 1. Request Line
            std::string_view method_str = (req.method == HttpMethod::Get) ? "GET" : "POST";
            append(method_str);
            append(" ");
            append(req.path);
            append(" HTTP/1.1\r\n");

            // 2. Headers
            for (const auto& header : req.headers) {
                append(header.first);
                append(": ");
                append(header.second);
                append("\r\n");
            }

            // 3. End of Headers
            append("\r\n");

            // 4. Body
            if (!req.body.empty()) {
                buffer_.insert(buffer_.end(), req.body.begin(), req.body.end());
            }
        }

        [[nodiscard]] auto read_full_response() noexcept -> std::expected<void, Error> {
            buffer_.clear();
            header_size_ = 0;
            content_length_ = std::nullopt;

            while (true) {
                const size_t available_capacity = buffer_.capacity() - buffer_.size();
                const size_t read_amount = std::max(available_capacity, static_cast<size_t>(1024));
                const size_t old_size = buffer_.size();
                buffer_.resize(old_size + read_amount);

                std::span<std::byte> write_area(buffer_.data() + old_size, read_amount);

                auto read_result = transport_.read(write_area);

                if (!read_result) {
                    buffer_.resize(old_size);
                    if (read_result.error() == TransportError::ConnectionClosed) {
                        if (content_length_.has_value() && buffer_.size() < header_size_ + *content_length_) {
                            return std::unexpected(Error{HttpClientError::HttpParseFailure});
                        }
                        break;
                    }
                    return std::unexpected(Error{read_result.error()});
                }

                buffer_.resize(old_size + *read_result);

                if (header_size_ == 0) {
                    auto it = std::search(
                        buffer_.begin(), buffer_.end(),
                        HEADER_SEPARATOR_.begin(), HEADER_SEPARATOR_.end(),
                        [](std::byte b, char c) {
                            return static_cast<unsigned char>(b) == static_cast<unsigned char>(c);
                        }
                    );
                    if (it != buffer_.end()) {
                        header_size_ = std::distance(buffer_.begin(), it) + HEADER_SEPARATOR_.size();
                        std::string_view headers_view(reinterpret_cast<const char*>(buffer_.data()), header_size_);

                        size_t line_start = headers_view.find("\r\n") + 2;
                        while (line_start < headers_view.size()) {
                            size_t line_end = headers_view.find("\r\n", line_start);
                            std::string_view line = headers_view.substr(line_start, line_end - line_start);
                            if (line.empty()) break;
                            if (line.size() >= 15 && strncasecmp(line.data(), "Content-Length:", 15) == 0) {
                                auto value_sv = line.substr(15);
                                value_sv.remove_prefix(std::min(value_sv.find_first_not_of(" \t"), value_sv.size()));
                                size_t length = 0;
                                if (auto [ptr, ec] = std::from_chars(value_sv.data(), value_sv.data() + value_sv.size(), length); ec == std::errc()) {
                                    content_length_ = length;
                                }
                                break;
                            }
                            if (line_end == std::string_view::npos) break;
                            line_start = line_end + 2;
                        }
                    }
                }

                if (content_length_.has_value()) {
                    if (buffer_.size() >= header_size_ + *content_length_) {
                        break;
                    }
                }
            }

            if (header_size_ == 0 && !buffer_.empty()) {
                return std::unexpected(Error{HttpClientError::HttpParseFailure});
            }

            return {};
        }

        [[nodiscard]] auto parse_unsafe_response() noexcept -> std::expected<UnsafeHttpResponse, Error> {
            UnsafeHttpResponse res;
            std::string_view response_view(reinterpret_cast<const char*>(buffer_.data()), buffer_.size());

            if (header_size_ == 0) {
                return std::unexpected(Error{HttpClientError::HttpParseFailure});
            }

            std::string_view headers_block = response_view.substr(0, header_size_ - HEADER_SEPARATOR_.size());

            size_t status_line_end = headers_block.find("\r\n");
            if (status_line_end == std::string_view::npos) return std::unexpected(Error{HttpClientError::HttpParseFailure});
            std::string_view status_line = headers_block.substr(0, status_line_end);
            auto code_start = status_line.find(' ');
            if (code_start == std::string_view::npos) return std::unexpected(Error{HttpClientError::HttpParseFailure});
            auto code_end = status_line.find(' ', code_start + 1);
            if (code_end == std::string_view::npos) return std::unexpected(Error{HttpClientError::HttpParseFailure});
            auto code_sv = status_line.substr(code_start + 1, code_end - (code_start + 1));
            if (auto [ptr, ec] = std::from_chars(code_sv.data(), code_sv.data() + code_sv.size(), res.status_code); ec != std::errc()) {
                return std::unexpected(Error{HttpClientError::HttpParseFailure});
            }
            res.status_message = status_line.substr(code_end + 1);

            // 3. Parse all header key-value pairs.
            headers_block.remove_prefix(status_line_end + 2);
            while (!headers_block.empty()) {
                size_t line_end = headers_block.find("\r\n");
                std::string_view line = headers_block.substr(0, line_end);
                size_t colon_pos = line.find(':');
                if (colon_pos != std::string_view::npos) {
                    auto key = line.substr(0, colon_pos);
                    auto value = line.substr(colon_pos + 1);
                    value.remove_prefix(std::min(value.find_first_not_of(" \t"), value.size()));
                    res.headers.emplace_back(key, value);
                }
                if (line_end == std::string_view::npos) break;
                headers_block.remove_prefix(line_end + 2);
            }

            // 4. Create the body span using the pre-calculated content_length_.
            if (content_length_.has_value()) {
                res.body = std::span(buffer_).subspan(header_size_, *content_length_);
            } else {
                // Fallback for connection-close scenarios where content length is unknown.
                res.body = std::span(buffer_).subspan(header_size_);
            }

            return res;
        }

        static constexpr std::string_view HEADER_SEPARATOR_ = "\r\n\r\n";
        size_t header_size_ = 0; // Stores the total size of the response headers
        T transport_;
        std::vector<std::byte> buffer_;
        std::optional<size_t> content_length_;
    };

} // namespace httpcpp