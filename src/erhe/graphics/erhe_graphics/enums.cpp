#include "erhe_graphics/enums.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_verify/verify.hpp"

#include <sstream>

namespace erhe::graphics {

auto to_string(Buffer_usage usage) -> std::string
{
    using namespace erhe::utility;
    std::stringstream ss;
    bool is_empty = true;
    if (test_bit_set(usage, Buffer_usage::vertex)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "vertex";
        is_empty = false;
    }
    if (test_bit_set(usage, Buffer_usage::index)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "index";
        is_empty = false;
    }
    if (test_bit_set(usage, Buffer_usage::uniform)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "uniform";
        is_empty = false;
    }
    if (test_bit_set(usage, Buffer_usage::storage)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "storage";
        is_empty = false;
    }
    if (test_bit_set(usage, Buffer_usage::indirect)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "indirect";
        is_empty = false;
    }
    if (test_bit_set(usage, Buffer_usage::texture)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "texture";
        is_empty = false;
    }
    if (test_bit_set(usage, Buffer_usage::pixel)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "pixel";
        is_empty = false;
    }
    if (test_bit_set(usage, Buffer_usage::transfer)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "transfer";
        is_empty = false;
    }
    return ss.str();
}
auto c_str(Buffer_direction direction) -> const char*
{
    switch (direction) {
        case Buffer_direction::cpu_to_gpu: return "cpu_to_gpu";
        case Buffer_direction::gpu_only  : return "gpu_only";
        case Buffer_direction::gpu_to_cpu: return "gpu_to_cpu";
        default: return "?";
    }
}
auto c_str(Buffer_cache_mode cache_mode) -> const char*
{
    switch (cache_mode) {
        case Buffer_cache_mode::write_combined: return "write_combined";
        case Buffer_cache_mode::default_:       return "defaul";
        default: return "?";
    }
}
auto c_str(Buffer_mapping mapping) -> const char*
{
    switch (mapping) {
        case Buffer_mapping::not_mappable: return "not_mappable";
        case Buffer_mapping::persistent:   return "persistent";
        case Buffer_mapping::transient:    return "transient";
        default: return "?";
    }
}
auto c_str(Buffer_coherency coherency) -> const char*
{
    switch (coherency) {
        case Buffer_coherency::on:  return "on";
        case Buffer_coherency::off: return "off";
        default: return "?";
    }
}
auto to_string(Buffer_map_flags flags) -> std::string
{
    using namespace erhe::utility;
    std::stringstream ss;
    bool is_empty = true;
    if (test_bit_set(flags, Buffer_map_flags::explicit_flush)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "explicit_flush";
        is_empty = false;
    }
    if (test_bit_set(flags, Buffer_map_flags::invalidate_range)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "invalidate_range";
        is_empty = false;
    }
    if (test_bit_set(flags, Buffer_map_flags::invalidate_buffer)) {
        if (!is_empty) {
            ss << " | ";
        }
        ss << "invalidate_buffer";
        is_empty = false;
    }
    return ss.str();
}

auto glsl_type_c_str(const Glsl_type type) -> const char*
{
    switch (type) {
        //using enum igl::UniformType;
        case Glsl_type::invalid:                                     return "error_type";
        case Glsl_type::float_:                                      return "float";
        case Glsl_type::float_vec2:                                  return "vec2 ";
        case Glsl_type::float_vec3:                                  return "vec3 ";
        case Glsl_type::float_vec4:                                  return "vec4 ";
        case Glsl_type::bool_:                                       return "bool ";
        case Glsl_type::int_:                                        return "int  ";
        case Glsl_type::int_vec2:                                    return "ivec2";
        case Glsl_type::int_vec3:                                    return "ivec3";
        case Glsl_type::int_vec4:                                    return "ivec4";
        case Glsl_type::unsigned_int:                                return "uint ";
        case Glsl_type::unsigned_int_vec2:                           return "uvec2";
        case Glsl_type::unsigned_int_vec3:                           return "uvec3";
        case Glsl_type::unsigned_int_vec4:                           return "uvec4";
        case Glsl_type::float_mat_2x2:                               return "mat2 ";
        case Glsl_type::float_mat_3x3:                               return "mat3 ";
        case Glsl_type::float_mat_4x4:                               return "mat4 ";
        case Glsl_type::sampler_1d:                                  return "sampler1D";
        case Glsl_type::sampler_2d:                                  return "sampler2D";
        case Glsl_type::sampler_3d:                                  return "sampler3D";
        case Glsl_type::sampler_cube:                                return "samplerCube";
        case Glsl_type::sampler_1d_shadow:                           return "sampler1DShadow";
        case Glsl_type::sampler_2d_shadow:                           return "sampler2DShadow";
        case Glsl_type::sampler_1d_array:                            return "sampler1DArray";
        case Glsl_type::sampler_2d_array:                            return "sampler2DArray";
        case Glsl_type::sampler_buffer:                              return "samplerBuffer";
        case Glsl_type::sampler_1d_array_shadow:                     return "sampler1DArrayShadow";
        case Glsl_type::sampler_2d_array_shadow:                     return "sampler2DArrayShadow";
        case Glsl_type::sampler_cube_shadow:                         return "samplerCubeShadow";
        case Glsl_type::int_sampler_1d:                              return "intSampler1D";
        case Glsl_type::int_sampler_2d:                              return "intSampler2D";
        case Glsl_type::int_sampler_3d:                              return "intSampler3D";
        case Glsl_type::int_sampler_cube:                            return "intSamplerCube";
        case Glsl_type::int_sampler_1d_array:                        return "intSampler1DArray";
        case Glsl_type::int_sampler_2d_array:                        return "intSampler2DArray";
        case Glsl_type::int_sampler_buffer:                          return "intSamplerBuffer";
        case Glsl_type::unsigned_int_sampler_1d:                     return "unsignedIntSampler1D";
        case Glsl_type::unsigned_int_sampler_2d:                     return "unsignedIntSampler2D";
        case Glsl_type::unsigned_int_sampler_3d:                     return "unsignedIntSampler3D";
        case Glsl_type::unsigned_int_sampler_cube:                   return "unsignedIntSamplerCube";
        case Glsl_type::unsigned_int_sampler_1d_array:               return "unsignedIntSampler1DArray";
        case Glsl_type::unsigned_int_sampler_2d_array:               return "unsignedIntSampler2DArray";
        case Glsl_type::unsigned_int_sampler_buffer:                 return "unsignedIntSamplerBuffer";
        case Glsl_type::sampler_cube_map_array:                      return "samplerCubemapArray";
        case Glsl_type::sampler_cube_map_array_shadow:               return "samplerCubemapArrayShadow";
        case Glsl_type::int_sampler_cube_map_array:                  return "intSamplerCubemapArray";
        case Glsl_type::unsigned_int_sampler_cube_map_array:         return "unsignedIntSamplerCubemapArray";
        case Glsl_type::sampler_2d_multisample:                      return "sampler2DMultisampler";
        case Glsl_type::int_sampler_2d_multisample:                  return "intSampler2DMultisampler";
        case Glsl_type::unsigned_int_sampler_2d_multisample:         return "unsignedIntSampler2DMultisample";
        case Glsl_type::sampler_2d_multisample_array:                return "sampler2DMultisampleArray";
        case Glsl_type::int_sampler_2d_multisample_array:            return "intSampler2DMultisampleArray";
        case Glsl_type::unsigned_int_sampler_2d_multisample_array:   return "unsignedIntSampler2DMultisampleArray";

        default: {
            ERHE_FATAL("Bad Glsl_type");
        }
    }
}

auto get_dimension(const Glsl_type type) -> std::size_t
{
    switch (type) {
        //using enum igl::UniformType;
        case Glsl_type::invalid:                                     return 0;
        case Glsl_type::float_:                                      return 1;
        case Glsl_type::float_vec2:                                  return 2;
        case Glsl_type::float_vec3:                                  return 3;
        case Glsl_type::float_vec4:                                  return 4;
        case Glsl_type::bool_:                                       return 1;
        case Glsl_type::int_:                                        return 1;
        case Glsl_type::int_vec2:                                    return 2;
        case Glsl_type::int_vec3:                                    return 3;
        case Glsl_type::int_vec4:                                    return 4;
        case Glsl_type::unsigned_int:                                return 1;
        case Glsl_type::unsigned_int_vec2:                           return 2;
        case Glsl_type::unsigned_int_vec3:                           return 3;
        case Glsl_type::unsigned_int_vec4:                           return 4;
        case Glsl_type::float_mat_2x2:                               return 4;
        case Glsl_type::float_mat_3x3:                               return 9;
        case Glsl_type::float_mat_4x4:                               return 16;
        case Glsl_type::sampler_1d:                                  return 1;
        case Glsl_type::sampler_2d:                                  return 2;
        case Glsl_type::sampler_3d:                                  return 3;
        case Glsl_type::sampler_cube:                                return 6;
        case Glsl_type::sampler_1d_shadow:                           return 1;
        case Glsl_type::sampler_2d_shadow:                           return 2;
        case Glsl_type::sampler_1d_array:                            return 1;
        case Glsl_type::sampler_2d_array:                            return 2;
        case Glsl_type::sampler_buffer:                              return 1;
        case Glsl_type::sampler_1d_array_shadow:                     return 1;
        case Glsl_type::sampler_2d_array_shadow:                     return 2;
        case Glsl_type::sampler_cube_shadow:                         return 6;
        case Glsl_type::int_sampler_1d:                              return 1;
        case Glsl_type::int_sampler_2d:                              return 2;
        case Glsl_type::int_sampler_3d:                              return 3;
        case Glsl_type::int_sampler_cube:                            return 6;
        case Glsl_type::int_sampler_1d_array:                        return 1;
        case Glsl_type::int_sampler_2d_array:                        return 2;
        case Glsl_type::int_sampler_buffer:                          return 1;
        case Glsl_type::unsigned_int_sampler_1d:                     return 1;
        case Glsl_type::unsigned_int_sampler_2d:                     return 2;
        case Glsl_type::unsigned_int_sampler_3d:                     return 3;
        case Glsl_type::unsigned_int_sampler_cube:                   return 6;
        case Glsl_type::unsigned_int_sampler_1d_array:               return 1;
        case Glsl_type::unsigned_int_sampler_2d_array:               return 2;
        case Glsl_type::unsigned_int_sampler_buffer:                 return 1;
        case Glsl_type::sampler_cube_map_array:                      return 6;
        case Glsl_type::sampler_cube_map_array_shadow:               return 6;
        case Glsl_type::int_sampler_cube_map_array:                  return 6;
        case Glsl_type::unsigned_int_sampler_cube_map_array:         return 6;
        case Glsl_type::sampler_2d_multisample:                      return 2;
        case Glsl_type::int_sampler_2d_multisample:                  return 2;
        case Glsl_type::unsigned_int_sampler_2d_multisample:         return 2;
        case Glsl_type::sampler_2d_multisample_array:                return 2;
        case Glsl_type::int_sampler_2d_multisample_array:            return 2;
        case Glsl_type::unsigned_int_sampler_2d_multisample_array:   return 2;

        default: {
            ERHE_FATAL("Bad Glsl_type");
        }
    }
}

auto c_str(const Primitive_type primitive_type) -> const char*
{
    switch (primitive_type) {
        case Primitive_type::point         : return "point";
        case Primitive_type::line          : return "line";
        case Primitive_type::line_strip    : return "line_strip";
        case Primitive_type::triangle      : return "triangle";
        case Primitive_type::triangle_strip: return "triangle_strip";
        default: return "?";
    }
}

auto c_str(const Buffer_target buffer_target) -> const char*
{
    switch (buffer_target) {
        case Buffer_target::vertex       : return "vertex";
        case Buffer_target::uniform      : return "uniform";
        case Buffer_target::storage      : return "storage";
        case Buffer_target::draw_indirect: return "draw_indirect";
        default: return "?";
    }
}

auto is_indexed(const Buffer_target buffer_target) -> bool
{
    switch (buffer_target) {
        case Buffer_target::vertex       : return false;
        case Buffer_target::uniform      : return true;
        case Buffer_target::storage      : return true;
        case Buffer_target::draw_indirect: return false;
        default: return false;
    }
}

auto c_str(const Texture_type type) -> const char*
{
    switch (type) {
        case Texture_type::texture_buffer  : return "texture_buffer";
        case Texture_type::texture_1d      : return "texture_1d";
        case Texture_type::texture_2d      : return "texture_2d";
        case Texture_type::texture_3d      : return "texture_3d";
        case Texture_type::texture_cube_map: return "texture_cube_map";
        default: return "?";
    }
}

auto c_str(const Filter filter) -> const char*
{
    switch (filter) {
        case Filter::nearest: return "nearest";
        case Filter::linear : return "linear";
        default: return "?";
    }
}

auto c_str(const Sampler_mipmap_mode sampler_mipmap_mode) -> const char*
{
    switch (sampler_mipmap_mode) {
        case Sampler_mipmap_mode::not_mipmapped: return "not_mipmapped";
        case Sampler_mipmap_mode::nearest      : return "nearest";
        case Sampler_mipmap_mode::linear       : return "linear";
        default: return "?";
    }
}

auto c_str(const Sampler_address_mode sampler_address_mode) -> const char*
{
    switch (sampler_address_mode) {
        case Sampler_address_mode::repeat         : return "repeat";
        case Sampler_address_mode::clamp_to_edge  : return "clamp_to_edge";
        case Sampler_address_mode::mirrored_repeat: return "mirrored_repeat";
        default: return "?";
    }
}

auto c_str(const Compare_operation compare_operation) -> const char*
{
    switch (compare_operation) {
        case Compare_operation::never:            return "never";
        case Compare_operation::less:             return "less";
        case Compare_operation::equal:            return "equal";
        case Compare_operation::less_or_equal:    return "less_or_equal";
        case Compare_operation::greater:          return "greater";
        case Compare_operation::not_equal:        return "not_equal";
        case Compare_operation::greater_or_equal: return "greater_or_equal";
        case Compare_operation::always:           return "always";
        default: return "?";
    }
}

auto c_str(const Shader_type shader_type) -> const char*
{
    switch (shader_type) {
        case Shader_type::vertex_shader         : return "vertex_shader";
        case Shader_type::fragment_shader       : return "fragment_shader";
        case Shader_type::compute_shader        : return "compute_shader";
        case Shader_type::geometry_shader       : return "geometry_shader";
        case Shader_type::tess_control_shader   : return "tess_control_shader";
        case Shader_type::tess_evaluation_shader: return "tess_evaluation_shader";
        default: return "?";
    }
}

auto to_glsl_attribute_type(const erhe::dataformat::Format format) -> Glsl_type
{
    using namespace erhe::dataformat;
    switch (format) {
        case Format::format_8_scalar_unorm:           return Glsl_type::float_;
        case Format::format_8_scalar_snorm:           return Glsl_type::float_;
        case Format::format_8_scalar_uscaled:         return Glsl_type::float_;
        case Format::format_8_scalar_sscaled:         return Glsl_type::float_;
        case Format::format_8_scalar_uint:            return Glsl_type::unsigned_int;
        case Format::format_8_scalar_sint:            return Glsl_type::int_;
        case Format::format_8_vec2_unorm:             return Glsl_type::float_vec2;
        case Format::format_8_vec2_snorm:             return Glsl_type::float_vec2;
        case Format::format_8_vec2_uscaled:           return Glsl_type::float_vec2;
        case Format::format_8_vec2_sscaled:           return Glsl_type::float_vec2;
        case Format::format_8_vec2_uint:              return Glsl_type::unsigned_int_vec2;
        case Format::format_8_vec2_sint:              return Glsl_type::int_vec2;
        case Format::format_8_vec3_unorm:             return Glsl_type::float_vec3;
        case Format::format_8_vec3_snorm:             return Glsl_type::float_vec3;
        case Format::format_8_vec3_uscaled:           return Glsl_type::float_vec3;
        case Format::format_8_vec3_sscaled:           return Glsl_type::float_vec3;
        case Format::format_8_vec3_uint:              return Glsl_type::unsigned_int_vec3;
        case Format::format_8_vec3_sint:              return Glsl_type::int_vec3;
        case Format::format_8_vec4_unorm:             return Glsl_type::float_vec4;
        case Format::format_8_vec4_snorm:             return Glsl_type::float_vec4;
        case Format::format_8_vec4_uscaled:           return Glsl_type::float_vec4;
        case Format::format_8_vec4_sscaled:           return Glsl_type::float_vec4;
        case Format::format_8_vec4_uint:              return Glsl_type::unsigned_int_vec4;
        case Format::format_8_vec4_sint:              return Glsl_type::int_vec4;
        case Format::format_16_scalar_unorm:          return Glsl_type::float_;
        case Format::format_16_scalar_snorm:          return Glsl_type::float_;
        case Format::format_16_scalar_uscaled:        return Glsl_type::float_;
        case Format::format_16_scalar_sscaled:        return Glsl_type::float_;
        case Format::format_16_scalar_uint:           return Glsl_type::unsigned_int;
        case Format::format_16_scalar_sint:           return Glsl_type::int_;
        case Format::format_16_scalar_float:          return Glsl_type::float_;
        case Format::format_16_vec2_unorm:            return Glsl_type::float_vec2;
        case Format::format_16_vec2_snorm:            return Glsl_type::float_vec2;
        case Format::format_16_vec2_uscaled:          return Glsl_type::float_vec2;
        case Format::format_16_vec2_sscaled:          return Glsl_type::float_vec2;
        case Format::format_16_vec2_uint:             return Glsl_type::unsigned_int_vec2;
        case Format::format_16_vec2_sint:             return Glsl_type::int_vec2;
        case Format::format_16_vec2_float:            return Glsl_type::float_vec2;
        case Format::format_16_vec3_unorm:            return Glsl_type::float_vec3;
        case Format::format_16_vec3_snorm:            return Glsl_type::float_vec3;
        case Format::format_16_vec3_uscaled:          return Glsl_type::float_vec3;
        case Format::format_16_vec3_sscaled:          return Glsl_type::float_vec3;
        case Format::format_16_vec3_uint:             return Glsl_type::unsigned_int_vec3;
        case Format::format_16_vec3_sint:             return Glsl_type::int_vec3;
        case Format::format_16_vec3_float:            return Glsl_type::float_vec3;
        case Format::format_16_vec4_unorm:            return Glsl_type::float_vec4;
        case Format::format_16_vec4_snorm:            return Glsl_type::float_vec4;
        case Format::format_16_vec4_uscaled:          return Glsl_type::float_vec4;
        case Format::format_16_vec4_sscaled:          return Glsl_type::float_vec4;
        case Format::format_16_vec4_uint:             return Glsl_type::unsigned_int_vec4;
        case Format::format_16_vec4_sint:             return Glsl_type::int_vec4;
        case Format::format_16_vec4_float:            return Glsl_type::float_vec4;
        case Format::format_32_scalar_uint:           return Glsl_type::unsigned_int;
        case Format::format_32_scalar_sint:           return Glsl_type::int_;
        case Format::format_32_scalar_float:          return Glsl_type::float_;
        case Format::format_32_vec2_uint:             return Glsl_type::unsigned_int_vec2;
        case Format::format_32_vec2_sint:             return Glsl_type::int_vec2;
        case Format::format_32_vec2_float:            return Glsl_type::float_vec2;
        case Format::format_32_vec3_uint:             return Glsl_type::unsigned_int_vec3;
        case Format::format_32_vec3_sint:             return Glsl_type::int_vec3;
        case Format::format_32_vec3_float:            return Glsl_type::float_vec3;
        case Format::format_32_vec4_uint:             return Glsl_type::unsigned_int_vec4;
        case Format::format_32_vec4_sint:             return Glsl_type::int_vec4;
        case Format::format_32_vec4_float:            return Glsl_type::float_vec4;
        case Format::format_packed1010102_vec4_unorm: return Glsl_type::float_vec4;
        case Format::format_packed1010102_vec4_snorm: return Glsl_type::float_vec4;
        case Format::format_packed1010102_vec4_uint:  return Glsl_type::unsigned_int_vec4;
        case Format::format_packed1010102_vec4_sint:  return Glsl_type::int_vec4;
        case Format::format_packed111110_vec3_unorm:  return Glsl_type::float_vec3;

        default: {
            ERHE_FATAL("Bad format");
            return static_cast<Glsl_type>(0);
        }
    }
}

} // namespace erhe::graphics
