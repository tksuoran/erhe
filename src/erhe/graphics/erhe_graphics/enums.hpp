#pragma once

#include <cstddef>

namespace erhe::graphics {

enum class Glsl_type
{
    invalid = 0,
    float_,
    float_vec2,
    float_vec3,
    float_vec4,
    bool_,
    int_,
    int_vec2,
    int_vec3,
    int_vec4,
    unsigned_int,
    unsigned_int_vec2,
    unsigned_int_vec3,
    unsigned_int_vec4,
    unsigned_int64,
    float_mat_2x2,
    float_mat_3x3,
    float_mat_4x4,
    sampler_1d,
    sampler_2d,
    sampler_3d,
    sampler_cube,
    sampler_1d_shadow,
    sampler_2d_shadow,
    sampler_1d_array,
    sampler_2d_array,
    sampler_buffer,
    sampler_1d_array_shadow,
    sampler_2d_array_shadow,
    sampler_cube_shadow,
    int_sampler_1d,
    int_sampler_2d,
    int_sampler_3d,
    int_sampler_cube,
    int_sampler_1d_array,
    int_sampler_2d_array,
    int_sampler_buffer,
    unsigned_int_sampler_1d,
    unsigned_int_sampler_2d,
    unsigned_int_sampler_3d,
    unsigned_int_sampler_cube,
    unsigned_int_sampler_1d_array,
    unsigned_int_sampler_2d_array,
    unsigned_int_sampler_buffer,
    sampler_cube_map_array,
    sampler_cube_map_array_shadow,
    int_sampler_cube_map_array,
    unsigned_int_sampler_cube_map_array,
    sampler_2d_multisample,
    int_sampler_2d_multisample,
    unsigned_int_sampler_2d_multisample,
    sampler_2d_multisample_array,
    int_sampler_2d_multisample_array,
    unsigned_int_sampler_2d_multisample_array,
};

enum class Primitive_type : unsigned int
{
    point          = 0,
    line           = 1,
    line_strip     = 2,
    triangle       = 3,
    triangle_strip = 4
};

enum class Buffer_target : unsigned int
{
    index         = 0,
    vertex        = 1,
    uniform       = 2,
    storage       = 3,
    draw_indirect = 4
};

enum class Texture_type : unsigned int
{
    texture_buffer   = 0,
    texture_1d       = 1,
    texture_2d       = 2,
    texture_3d       = 3,
    texture_cube_map = 4
};

[[nodiscard]] auto glsl_type_c_str(const Glsl_type      type) -> const char*;
[[nodiscard]] auto get_dimension  (const Glsl_type      type) -> std::size_t;
[[nodiscard]] auto c_str          (const Primitive_type primitive_type) -> const char*;
[[nodiscard]] auto c_str          (const Buffer_target  buffer_target) -> const char*;
[[nodiscard]] auto is_indexed     (const Buffer_target  buffer_target) -> bool;
[[nodiscard]] auto c_str          (const Texture_type   texture_type) -> const char*;

} // namespace erhe::graphics
