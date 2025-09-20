#include <concepts>
#include <string>
#include <string_view>
#include <span>
#include <cstddef>

template <typename T>
concept StringLike = std::convertible_to<T, std::string_view>;

template <typename T>
concept PortLike = std::integral<T>;

template <typename T>
concept ConstBufferLike = std::convertible_to<T, std::span<const std::byte>>;

template <typename T>
concept BufferLike = std::convertible_to<T, std::span<std::byte>>;


template <typename T>
concept TransportConcept = requires(T t) {
    { t.connect(std::declval<StringLike auto>(), std::declval<PortLike auto>()) } -> std::same_as<void>;
    { t.write(std::declval<ConstBufferLike auto>()) } -> std::convertible_to<size_t>;
    { t.read(std::declval<BufferLike auto>()) } -> std::convertible_to<size_t>;
    { t.close() } -> std::same_as<void>;
};

template <typename T>
concept HttpProtocolConcept = requires(T t) {
    { t.execute_request(std::declval<StringLike auto>(), 
                         std::declval<PortLike auto>(), 
                         std::declval<StringLike auto>()) } -> std::same_as<std::string>;
};

template <typename T>
concept HttpClientConcept = requires(T t) {
    { t.get(std::declval<StringLike auto>()) } -> std::same_as<std::string>;
};