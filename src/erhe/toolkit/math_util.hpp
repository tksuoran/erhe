#ifndef math_util_hpp_erhe_toolkit
#define math_util_hpp_erhe_toolkit

#include <cstdint>
#include <glm/glm.hpp>

namespace erhe::toolkit
{

template <typename T, typename U>
auto round(float num)
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
    float rf = v.x * 255.0f;
    float gf = v.y * 255.0f;
    float bf = v.z * 255.0f;
    unsigned int r = round<unsigned int, float>(rf) << 16u;
    unsigned int g = round<unsigned int, float>(gf) << 8u;
    unsigned int b = round<unsigned int, float>(bf) << 0u;
    unsigned int i = r | g | b;

    return static_cast<uint32_t>(i);
}

inline auto vec3_from_uint(uint32_t i)
-> glm::vec3
{
    uint32_t r = (i >> 16u) & 0xffu;
    uint32_t g = (i >>  8u) & 0xffu;
    uint32_t b = (i >>  0u) & 0xffu;

    return glm::vec3(
        r / 255.0f,
        g / 255.0f,
        b / 255.0f);
}

constexpr glm::vec3 vec3_unit_x{1.0f, 0.0f, 0.0f};
constexpr glm::vec3 vec3_unit_y{0.0f, 1.0f, 0.0f};
constexpr glm::vec3 vec3_unit_z{0.0f, 0.0f, 1.0f};

inline auto max_axis(glm::vec3 v)
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

inline auto min_axis(glm::vec3 v)
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

constexpr glm::mat4 mat4_identity{1.0f, 0.0f, 0.0f, 0.0f,
                                  0.0f, 1.0f, 0.0f, 0.0f,
                                  0.0f, 0.0f, 1.0f, 0.0f,
                                  0.0f, 0.0f, 0.0f, 1.0f};

constexpr glm::mat4 mat4_swap_xy{0.0f, 1.0f, 0.0f, 0.0f,
                                 1.0f, 0.0f, 0.0f, 0.0f,
                                 0.0f, 0.0f, 1.0f, 0.0f,
                                 0.0f, 0.0f, 0.0f, 1.0f};

constexpr glm::mat4 mat4_rotate_xy_cw{0.0f, -1.0f, 0.0f, 0.0f,
                                      1.0f,  0.0f, 0.0f, 0.0f,
                                      0.0f,  0.0f, 1.0f, 0.0f,
                                      0.0f,  0.0f, 0.0f, 1.0f};

constexpr glm::mat4 mat4_rotate_xz_cw{0.0f, 0.0f, -1.0f, 0.0f,
                                      0.0f, 1.0f,  0.0f, 0.0f,
                                      1.0f, 0.0f,  0.0f, 0.0f,
                                      0.0f, 0.0f,  0.0f, 1.0f};

auto unproject(glm::mat4 world_from_clip,
               glm::vec3 window,
               float     depth_range_near,
               float     depth_range_far,
               float     viewport_x,
               float     viewport_y,
               float     viewport_width,
               float     viewport_height)
-> glm::vec3;

auto project_to_screen_space(glm::mat4 clip_from_world,
                             glm::vec3 position_in_world,
                             float     depth_range_near,
                             float     depth_range_far,
                             float     viewport_x,
                             float     viewport_y,
                             float     viewport_width,
                             float     viewport_height)
-> glm::vec3;

auto create_frustum(float left, float right, float bottom, float top, float z_near, float z_far)
-> glm::mat4;

auto create_frustum_simple(float width, float height, float z_near, float z_far)
-> glm::mat4;

auto create_perspective(float fov_x, float fov_y, float z_near, float z_far)
-> glm::mat4;

auto create_perspective_vertical(float fov_y, float aspect_ratio, float z_near, float z_far)
-> glm::mat4;

auto create_perspective_horizontal(float fov_x, float aspect_ratio, float z_near, float z_far)
-> glm::mat4;

auto create_projection(float s, float p, float n, float f, float w, float h, glm::vec3 v, glm::vec3 e)
-> glm::mat4;

auto create_orthographic(float left, float right, float bottom, float top, float z_near, float z_far)
-> glm::mat4;

auto create_orthographic_centered(float width, float height, float z_near, float z_far)
-> glm::mat4;

auto create_translation(glm::vec2 t)
-> glm::mat4;

auto create_translation(glm::vec3 t)
-> glm::mat4;

auto create_translation(float x, float y, float z)
-> glm::mat4;

auto create_rotation(float angle_radians, glm::vec3 axis)
-> glm::mat4;

auto create_scale(float x, float y, float z)
-> glm::mat4;

auto create_scale(float s)
-> glm::mat4;

auto create_look_at(glm::vec3 eye, glm::vec3 center, glm::vec3 up0)
-> glm::mat4;

auto degrees_to_radians(float degrees)
-> float;

void hsv_to_rgb(float h, float s, float v, float& r, float& g, float& b);

void rgb_to_hsv(float r, float g, float b, float& h, float& s, float& v);

auto srgb_to_linear(float cs)
-> float;

auto linear_rgb_to_srgb(float cl)
-> float;

auto srgb_to_linear_rgb(glm::vec3 srgb)
-> glm::vec3;

auto linear_rgb_to_srgb(glm::vec3 linear_rgb)
-> glm::vec3;

void cartesian_to_heading_elevation(glm::vec3 v, float& out_elevation, float& out_heading);

void cartesian_to_spherical_iso(glm::vec3 v, float& out_theta, float& out_phi);

auto spherical_to_cartesian_iso(float theta, float phi)
-> glm::vec3;

auto closest_points(glm::vec3 P0, glm::vec3 P1, glm::vec3 Q0, glm::vec3 Q1, glm::vec3& out_PC, glm::vec3& out_QC)
-> bool;

auto closest_points(glm::vec2 P0, glm::vec2 P1, glm::vec2 Q0, glm::vec2 Q1, glm::vec2& out_PC, glm::vec2& out_QC)
-> bool;

auto closest_point(glm::vec2 P0, glm::vec2 P1, glm::vec2 Q, glm::vec2& out_PC)
-> bool;

auto closest_point(glm::vec3 P0, glm::vec3 P1, glm::vec3 Q, glm::vec3& out_PC)
-> bool;

auto line_point_distance(glm::vec2 P0, glm::vec2 P1, glm::vec2 Q, float& distance)
-> bool;

auto line_point_distance(glm::vec3 P0, glm::vec3 P1, glm::vec3 Q, float& distance)
-> bool;

auto intersect_plane(glm::vec3 n, glm::vec3 p0, glm::vec3 l0, glm::vec3 l, float& t)
-> bool;

auto project_point_to_plane(glm::vec3 n, glm::vec3 p, glm::vec3& q)
-> bool;

} // namespace erhe::toolkit

#endif
                                                    