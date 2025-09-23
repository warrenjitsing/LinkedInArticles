#pragma once

#include <concepts>
#include <span>
#include <cstdint>
#include <expected>

#include <httpcpp/error.hpp>

namespace httpcpp {

    template<typename T>
    concept Transport = requires(T t,
                                  const char* host,
                                  uint16_t port,
                                  std::span<std::byte> buffer,
                                  std::span<const std::byte> const_buffer) {
        { t.connect(host, port) } noexcept -> std::same_as<std::expected<void, TransportError>>;
        { t.close() } noexcept -> std::same_as<std::expected<void, TransportError>>;
        { t.write(const_buffer) } noexcept -> std::same_as<std::expected<size_t, TransportError>>;
        { t.read(buffer) } noexcept -> std::same_as<std::expected<size_t, TransportError>>;
                                  };

} // namespace httpcpp