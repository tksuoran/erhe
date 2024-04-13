#pragma once

#include <glm/gtc/type_ptr.hpp>
#include <span>

namespace erhe::graphics
{

[[nodiscard]] inline auto as_span(const glm::mat4& m) -> std::span<const float>
{
    return std::span<const float>(reinterpret_cast<const float*>(glm::value_ptr(m)), 16);
}

[[nodiscard]] inline auto as_span(const glm::vec4& v) -> std::span<const float>
{
    return std::span<const float>(reinterpret_cast<const float*>(glm::value_ptr(v)), 4);
}

[[nodiscard]] inline auto as_span(const glm::vec3& v) -> std::span<const float>
{
    return std::span<const float>(reinterpret_cast<const float*>(glm::value_ptr(v)), 3);
}

[[nodiscard]] inline auto as_span(const glm::vec2& v) -> std::span<const float>
{
    return std::span<const float>(reinterpret_cast<const float*>(glm::value_ptr(v)), 2);
}

template <typename T>
[[nodiscard]] inline auto as_span(const T& value) -> std::span<const T>
{
    return std::span<const T>(&value, 1);
}

template <typename T>
inline void write(
    const std::span<std::byte>& dst,
    const std::size_t           write_byte_offset,
    const std::span<const T>    source
)
{
    //Expects(dst.size_bytes() >= source.size_bytes() + write_byte_offset);
    auto source_bytes = std::as_bytes(source);
    auto write_dst = std::as_writable_bytes(dst).data() + write_byte_offset;
    memcpy(write_dst, source_bytes.data(), source_bytes.size_bytes());
}

template <typename T>
inline void write(
    const std::span<T>&      dst,
    const std::size_t        write_byte_offset,
    const std::span<const T> source
)
{
    //Expects(dst.size_bytes() >= source.size_bytes() + write_byte_offset);
    auto source_bytes = std::as_bytes(source);
    auto write_dst    = std::as_writable_bytes(dst).data() + write_byte_offset;
    memcpy(write_dst, source_bytes.data(), source_bytes.size_bytes());
}

template <typename T>
inline void write(
    const std::span<T>& dst,
    const std::size_t   write_byte_offset,
    const T&            value
)
{
    write(dst, write_byte_offset, as_span(value));
}

} // namespace erhe::graphics
