#pragma once

#include "erhe_profile/profile.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace erhe::math
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

template <typename T> struct vector_types { };
template <> struct vector_types<float>
{
    using vec2 = glm::vec2;
    using vec3 = glm::vec3;
    using vec4 = glm::vec4;
    using mat2 = glm::mat2;
    using mat3 = glm::mat3;
    using mat4 = glm::mat4;
    static constexpr auto vec3_unit_x () -> glm::vec3              { return glm::vec3{1.0f, 0.0f, 0.0f}; }
    static constexpr auto vec3_unit_y () -> glm::vec3              { return glm::vec3{0.0f, 1.0f, 0.0f}; }
    static constexpr auto vec3_unit_z () -> glm::vec3              { return glm::vec3{0.0f, 0.0f, 1.0f}; }
    static constexpr auto vec3_index_x() -> glm::vec3::length_type { return glm::vec3::length_type{0}; }
    static constexpr auto vec3_index_y() -> glm::vec3::length_type { return glm::vec3::length_type{1}; }
    static constexpr auto vec3_index_z() -> glm::vec3::length_type { return glm::vec3::length_type{2}; }
};

template <> struct vector_types<double>
{
    using vec2 = glm::dvec2;
    using vec3 = glm::dvec3;
    using vec4 = glm::dvec4;
    using mat2 = glm::dmat2;
    using mat3 = glm::dmat3;
    using mat4 = glm::dmat4;
    static constexpr auto vec3_unit_x () -> glm::dvec3              { return glm::dvec3{1.0, 0.0, 0.0}; }
    static constexpr auto vec3_unit_y () -> glm::dvec3              { return glm::dvec3{0.0, 1.0, 0.0}; }
    static constexpr auto vec3_unit_z () -> glm::dvec3              { return glm::dvec3{0.0, 0.0, 1.0}; }
    static constexpr auto vec3_index_x() -> glm::dvec3::length_type { return glm::dvec3::length_type{0}; }
    static constexpr auto vec3_index_y() -> glm::dvec3::length_type { return glm::dvec3::length_type{1}; }
    static constexpr auto vec3_index_z() -> glm::dvec3::length_type { return glm::dvec3::length_type{2}; }
};

template <typename T>
[[nodiscard]] inline auto max_axis(
    const typename vector_types<T>::vec3 v
) -> typename vector_types<T>::vec3
{
    if (
        (std::abs(v.x) >= std::abs(v.y)) &&
        (std::abs(v.x) >= std::abs(v.z))
    ) {
        return vector_types<T>::vec3_unit_x();
    }

    if (
        (std::abs(v.y) >= std::abs(v.x)) &&
        (std::abs(v.y) >= std::abs(v.z))
    ) {
        return vector_types<T>::vec3_unit_y();
    }
    return vector_types<T>::vec3_unit_z();
}

template <typename T>
[[nodiscard]] auto min_axis(
    const typename vector_types<T>::vec3 v
) -> typename vector_types<T>::vec3
{
    if (
        (std::abs(v.x) <= std::abs(v.y)) &&
        (std::abs(v.x) <= std::abs(v.z))
    ) {
        return vector_types<T>::vec3_unit_x();
    }

    if (
        (std::abs(v.y) <= std::abs(v.x)) &&
        (std::abs(v.y) <= std::abs(v.z))
    ) {
        return vector_types<T>::vec3_unit_y();
    }

    return vector_types<T>::vec3_unit_z();
}

template <typename T>
[[nodiscard]] inline auto max_axis_index(
    const typename vector_types<T>::vec3 v
) -> typename vector_types<T>::vec3::length_type
{
    if (
        (std::abs(v.x) >= std::abs(v.y)) &&
        (std::abs(v.x) >= std::abs(v.z))
    ) {
        return vector_types<T>::vec3_index_x();
    }

    if (
        (std::abs(v.y) >= std::abs(v.x)) &&
        (std::abs(v.y) >= std::abs(v.z))
    ) {
        return vector_types<T>::vec3_index_y();
    }
    return vector_types<T>::vec3_index_z();
}

