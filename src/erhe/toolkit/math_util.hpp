#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace erhe::toolkit
{

template <typename T, typename U>
auto round(const float num)
-> T
{
    return static_cast<T>((num > U{0.0}) ? std::floor(num + U{0.5}) : std::ceil(num - U{0.5}));
}

inline auto next_power_of_two(uint32_t x)
-> uint32_t
{
    x--;
    x |= x >> 1u;  // handle  2 bit numbers
    x |= x >> 2u;  // handle  4 bit numbers
    x |= x >> 4u;  // handle  8 bit numbers
    x |= x >> 8u;  // handle 16 bit numbers
    x |= x >> 16u; // handle 32 bit numbers
    x++;

    return x;
}

inline auto uint_from_vector3(glm::vec3 v)
-> uint32_t
{
    const float        rf = v.x * 255.0f;
    const float        gf = v.y * 255.0f;
    const float        bf = v.z * 255.0f;
    const unsigned int r  = round<unsigned int, float>(rf) << 16u;
    const unsigned int g  = round<unsigned int, float>(gf) << 8u;
    const unsigned int b  = round<unsigned int, float>(bf) << 0u;
    const unsigned int i  = r | g | b;

    return static_cast<uint32_t>(i);
}

inline auto vec3_from_uint(const uint32_t i)
-> glm::vec3
{
    const uint32_t r = (i >> 16u) & 0xffu;
    const uint32_t g = (i >>  8u) & 0xffu;
    const uint32_t b = (i >>  0u) & 0xffu;

    return glm::vec3{r / 255.0f,
                     g / 255.0f,
                     b / 255.0f};
}

constexpr glm::vec3 vec3_unit_x{1.0f, 0.0f, 0.0f};
constexpr glm::vec3 vec3_unit_y{0.0f, 1.0f, 0.0f};
constexpr glm::vec3 vec3_unit_z{0.0f, 0.0f, 1.0f};

inline auto max_axis(const glm::vec3 v)
-> glm::vec3
{
    if (std::abs(v.x) >= std::abs(v.y) && std::abs(v.x) >= std::abs(v.z))
    {
        return vec3_unit_x;
    }

    if (std::abs(v.y) >= std::abs(v.x) && std::abs(v.y) >= std::abs(v.z))
    {
        return vec3_unit_y;
    }
    return vec3_unit_z;
}

inline auto min_axis(const glm::vec3 v)
-> glm::vec3
{
    if (std::abs(v.x) <= std::abs(v.y) && std::abs(v.x) <= std::abs(v.z))
    {
        return vec3_unit_x;
    }

    if (std::abs(v.y) <= std::abs(v.x) && std::abs(v.y) <= std::abs(v.z))
    {
        return vec3_unit_y;
    }

    return vec3_unit_z;
}

inline auto max_axis_index(const glm::vec3 v)
-> glm::vec3::length_type
{
    if (std::abs(v.x) >= std::abs(v.y) && std::abs(v.x) >= std::abs(v.z))
    {
        return glm::vec3::length_type{0};
    }

    if (std::abs(v.y) >= std::abs(v.x) && std::abs(v.y) >= std::abs(v.z))
    {
        return glm::vec3::length_type{1};
    }
    return glm::vec3::length_type{2};
}

inline auto min_axis_index(const glm::vec3 v)
-> glm::vec3::length_type
{
    if (std::abs(v.x) <= std::abs(v.y) && std::abs(v.x) <= std::abs(v.z))
    {
        return glm::vec3::length_type{0};
    }

    if (std::abs(v.y) <= std::abs(v.x) && std::abs(v.y) <= std::abs(v.z))
    {
        return glm::vec3::length_type{1};
    }
    return glm::vec3::length_type{2};
}

constexpr glm::mat4 mat4_swap_xy{0.0f, 1.0f, 0.0f, 0.0f,
                                 1.0f, 0.0f, 0.0f, 0.0f,
                                 0.0f, 0.0f, 1.0f, 0.0f,
                                 0.0f, 0.0f, 0.0f, 1.0f};

constexpr glm::mat4 mat4_swap_xz{0.0f, 0.0f, 1.0f, 0.0f,
                                 0.0f, 1.0f, 0.0f, 0.0f,
                                 1.0f, 0.0f, 0.0f, 0.0f,
                                 0.0f, 0.0f, 0.0f, 1.0f};

constexpr glm::mat4 mat4_swap_yz{1.0f, 0.0f, 0.0f, 0.0f,
                                 0.0f, 0.0f, 1.0f, 0.0f,
                                 0.0f, 1.0f, 0.0f, 0.0f,
                                 0.0f, 0.0f, 0.0f, 1.0f};

constexpr glm::mat4 mat4_rotate_xy_cw{0.0f, -1.0f, 0.0f, 0.0f,
                                      1.0f,  0.0f, 0.0f, 0.0f,
                                      0.0f,  0.0f, 1.0f, 0.0f,
                                      0.0f,  0.0f, 0.0f, 1.0f};

constexpr glm::mat4 mat4_rotate_xz_cw{0.0f, 0.0f, -1.0f, 0.0f,
                                      0.0f, 1.0f,  0.0f, 0.0f,
                                      1.0f, 0.0f,  0.0f, 0.0f,
                                      0.0f, 0.0f,  0.0f, 1.0f};

auto unproject(const glm::mat4 world_from_clip,
               const glm::vec3 window,
               const float     depth_range_near,
               const float     depth_range_far,
               const float     viewport_x,
               const float     viewport_y,
               const float     viewport_width,
               const float     viewport_height)
-> glm::vec3;

auto project_to_screen_space(const glm::mat4 clip_from_world,
                             const glm::vec3 position_in_world,
                             const float     depth_range_near,
                             const float     depth_range_far,
                             const float     viewport_x,
                             const float     viewport_y,
                             const float     viewport_width,
                             const float     viewport_height)
-> glm::vec3;

auto create_frustum(const float left, const float right, const float bottom, const float top, const float z_near, const float z_far)
-> glm::mat4;

auto create_frustum_simple(const float width, const float height, const float z_near, const float z_far)
-> glm::mat4;

auto create_perspective(const float fov_x, const float fov_y, const float z_near, const float z_far)
-> glm::mat4;

auto create_perspective_xr(const float fov_left, const float fov_right, const float fov_up, const float fov_down, const float z_near, const float z_far)
-> glm::mat4;

auto create_perspective_vertical(const float fov_y, const float aspect_ratio, const float z_near, const float z_far)
-> glm::mat4;

auto create_perspective_horizontal(const float fov_x, const float aspect_ratio, const float z_near, const float z_far)
-> glm::mat4;

auto create_projection(const float s, const float p, const float n, const float f, const float w, const float h, const glm::vec3 v, const glm::vec3 e)
-> glm::mat4;

auto create_orthographic(const float left, const float right, const float bottom, const float top, const float z_near, const float z_far)
-> glm::mat4;

auto create_orthographic_centered(const float width, const float height, const float z_near, const float z_far)
-> glm::mat4;

auto create_translation(const glm::vec2 t)
-> glm::mat4;

auto create_translation(const glm::vec3 t)
-> glm::mat4;

auto create_translation(const float x, const float y, const float z)
-> glm::mat4;

auto create_rotation(const float angle_radians, const glm::vec3 axis)
-> glm::mat4;

auto create_scale(const float x, const float y, const float z)
-> glm::mat4;

auto create_scale(const float s)
-> glm::mat4;

auto create_look_at(const glm::vec3 eye, const glm::vec3 center, const glm::vec3 up0)
-> glm::mat4;

auto degrees_to_radians(const float degrees)
-> float;

void hsv_to_rgb(float h, float s, float v, float& r, float& g, float& b);

void rgb_to_hsv(float r, float g, float b, float& h, float& s, float& v);

auto srgb_to_linear(const float cs)
-> float;

auto linear_rgb_to_srgb(const float cl)
-> float;

auto srgb_to_linear_rgb(const glm::vec3 srgb)
-> glm::vec3;

auto linear_rgb_to_srgb(const glm::vec3 linear_rgb)
-> glm::vec3;

void cartesian_to_heading_elevation(const glm::vec3 v, float& out_elevation, float& out_heading);

void cartesian_to_spherical_iso(const glm::vec3 v, float& out_theta, float& out_phi);

auto spherical_to_cartesian_iso(const float theta, const float phi)
-> glm::vec3;

auto closest_points(const glm::vec3 P0, const glm::vec3 P1, const glm::vec3 Q0, const glm::vec3 Q1, glm::vec3& out_PC, glm::vec3& out_QC)
-> bool;

auto closest_points(const glm::vec2 P0, const glm::vec2 P1, const glm::vec2 Q0, const glm::vec2 Q1, glm::vec2& out_PC, glm::vec2& out_QC)
-> bool;

auto closest_point(const glm::vec2 P0, const glm::vec2 P1, const glm::vec2 Q, glm::vec2& out_PC)
-> bool;

auto closest_point(const glm::vec3 P0, const glm::vec3 P1, const glm::vec3 Q, glm::vec3& out_PC)
-> bool;

auto line_point_distance(const glm::vec2 P0, const glm::vec2 P1, const glm::vec2 Q, float& distance)
-> bool;

auto line_point_distance(const glm::vec3 P0, const glm::vec3 P1, const glm::vec3 Q, float& distance)
-> bool;

auto intersect_plane(const glm::vec3 n, const glm::vec3 p0, const glm::vec3 l0, const glm::vec3 l, float& t)
-> bool;

auto project_point_to_plane(const glm::vec3 n, const glm::vec3 p, glm::vec3& in_out_q)
-> bool;

} // namespace erhe::toolkit
