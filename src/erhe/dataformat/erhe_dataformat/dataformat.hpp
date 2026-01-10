#pragma once

#include <cstddef>
#include <cstdint>

namespace erhe::dataformat {

auto float_to_snorm16(float    v) -> int16_t;
auto snorm16_to_float(int16_t  v) -> float;
auto float_to_snorm8 (float    v) -> int8_t;
auto snorm8_to_float (int8_t   v) -> float;
auto float_to_unorm16(float    v) -> uint16_t;
auto unorm16_to_float(uint16_t v) -> float;
auto float_to_unorm8 (float    v) -> uint8_t;
auto unorm8_to_float (uint8_t  v) -> float;
auto pack_unorm2x16  (float x, float y) -> uint32_t;
auto pack_unorm4x8   (float x, float y, float z, float w) -> uint32_t;
auto pack_snorm2x16  (float x, float y) -> uint32_t;
auto pack_snorm4x8   (float x, float y, float z, float w) -> uint32_t;
auto pack_int2x16    (int x, int y) -> uint32_t;

enum class Format : unsigned int {
    format_undefined = 0,
    format_8_scalar_srgb,
    format_8_scalar_unorm,
    format_8_scalar_snorm,
    format_8_scalar_uscaled,
    format_8_scalar_sscaled,
    format_8_scalar_uint,
    format_8_scalar_sint,
    format_8_vec2_srgb,
    format_8_vec2_unorm,
    format_8_vec2_snorm,
    format_8_vec2_uscaled,
    format_8_vec2_sscaled,
    format_8_vec2_uint,
    format_8_vec2_sint,
    format_8_vec3_srgb,
    format_8_vec3_unorm,
    format_8_vec3_snorm,
    format_8_vec3_uscaled,
    format_8_vec3_sscaled,
    format_8_vec3_uint,
    format_8_vec3_sint,
    format_8_vec4_srgb,
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
    format_16_scalar_float,
    format_16_vec2_unorm,
    format_16_vec2_snorm,
    format_16_vec2_uscaled,
    format_16_vec2_sscaled,
    format_16_vec2_uint,
    format_16_vec2_sint,
    format_16_vec2_float,
    format_16_vec3_unorm,
    format_16_vec3_snorm,
    format_16_vec3_uscaled,
    format_16_vec3_sscaled,
    format_16_vec3_uint,
    format_16_vec3_sint,
    format_16_vec3_float,
    format_16_vec4_unorm,
    format_16_vec4_snorm,
    format_16_vec4_uscaled,
    format_16_vec4_sscaled,
    format_16_vec4_uint,
    format_16_vec4_sint,
    format_16_vec4_float,
    format_32_scalar_uint,
    format_32_scalar_sint,
    format_32_scalar_float,
    format_32_vec2_uint,
    format_32_vec2_sint,
    format_32_vec2_float,
    format_32_vec3_uint,
    format_32_vec3_sint,
    format_32_vec3_float,
    format_32_vec4_uint,
    format_32_vec4_sint,
    format_32_vec4_float,

    format_packed1010102_vec4_unorm,
    format_packed1010102_vec4_snorm,
    format_packed1010102_vec4_uint,
    format_packed1010102_vec4_sint,

    format_packed111110_vec3_unorm,

    //format_b10g11r11_ufloat_pack32

    // Depth / stencil formats
    format_d16_unorm,           // a one-component, 16-bit unsigned normalized format that has a single 16-bit depth component.
    format_x8_d24_unorm_pack32, // a two-component, 32-bit format that has 24 unsigned normalized bits in the depth component and, optionally, 8 bits that are unused.
    format_d32_sfloat,          // a one-component, 32-bit signed floating-point format that has 32 bits in the depth component
    format_s8_uint,             // a one-component, 8-bit unsigned integer format that has 8 bits in the stencil component
    //format_d16_unorm_s8_uint,   // a two-component, 24-bit format that has 16 unsigned normalized bits in the depth component and 8 unsigned integer bits in the stencil component
    format_d24_unorm_s8_uint,   // a two-component, 32-bit packed format that has 8 unsigned integer bits in the stencil component, and 24 unsigned normalized bits in the depth component
    format_d32_sfloat_s8_uint   // a two-component format that has 32 signed float bits in the depth component and 8 unsigned integer bits in the stencil component.
                                // There are optionally 24 bits that are unused
};

enum class Format_kind {
    format_kind_unsigned_integer,
    format_kind_signed_integer,
    format_kind_float,
    format_kind_depth_stencil
};

[[nodiscard]] auto srgb_to_linear(float cs) -> float;
[[nodiscard]] auto linear_rgb_to_srgb(float cl) -> float;

[[nodiscard]] auto c_str                  (Format format) -> const char*;
[[nodiscard]] auto get_format_kind        (Format format) -> Format_kind;
[[nodiscard]] auto get_component_count    (Format format) -> std::size_t;
[[nodiscard]] auto get_component_byte_size(Format format) -> std::size_t;
[[nodiscard]] auto has_color              (Format format) -> bool;
[[nodiscard]] auto get_format_size        (Format format) -> std::size_t;
[[nodiscard]] auto get_red_size           (Format format) -> std::size_t;
[[nodiscard]] auto get_green_size         (Format format) -> std::size_t;
[[nodiscard]] auto get_blue_size          (Format format) -> std::size_t;
[[nodiscard]] auto get_alpha_size         (Format format) -> std::size_t;
[[nodiscard]] auto get_depth_size         (Format format) -> std::size_t;
[[nodiscard]] auto get_stencil_size       (Format format) -> std::size_t;
void convert(const void* src, Format src_format, void* dst, Format dst_format, float scale);

} // namespace erhe::dataformat