template <typename T>
[[nodiscard]] inline auto min_axis_index(
    const typename vector_types<T>::vec3 v
) -> typename vector_types<T>::vec3::length_type
{
    if (
        (std::abs(v.x) <= std::abs(v.y)) &&
        (std::abs(v.x) <= std::abs(v.z))
    ) {
        return vector_types<T>::vec3_index_x();
    }

    if (
        (std::abs(v.y) <= std::abs(v.x)) &&
        (std::abs(v.y) <= std::abs(v.z))
    ) {
        return vector_types<T>::vec3_index_y();
    }
    return vector_types<T>::vec3_index_z();
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

constexpr glm::mat4 mat4_rotate_xz_180{
    -1.0f, 0.0f,  0.0f, 0.0f,
     0.0f, 1.0f,  0.0f, 0.0f,
     0.0f, 0.0f, -1.0f, 0.0f,
     0.0f, 0.0f,  0.0f, 1.0f
};

constexpr glm::mat4 mat4_rotate_xy_180{
    -1.0f,  0.0f, 0.0f, 0.0f,
     0.0f, -1.0f, 0.0f, 0.0f,
     0.0f,  0.0f, 1.0f, 0.0f,
     0.0f,  0.0f, 0.0f, 1.0f
};

constexpr glm::mat4 mat4_rotate_yz_180{
    1.0f,  0.0f,  0.0f, 0.0f,
    0.0f, -1.0f,  0.0f, 0.0f,
    0.0f,  0.0f, -1.0f, 0.0f,
    0.0f,  0.0f,  0.0f, 1.0f
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
) -> std::optional<typename vector_types<T>::vec3>
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
    if (world_homogeneous.w == T{0.0}) {
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

template <typename T>
[[nodiscard]] auto project_to_screen_space_2d(
    const typename vector_types<T>::mat4 clip_from_world,
    const typename vector_types<T>::vec3 position_in_world,
    const T                              viewport_width,
    const T                              viewport_height
) -> typename vector_types<T>::vec2
{
    using vec2 = typename vector_types<T>::vec2;
    using vec4 = typename vector_types<T>::vec4;

    const T    viewport_center_x = viewport_width  * T{0.5};
    const T    viewport_center_y = viewport_height * T{0.5};
    const vec4 clip = clip_from_world * vec4{position_in_world, T{1.0}};

    const vec2 ndc{
        clip.x / clip.w,
        clip.y / clip.w
    };

    return vec2{
        viewport_width  * T{0.5} * ndc.x + viewport_center_x,
        viewport_height * T{0.5} * ndc.y + viewport_center_y,
    };
}

[[nodiscard]] auto create_frustum(
    float left,
    float right,
    float bottom,
    float top,
    float z_near,
    float z_far
) -> glm::mat4;

[[nodiscard]] auto create_frustum_simple(
    float width,
    float height,
    float z_near,
    float z_far
) -> glm::mat4;

[[nodiscard]] auto create_perspective(
    float fov_x,
    float fov_y,
    float z_near,
    float z_far
) -> glm::mat4;

[[nodiscard]] auto create_perspective_xr(
    float fov_left,
    float fov_right,
    float fov_up,
    float fov_down,
    float z_near,
    float z_far
) -> glm::mat4;

[[nodiscard]] auto create_perspective_vertical(
    float fov_y,
    float aspect_ratio,
    float z_near,
    float z_far
) -> glm::mat4;

[[nodiscard]] auto create_perspective_horizontal(
    float fov_x,
    float aspect_ratio,
    float z_near,
    float z_far
) -> glm::mat4;

[[nodiscard]] auto create_projection(
    float     s,
    float     p,
    float     n,
    float     f,
    float     w,
    float     h,
    glm::vec3 v,
    glm::vec3 e
) -> glm::mat4;

[[nodiscard]] auto create_orthographic(
    float left,
    float right,
    float bottom,
    float top,
    float z_near,
    float z_far
) -> glm::mat4;

[[nodiscard]] auto create_orthographic_centered(
    float width,
    float height,
    float z_near,
    float z_far
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

[[nodiscard]] auto srgb_to_linear(float cs) -> float;

[[nodiscard]] auto linear_rgb_to_srgb(float cl) -> float;

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
    float theta,
    float phi
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
    const typename vector_types<T>::vec3& P0,
    const typename vector_types<T>::vec3& P1,
    const typename vector_types<T>::vec3& Q0,
    const typename vector_types<T>::vec3& Q1
) -> std::optional<
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

    if (denominator < std::numeric_limits<T>::epsilon()) {
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
    const typename vector_types<T>::vec2& P0,
    const typename vector_types<T>::vec2& P1,
    const typename vector_types<T>::vec2& Q
) -> std::optional<typename vector_types<T>::vec2>
{
    const auto u = P1 - P0;
    if (glm::dot(u, u) < std::numeric_limits<T>::epsilon()) {
        return {};
    }
    const auto t = glm::dot(u, Q - P0) / dot(u, u);
    return P0 + t * u;
}

template <typename T>
[[nodiscard]] auto closest_point(
    const typename vector_types<T>::vec3& P0,
    const typename vector_types<T>::vec3& P1,
    const typename vector_types<T>::vec3& Q
) -> std::optional<typename vector_types<T>::vec3>
{
    const auto u = P1 - P0;
    if (dot(u, u) < std::numeric_limits<T>::epsilon()) {
        return {};
    }
    const auto t = glm::dot(u, Q - P0) / dot(u, u);
    return {
        P0 + t * u
    };
}

template <typename T>
[[nodiscard]] auto line_point_distance(
    const typename vector_types<T>::vec2& P0,
    const typename vector_types<T>::vec2& P1,
    const typename vector_types<T>::vec2& Q
) -> std::optional<T>
{
    typename vector_types<T>::vec2 PC;
    if (!closest_point<T>(P0, P1, Q, PC)) {
        return {};
    }
    return glm::distance(Q, PC);
}

template <typename T>
[[nodiscard]] auto line_point_distance(
    const typename vector_types<T>::vec3& P0,
    const typename vector_types<T>::vec3& P1,
    const typename vector_types<T>::vec3& Q
) -> std::optional<T>
{
    typename vector_types<T>::vec3 PC;
    if (!closest_point<T>(P0, P1, Q, PC)) {
        return {};
    }
    //distance = glm::distance(Q, PC);
    return glm::distance(Q, PC);
}

template <typename T>
[[nodiscard]] auto intersect_plane(
    const typename vector_types<T>::vec3& plane_normal,
    const typename vector_types<T>::vec3& point_on_plane,
    const typename vector_types<T>::vec3& ray_origin,
    const typename vector_types<T>::vec3& ray_direction
) -> std::optional<T>
{
    const T denominator = glm::dot(plane_normal, ray_direction);
    if (std::abs(denominator) < std::numeric_limits<T>::epsilon()) {
        return {};
    }
    return glm::dot(point_on_plane - ray_origin, plane_normal) / denominator;
}

template <typename T>
[[nodiscard]] auto project_point_to_plane(
    const typename vector_types<T>::vec3& plane_normal,
    const typename vector_types<T>::vec3& point_on_plane,
    typename vector_types<T>::vec3&       point_to_project
) -> std::optional<typename vector_types<T>::vec3>
{
    const auto n = plane_normal;
    const auto p = point_on_plane;
    const auto q = point_to_project;
    const T nominator   = dot(n, q - p);
    const T denominator = dot(n, n);
    if (std::abs(denominator) < std::numeric_limits<T>::epsilon()) {
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
    const typename vector_types<T>::vec3& q,
    const typename vector_types<T>::vec3& axis_for_rotation,
    const typename vector_types<T>::vec3& reference_direction
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

template <typename T>
auto safe_normalize_cross(
    const typename vector_types<T>::vec3& lhs,
    const typename vector_types<T>::vec3& rhs
) -> typename vector_types<T>::vec3
{
    const T d = glm::dot(lhs, rhs);
    if (std::abs(d) > T{0.999}) {
        return min_axis<T>(lhs);
    }

    const auto c0 = glm::cross(lhs, rhs);
    if (glm::length(c0) < glm::epsilon<T>())
    {
        return min_axis<T>(lhs);
    }
    return glm::normalize(c0);
}

class Bounding_box
{
public:
    [[nodiscard]] auto diagonal() const
    {
        return max - min;
    }
    [[nodiscard]] auto center() const
    {
        return (max + min) * 0.5f;
    }
    [[nodiscard]] auto volume() const
    {
        const auto d = diagonal();
        return d.x * d.y * d.z;
    }

    glm::vec3 min{std::numeric_limits<float>::max()}; // bounding box
    glm::vec3 max{std::numeric_limits<float>::lowest()};

};

class Bounding_sphere
{
public:
    [[nodiscard]] auto volume() const
    {
        return (4.0f / 3.0f) * glm::pi<float>() * radius * radius * radius;
    }

    glm::vec3 center{0.0f};
    float     radius{0.0f};
};

[[nodiscard]] auto transform(
    const glm::mat4&       m,
    const Bounding_sphere& sphere
) -> Bounding_sphere;

class Bounding_volume_source
{
public:
    virtual auto get_element_count      () const -> std::size_t = 0;
    virtual auto get_element_point_count(std::size_t element_index) const -> std::size_t = 0;
    virtual auto get_point              (std::size_t element_index, std::size_t point_index) const -> std::optional<glm::vec3> = 0;
};

class Point_vector_bounding_volume_source : public Bounding_volume_source
{
public:
    Point_vector_bounding_volume_source();
    explicit Point_vector_bounding_volume_source(std::size_t capacity);

    void add                    (float x, float y, float z);
    void add                    (glm::vec3 point);
    auto get_element_count      () const -> std::size_t override;
    auto get_element_point_count(std::size_t element_index) const -> std::size_t override;
    auto get_point              (std::size_t element_index, std::size_t point_index) const -> std::optional<glm::vec3> override;

private:
    std::vector<glm::vec3> m_points;
};

void calculate_bounding_volume(
    const Bounding_volume_source& source,
    Bounding_box&                 bounding_box,
    Bounding_sphere&              bounding_sphere
);

class Bounding_volume_combiner
    : public erhe::math::Bounding_volume_source
{
public:
    Bounding_volume_combiner()
    {
        // https://polyhedr.com/mathematical-properties-of-the-platonic-solids.html
        // Radius

        // To find the radius of the circumscribed sphere of an icosahedron
        // (a regular polyhedron with 20 equilateral triangular faces),
        // we can use the relationship between the inradius and the circumradius.

        // The inradius (r) of an icosahedron is related to the edge length (a) of
        // its triangular faces by the formula:

        // r = a * √(10 + 2 * √5) / (4 * √3)

        // Now, to find the radius of the circumscribed sphere (R), we can use the relationship:

        // R = (2 * r) / √3

        const float r     =  1.0f; // inscribed sphere radius
        const float R     = (2.0f * r) / std::sqrt(3.0f);
        const float phi   = (1.0f + std::sqrt(5.0f)) / 2.0f; // golden ratio
        const float denom = std::sqrt(phi * sqrt(5.0f));
        const float k     = R / denom;
        const float k_phi = k * phi;

        const glm::vec3 p0 { 0.0f,    k,      k_phi };
        const glm::vec3 p1 { 0.0f,    k,     -k_phi };
        const glm::vec3 p2 { 0.0f,   -k,      k_phi };
        const glm::vec3 p3 { 0.0f,   -k,     -k_phi };
        const glm::vec3 p4 {  k,      k_phi,  0.0f  };
        const glm::vec3 p5 {  k,     -k_phi,  0.0f  };
        const glm::vec3 p6 { -k,      k_phi,  0.0f  };
        const glm::vec3 p7 { -k,     -k_phi,  0.0f  };
        const glm::vec3 p8 {  k_phi,  0.0f,   k     };
        const glm::vec3 p9 { -k_phi,  0.0f,   k     };
        const glm::vec3 p10{  k_phi,  0.0f,  -k     };
        const glm::vec3 p11{ -k_phi,  0.0f,  -k     };

        const float r0  = glm::length(p0 );
        const float r1  = glm::length(p1 );
        const float r2  = glm::length(p2 );
        const float r3  = glm::length(p3 );
        const float r4  = glm::length(p4 );
        const float r5  = glm::length(p5 );
        const float r6  = glm::length(p6 );
        const float r7  = glm::length(p7 );
        const float r8  = glm::length(p8 );
        const float r9  = glm::length(p9 );
        const float r10 = glm::length(p10);
        const float r11 = glm::length(p11);

        static_cast<void>(r0 );
        static_cast<void>(r1 );
        static_cast<void>(r2 );
        static_cast<void>(r3 );
        static_cast<void>(r4 );
        static_cast<void>(r5 );
        static_cast<void>(r6 );
        static_cast<void>(r7 );
        static_cast<void>(r8 );
        static_cast<void>(r9 );
        static_cast<void>(r10);
        static_cast<void>(r11);
    }

    void add_point(const glm::mat4& transform, const glm::vec3& point)
    {
        m_offsets.push_back(m_points.size());
        m_sizes.push_back(1);
        m_points.push_back(glm::vec3{transform * glm::vec4{point, 1.0f}});
    }

    void add_box(const glm::mat4& transform, const glm::vec3& min_corner, const glm::vec3& max_corner)
    {
        const glm::vec3& a = min_corner;
        const glm::vec3& b = max_corner;
        m_offsets.push_back(m_points.size());
        m_sizes.push_back(8);
        m_points.push_back(glm::vec3{transform * glm::vec4{a.x, a.y, a.z, 1.0f}});
        m_points.push_back(glm::vec3{transform * glm::vec4{a.x, a.y, b.z, 1.0f}});
        m_points.push_back(glm::vec3{transform * glm::vec4{a.x, b.y, a.z, 1.0f}});
        m_points.push_back(glm::vec3{transform * glm::vec4{a.x, b.y, b.z, 1.0f}});
        m_points.push_back(glm::vec3{transform * glm::vec4{b.x, a.y, a.z, 1.0f}});
        m_points.push_back(glm::vec3{transform * glm::vec4{b.x, a.y, b.z, 1.0f}});
        m_points.push_back(glm::vec3{transform * glm::vec4{b.x, b.y, a.z, 1.0f}});
        m_points.push_back(glm::vec3{transform * glm::vec4{b.x, b.y, b.z, 1.0f}});
    }

    void add_sphere(const glm::mat4& transform, const glm::vec3& center, const float radius)
    {
        const glm::vec3 o = center;
        const float     r = radius;
        const float     k = std::sqrt(3.0f) / 3.0f;
        const float     c = k * r;
        const glm::vec3 a = o - glm::vec3{c, c, c};
        const glm::vec3 b = o + glm::vec3{c, c, c};
        m_offsets.push_back(m_points.size());
        m_sizes.push_back(14);
        m_points.push_back(glm::vec3{transform * glm::vec4{o.x,     o.y,     o.z - r, 1.0f}});
        m_points.push_back(glm::vec3{transform * glm::vec4{o.x,     o.y,     o.z + r, 1.0f}});
        m_points.push_back(glm::vec3{transform * glm::vec4{o.x,     o.y - r, o.z    , 1.0f}});
        m_points.push_back(glm::vec3{transform * glm::vec4{o.x,     o.y + r, o.z    , 1.0f}});
        m_points.push_back(glm::vec3{transform * glm::vec4{o.x - r, o.y,     o.z    , 1.0f}});
        m_points.push_back(glm::vec3{transform * glm::vec4{o.x + r, o.y,     o.z    , 1.0f}});
        m_points.push_back(glm::vec3{transform * glm::vec4{a.x,     a.y,     a.z    , 1.0f}});
        m_points.push_back(glm::vec3{transform * glm::vec4{a.x,     a.y,     b.z    , 1.0f}});
        m_points.push_back(glm::vec3{transform * glm::vec4{a.x,     b.y,     a.z    , 1.0f}});
        m_points.push_back(glm::vec3{transform * glm::vec4{a.x,     b.y,     b.z    , 1.0f}});
        m_points.push_back(glm::vec3{transform * glm::vec4{b.x,     a.y,     a.z    , 1.0f}});
        m_points.push_back(glm::vec3{transform * glm::vec4{b.x,     a.y,     b.z    , 1.0f}});
        m_points.push_back(glm::vec3{transform * glm::vec4{b.x,     b.y,     a.z    , 1.0f}});
        m_points.push_back(glm::vec3{transform * glm::vec4{b.x,     b.y,     b.z    , 1.0f}});
    }

    // Implements Bounding_volume_source
    auto get_element_count() const -> std::size_t override
    {
        return m_sizes.size();
    }

    auto get_element_point_count(const std::size_t element_index) const -> std::size_t override
    {
        return m_sizes.at(element_index);
    }

    auto get_point(
        const std::size_t element_index,
        const std::size_t point_index
    ) const -> std::optional<glm::vec3> override
    {
        return m_points.at(m_offsets.at(element_index) + point_index);
    }

private:
    std::vector<std::size_t> m_sizes;
    std::vector<std::size_t> m_offsets;
    std::vector<glm::vec3>   m_points;
};

static inline auto saturate(float value) -> float
{
    return (value < 0.0f)
        ? 0.0f
        : (value > 1.0f)
            ? 1.0f
            : value;
}

static inline auto float_to_int8_saturate(float value) -> uint32_t
{
    return static_cast<uint32_t>(saturate(value) * 255.0f + 0.5f);
}

static inline auto convert_float4_to_uint32(const glm::vec4& in) -> uint32_t
{
    return
        (float_to_int8_saturate(in.x)      ) |
        (float_to_int8_saturate(in.y) <<  8) |
        (float_to_int8_saturate(in.z) << 16) |
        (float_to_int8_saturate(in.w) << 24);
}

static inline auto convert_float4_to_uint32(const glm::vec3& in) -> uint32_t
{
    return
        (float_to_int8_saturate(in.x)      ) |
        (float_to_int8_saturate(in.y) <<  8) |
        (float_to_int8_saturate(in.z) << 16) |
        0xff000000u;
}

static inline auto compute_cofactor(glm::mat4 const& m) -> glm::mat4
{
    ERHE_PROFILE_FUNCTION();

    using namespace glm;

    float coef_00 = m[2][2] * m[3][3] - m[3][2] * m[2][3];
    float coef_02 = m[1][2] * m[3][3] - m[3][2] * m[1][3];
    float coef_03 = m[1][2] * m[2][3] - m[2][2] * m[1][3];

    float coef_04 = m[2][1] * m[3][3] - m[3][1] * m[2][3];
    float coef_06 = m[1][1] * m[3][3] - m[3][1] * m[1][3];
    float coef_07 = m[1][1] * m[2][3] - m[2][1] * m[1][3];

    float coef_08 = m[2][1] * m[3][2] - m[3][1] * m[2][2];
    float coef_10 = m[1][1] * m[3][2] - m[3][1] * m[1][2];
    float coef_11 = m[1][1] * m[2][2] - m[2][1] * m[1][2];

    float coef_12 = m[2][0] * m[3][3] - m[3][0] * m[2][3];
    float coef_14 = m[1][0] * m[3][3] - m[3][0] * m[1][3];
    float coef_15 = m[1][0] * m[2][3] - m[2][0] * m[1][3];

    float coef_16 = m[2][0] * m[3][2] - m[3][0] * m[2][2];
    float coef_18 = m[1][0] * m[3][2] - m[3][0] * m[1][2];
    float coef_19 = m[1][0] * m[2][2] - m[2][0] * m[1][2];

    float coef_20 = m[2][0] * m[3][1] - m[3][0] * m[2][1];
    float coef_22 = m[1][0] * m[3][1] - m[3][0] * m[1][1];
    float coef_23 = m[1][0] * m[2][1] - m[2][0] * m[1][1];

    vec4 fac_0(coef_00, coef_00, coef_02, coef_03);
    vec4 fac_1(coef_04, coef_04, coef_06, coef_07);
    vec4 fac_2(coef_08, coef_08, coef_10, coef_11);
    vec4 fac_3(coef_12, coef_12, coef_14, coef_15);
    vec4 fac_4(coef_16, coef_16, coef_18, coef_19);
    vec4 fac_5(coef_20, coef_20, coef_22, coef_23);

    vec4 vec_0(m[1][0], m[0][0], m[0][0], m[0][0]);
    vec4 vec_1(m[1][1], m[0][1], m[0][1], m[0][1]);
    vec4 vec_2(m[1][2], m[0][2], m[0][2], m[0][2]);
    vec4 vec_3(m[1][3], m[0][3], m[0][3], m[0][3]);

    vec4 inv_0(vec_1 * fac_0 - vec_2 * fac_1 + vec_3 * fac_2);
    vec4 inv_1(vec_0 * fac_0 - vec_2 * fac_3 + vec_3 * fac_4);
    vec4 inv_2(vec_0 * fac_1 - vec_1 * fac_3 + vec_3 * fac_5);
    vec4 inv_3(vec_0 * fac_2 - vec_1 * fac_4 + vec_2 * fac_5);

    vec4 sign_A(+1, -1, +1, -1);
    vec4 sign_B(-1, +1, -1, +1);
    mat4 inverse(inv_0 * sign_A, inv_1 * sign_B, inv_2 * sign_A, inv_3 * sign_B);

    return transpose(inverse);
}

[[nodiscard]] auto compose(
    glm::vec3 scale,
    glm::quat rotation,
    glm::vec3 translation,
    glm::vec3 skew,
    glm::vec4 perspective
) -> glm::mat4;

[[nodiscard]] auto compose(
    glm::vec3 scale,
    glm::quat rotation,
    glm::vec3 translation,
    glm::vec3 skew
) -> glm::mat4;

[[nodiscard]] auto compose_inverse(
    glm::vec3 scale,
    glm::quat rotation,
    glm::vec3 translation,
    glm::vec3 skew
) -> glm::mat4;

}
