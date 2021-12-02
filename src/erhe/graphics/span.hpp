#pragma once

#include <glm/gtc/type_ptr.hpp>
#include <gsl/gsl>

namespace erhe::graphics
{

inline auto as_span(const glm::mat4& m) -> gsl::span<const float>
{
    return gsl::span<const float>(reinterpret_cast<const float*>(glm::value_ptr(m)), 16);
}

inline auto as_span(const glm::vec4& v) -> gsl::span<const float>
{
    return gsl::span<const float>(reinterpret_cast<const float*>(glm::value_ptr(v)), 4);
}

inline auto as_span(const glm::vec3& v) -> gsl::span<const float>
{
    return gsl::span<const float>(reinterpret_cast<const float*>(glm::value_ptr(v)), 3);
}

inline auto as_span(const glm::vec2& v) -> gsl::span<const float>
{
    return gsl::span<const float>(reinterpret_cast<const float*>(glm::value_ptr(v)), 2);
}

template <typename T>
inline auto as_span(const T& value) -> gsl::span<const T>
{
    return gsl::span<const T>(&value, 1);
}

template <typename T>
inline void write(gsl::span<std::byte>& dst, const size_t write_byte_offset, gsl::span<const T> source)
{
    //Expects(dst.size_bytes() >= source.size_bytes() + write_byte_offset);
    auto source_bytes = gsl::as_bytes(source);
    auto write_dst = gsl::as_writable_bytes(dst).data() + write_byte_offset;
    memcpy(write_dst, source_bytes.data(), source_bytes.size_bytes());
}

template <typename T>
inline void write(gsl::span<T>& dst, const size_t write_byte_offset, gsl::span<const T> source)
{
    //Expects(dst.size_bytes() >= source.size_bytes() + write_byte_offset);
    auto source_bytes = gsl::as_bytes(source);
    auto write_dst = gsl::as_writable_bytes(dst).data() + write_byte_offset;
    memcpy(write_dst, source_bytes.data(), source_bytes.size_bytes());
}

} // namespace erhe::graphics
