#pragma once

#include <cstddef>
#include <cstdint>

namespace erhe::dataformat
{

int16_t float_to_snorm16(float v);
float snorm16_to_float(int16_t v);
int8_t float_to_snorm8(float v);
float snorm8_to_float(int8_t v);
uint16_t float_to_unorm16(float v);
float unorm16_to_float(uint16_t v);
uint8_t float_to_unorm8(float v);
float unorm8_to_float(uint8_t v);

enum class Format {
    format_undefined = 0,
    format_8_scalar_unorm,
    format_8_scalar_snorm,
    format_8_scalar_uscaled,
    format_8_scalar_sscaled,
    format_8_scalar_uint,
    format_8_scalar_sint,
    format_8_vec2_unorm,
    format_8_vec2_snorm,
    format_8_vec2_uscaled,
    format_8_vec2_sscaled,
    format_8_vec2_uint,
    format_8_vec2_sint,
    format_8_vec3_unorm,
    format_8_vec3_snorm,
    format_8_vec3_uscaled,
    format_8_vec3_sscaled,
    format_8_vec3_uint,
    format_8_vec3_sint,
    format_8_vec4_unorm,
    format_8_vec4_snorm,
    format_8_vec4_uscaled,
    format_8_vec4_sscaled,
    format_8_vec4_uint,
    format_8_vec4_sint,
    format_16_scalar_unorm,
    format_16_scalar_snorm,
    format_16_scalar_uscaled,
    format_16_scalar_sscaled,
    format_16_scalar_uint,
    format_16_scalar_sint,
    format_16_vec2_unorm,
    format_16_vec2_snorm,
    format_16_vec2_uscaled,
    format_16_vec2_sscaled,
    format_16_vec2_uint,
    format_16_vec2_sint,
    format_16_vec3_unorm,
    format_16_vec3_snorm,
    format_16_vec3_uscaled,
    format_16_vec3_sscaled,
    format_16_vec3_uint,
    format_16_vec3_sint,
    format_16_vec4_unorm,
    format_16_vec4_snorm,
    format_16_vec4_uscaled,
    format_16_vec4_sscaled,
    format_16_vec4_uint,
    format_16_vec4_sint,
    format_32_scalar_unorm,
    format_32_scalar_snorm,
    format_32_scalar_uscaled,
    format_32_scalar_sscaled,
    format_32_scalar_uint,
    format_32_scalar_sint,
    format_32_scalar_float,
    format_32_vec2_unorm,
    format_32_vec2_snorm,
    format_32_vec2_uscaled,
    format_32_vec2_sscaled,
    format_32_vec2_uint,
    format_32_vec2_sint,
    format_32_vec2_float,
    format_32_vec3_unorm,
    format_32_vec3_snorm,
    format_32_vec3_uscaled,
    format_32_vec3_sscaled,
    format_32_vec3_uint,
    format_32_vec3_sint,
    format_32_vec3_float,
    format_32_vec4_unorm,
    format_32_vec4_snorm,
    format_32_vec4_uscaled,
    format_32_vec4_sscaled,
    format_32_vec4_uint,
    format_32_vec4_sint,
    format_32_vec4_float,
    format_packed1010102_vec4_unorm,
    format_packed1010102_vec4_snorm,
    format_packed1010102_vec4_uint,
    format_packed1010102_vec4_sint
};

enum class Format_kind {
    format_kind_unsigned_integer,
    format_kind_signed_integer,
    format_kind_float
};

[[nodiscard]] auto c_str(Format format) -> const char*;
[[nodiscard]] auto get_format_kind(Format format) -> Format_kind;
[[nodiscard]] auto get_component_count(Format format) -> std::size_t;
[[nodiscard]] auto get_component_byte_size(Format format) -> std::size_t;
[[nodiscard]] auto get_format_size(Format format) -> std::size_t;
void convert(const void* src, Format src_format, void* dst, Format dst_format, float scale);

} // namespace erhe::dataformat
