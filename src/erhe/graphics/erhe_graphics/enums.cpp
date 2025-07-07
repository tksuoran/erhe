#include "erhe_graphics/enums.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

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

} // namespace erhe::graphics
