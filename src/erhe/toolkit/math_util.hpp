#pragma once

#include "erhe/toolkit/optional.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cstdint>
#include <tuple>

namespace erhe::toolkit
{

template <typename T, typename U>
[[nodiscard]]
auto round(const float num) -> T
{
    return
        static_cast<T>((num > U{0.0})
            ? std::floor(num + U{0.5})
            : std::ceil(num - U{0.5}));
}

[[nodiscard]] inline auto next_power_of_two(uint32_t x) -> uint32_t
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

template <typename T>
[[nodiscard]] inline auto uint_from_vector3(const glm::vec3 v) -> uint32_t
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

[[nodiscard]] inline auto vec3_from_uint(const uint32_t i) -> glm::vec3
{
    const uint32_t r = (i >> 16u) & 0xffu;
    const uint32_t g = (i >>  8u) & 0xffu;
    const uint32_t b = (i >>  0u) & 0xffu;

    return glm::vec3{
        r / 255.0f,
        g / 255.0f,
        b / 255.0f
    };
}

constexpr glm::vec3 vec3_unit_x{1.0f, 0.0f, 0.0f};
constexpr glm::vec3 vec3_unit_y{0.0f, 1.0f, 0.0f};
constexpr glm::vec3 vec3_unit_z{0.0f, 0.0f, 1.0f};

[[nodiscard]] inline auto max_axis(const glm::vec3 v) -> glm::vec3
{
    if (
        (std::abs(v.x) >= std::abs(v.y)) &&
        (std::abs(v.x) >= std::abs(v.z))
    )
    {
        return vec3_unit_x;
    }

    if (
        (std::abs(v.y) >= std::abs(v.x)) &&
        (std::abs(v.y) >= std::abs(v.z))
    )
    {
        return vec3_unit_y;
    }
    return vec3_unit_z;
}

[[nodiscard]] inline auto min_axis(const glm::vec3 v) -> glm::vec3
{
    if (
        (std::abs(v.x) <= std::abs(v.y)) &&
        (std::abs(v.x) <= std::abs(v.z))
    )
    {
        return vec3_unit_x;
    }

    if (
        (std::abs(v.y) <= std::abs(v.x)) &&
        (std::abs(v.y) <= std::abs(v.z))
    )
    {
        return vec3_unit_y;
    }

    return vec3_unit_z;
}

[[nodiscard]] inline auto max_axis_index(const glm::vec3 v) -> glm::vec3::length_type
{
    if (
        (std::abs(v.x) >= std::abs(v.y)) &&
        (std::abs(v.x) >= std::abs(v.z))
    )
    {
        return glm::vec3::length_type{0};
    }

    if (
        (std::abs(v.y) >= std::abs(v.x)) &&
        (std::abs(v.y) >= std::abs(v.z))
    )
    {
        return glm::vec3::length_type{1};
    }
    return glm::vec3::length_type{2};
}

[[nodiscard]] inline auto min_axis_index(const glm::vec3 v) -> glm::vec3::length_type
{
    if (
        (std::abs(v.x) <= std::abs(v.y)) &&
        (std::abs(v.x) <= std::abs(v.z))
    )
    {
        return glm::vec3::length_type{0};
    }

    if (
        (std::abs(v.y) <= std::abs(v.x)) &&
        (std::abs(v.y) <= std::abs(v.z))
    )
    {
        return glm::vec3::length_type{1};
    }
    return glm::vec3::length_type{2};
}

constexpr glm::mat4 mat4_swap_xy{
    0.0f, 1.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
};

constexpr glm::mat4 mat4_swap_xz{
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
};

constexpr glm::mat4 mat4_swap_yz{
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
};

constexpr glm::mat4 mat4_rotate_xy_cw{
    0.0f, -1.0f, 0.0f, 0.0f,
    1.0f,  0.0f, 0.0f, 0.0f,
    0.0f,  0.0f, 1.0f, 0.0f,
    0.0f,  0.0f, 0.0f, 1.0f
};

constexpr glm::mat4 mat4_rotate_xz_cw{
    0.0f, 0.0f, -1.0f, 0.0f,
    0.0f, 1.0f,  0.0f, 0.0f,
    1.0f, 0.0f,  0.0f, 0.0f,
    0.0f, 0.0f,  0.0f, 1.0f
};

template <typename T> struct vector_types { };
template <> struct vector_types<float>
{
    using vec2 = glm::vec2;
    using vec3 = glm::vec3;
    using vec4 = glm::vec4;
    using mat2 = glm::mat2;
    using mat3 = glm::mat3;
    using mat4 = glm::mat4;
};

template <> struct vector_types<double>
{
    using vec2 = glm::dvec2;
    using vec3 = glm::dvec3;
    using vec4 = glm::dvec4;
    using mat2 = glm::dmat2;
    using mat3 = glm::dmat3;
    using mat4 = glm::dmat4;
};

template <typename T>
[[nodiscard]] auto unproject(
    const typename vector_types<T>::mat4 world_from_clip,
    const typename vector_types<T>::vec3 window,
    const T                              depth_range_near,
    const T                              depth_range_far,
    const T                              viewport_x,
    const T                              viewport_y,
    const T                              viewport_width,
    const T                              viewport_height
) -> nonstd::optional<typename vector_types<T>::vec3>
{
    using vec3 = typename vector_types<T>::vec3;
    using vec4 = typename vector_types<T>::vec4;

    const T viewport_center_x = viewport_x + viewport_width  * T{0.5};
    const T viewport_center_y = viewport_y + viewport_height * T{0.5};
    const T s                 = depth_range_far - depth_range_near;
    const T b                 = depth_range_near;

    const vec4 ndc{
        (window.x - viewport_center_x) / (viewport_width  * T{0.5}),
        (window.y - viewport_center_y) / (viewport_height * T{0.5}),
        (window.z - b) / s,
        T{1.0}
    };

    const vec4 world_homogeneous = world_from_clip * ndc;
    if (world_homogeneous.w == T{0.0})
    {
        return {};
    }

    return vec3{
        world_homogeneous.x / world_homogeneous.w,
        world_homogeneous.y / world_homogeneous.w,
        world_homogeneous.z / world_homogeneous.w
    };
}

template <typename T>
[[nodiscard]] auto project_to_screen_space(
    const typename vector_types<T>::mat4 clip_from_world,
    const typename vector_types<T>::vec3 position_in_world,
    const T                              depth_range_near,
    const T                              depth_range_far,
    const T                              viewport_x,
    const T                              viewport_y,
    const T                              viewport_width,
    const T                              viewport_height
) -> typename vector_types<T>::vec3
{
    using vec3 = typename vector_types<T>::vec3;
    using vec4 = typename vector_types<T>::vec4;

    // 13.8 Coordinate Transformations
    //
    // Clip control: Zero to one
    //
    // x_window  =  viewport_width  * 0.5 * x_ndc + viewport_left
    // y_window  =  viewport_height * 0.5 * y_ndc + viewport_height
    // z_window  =  (depth_range_far - depth_range_near) * z_ndc + depth_range_near
    //
    // Clip control negative one to one
    //
    // x_window  =  viewport_width  * 0.5 * x_ndc + viewport_left
    // y_window  =  viewport_height * 0.5 * y_ndc + viewport_height
    // z_window  =  (depth_range_far - depth_range_near) * 0.5 * z_ndc + ((depth_range_near + depth_range_far) * 0.5)
    const T viewport_center_x = viewport_x + viewport_width  * T{0.5};
    const T viewport_center_y = viewport_y + viewport_height * T{0.5};
    const T s                 = depth_range_far - depth_range_near;
    const T b                 = depth_range_near;
    //const auto s = clip_negative_one_to_one ? (depth_range_far - depth_range_near) * 0.5f : (depth_range_far - depth_range_near);
    //const auto b = clip_negative_one_to_one ? ((depth_range_near + depth_range_far) * 0.5) : depth_range_near;

    const vec4 clip = clip_from_world * vec4{position_in_world, T{1.0}};

    const vec3 ndc{
        clip.x / clip.w,
        clip.y / clip.w,
        clip.z / clip.w
    };

    return vec3{
        viewport_width  * T{0.5} * ndc.x + viewport_center_x,
        viewport_height * T{0.5} * ndc.y + viewport_center_y,
        s * ndc.z + b
    };
}

[[nodiscard]] auto create_frustum(
    const float left,
    const float right,
    const float bottom,
    const float top,
    const float z_near,
    const float z_far
) -> glm::mat4;

[[nodiscard]] auto create_frustum_simple(
    const float width,
    const float height,
    const float z_near,
    const float z_far
) -> glm::mat4;

[[nodiscard]] auto create_perspective(
    const float fov_x,
    const float fov_y,
    const float z_near,
    const float z_far
) -> glm::mat4;

[[nodiscard]] auto create_perspective_xr(
    const float fov_left,
    const float fov_right,
    const float fov_up,
    const float fov_down,
    const float z_near,
    const float z_far
) -> glm::mat4;

[[nodiscard]] auto create_perspective_vertical(
    const float fov_y,
    const float aspect_ratio,
    const float z_near,
    const float z_far
) -> glm::mat4;

[[nodiscard]] auto create_perspective_horizontal(
    const float fov_x,
    const float aspect_ratio,
    const float z_near,
    const float z_far
) -> glm::mat4;

[[nodiscard]] auto create_projection(
    const float     s,
    const float     p,
    const float     n,
    const float     f,
    const float     w,
    const float     h,
    const glm::vec3 v,
    const glm::vec3 e
) -> glm::mat4;

[[nodiscard]] auto create_orthographic(
    const float left,
    const float right,
    const float bottom,
    const float top,
    const float z_near,
    const float z_far
) -> glm::mat4;

[[nodiscard]] auto create_orthographic_centered(
    const float width,
    const float height,
    const float z_near,
    const float z_far
) -> glm::mat4;

template <typename T>
[[nodiscard]] auto create_translation(
    const typename vector_types<T>::vec2 t
) -> typename vector_types<T>::mat4
{
    return typename vector_types<T>::mat4{
        T{1.0}, T{0.0}, T{0.0}, T{0.0},
        T{0.0}, T{1.0}, T{0.0}, T{0.0},
        T{0.0}, T{0.0}, T{1.0}, T{0.0},
        t.x,    t.y,    T{0.0}, T{1.0}
    };
}

template <typename T>
[[nodiscard]] auto create_translation(
    const typename vector_types<T>::vec3 t
) -> typename vector_types<T>::mat4
{
    return typename vector_types<T>::mat4{
        T{1.0}, T{0.0}, T{0.0}, T{0.0},
        T{0.0}, T{1.0}, T{0.0}, T{0.0},
        T{0.0}, T{0.0}, T{1.0}, T{0.0},
        t.x,    t.y,    t.z,    T{1.0}
    };
}

template <typename T>
[[nodiscard]] auto create_translation(
    const T x,
    const T y,
    const T z
) -> typename vector_types<T>::mat4
{
    return typename vector_types<T>::mat4{
        T{1.0}, T{0.0}, T{0.0}, T{0.0},
        T{0.0}, T{1.0}, T{0.0}, T{0.0},
        T{0.0}, T{0.0}, T{1.0}, T{0.0},
        x,      y,      z,      T{1.0}
    };
}

template <typename T>
[[nodiscard]] auto create_rotation(
    const T                              angle_radians,
    const typename vector_types<T>::vec3 axis
) -> typename vector_types<T>::mat4
{
    const T rsin = std::sin(angle_radians);
    const T rcos = std::cos(angle_radians);

    const T u = axis.x;
    const T v = axis.y;
    const T w = axis.z;

    return typename vector_types<T>::mat4{
             rcos + u * u * (T{1} - rcos), // column 0
         w * rsin + u * v * (T{1} - rcos),
        -v * rsin + u * w * (T{1} - rcos),
        T{0},

        -w * rsin + v * u * (T{1} - rcos), // column 1
             rcos + v * v * (T{1} - rcos),
         u * rsin + v * w * (T{1} - rcos),
        T{0},

         v * rsin + w * u * (T{1} - rcos), // column 2
        -u * rsin + w * v * (T{1} - rcos),
             rcos + w * w * (T{1} - rcos),
        T{0},

        T{0}, // column 3
        T{0},
        T{0},
        T{1}
    };
}

template <typename T>
[[nodiscard]] auto create_scale(
    const T x,
    const T y,
    const T z
) -> typename vector_types<T>::mat4
{
    return typename vector_types<T>::mat4{
        x,      T{0.0}, T{0.0}, T{0.0},
        T{0.0}, y,      T{0.0}, T{0.0},
        T{0.0}, T{0.0}, z,      T{0.0},
        T{0.0}, T{0.0}, T{0.0}, T{1.0}
    };
}

template <typename T>
[[nodiscard]] auto create_scale(
    const typename vector_types<T>::vec3 s
) -> typename vector_types<T>::mat4
{
    return typename vector_types<T>::mat4{
        s.x,    T{0.0}, T{0.0}, T{0.0},
        T{0.0}, s.y,    T{0.0}, T{0.0},
        T{0.0}, T{0.0}, s.z,    T{0.0},
        T{0.0}, T{0.0}, T{0.0}, T{1.0}
    };
}

template <typename T>
[[nodiscard]] auto create_scale(
    const T s
) -> typename vector_types<T>::mat4
{
    return typename vector_types<T>::mat4{
        s,      T{0.0}, T{0.0}, T{0.0},
        T{0.0}, s,      T{0.0}, T{0.0},
        T{0.0}, T{0.0}, s,      T{0.0},
        T{0.0}, T{0.0}, T{0.0}, T{1.0}
    };
}

[[nodiscard]] auto create_look_at(
    const glm::vec3 eye,
    const glm::vec3 center,
    const glm::vec3 up0
) -> glm::mat4;

void hsv_to_rgb(float h, float s, float v, float& r, float& g, float& b);

void rgb_to_hsv(float r, float g, float b, float& h, float& s, float& v);

[[nodiscard]] auto srgb_to_linear(const float cs) -> float;

[[nodiscard]] auto linear_rgb_to_srgb(const float cl) -> float;

[[nodiscard]] auto srgb_to_linear_rgb(const glm::vec3 srgb) -> glm::vec3;

[[nodiscard]] auto linear_rgb_to_srgb(const glm::vec3 linear_rgb) -> glm::vec3;

void cartesian_to_heading_elevation(
    const glm::vec3 v,
    float&          out_elevation,
    float&          out_heading
);

void cartesian_to_spherical_iso(
    const glm::vec3 v,
    float&          out_theta,
    float&          out_phi
);

[[nodiscard]] auto spherical_to_cartesian_iso(
    const float theta,
    const float phi
) -> glm::vec3;

template <typename T>
class Closest_points
{
public:
    typename vector_types<T>::vec3 P;
    typename vector_types<T>::vec3 Q;
};

template <typename T>
[[nodiscard]] auto closest_points(
    const typename vector_types<T>::vec3 P0,
    const typename vector_types<T>::vec3 P1,
    const typename vector_types<T>::vec3 Q0,
    const typename vector_types<T>::vec3 Q1
) -> nonstd::optional<
    Closest_points<T>
>
{
    using vec3 = typename vector_types<T>::vec3;

    const vec3 u  = P1 - P0;
    const vec3 v  = Q1 - Q0;
    const vec3 w0 = P0 - Q0;
    const auto a  = glm::dot(u, u);
    const auto b  = glm::dot(u, v);
    const auto c  = glm::dot(v, v);
    const auto d  = glm::dot(u, w0);
    const auto e  = glm::dot(v, w0);
    const auto denominator = (a * c) - (b * b);

    if (denominator < std::numeric_limits<T>::epsilon())
    {
        return {};
    }

    const auto sC = ((b * e) - (c * d)) / denominator;
    const auto tC = ((a * e) - (b * d)) / denominator;

    return Closest_points<T>{
        .P = P0 + sC * u,
        .Q = Q0 + tC * v
    };
}

template <typename T>
[[nodiscard]] auto closest_point(
    const typename vector_types<T>::vec2 P0,
    const typename vector_types<T>::vec2 P1,
    const typename vector_types<T>::vec2 Q
) -> nonstd::optional<typename vector_types<T>::vec2>
{
    const auto u = P1 - P0;
    if (glm::dot(u, u) < std::numeric_limits<T>::epsilon())
    {
        return {};
    }
    const auto t = glm::dot(u, Q - P0) / dot(u, u);
    return P0 + t * u;
}

template <typename T>
[[nodiscard]] auto closest_point(
    const typename vector_types<T>::vec3 P0,
    const typename vector_types<T>::vec3 P1,
    const typename vector_types<T>::vec3 Q
) -> nonstd::optional<typename vector_types<T>::vec3>
{
    const auto u = P1 - P0;
    if (dot(u, u) < std::numeric_limits<T>::epsilon())
    {
        return {};
    }
    const auto t = glm::dot(u, Q - P0) / dot(u, u);
    return {
        P0 + t * u
    };
}

template <typename T>
[[nodiscard]] auto line_point_distance(
    const typename vector_types<T>::vec2 P0,
    const typename vector_types<T>::vec2 P1,
    const typename vector_types<T>::vec2 Q
) -> nonstd::optional<T>
{
    typename vector_types<T>::vec2 PC;
    if (!closest_point<T>(P0, P1, Q, PC))
    {
        return {};
    }
    return glm::distance(Q, PC);
}

template <typename T>
[[nodiscard]] auto line_point_distance(
    const typename vector_types<T>::vec3 P0,
    const typename vector_types<T>::vec3 P1,
    const typename vector_types<T>::vec3 Q
) -> nonstd::optional<T>
{
    typename vector_types<T>::vec3 PC;
    if (!closest_point<T>(P0, P1, Q, PC))
    {
        return {};
    }
    //distance = glm::distance(Q, PC);
    return glm::distance(Q, PC);
}

template <typename T>
[[nodiscard]] auto intersect_plane(
    const typename vector_types<T>::vec3 plane_normal,
    const typename vector_types<T>::vec3 point_on_plane,
    const typename vector_types<T>::vec3 ray_origin,
    const typename vector_types<T>::vec3 ray_direction
) -> nonstd::optional<T>
{
    const T denominator = glm::dot(plane_normal, ray_direction);
    if (std::abs(denominator) < std::numeric_limits<T>::epsilon())
    {
        return {};
    }
    return glm::dot(point_on_plane - ray_origin, plane_normal) / denominator;
}

template <typename T>
[[nodiscard]] auto project_point_to_plane(
    const typename vector_types<T>::vec3 plane_normal,
    const typename vector_types<T>::vec3 point_on_plane,
    typename vector_types<T>::vec3       point_to_project
) -> nonstd::optional<typename vector_types<T>::vec3>
{
    const auto n = plane_normal;
    const auto p = point_on_plane;
    const auto q = point_to_project;
    const T nominator   = dot(n, q - p);
    const T denominator = dot(n, n);
    if (std::abs(denominator) < std::numeric_limits<T>::epsilon())
    {
        return {};
    }
    const T                              t            = nominator / denominator;
    const typename vector_types<T>::vec3 Pn_v         = t * n;
    const typename vector_types<T>::vec3 projected_q1 = q - Pn_v;

    return projected_q1;
}

// Returns angle of rotation in radians for point q from reference_direction
template <typename T>
[[nodiscard]] auto angle_of_rotation(
    const typename vector_types<T>::vec3 q,
    const typename vector_types<T>::vec3 axis_for_rotation,
    const typename vector_types<T>::vec3 reference_direction
) -> T
{
    using vec3 = typename vector_types<T>::vec3;

    const vec3 q_      = normalize(q);
    const vec3 r_      = normalize(reference_direction);
    const vec3 m       = normalize(cross(q_, r_));
    const T    n_dot_m = dot(axis_for_rotation, m);
    const T    q_dot_r = glm::clamp(dot(q_, r_), T{-1.0}, T{1.0});
    const T    angle   = (n_dot_m < 0) ? std::acos(q_dot_r) : -std::acos(q_dot_r);
    return angle;
}

} // namespace erhe::toolkit
