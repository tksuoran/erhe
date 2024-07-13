#include "erhe_dataformat/dataformat.hpp"
#include "erhe_verify/verify.hpp"
#include <algorithm>

namespace erhe::dataformat {

int16_t float_to_snorm16(float v)
{
    float a = (v >= 0.0f) ? (v * 32767.0f + 0.5f) : (v * 32767.0f - 0.5f);
    if (a < -32768.0f) {
        a = -32768.0f;
    }
    if (a > 32767.0f) {
        a = 32767.0f;
    }
    return static_cast<int16_t>(a);
}

float snorm16_to_float(int16_t v)
{
    return std::max(static_cast<float>(v) / 32767.0f, -1.0f);
}

int8_t float_to_snorm8(float v)
{
    float a = (v >= 0.0f) ? (v * 127.0f + 0.5f) : (v * 127.0f - 0.5f);
    if (a < -128.0f) {
        a = -128.0f;
    }
    if (a > 127.0f) {
        a = 127.0f;
    }
    return static_cast<int8_t>(a);
}

float snorm8_to_float(int8_t v)
{
    return std::max(static_cast<float>(v) / 127.0f, -1.0f);
}

uint16_t float_to_unorm16(float v)
{
    float a = 65535.0f * v + 0.5f;
    if (a < 0.0f) {
        a = 0.0f;
    }
    if (a > 65535.0f) {
        a = 65535.0f;
    }
    return static_cast<uint16_t>(a);
}

float unorm16_to_float(uint16_t v)
{
    return static_cast<float>(v) / 65535.0f;
}

uint8_t float_to_unorm8(float v)
{
    float a = 255.0f * v + 0.5f;
    if (a < 0.0f) {
        a = 0.0f;
    }
    if (a > 255.0f) {
        a = 255.0f;
    }
    return static_cast<uint8_t>(a);
}

float unorm8_to_float(uint8_t v)
{
    return static_cast<float>(v) / 255.0f;
}

auto c_str(Format format) -> const char*
{
    switch (format) {
        case Format::format_8_scalar_unorm:           return "format_8_scalar_unorm";
        case Format::format_8_scalar_snorm:           return "format_8_scalar_snorm";
        case Format::format_8_scalar_uscaled:         return "format_8_scalar_uscaled";
        case Format::format_8_scalar_sscaled:         return "format_8_scalar_sscaled";
        case Format::format_8_scalar_uint:            return "format_8_scalar_uint";
        case Format::format_8_scalar_sint:            return "format_8_scalar_sint";
        case Format::format_8_vec2_unorm:             return "format_8_vec2_unorm";
        case Format::format_8_vec2_snorm:             return "format_8_vec2_snorm";
        case Format::format_8_vec2_uscaled:           return "format_8_vec2_uscaled";
        case Format::format_8_vec2_sscaled:           return "format_8_vec2_sscaled";
        case Format::format_8_vec2_uint:              return "format_8_vec2_uint";
        case Format::format_8_vec2_sint:              return "format_8_vec2_sint";
        case Format::format_8_vec3_unorm:             return "format_8_vec3_unorm";
        case Format::format_8_vec3_snorm:             return "format_8_vec3_snorm";
        case Format::format_8_vec3_uscaled:           return "format_8_vec3_uscaled";
        case Format::format_8_vec3_sscaled:           return "format_8_vec3_sscaled";
        case Format::format_8_vec3_uint:              return "format_8_vec3_uint";
        case Format::format_8_vec3_sint:              return "format_8_vec3_sint";
        case Format::format_8_vec4_unorm:             return "format_8_vec4_unorm";
        case Format::format_8_vec4_snorm:             return "format_8_vec4_snorm";
        case Format::format_8_vec4_uscaled:           return "format_8_vec4_uscaled";
        case Format::format_8_vec4_sscaled:           return "format_8_vec4_sscaled";
        case Format::format_8_vec4_uint:              return "format_8_vec4_uint";
        case Format::format_8_vec4_sint:              return "format_8_vec4_sint";
        case Format::format_16_scalar_unorm:          return "format_16_scalar_unorm";
        case Format::format_16_scalar_snorm:          return "format_16_scalar_snorm";
        case Format::format_16_scalar_uscaled:        return "format_16_scalar_uscaled";
        case Format::format_16_scalar_sscaled:        return "format_16_scalar_sscaled";
        case Format::format_16_scalar_uint:           return "format_16_scalar_uint";
        case Format::format_16_scalar_sint:           return "format_16_scalar_sint";
        case Format::format_16_vec2_unorm:            return "format_16_vec2_unorm";
        case Format::format_16_vec2_snorm:            return "format_16_vec2_snorm";
        case Format::format_16_vec2_uscaled:          return "format_16_vec2_uscaled";
        case Format::format_16_vec2_sscaled:          return "format_16_vec2_sscaled";
        case Format::format_16_vec2_uint:             return "format_16_vec2_uint";
        case Format::format_16_vec2_sint:             return "format_16_vec2_sint";
        case Format::format_16_vec3_unorm:            return "format_16_vec3_unorm";
        case Format::format_16_vec3_snorm:            return "format_16_vec3_snorm";
        case Format::format_16_vec3_uscaled:          return "format_16_vec3_uscaled";
        case Format::format_16_vec3_sscaled:          return "format_16_vec3_sscaled";
        case Format::format_16_vec3_uint:             return "format_16_vec3_uint";
        case Format::format_16_vec3_sint:             return "format_16_vec3_sint";
        case Format::format_16_vec4_unorm:            return "format_16_vec4_unorm";
        case Format::format_16_vec4_snorm:            return "format_16_vec4_snorm";
        case Format::format_16_vec4_uscaled:          return "format_16_vec4_uscaled";
        case Format::format_16_vec4_sscaled:          return "format_16_vec4_sscaled";
        case Format::format_16_vec4_uint:             return "format_16_vec4_uint";
        case Format::format_16_vec4_sint:             return "format_16_vec4_sint";
        case Format::format_32_scalar_unorm:          return "format_32_scalar_unorm";
        case Format::format_32_scalar_snorm:          return "format_32_scalar_snorm";
        case Format::format_32_scalar_uscaled:        return "format_32_scalar_uscaled";
        case Format::format_32_scalar_sscaled:        return "format_32_scalar_sscaled";
        case Format::format_32_scalar_uint:           return "format_32_scalar_uint";
        case Format::format_32_scalar_sint:           return "format_32_scalar_sint";
        case Format::format_32_scalar_float:          return "format_32_scalar_float";
        case Format::format_32_vec2_unorm:            return "format_32_vec2_unorm";
        case Format::format_32_vec2_snorm:            return "format_32_vec2_snorm";
        case Format::format_32_vec2_uscaled:          return "format_32_vec2_uscaled";
        case Format::format_32_vec2_sscaled:          return "format_32_vec2_sscaled";
        case Format::format_32_vec2_uint:             return "format_32_vec2_uint";
        case Format::format_32_vec2_sint:             return "format_32_vec2_sint";
        case Format::format_32_vec2_float:            return "format_32_vec2_float";
        case Format::format_32_vec3_unorm:            return "format_32_vec3_unorm";
        case Format::format_32_vec3_snorm:            return "format_32_vec3_snorm";
        case Format::format_32_vec3_uscaled:          return "format_32_vec3_uscaled";
        case Format::format_32_vec3_sscaled:          return "format_32_vec3_sscaled";
        case Format::format_32_vec3_uint:             return "format_32_vec3_uint";
        case Format::format_32_vec3_sint:             return "format_32_vec3_sint";
        case Format::format_32_vec3_float:            return "format_32_vec3_float";
        case Format::format_32_vec4_unorm:            return "format_32_vec4_unorm";
        case Format::format_32_vec4_snorm:            return "format_32_vec4_snorm";
        case Format::format_32_vec4_uscaled:          return "format_32_vec4_uscaled";
        case Format::format_32_vec4_sscaled:          return "format_32_vec4_sscaled";
        case Format::format_32_vec4_uint:             return "format_32_vec4_uint";
        case Format::format_32_vec4_sint:             return "format_32_vec4_sint";
        case Format::format_32_vec4_float:            return "format_32_vec4_float";
        case Format::format_packed1010102_vec4_unorm: return "format_packed1010102_vec4_unorm";
        case Format::format_packed1010102_vec4_snorm: return "format_packed1010102_vec4_snorm";
        case Format::format_packed1010102_vec4_uint:  return "format_packed1010102_vec4_uint";
        case Format::format_packed1010102_vec4_sint:  return "format_packed1010102_vec4_sint";
        default:                                      return "?";
    }
}

auto get_format_kind(Format format) -> Format_kind
{
    switch (format) {
        case Format::format_8_scalar_unorm:           return Format_kind::format_kind_float;
        case Format::format_8_scalar_snorm:           return Format_kind::format_kind_float;
        case Format::format_8_scalar_uscaled:         return Format_kind::format_kind_float;
        case Format::format_8_scalar_sscaled:         return Format_kind::format_kind_float;
        case Format::format_8_scalar_uint:            return Format_kind::format_kind_unsigned_integer;
        case Format::format_8_scalar_sint:            return Format_kind::format_kind_signed_integer;
        case Format::format_8_vec2_unorm:             return Format_kind::format_kind_float;
        case Format::format_8_vec2_snorm:             return Format_kind::format_kind_float;
        case Format::format_8_vec2_uscaled:           return Format_kind::format_kind_float;
        case Format::format_8_vec2_sscaled:           return Format_kind::format_kind_float;
        case Format::format_8_vec2_uint:              return Format_kind::format_kind_unsigned_integer;
        case Format::format_8_vec2_sint:              return Format_kind::format_kind_signed_integer;
        case Format::format_8_vec3_unorm:             return Format_kind::format_kind_float;
        case Format::format_8_vec3_snorm:             return Format_kind::format_kind_float;
        case Format::format_8_vec3_uscaled:           return Format_kind::format_kind_float;
        case Format::format_8_vec3_sscaled:           return Format_kind::format_kind_float;
        case Format::format_8_vec3_uint:              return Format_kind::format_kind_unsigned_integer;
        case Format::format_8_vec3_sint:              return Format_kind::format_kind_signed_integer;
        case Format::format_8_vec4_unorm:             return Format_kind::format_kind_float;
        case Format::format_8_vec4_snorm:             return Format_kind::format_kind_float;
        case Format::format_8_vec4_uscaled:           return Format_kind::format_kind_float;
        case Format::format_8_vec4_sscaled:           return Format_kind::format_kind_float;
        case Format::format_8_vec4_uint:              return Format_kind::format_kind_unsigned_integer;
        case Format::format_8_vec4_sint:              return Format_kind::format_kind_signed_integer;
        case Format::format_16_scalar_unorm:          return Format_kind::format_kind_float;
        case Format::format_16_scalar_snorm:          return Format_kind::format_kind_float;
        case Format::format_16_scalar_uscaled:        return Format_kind::format_kind_float;
        case Format::format_16_scalar_sscaled:        return Format_kind::format_kind_float;
        case Format::format_16_scalar_uint:           return Format_kind::format_kind_unsigned_integer;
        case Format::format_16_scalar_sint:           return Format_kind::format_kind_signed_integer;
        case Format::format_16_vec2_unorm:            return Format_kind::format_kind_float;
        case Format::format_16_vec2_snorm:            return Format_kind::format_kind_float;
        case Format::format_16_vec2_uscaled:          return Format_kind::format_kind_float;
        case Format::format_16_vec2_sscaled:          return Format_kind::format_kind_float;
        case Format::format_16_vec2_uint:             return Format_kind::format_kind_unsigned_integer;
        case Format::format_16_vec2_sint:             return Format_kind::format_kind_signed_integer;
        case Format::format_16_vec3_unorm:            return Format_kind::format_kind_float;
        case Format::format_16_vec3_snorm:            return Format_kind::format_kind_float;
        case Format::format_16_vec3_uscaled:          return Format_kind::format_kind_float;
        case Format::format_16_vec3_sscaled:          return Format_kind::format_kind_float;
        case Format::format_16_vec3_uint:             return Format_kind::format_kind_unsigned_integer;
        case Format::format_16_vec3_sint:             return Format_kind::format_kind_signed_integer;
        case Format::format_16_vec4_unorm:            return Format_kind::format_kind_float;
        case Format::format_16_vec4_snorm:            return Format_kind::format_kind_float;
        case Format::format_16_vec4_uscaled:          return Format_kind::format_kind_float;
        case Format::format_16_vec4_sscaled:          return Format_kind::format_kind_float;
        case Format::format_16_vec4_uint:             return Format_kind::format_kind_unsigned_integer;
        case Format::format_16_vec4_sint:             return Format_kind::format_kind_signed_integer;
        case Format::format_32_scalar_unorm:          return Format_kind::format_kind_float;
        case Format::format_32_scalar_snorm:          return Format_kind::format_kind_float;
        case Format::format_32_scalar_uscaled:        return Format_kind::format_kind_float;
        case Format::format_32_scalar_sscaled:        return Format_kind::format_kind_float;
        case Format::format_32_scalar_uint:           return Format_kind::format_kind_unsigned_integer;
        case Format::format_32_scalar_sint:           return Format_kind::format_kind_signed_integer;
        case Format::format_32_scalar_float:          return Format_kind::format_kind_float;
        case Format::format_32_vec2_unorm:            return Format_kind::format_kind_float;
        case Format::format_32_vec2_snorm:            return Format_kind::format_kind_float;
        case Format::format_32_vec2_uscaled:          return Format_kind::format_kind_float;
        case Format::format_32_vec2_sscaled:          return Format_kind::format_kind_float;
        case Format::format_32_vec2_uint:             return Format_kind::format_kind_unsigned_integer;
        case Format::format_32_vec2_sint:             return Format_kind::format_kind_signed_integer;
        case Format::format_32_vec2_float:            return Format_kind::format_kind_float;
        case Format::format_32_vec3_unorm:            return Format_kind::format_kind_float;
        case Format::format_32_vec3_snorm:            return Format_kind::format_kind_float;
        case Format::format_32_vec3_uscaled:          return Format_kind::format_kind_float;
        case Format::format_32_vec3_sscaled:          return Format_kind::format_kind_float;
        case Format::format_32_vec3_uint:             return Format_kind::format_kind_unsigned_integer;
        case Format::format_32_vec3_sint:             return Format_kind::format_kind_signed_integer;
        case Format::format_32_vec3_float:            return Format_kind::format_kind_float;
        case Format::format_32_vec4_unorm:            return Format_kind::format_kind_float;
        case Format::format_32_vec4_snorm:            return Format_kind::format_kind_float;
        case Format::format_32_vec4_uscaled:          return Format_kind::format_kind_float;
        case Format::format_32_vec4_sscaled:          return Format_kind::format_kind_float;
        case Format::format_32_vec4_uint:             return Format_kind::format_kind_unsigned_integer;
        case Format::format_32_vec4_sint:             return Format_kind::format_kind_signed_integer;
        case Format::format_32_vec4_float:            return Format_kind::format_kind_float;
        case Format::format_packed1010102_vec4_unorm: return Format_kind::format_kind_float;
        case Format::format_packed1010102_vec4_snorm: return Format_kind::format_kind_float;
        case Format::format_packed1010102_vec4_uint:  return Format_kind::format_kind_unsigned_integer;
        case Format::format_packed1010102_vec4_sint:  return Format_kind::format_kind_signed_integer;

        default: {
            ERHE_FATAL("Bad format");
        }
    }
}

auto get_component_count(Format format) -> std::size_t
{
    switch (format) {
        case Format::format_8_scalar_unorm:           return 1;
        case Format::format_8_scalar_snorm:           return 1;
        case Format::format_8_scalar_uscaled:         return 1;
        case Format::format_8_scalar_sscaled:         return 1;
        case Format::format_8_scalar_uint:            return 1;
        case Format::format_8_scalar_sint:            return 1;
        case Format::format_8_vec2_unorm:             return 2;
        case Format::format_8_vec2_snorm:             return 2;
        case Format::format_8_vec2_uscaled:           return 2;
        case Format::format_8_vec2_sscaled:           return 2;
        case Format::format_8_vec2_uint:              return 2;
        case Format::format_8_vec2_sint:              return 2;
        case Format::format_8_vec3_unorm:             return 3;
        case Format::format_8_vec3_snorm:             return 3;
        case Format::format_8_vec3_uscaled:           return 3;
        case Format::format_8_vec3_sscaled:           return 3;
        case Format::format_8_vec3_uint:              return 3;
        case Format::format_8_vec3_sint:              return 3;
        case Format::format_8_vec4_unorm:             return 4;
        case Format::format_8_vec4_snorm:             return 4;
        case Format::format_8_vec4_uscaled:           return 4;
        case Format::format_8_vec4_sscaled:           return 4;
        case Format::format_8_vec4_uint:              return 4;
        case Format::format_8_vec4_sint:              return 4;
        case Format::format_16_scalar_unorm:          return 1;
        case Format::format_16_scalar_snorm:          return 1;
        case Format::format_16_scalar_uscaled:        return 1;
        case Format::format_16_scalar_sscaled:        return 1;
        case Format::format_16_scalar_uint:           return 1;
        case Format::format_16_scalar_sint:           return 1;
        case Format::format_16_vec2_unorm:            return 2;
        case Format::format_16_vec2_snorm:            return 2;
        case Format::format_16_vec2_uscaled:          return 2;
        case Format::format_16_vec2_sscaled:          return 2;
        case Format::format_16_vec2_uint:             return 2;
        case Format::format_16_vec2_sint:             return 2;
        case Format::format_16_vec3_unorm:            return 3;
        case Format::format_16_vec3_snorm:            return 3;
        case Format::format_16_vec3_uscaled:          return 3;
        case Format::format_16_vec3_sscaled:          return 3;
        case Format::format_16_vec3_uint:             return 3;
        case Format::format_16_vec3_sint:             return 3;
        case Format::format_16_vec4_unorm:            return 4;
        case Format::format_16_vec4_snorm:            return 4;
        case Format::format_16_vec4_uscaled:          return 4;
        case Format::format_16_vec4_sscaled:          return 4;
        case Format::format_16_vec4_uint:             return 4;
        case Format::format_16_vec4_sint:             return 4;
        case Format::format_32_scalar_unorm:          return 1;
        case Format::format_32_scalar_snorm:          return 1;
        case Format::format_32_scalar_uscaled:        return 1;
        case Format::format_32_scalar_sscaled:        return 1;
        case Format::format_32_scalar_uint:           return 1;
        case Format::format_32_scalar_sint:           return 1;
        case Format::format_32_scalar_float:          return 1;
        case Format::format_32_vec2_unorm:            return 2;
        case Format::format_32_vec2_snorm:            return 2;
        case Format::format_32_vec2_uscaled:          return 2;
        case Format::format_32_vec2_sscaled:          return 2;
        case Format::format_32_vec2_uint:             return 2;
        case Format::format_32_vec2_sint:             return 2;
        case Format::format_32_vec2_float:            return 2;
        case Format::format_32_vec3_unorm:            return 3;
        case Format::format_32_vec3_snorm:            return 3;
        case Format::format_32_vec3_uscaled:          return 3;
        case Format::format_32_vec3_sscaled:          return 3;
        case Format::format_32_vec3_uint:             return 3;
        case Format::format_32_vec3_sint:             return 3;
        case Format::format_32_vec3_float:            return 3;
        case Format::format_32_vec4_unorm:            return 4;
        case Format::format_32_vec4_snorm:            return 4;
        case Format::format_32_vec4_uscaled:          return 4;
        case Format::format_32_vec4_sscaled:          return 4;
        case Format::format_32_vec4_uint:             return 4;
        case Format::format_32_vec4_sint:             return 4;
        case Format::format_32_vec4_float:            return 4;
        case Format::format_packed1010102_vec4_unorm: return 4;
        case Format::format_packed1010102_vec4_snorm: return 4;
        case Format::format_packed1010102_vec4_uint:  return 4;
        case Format::format_packed1010102_vec4_sint:  return 4;

        default: {
            ERHE_FATAL("Bad format");
        }
    }
}

auto get_component_byte_size(Format format) -> std::size_t
{
    switch (format) {
        case Format::format_8_scalar_unorm:           return 1;
        case Format::format_8_scalar_snorm:           return 1;
        case Format::format_8_scalar_uscaled:         return 1;
        case Format::format_8_scalar_sscaled:         return 1;
        case Format::format_8_scalar_uint:            return 1;
        case Format::format_8_scalar_sint:            return 1;
        case Format::format_8_vec2_unorm:             return 1;
        case Format::format_8_vec2_snorm:             return 1;
        case Format::format_8_vec2_uscaled:           return 1;
        case Format::format_8_vec2_sscaled:           return 1;
        case Format::format_8_vec2_uint:              return 1;
        case Format::format_8_vec2_sint:              return 1;
        case Format::format_8_vec3_unorm:             return 1;
        case Format::format_8_vec3_snorm:             return 1;
        case Format::format_8_vec3_uscaled:           return 1;
        case Format::format_8_vec3_sscaled:           return 1;
        case Format::format_8_vec3_uint:              return 1;
        case Format::format_8_vec3_sint:              return 1;
        case Format::format_8_vec4_unorm:             return 1;
        case Format::format_8_vec4_snorm:             return 1;
        case Format::format_8_vec4_uscaled:           return 1;
        case Format::format_8_vec4_sscaled:           return 1;
        case Format::format_8_vec4_uint:              return 1;
        case Format::format_8_vec4_sint:              return 1;
        case Format::format_16_scalar_unorm:          return 2;
        case Format::format_16_scalar_snorm:          return 2;
        case Format::format_16_scalar_uscaled:        return 2;
        case Format::format_16_scalar_sscaled:        return 2;
        case Format::format_16_scalar_uint:           return 2;
        case Format::format_16_scalar_sint:           return 2;
        case Format::format_16_vec2_unorm:            return 2;
        case Format::format_16_vec2_snorm:            return 2;
        case Format::format_16_vec2_uscaled:          return 2;
        case Format::format_16_vec2_sscaled:          return 2;
        case Format::format_16_vec2_uint:             return 2;
        case Format::format_16_vec2_sint:             return 2;
        case Format::format_16_vec3_unorm:            return 2;
        case Format::format_16_vec3_snorm:            return 2;
        case Format::format_16_vec3_uscaled:          return 2;
        case Format::format_16_vec3_sscaled:          return 2;
        case Format::format_16_vec3_uint:             return 2;
        case Format::format_16_vec3_sint:             return 2;
        case Format::format_16_vec4_unorm:            return 2;
        case Format::format_16_vec4_snorm:            return 2;
        case Format::format_16_vec4_uscaled:          return 2;
        case Format::format_16_vec4_sscaled:          return 2;
        case Format::format_16_vec4_uint:             return 2;
        case Format::format_16_vec4_sint:             return 2;
        case Format::format_32_scalar_unorm:          return 4;
        case Format::format_32_scalar_snorm:          return 4;
        case Format::format_32_scalar_uscaled:        return 4;
        case Format::format_32_scalar_sscaled:        return 4;
        case Format::format_32_scalar_uint:           return 4;
        case Format::format_32_scalar_sint:           return 4;
        case Format::format_32_scalar_float:          return 4;
        case Format::format_32_vec2_unorm:            return 4;
        case Format::format_32_vec2_snorm:            return 4;
        case Format::format_32_vec2_uscaled:          return 4;
        case Format::format_32_vec2_sscaled:          return 4;
        case Format::format_32_vec2_uint:             return 4;
        case Format::format_32_vec2_sint:             return 4;
        case Format::format_32_vec2_float:            return 4;
        case Format::format_32_vec3_unorm:            return 4;
        case Format::format_32_vec3_snorm:            return 4;
        case Format::format_32_vec3_uscaled:          return 4;
        case Format::format_32_vec3_sscaled:          return 4;
        case Format::format_32_vec3_uint:             return 4;
        case Format::format_32_vec3_sint:             return 4;
        case Format::format_32_vec3_float:            return 4;
        case Format::format_32_vec4_unorm:            return 4;
        case Format::format_32_vec4_snorm:            return 4;
        case Format::format_32_vec4_uscaled:          return 4;
        case Format::format_32_vec4_sscaled:          return 4;
        case Format::format_32_vec4_uint:             return 4;
        case Format::format_32_vec4_sint:             return 4;
        case Format::format_32_vec4_float:            return 4;
        case Format::format_packed1010102_vec4_unorm: return 4;
        case Format::format_packed1010102_vec4_snorm: return 4;
        case Format::format_packed1010102_vec4_uint:  return 4;
        case Format::format_packed1010102_vec4_sint:  return 4;

        default: {
            ERHE_FATAL("Bad format");
        }
    }
}

auto get_format_size(Format format) -> std::size_t
{
    switch (format) {
        case Format::format_8_scalar_unorm:           return 1 * 1;
        case Format::format_8_scalar_snorm:           return 1 * 1;
        case Format::format_8_scalar_uscaled:         return 1 * 1;
        case Format::format_8_scalar_sscaled:         return 1 * 1;
        case Format::format_8_scalar_uint:            return 1 * 1;
        case Format::format_8_scalar_sint:            return 1 * 1;
        case Format::format_8_vec2_unorm:             return 2 * 1;
        case Format::format_8_vec2_snorm:             return 2 * 1;
        case Format::format_8_vec2_uscaled:           return 2 * 1;
        case Format::format_8_vec2_sscaled:           return 2 * 1;
        case Format::format_8_vec2_uint:              return 2 * 1;
        case Format::format_8_vec2_sint:              return 2 * 1;
        case Format::format_8_vec3_unorm:             return 3 * 1;
        case Format::format_8_vec3_snorm:             return 3 * 1;
        case Format::format_8_vec3_uscaled:           return 3 * 1;
        case Format::format_8_vec3_sscaled:           return 3 * 1;
        case Format::format_8_vec3_uint:              return 3 * 1;
        case Format::format_8_vec3_sint:              return 3 * 1;
        case Format::format_8_vec4_unorm:             return 4 * 1;
        case Format::format_8_vec4_snorm:             return 4 * 1;
        case Format::format_8_vec4_uscaled:           return 4 * 1;
        case Format::format_8_vec4_sscaled:           return 4 * 1;
        case Format::format_8_vec4_uint:              return 4 * 1;
        case Format::format_8_vec4_sint:              return 4 * 1;
        case Format::format_16_scalar_unorm:          return 1 * 2;
        case Format::format_16_scalar_snorm:          return 1 * 2;
        case Format::format_16_scalar_uscaled:        return 1 * 2;
        case Format::format_16_scalar_sscaled:        return 1 * 2;
        case Format::format_16_scalar_uint:           return 1 * 2;
        case Format::format_16_scalar_sint:           return 1 * 2;
        case Format::format_16_vec2_unorm:            return 2 * 2;
        case Format::format_16_vec2_snorm:            return 2 * 2;
        case Format::format_16_vec2_uscaled:          return 2 * 2;
        case Format::format_16_vec2_sscaled:          return 2 * 2;
        case Format::format_16_vec2_uint:             return 2 * 2;
        case Format::format_16_vec2_sint:             return 2 * 2;
        case Format::format_16_vec3_unorm:            return 3 * 2;
        case Format::format_16_vec3_snorm:            return 3 * 2;
        case Format::format_16_vec3_uscaled:          return 3 * 2;
        case Format::format_16_vec3_sscaled:          return 3 * 2;
        case Format::format_16_vec3_uint:             return 3 * 2;
        case Format::format_16_vec3_sint:             return 3 * 2;
        case Format::format_16_vec4_unorm:            return 4 * 2;
        case Format::format_16_vec4_snorm:            return 4 * 2;
        case Format::format_16_vec4_uscaled:          return 4 * 2;
        case Format::format_16_vec4_sscaled:          return 4 * 2;
        case Format::format_16_vec4_uint:             return 4 * 2;
        case Format::format_16_vec4_sint:             return 4 * 2;
        case Format::format_32_scalar_unorm:          return 1 * 4;
        case Format::format_32_scalar_snorm:          return 1 * 4;
        case Format::format_32_scalar_uscaled:        return 1 * 4;
        case Format::format_32_scalar_sscaled:        return 1 * 4;
        case Format::format_32_scalar_uint:           return 1 * 4;
        case Format::format_32_scalar_sint:           return 1 * 4;
        case Format::format_32_scalar_float:          return 1 * 4;
        case Format::format_32_vec2_unorm:            return 2 * 4;
        case Format::format_32_vec2_snorm:            return 2 * 4;
        case Format::format_32_vec2_uscaled:          return 2 * 4;
        case Format::format_32_vec2_sscaled:          return 2 * 4;
        case Format::format_32_vec2_uint:             return 2 * 4;
        case Format::format_32_vec2_sint:             return 2 * 4;
        case Format::format_32_vec2_float:            return 2 * 4;
        case Format::format_32_vec3_unorm:            return 3 * 4;
        case Format::format_32_vec3_snorm:            return 3 * 4;
        case Format::format_32_vec3_uscaled:          return 3 * 4;
        case Format::format_32_vec3_sscaled:          return 3 * 4;
        case Format::format_32_vec3_uint:             return 3 * 4;
        case Format::format_32_vec3_sint:             return 3 * 4;
        case Format::format_32_vec3_float:            return 3 * 4;
        case Format::format_32_vec4_unorm:            return 4 * 4;
        case Format::format_32_vec4_snorm:            return 4 * 4;
        case Format::format_32_vec4_uscaled:          return 4 * 4;
        case Format::format_32_vec4_sscaled:          return 4 * 4;
        case Format::format_32_vec4_uint:             return 4 * 4;
        case Format::format_32_vec4_sint:             return 4 * 4;
        case Format::format_32_vec4_float:            return 4 * 4;
        case Format::format_packed1010102_vec4_unorm: return 4;
        case Format::format_packed1010102_vec4_snorm: return 4;
        case Format::format_packed1010102_vec4_uint:  return 4;
        case Format::format_packed1010102_vec4_sint:  return 4;

        default: {
            ERHE_FATAL("Bad format");
        }
    }
}

auto get_unsigned_integer_format(Format format) -> Format
{
    std::size_t component_count = get_component_count(format);
    switch (component_count) {
        case 1:  return Format::format_32_scalar_uint;
        case 2:  return Format::format_32_vec2_uint;
        case 3:  return Format::format_32_vec3_uint;
        case 4:  return Format::format_32_vec4_uint;
        default: return Format::format_undefined;
    }
}

auto get_float_format(Format format) -> Format
{
    std::size_t component_count = get_component_count(format);
    switch (component_count) {
        case 1:  return Format::format_32_scalar_float;
        case 2:  return Format::format_32_vec2_float;
        case 3:  return Format::format_32_vec2_float;
        case 4:  return Format::format_32_vec4_float;
        default: return Format::format_undefined;
    }
}

void convert(const void* src, Format src_format, void* dst, Format dst_format, float scale)
{
    if (src == nullptr) {
        std::size_t size = get_format_size(dst_format);
        memset(dst, 0, size);
        return;
    }
    if ((dst_format == src_format) && (scale == 1.0f))
    {
        std::size_t size = get_format_size(dst_format);
        memcpy(dst, src, size);
        return;
    }

    uint32_t    ui_value[4] = { 0, 0, 0, 0 };
    int32_t     i_value [4] = { 0, 0, 0, 0 };
    float       f_value [4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    Format_kind src_kind    = get_format_kind(src_format);
    Format_kind dst_kind    = get_format_kind(dst_format);
    switch (src_format) {
        case Format::format_8_scalar_sint: {
            const int8_t* i_src = reinterpret_cast<const int8_t*>(src);
            ui_value[0] = i_src[0];
            break;
        }
        case Format::format_8_vec2_sint: {
            const int8_t* i_src = reinterpret_cast<const int8_t*>(src);
            ui_value[0] = i_src[0];
            ui_value[1] = i_src[1];
            break;
        }
        case Format::format_8_vec3_sint: {
            const int8_t* i_src = reinterpret_cast<const int8_t*>(src);
            i_value[0] = i_src[0];
            i_value[1] = i_src[1];
            i_value[2] = i_src[2];
            break;
        }
        case Format::format_8_vec4_sint: {
            const int8_t* i_src = reinterpret_cast<const int8_t*>(src);
            i_value[0] = i_src[0];
            i_value[1] = i_src[1];
            i_value[2] = i_src[2];
            i_value[3] = i_src[3];
            break;
        }

        case Format::format_8_scalar_snorm: {
            const int8_t* i_src = reinterpret_cast<const int8_t*>(src);
            f_value[0] = snorm8_to_float(i_src[0]);
            f_value[1] = snorm8_to_float(i_src[1]);
            break;
        }
        case Format::format_8_vec2_snorm: {
            const int8_t* i_src = reinterpret_cast<const int8_t*>(src);
            f_value[0] = snorm8_to_float(i_src[0]);
            f_value[1] = snorm8_to_float(i_src[1]);
            break;
        }
        case Format::format_8_vec3_snorm: {
            const int8_t* i_src = reinterpret_cast<const int8_t*>(src);
            f_value[0] = snorm8_to_float(i_src[0]);
            f_value[1] = snorm8_to_float(i_src[1]);
            f_value[2] = snorm8_to_float(i_src[2]);
            break;
        }
        case Format::format_8_vec4_snorm: {
            const int8_t* i_src = reinterpret_cast<const int8_t*>(src);
            f_value[0] = snorm8_to_float(i_src[0]);
            f_value[1] = snorm8_to_float(i_src[1]);
            f_value[2] = snorm8_to_float(i_src[2]);
            f_value[3] = snorm8_to_float(i_src[3]);
            break;
        }

        case Format::format_8_scalar_uint: {
            const uint8_t* ui_src = reinterpret_cast<const uint8_t*>(src);
            ui_value[0] = ui_src[0];
            break;
        }
        case Format::format_8_vec2_uint: {
            const uint8_t* ui_src = reinterpret_cast<const uint8_t*>(src);
            ui_value[0] = ui_src[0];
            ui_value[1] = ui_src[1];
            break;
        }
        case Format::format_8_vec3_uint: {
            const uint8_t* ui_src = reinterpret_cast<const uint8_t*>(src);
            ui_value[0] = ui_src[0];
            ui_value[1] = ui_src[1];
            ui_value[2] = ui_src[2];
            break;
        }
        case Format::format_8_vec4_uint: {
            const uint8_t* ui_src = reinterpret_cast<const uint8_t*>(src);
            ui_value[0] = ui_src[0];
            ui_value[1] = ui_src[1];
            ui_value[2] = ui_src[2];
            ui_value[3] = ui_src[3];
            break;
        }

        case Format::format_8_scalar_unorm: {
            const uint8_t* ui_src = reinterpret_cast<const uint8_t*>(src);
            f_value[0] = unorm8_to_float(ui_src[0]);
            f_value[1] = unorm8_to_float(ui_src[1]);
            break;
        }
        case Format::format_8_vec2_unorm: {
            const uint8_t* ui_src = reinterpret_cast<const uint8_t*>(src);
            f_value[0] = unorm8_to_float(ui_src[0]);
            f_value[1] = unorm8_to_float(ui_src[1]);
            break;
        }
        case Format::format_8_vec3_unorm: {
            const uint8_t* ui_src = reinterpret_cast<const uint8_t*>(src);
            f_value[0] = unorm8_to_float(ui_src[0]);
            f_value[1] = unorm8_to_float(ui_src[1]);
            f_value[2] = unorm8_to_float(ui_src[2]);
            break;
        }
        case Format::format_8_vec4_unorm: {
            const uint8_t* ui_src = reinterpret_cast<const uint8_t*>(src);
            f_value[0] = unorm8_to_float(ui_src[0]);
            f_value[1] = unorm8_to_float(ui_src[1]);
            f_value[2] = unorm8_to_float(ui_src[2]);
            f_value[3] = unorm8_to_float(ui_src[3]);
            break;
        }

        case Format::format_16_scalar_sint: {
            const int16_t* i_src = reinterpret_cast<const int16_t*>(src);
            i_value[0] = i_src[0];
            break;
        }
        case Format::format_16_vec2_sint: {
            const int16_t* i_src = reinterpret_cast<const int16_t*>(src);
            i_value[0] = i_src[0];
            i_value[1] = i_src[1];
            break;
        }
        case Format::format_16_vec3_sint: {
            const int16_t* i_src = reinterpret_cast<const int16_t*>(src);
            i_value[0] = i_src[0];
            i_value[1] = i_src[1];
            i_value[2] = i_src[2];
            break;
        }
        case Format::format_16_vec4_sint: {
            const int16_t* i_src = reinterpret_cast<const int16_t*>(src);
            i_value[0] = i_src[0];
            i_value[1] = i_src[1];
            i_value[2] = i_src[2];
            i_value[3] = i_src[3];
            break;
        }

        case Format::format_16_scalar_snorm: {
            const int16_t* i_src = reinterpret_cast<const int16_t*>(src);
            f_value[0] = snorm16_to_float(i_src[0]);
            break;
        }
        case Format::format_16_vec2_snorm: {
            const int16_t* i_src = reinterpret_cast<const int16_t*>(src);
            f_value[0] = snorm16_to_float(i_src[0]);
            f_value[1] = snorm16_to_float(i_src[1]);
            break;
        }
        case Format::format_16_vec3_snorm: {
            const int16_t* i_src = reinterpret_cast<const int16_t*>(src);
            f_value[0] = snorm16_to_float(i_src[0]);
            f_value[1] = snorm16_to_float(i_src[1]);
            f_value[2] = snorm16_to_float(i_src[2]);
            break;
        }
        case Format::format_16_vec4_snorm: {
            const int16_t* i_src = reinterpret_cast<const int16_t*>(src);
            f_value[0] = snorm16_to_float(i_src[0]);
            f_value[1] = snorm16_to_float(i_src[1]);
            f_value[2] = snorm16_to_float(i_src[2]);
            f_value[3] = snorm16_to_float(i_src[3]);
            break;
        }

        case Format::format_16_scalar_uint: {
            const uint16_t* ui_src = reinterpret_cast<const uint16_t*>(src);
            ui_value[0] = ui_src[0];
            break;
        }
        case Format::format_16_vec2_uint: {
            const uint16_t* ui_src = reinterpret_cast<const uint16_t*>(src);
            ui_value[0] = ui_src[0];
            ui_value[1] = ui_src[1];
            break;
        }
        case Format::format_16_vec3_uint: {
            const uint16_t* ui_src = reinterpret_cast<const uint16_t*>(src);
            ui_value[0] = ui_src[0];
            ui_value[1] = ui_src[1];
            ui_value[2] = ui_src[2];
            break;
        }
        case Format::format_16_vec4_uint: {
            const uint16_t* ui_src = reinterpret_cast<const uint16_t*>(src);
            ui_value[0] = ui_src[0];
            ui_value[1] = ui_src[1];
            ui_value[2] = ui_src[2];
            ui_value[3] = ui_src[3];
            break;
        }

        case Format::format_16_scalar_unorm: {
            const uint16_t* ui_src = reinterpret_cast<const uint16_t*>(src);
            f_value[0] = unorm16_to_float(ui_src[0]);
            f_value[1] = unorm16_to_float(ui_src[1]);
            break;
        }
        case Format::format_16_vec2_unorm: {
            const uint16_t* ui_src = reinterpret_cast<const uint16_t*>(src);
            f_value[0] = unorm16_to_float(ui_src[0]);
            f_value[1] = unorm16_to_float(ui_src[1]);
            break;
        }
        case Format::format_16_vec3_unorm: {
            const uint16_t* ui_src = reinterpret_cast<const uint16_t*>(src);
            f_value[0] = unorm16_to_float(ui_src[0]);
            f_value[1] = unorm16_to_float(ui_src[1]);
            f_value[2] = unorm16_to_float(ui_src[2]);
            break;
        }
        case Format::format_16_vec4_unorm: {
            const uint16_t* ui_src = reinterpret_cast<const uint16_t*>(src);
            f_value[0] = unorm16_to_float(ui_src[0]);
            f_value[1] = unorm16_to_float(ui_src[1]);
            f_value[2] = unorm16_to_float(ui_src[2]);
            f_value[3] = unorm16_to_float(ui_src[3]);
            break;
        }

        case Format::format_32_scalar_float: {
            const float* f_src = reinterpret_cast<const float*>(src);
            f_value[0] = f_src[0];
            break;
        }
        case Format::format_32_vec2_float: {
            const float* f_src = reinterpret_cast<const float*>(src);
            f_value[0] = f_src[0];
            f_value[1] = f_src[1];
            break;
        }
        case Format::format_32_vec3_float: {
            const float* f_src = reinterpret_cast<const float*>(src);
            f_value[0] = f_src[0];
            f_value[1] = f_src[1];
            f_value[2] = f_src[2];
            break;
        }
        case Format::format_32_vec4_float: {
            const float* f_src = reinterpret_cast<const float*>(src);
            f_value[0] = f_src[0];
            f_value[1] = f_src[1];
            f_value[2] = f_src[2];
            f_value[3] = f_src[3];
            break;
        }

        case Format::format_32_scalar_sint: {
            const int32_t* i_src = reinterpret_cast<const int32_t*>(src);
            i_value[0] = i_src[0];
            break;
        }
        case Format::format_32_vec2_sint: {
            const int32_t* i_src = reinterpret_cast<const int32_t*>(src);
            i_value[0] = i_src[0];
            i_value[1] = i_src[1];
            break;
        }
        case Format::format_32_vec3_sint: {
            const int32_t* i_src = reinterpret_cast<const int32_t*>(src);
            i_value[0] = i_src[0];
            i_value[1] = i_src[1];
            i_value[2] = i_src[2];
            break;
        }
        case Format::format_32_vec4_sint: {
            const int32_t* i_src = reinterpret_cast<const int32_t*>(src);
            i_value[0] = i_src[0];
            i_value[1] = i_src[1];
            i_value[2] = i_src[2];
            i_value[3] = i_src[3];
            break;
        }

        case Format::format_32_scalar_uint: {
            const uint32_t* ui_src = reinterpret_cast<const uint32_t*>(src);
            ui_value[0] = ui_src[0];
            break;
        }
        case Format::format_32_vec2_uint: {
            const uint32_t* ui_src = reinterpret_cast<const uint32_t*>(src);
            ui_value[0] = ui_src[0];
            ui_value[1] = ui_src[1];
            break;
        }
        case Format::format_32_vec3_uint: {
            const uint32_t* ui_src = reinterpret_cast<const uint32_t*>(src);
            ui_value[0] = ui_src[0];
            ui_value[1] = ui_src[1];
            ui_value[2] = ui_src[2];
            break;
        }
        case Format::format_32_vec4_uint: {
            const uint32_t* ui_src = reinterpret_cast<const uint32_t*>(src);
            ui_value[0] = ui_src[0];
            ui_value[1] = ui_src[1];
            ui_value[2] = ui_src[2];
            ui_value[3] = ui_src[3];
            break;
        }

        default: {
            ERHE_FATAL("Unsupported path in convert()");
        }
    }

    switch (dst_kind) {
        case Format_kind::format_kind_float: {
            switch (src_kind) {
                case Format_kind::format_kind_float: {
                    break;
                }
                case Format_kind::format_kind_signed_integer: {
                    f_value[0] = static_cast<float>(i_value[0]);
                    f_value[1] = static_cast<float>(i_value[1]);
                    f_value[2] = static_cast<float>(i_value[2]);
                    f_value[3] = static_cast<float>(i_value[3]);
                    break;
                }
                case Format_kind::format_kind_unsigned_integer: {
                    f_value[0] = static_cast<float>(ui_value[0]);
                    f_value[1] = static_cast<float>(ui_value[1]);
                    f_value[2] = static_cast<float>(ui_value[2]);
                    f_value[3] = static_cast<float>(ui_value[3]);
                    break;
                }
                default: {
                    ERHE_FATAL("Unsupported path in convert()");
                    break;
                }
            }
            break;
        }

        case Format_kind::format_kind_signed_integer: {
            switch (src_kind) {
                case Format_kind::format_kind_float: {
                    i_value[0] = static_cast<int32_t>(f_value[0]);
                    i_value[1] = static_cast<int32_t>(f_value[1]);
                    i_value[2] = static_cast<int32_t>(f_value[2]);
                    i_value[3] = static_cast<int32_t>(f_value[3]);
                    break;
                }
                case Format_kind::format_kind_signed_integer: {
                    break;
                }
                case Format_kind::format_kind_unsigned_integer: {
                    i_value[0] = static_cast<uint32_t>(std::min(ui_value[0], static_cast<uint32_t>(std::numeric_limits<int32_t>::max())));
                    i_value[1] = static_cast<uint32_t>(std::min(ui_value[1], static_cast<uint32_t>(std::numeric_limits<int32_t>::max())));
                    i_value[2] = static_cast<uint32_t>(std::min(ui_value[2], static_cast<uint32_t>(std::numeric_limits<int32_t>::max())));
                    i_value[3] = static_cast<uint32_t>(std::min(ui_value[3], static_cast<uint32_t>(std::numeric_limits<int32_t>::max())));
                    break;
                }
                default: {
                    ERHE_FATAL("Unsupported path in convert()");
                    break;
                }
            }
            break;
        }

        case Format_kind::format_kind_unsigned_integer: {
            switch (src_kind) {
                case Format_kind::format_kind_float: {
                    ui_value[0] = static_cast<uint32_t>(f_value[0]);
                    ui_value[1] = static_cast<uint32_t>(f_value[1]);
                    ui_value[2] = static_cast<uint32_t>(f_value[2]);
                    ui_value[3] = static_cast<uint32_t>(f_value[3]);
                    break;
                }
                case Format_kind::format_kind_signed_integer: {
                    ui_value[0] = static_cast<uint32_t>(std::max(0, i_value[0]));
                    ui_value[1] = static_cast<uint32_t>(std::max(0, i_value[1]));
                    ui_value[2] = static_cast<uint32_t>(std::max(0, i_value[2]));
                    ui_value[3] = static_cast<uint32_t>(std::max(0, i_value[3]));
                    break;
                }
                case Format_kind::format_kind_unsigned_integer: {
                    break;
                }
                default: {
                    ERHE_FATAL("Unsupported path in convert()");
                    break;
                }
            }
            break;
        }

        default: {
            ERHE_FATAL("Unsupported path in convert()");
        }
    }

    switch (dst_format) {
        case Format::format_8_scalar_sint: {
            ERHE_VERIFY(i_value[0] >= std::numeric_limits<int8_t>::lowest());
            ERHE_VERIFY(i_value[0] <= std::numeric_limits<int8_t>::max());
            int8_t i[1];
            i[0] = static_cast<int8_t>(i_value[0]);
            memcpy(dst, &i[0], 1 * sizeof(int8_t));
            break;
        }
        case Format::format_8_vec2_sint: {
            ERHE_VERIFY(i_value[0] >= std::numeric_limits<int8_t>::lowest());
            ERHE_VERIFY(i_value[1] >= std::numeric_limits<int8_t>::lowest());
            ERHE_VERIFY(i_value[0] <= std::numeric_limits<int8_t>::max());
            ERHE_VERIFY(i_value[1] <= std::numeric_limits<int8_t>::max());
            int8_t i[2];
            i[0] = static_cast<int8_t>(i_value[0]);
            i[1] = static_cast<int8_t>(i_value[1]);
            memcpy(dst, &i[0], 2 * sizeof(int8_t));
            break;
        }
        case Format::format_8_vec3_sint: {
            ERHE_VERIFY(i_value[0] >= std::numeric_limits<int8_t>::lowest());
            ERHE_VERIFY(i_value[1] >= std::numeric_limits<int8_t>::lowest());
            ERHE_VERIFY(i_value[2] >= std::numeric_limits<int8_t>::lowest());
            ERHE_VERIFY(i_value[0] <= std::numeric_limits<int8_t>::max());
            ERHE_VERIFY(i_value[1] <= std::numeric_limits<int8_t>::max());
            ERHE_VERIFY(i_value[2] <= std::numeric_limits<int8_t>::max());
            int8_t i[3];
            i[0] = static_cast<int8_t>(i_value[0]);
            i[1] = static_cast<int8_t>(i_value[1]);
            i[2] = static_cast<int8_t>(i_value[2]);
            memcpy(dst, &i[0], 3 * sizeof(int8_t));
            break;
        }
        case Format::format_8_vec4_sint: {
            ERHE_VERIFY(i_value[0] >= std::numeric_limits<int8_t>::lowest());
            ERHE_VERIFY(i_value[1] >= std::numeric_limits<int8_t>::lowest());
            ERHE_VERIFY(i_value[2] >= std::numeric_limits<int8_t>::lowest());
            ERHE_VERIFY(i_value[3] >= std::numeric_limits<int8_t>::lowest());
            ERHE_VERIFY(i_value[0] <= std::numeric_limits<int8_t>::max());
            ERHE_VERIFY(i_value[1] <= std::numeric_limits<int8_t>::max());
            ERHE_VERIFY(i_value[2] <= std::numeric_limits<int8_t>::max());
            ERHE_VERIFY(i_value[3] <= std::numeric_limits<int8_t>::max());
            int8_t i[4];
            i[0] = static_cast<int8_t>(i_value[0]);
            i[1] = static_cast<int8_t>(i_value[1]);
            i[2] = static_cast<int8_t>(i_value[2]);
            i[3] = static_cast<int8_t>(i_value[3]);
            memcpy(dst, &i[0], 4 * sizeof(int8_t));
            break;
        }

        case Format::format_8_scalar_uint: {
            ERHE_VERIFY(i_value[0] <= std::numeric_limits<uint8_t>::max());
            uint8_t ui[1];
            ui[0] = static_cast<uint8_t>(ui_value[0]);
            memcpy(dst, &ui[0], 1 * sizeof(uint8_t));
            break;
        }
        case Format::format_8_vec2_uint: {
            ERHE_VERIFY(i_value[0] <= std::numeric_limits<uint8_t>::max());
            ERHE_VERIFY(i_value[1] <= std::numeric_limits<uint8_t>::max());
            uint8_t ui[2];
            ui[0] = static_cast<uint8_t>(ui_value[0]);
            ui[1] = static_cast<uint8_t>(ui_value[1]);
            memcpy(dst, &ui[0], 2 * sizeof(uint8_t));
            break;
        }
        case Format::format_8_vec3_uint:
        {
            ERHE_VERIFY(i_value[0] <= std::numeric_limits<uint8_t>::max());
            ERHE_VERIFY(i_value[1] <= std::numeric_limits<uint8_t>::max());
            ERHE_VERIFY(i_value[2] <= std::numeric_limits<uint8_t>::max());
            uint8_t ui[3];
            ui[0] = static_cast<uint8_t>(ui_value[0]);
            ui[1] = static_cast<uint8_t>(ui_value[1]);
            ui[2] = static_cast<uint8_t>(ui_value[2]);
            memcpy(dst, &ui[0], 3 * sizeof(uint8_t));
            break;
        }
        case Format::format_8_vec4_uint:
        {
            ERHE_VERIFY(i_value[0] <= std::numeric_limits<uint8_t>::max());
            ERHE_VERIFY(i_value[1] <= std::numeric_limits<uint8_t>::max());
            ERHE_VERIFY(i_value[2] <= std::numeric_limits<uint8_t>::max());
            ERHE_VERIFY(i_value[3] <= std::numeric_limits<uint8_t>::max());
            uint8_t ui[4];
            ui[0] = static_cast<uint8_t>(ui_value[0]);
            ui[1] = static_cast<uint8_t>(ui_value[1]);
            ui[2] = static_cast<uint8_t>(ui_value[2]);
            ui[3] = static_cast<uint8_t>(ui_value[3]);
            memcpy(dst, &ui[0], 4 * sizeof(uint8_t));
            break;
        }

        case Format::format_8_scalar_snorm: {
            float fx = f_value[0] / scale;
            ERHE_VERIFY(fx >= -1.001f);
            ERHE_VERIFY(fx <= 1.001f);
            int8_t i[2];
            i[0] = float_to_snorm8(fx);
            memcpy(dst, &i[0], 1 * sizeof(int8_t));
            break;
        }
        case Format::format_8_vec2_snorm: {
            float fx = f_value[0] / scale;
            float fy = f_value[1] / scale;
            ERHE_VERIFY(fx >= -1.001f);
            ERHE_VERIFY(fy >= -1.001f);
            ERHE_VERIFY(fx <= 1.001f);
            ERHE_VERIFY(fy <= 1.001f);
            int8_t i[2];
            i[0] = float_to_snorm8(fx);
            i[1] = float_to_snorm8(fy);
            memcpy(dst, &i[0], 2 * sizeof(int8_t));
            break;
        }
        case Format::format_8_vec3_snorm: {
            float fx = f_value[0] / scale;
            float fy = f_value[1] / scale;
            float fz = f_value[2] / scale;
            ERHE_VERIFY(fx >= -1.001f);
            ERHE_VERIFY(fy >= -1.001f);
            ERHE_VERIFY(fz >= -1.001f);
            ERHE_VERIFY(fx <= 1.001f);
            ERHE_VERIFY(fy <= 1.001f);
            ERHE_VERIFY(fz <= 1.001f);
            uint8_t i[3];
            i[0] = float_to_snorm8(fx);
            i[1] = float_to_snorm8(fy);
            i[2] = float_to_snorm8(fz);
            memcpy(dst, &i[0], 3 * sizeof(int8_t));
            break;
        }
        case Format::format_8_vec4_snorm: {
            float fx = f_value[0] / scale;
            float fy = f_value[1] / scale;
            float fz = f_value[2] / scale;
            float fw = f_value[3] / scale;
            ERHE_VERIFY(fx >= -1.001f);
            ERHE_VERIFY(fy >= -1.001f);
            ERHE_VERIFY(fz >= -1.001f);
            ERHE_VERIFY(fw >= -1.001f);
            ERHE_VERIFY(fx <= 1.001f);
            ERHE_VERIFY(fy <= 1.001f);
            ERHE_VERIFY(fz <= 1.001f);
            ERHE_VERIFY(fw <= 1.001f);
            int8_t i[4];
            i[0] = float_to_snorm8(fx);
            i[1] = float_to_snorm8(fy);
            i[2] = float_to_snorm8(fz);
            i[3] = float_to_snorm8(fw);
            memcpy(dst, &i[0], 4 * sizeof(int8_t));
            break;
        }

        case Format::format_8_scalar_unorm: {
            float fx = f_value[0] / scale;
            ERHE_VERIFY(fx <= 1.001f);
            uint8_t ui[1];
            ui[0] = float_to_unorm8(fx);
            memcpy(dst, &ui[0], 1 * sizeof(uint8_t));
            break;
        }
        case Format::format_8_vec2_unorm: {
            float fx = f_value[0] / scale;
            float fy = f_value[1] / scale;
            ERHE_VERIFY(fx <= 1.001f);
            ERHE_VERIFY(fy <= 1.001f);
            uint8_t ui[2];
            ui[0] = float_to_unorm8(fx);
            ui[1] = float_to_unorm8(fy);
            memcpy(dst, &ui[0], 2 * sizeof(uint8_t));
            break;
        }
        case Format::format_8_vec3_unorm: {
            float fx = f_value[0] / scale;
            float fy = f_value[1] / scale;
            float fz = f_value[2] / scale;
            ERHE_VERIFY(fx <= 1.001f);
            ERHE_VERIFY(fy <= 1.001f);
            ERHE_VERIFY(fz <= 1.001f);
            uint8_t ui[3];
            ui[0] = float_to_unorm8(fx);
            ui[1] = float_to_unorm8(fy);
            ui[2] = float_to_unorm8(fz);
            memcpy(dst, &ui[0], 3 * sizeof(uint8_t));
            break;
        }
        case Format::format_8_vec4_unorm: {
            float fx = f_value[0] / scale;
            float fy = f_value[1] / scale;
            float fz = f_value[2] / scale;
            float fw = f_value[3] / scale;
            ERHE_VERIFY(fx <= 1.001f);
            ERHE_VERIFY(fy <= 1.001f);
            ERHE_VERIFY(fz <= 1.001f);
            ERHE_VERIFY(fw <= 1.001f);
            uint8_t ui[4];
            ui[0] = float_to_unorm8(fx);
            ui[1] = float_to_unorm8(fy);
            ui[2] = float_to_unorm8(fz);
            ui[3] = float_to_unorm8(fw);
            memcpy(dst, &ui[0], 4 * sizeof(uint8_t));
            break;
        }

        case Format::format_16_scalar_sint: {
            ERHE_VERIFY(i_value[0] >= std::numeric_limits<int16_t>::lowest());
            ERHE_VERIFY(i_value[0] <= std::numeric_limits<int16_t>::max());
            int16_t i[1];
            i[0] = static_cast<int16_t>(i_value[0]);
            memcpy(dst, &i[0], 1 * sizeof(int16_t));
            break;
        }
        case Format::format_16_vec2_sint: {
            ERHE_VERIFY(i_value[0] >= std::numeric_limits<int16_t>::lowest());
            ERHE_VERIFY(i_value[1] >= std::numeric_limits<int16_t>::lowest());
            ERHE_VERIFY(i_value[0] <= std::numeric_limits<int16_t>::max());
            ERHE_VERIFY(i_value[1] <= std::numeric_limits<int16_t>::max());
            int16_t i[2];
            i[0] = static_cast<int16_t>(i_value[0]);
            i[1] = static_cast<int16_t>(i_value[1]);
            memcpy(dst, &i[0], 2 * sizeof(int16_t));
            break;
        }
        case Format::format_16_vec3_sint: {
            ERHE_VERIFY(i_value[0] >= std::numeric_limits<int16_t>::lowest());
            ERHE_VERIFY(i_value[1] >= std::numeric_limits<int16_t>::lowest());
            ERHE_VERIFY(i_value[2] >= std::numeric_limits<int16_t>::lowest());
            ERHE_VERIFY(i_value[0] <= std::numeric_limits<int16_t>::max());
            ERHE_VERIFY(i_value[1] <= std::numeric_limits<int16_t>::max());
            ERHE_VERIFY(i_value[2] <= std::numeric_limits<int16_t>::max());
            int16_t i[3];
            i[0] = static_cast<int16_t>(i_value[0]);
            i[1] = static_cast<int16_t>(i_value[1]);
            i[2] = static_cast<int16_t>(i_value[2]);
            memcpy(dst, &i[0], 3 * sizeof(int16_t));
            break;
        }
        case Format::format_16_vec4_sint: {
            ERHE_VERIFY(i_value[0] >= std::numeric_limits<int16_t>::lowest());
            ERHE_VERIFY(i_value[1] >= std::numeric_limits<int16_t>::lowest());
            ERHE_VERIFY(i_value[2] >= std::numeric_limits<int16_t>::lowest());
            ERHE_VERIFY(i_value[3] >= std::numeric_limits<int16_t>::lowest());
            ERHE_VERIFY(i_value[0] <= std::numeric_limits<int16_t>::max());
            ERHE_VERIFY(i_value[1] <= std::numeric_limits<int16_t>::max());
            ERHE_VERIFY(i_value[2] <= std::numeric_limits<int16_t>::max());
            ERHE_VERIFY(i_value[3] <= std::numeric_limits<int16_t>::max());
            int16_t i[4];
            i[0] = static_cast<int16_t>(i_value[0]);
            i[1] = static_cast<int16_t>(i_value[1]);
            i[2] = static_cast<int16_t>(i_value[2]);
            i[3] = static_cast<int16_t>(i_value[3]);
            memcpy(dst, &i[0], 4 * sizeof(int16_t));
            break;
        }

        case Format::format_16_scalar_uint: {
            ERHE_VERIFY(ui_value[0] <= std::numeric_limits<uint16_t>::max());
            uint16_t ui[1];
            ui[0] = static_cast<uint16_t>(ui_value[0]);
            memcpy(dst, &ui[0], 1 * sizeof(uint16_t));
            break;
        }
        case Format::format_16_vec2_uint: {
            ERHE_VERIFY(ui_value[0] <= std::numeric_limits<uint16_t>::max());
            ERHE_VERIFY(ui_value[1] <= std::numeric_limits<uint16_t>::max());
            uint16_t ui[2];
            ui[0] = static_cast<uint16_t>(ui_value[0]);
            ui[1] = static_cast<uint16_t>(ui_value[1]);
            memcpy(dst, &ui[0], 2 * sizeof(uint16_t));
            break;
        }
        case Format::format_16_vec3_uint: {
            ERHE_VERIFY(ui_value[0] <= std::numeric_limits<uint16_t>::max());
            ERHE_VERIFY(ui_value[1] <= std::numeric_limits<uint16_t>::max());
            ERHE_VERIFY(ui_value[2] <= std::numeric_limits<uint16_t>::max());
            uint16_t ui[3];
            ui[0] = static_cast<uint16_t>(ui_value[0]);
            ui[1] = static_cast<uint16_t>(ui_value[1]);
            ui[2] = static_cast<uint16_t>(ui_value[2]);
            memcpy(dst, &ui[0], 3 * sizeof(uint16_t));
            break;
        }
        case Format::format_16_vec4_uint: {
            ERHE_VERIFY(ui_value[0] <= std::numeric_limits<uint16_t>::max());
            ERHE_VERIFY(ui_value[1] <= std::numeric_limits<uint16_t>::max());
            ERHE_VERIFY(ui_value[2] <= std::numeric_limits<uint16_t>::max());
            ERHE_VERIFY(ui_value[3] <= std::numeric_limits<uint16_t>::max());
            uint16_t ui[4];
            ui[0] = static_cast<uint16_t>(ui_value[0]);
            ui[1] = static_cast<uint16_t>(ui_value[1]);
            ui[2] = static_cast<uint16_t>(ui_value[2]);
            ui[3] = static_cast<uint16_t>(ui_value[3]);
            memcpy(dst, &ui[0], 4 * sizeof(uint16_t));
            break;
        }

        case Format::format_16_scalar_snorm: {
            float fx = f_value[0] / scale;
            ERHE_VERIFY(fx >= -1.001f);
            ERHE_VERIFY(fx <= 1.001f);
            int16_t i[1];
            i[0] = float_to_snorm16(fx);
            memcpy(dst, &i[0], 1 * sizeof(int16_t));
            break;
        }
        case Format::format_16_vec2_snorm: {
            float fx = f_value[0] / scale;
            float fy = f_value[1] / scale;
            ERHE_VERIFY(fx >= -1.001f);
            ERHE_VERIFY(fy >= -1.001f);
            ERHE_VERIFY(fx <= 1.001f);
            ERHE_VERIFY(fy <= 1.001f);
            int16_t i[2];
            i[0] = float_to_snorm16(fx);
            i[1] = float_to_snorm16(fy);
            memcpy(dst, &i[0], 2 * sizeof(int16_t));
            break;
        }
        case Format::format_16_vec3_snorm: {
            float fx = f_value[0] / scale;
            float fy = f_value[1] / scale;
            float fz = f_value[2] / scale;
            ERHE_VERIFY(fx >= -1.001f);
            ERHE_VERIFY(fy >= -1.001f);
            ERHE_VERIFY(fz >= -1.001f);
            ERHE_VERIFY(fx <= 1.001f);
            ERHE_VERIFY(fy <= 1.001f);
            ERHE_VERIFY(fz <= 1.001f);
            int16_t i[3];
            i[0] = float_to_snorm16(fx);
            i[1] = float_to_snorm16(fy);
            i[2] = float_to_snorm16(fz);
            memcpy(dst, &i[0], 3 * sizeof(int16_t));
            break;
        }
        case Format::format_16_vec4_snorm: {
            float fx = f_value[0] / scale;
            float fy = f_value[1] / scale;
            float fz = f_value[2] / scale;
            float fw = f_value[3] / scale;
            ERHE_VERIFY(fx >= -1.001f);
            ERHE_VERIFY(fy >= -1.001f);
            ERHE_VERIFY(fz >= -1.001f);
            ERHE_VERIFY(fw >= -1.001f);
            ERHE_VERIFY(fx <= 1.001f);
            ERHE_VERIFY(fy <= 1.001f);
            ERHE_VERIFY(fz <= 1.001f);
            ERHE_VERIFY(fw <= 1.001f);
            int16_t i[4];
            i[0] = float_to_snorm16(fx);
            i[1] = float_to_snorm16(fy);
            i[2] = float_to_snorm16(fz);
            i[3] = float_to_snorm16(fw);
            memcpy(dst, &i[0], 4 * sizeof(int16_t));
            break;
        }

        case Format::format_16_scalar_unorm: {
            float fx = f_value[0] / scale;
            ERHE_VERIFY(fx <= 1.001f);
            uint16_t ui[1];
            ui[0] = float_to_unorm16(fx);
            memcpy(dst, &ui[0], 1 * sizeof(uint16_t));
            break;
        }
        case Format::format_16_vec2_unorm: {
            float fx = f_value[0] / scale;
            float fy = f_value[1] / scale;
            ERHE_VERIFY(fx <= 1.001f);
            ERHE_VERIFY(fy <= 1.001f);
            uint16_t ui[2];
            ui[0] = float_to_unorm16(fx);
            ui[1] = float_to_unorm16(fy);
            memcpy(dst, &ui[0], 2 * sizeof(uint16_t));
            break;
        }
        case Format::format_16_vec3_unorm: {
            float fx = f_value[0] / scale;
            float fy = f_value[1] / scale;
            float fz = f_value[2] / scale;
            ERHE_VERIFY(fx <= 1.001f);
            ERHE_VERIFY(fy <= 1.001f);
            ERHE_VERIFY(fz <= 1.001f);
            uint16_t ui[3];
            ui[0] = float_to_unorm16(fx);
            ui[1] = float_to_unorm16(fy);
            ui[2] = float_to_unorm16(fz);
            memcpy(dst, &ui[0], 3 * sizeof(uint16_t));
            break;
        }
        case Format::format_16_vec4_unorm: {
            float fx = f_value[0] / scale;
            float fy = f_value[1] / scale;
            float fz = f_value[2] / scale;
            float fw = f_value[3] / scale;
            ERHE_VERIFY(fx <= 1.001f);
            ERHE_VERIFY(fy <= 1.001f);
            ERHE_VERIFY(fz <= 1.001f);
            ERHE_VERIFY(fw <= 1.001f);
            uint16_t ui[4];
            ui[0] = float_to_unorm16(fx);
            ui[1] = float_to_unorm16(fy);
            ui[2] = float_to_unorm16(fz);
            ui[3] = float_to_unorm16(fw);
            memcpy(dst, &ui[0], 4 * sizeof(uint16_t));
            break;
        }

        case Format::format_32_scalar_float: {
            memcpy(dst, &f_value[0], 1 * sizeof(float));
            break;
        }
        case Format::format_32_vec2_float: {
            memcpy(dst, &f_value[0], 2 * sizeof(float));
            break;
        }
        case Format::format_32_vec3_float: {
            memcpy(dst, &f_value[0], 3 * sizeof(float));
            break;
        }
        case Format::format_32_vec4_float: {
            memcpy(dst, &f_value[0], 4 * sizeof(float));
            break;
        }

        case Format::format_32_scalar_sint: {
            memcpy(dst, &i_value[0], 1 * sizeof(int32_t));
            break;
        }
        case Format::format_32_vec2_sint: {
            memcpy(dst, &i_value[0], 2 * sizeof(int32_t));
            break;
        }
        case Format::format_32_vec3_sint: {
            memcpy(dst, &i_value[0], 3 * sizeof(int32_t));
            break;
        }
        case Format::format_32_vec4_sint: {
            memcpy(dst, &i_value[0], 4 * sizeof(int32_t));
            break;
        }

        case Format::format_32_scalar_uint: {
            memcpy(dst, &ui_value[0], 1 * sizeof(uint32_t));
            break;
        }
        case Format::format_32_vec2_uint: {
            memcpy(dst, &ui_value[0], 2 * sizeof(uint32_t));
            break;
        }
        case Format::format_32_vec3_uint: {
            memcpy(dst, &ui_value[0], 3 * sizeof(uint32_t));
            break;
        }
        case Format::format_32_vec4_uint: {
            memcpy(dst, &ui_value[0], 4 * sizeof(uint32_t));
            break;
        }
        default: {
            ERHE_FATAL("Unsupported path in convert()");
            break;
        }
    }
}

} // namespace erhe::dataformat
