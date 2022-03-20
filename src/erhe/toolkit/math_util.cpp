#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/verify.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <stdexcept>

namespace erhe::toolkit
{

using glm::mat4;
using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::dmat4;
using glm::dvec2;
using glm::dvec3;
using glm::dvec4;

namespace
{
    constexpr float epsilon         {1e-6f};
    constexpr float pi_minus_epsilon{glm::pi<float>() - epsilon};
}

auto create_frustum(
    const float left,
    const float right,
    const float bottom,
    const float top,
    const float z_near,
    const float z_far
) -> mat4
{
    const float x =  (2.0f  * z_near        ) / (right  - left  );
    const float y =  (2.0f  * z_near        ) / (top    - bottom);
    const float a =  (right + left          ) / (right  - left  );
    const float b =  (top   + bottom        ) / (top    - bottom);
  //const float c = -(z_far + z_near        ) / (z_far  - z_near); -- negative one to one
    const float c =   z_far                   / (z_near - z_far ); // zero to one
  //const float d = -(2.0f  * z_far * z_near) / (z_far  - z_near); -- negative one to one
    const float d = -(z_far * z_near        ) / (z_far  - z_near); // zero to one

    return mat4{
        x, 0, 0, 0,
        0, y, 0, 0,
        a, b, c, -1.0f,
        0, 0, d, 0,
    };
}

auto create_frustum_infinite_far(
    const float left,
    const float right,
    const float bottom,
    const float top,
    const float z_near
) -> mat4
{
    assert(z_near >= -0.0f);
    const float x =  (2.0f  * z_near) / (right - left  );
    const float y =  (2.0f  * z_near) / (top   - bottom);
    const float a =  (right + left  ) / (right - left  );
    const float b =  (top   + bottom) / (top   - bottom);
  //const float c = -1.0f;           -- negative one to one
    const float c = -1.0f;           // zero to one
  //const float d = -2.0f * z_near;  -- negative one to one
    const float d = -z_near;         // zero to one

    return mat4{
        x, 0, 0, 0,
        0, y, 0, 0,
        a, b, c, -1.0f,
        0, 0, d, 0
    };
}

auto create_frustum_simple(
    const float width,
    const float height,
    const float z_near,
    const float z_far
) -> mat4
{
    return create_frustum(-width / 2.0f, width / 2.0f, -height / 2.0f, height / 2.0f, z_near, z_far);
}

auto create_perspective(
    const float fov_x,
    const float fov_y,
    const float z_near,
    const float z_far
) -> mat4
{
    const auto fov_y_clamped    = std::min(std::max(fov_y, epsilon), pi_minus_epsilon);
    const auto fov_x_clamped    = std::min(std::max(fov_x, epsilon), pi_minus_epsilon);
    const auto tan_x_half_angle = std::tan(fov_x_clamped * 0.5f);
    const auto tan_y_half_angle = std::tan(fov_y_clamped * 0.5f);
    const auto width            = 2.0f * z_near * tan_x_half_angle;
    const auto height           = 2.0f * z_near * tan_y_half_angle;
    return create_frustum_simple(width, height, z_near, z_far);
}

auto create_perspective_xr(
    const float fov_left,
    const float fov_right,
    const float fov_up,
    const float fov_down,
    const float z_near,
    const float z_far
) -> glm::mat4
{
    const auto fov_left_clamped  = std::min(std::max(fov_left,  -pi_minus_epsilon), pi_minus_epsilon);
    const auto fov_right_clamped = std::min(std::max(fov_right, -pi_minus_epsilon), pi_minus_epsilon);
    const auto fov_up_clamped    = std::min(std::max(fov_up,    -pi_minus_epsilon), pi_minus_epsilon);
    const auto fov_down_clamped  = std::min(std::max(fov_down,  -pi_minus_epsilon), pi_minus_epsilon);
    const auto tan_left          = std::tan(fov_left_clamped );
    const auto tan_right         = std::tan(fov_right_clamped);
    const auto tan_up            = std::tan(fov_up_clamped   );
    const auto tan_down          = std::tan(fov_down_clamped );
    const auto left              = z_near * tan_left;
    const auto right             = z_near * tan_right;
    const auto up                = z_near * tan_up;
    const auto down              = z_near * tan_down;
    return create_frustum(left, right, down, up, z_near, z_far);
}

auto create_perspective_vertical(
    const float fov_y,
    const float aspect_ratio,
    const float z_near,
    const float z_far
) -> mat4
{
    const float fov_y_clamped = std::min(std::max(fov_y, epsilon), pi_minus_epsilon);
    return glm::perspective(fov_y_clamped, aspect_ratio, z_near, z_far);
}

auto create_perspective_horizontal(
    const float fov_x,
    const float aspect_ratio,
    const float z_near,
    const float z_far
) -> mat4
{
    const auto fov_x_clamped  = std::min(std::max(fov_x, epsilon), pi_minus_epsilon);
    const auto tan_half_angle = std::tan(fov_x_clamped * 0.5f);
    const auto width          = 2.0f * z_near * tan_half_angle;
    const auto height         = width / aspect_ratio;
    return create_frustum_simple(width, height, z_near, z_far);
}

// http://and-what-happened.blogspot.com/p/just-formulas.html
// The projection produced by this formula has x, y and z extents of -1:+1.
// The perspective control value p is not restricted to integer values.
// The view plane is defined by z.
// Objects on the view plane will have a homogeneous w value of 1.0 after the transform.
auto create_projection(
    const float s,                // Stereo-scopic 3D eye separation
    const float p,                // Perspective (0 == parallel, 1 == perspective)
    const float n, const float f, // Near and z_far z clip depths
    const float w, const float h, // Width and height of viewport (at depth vz)
    const vec3  c,                // Center of viewport
    const vec3  e                 // Center of projection (eye position)
) -> mat4
{
    const float flip_handed = -1.0f;
    return mat4{
          2.0f / w,
          0.0f,
        ((2.0f * (e.x - c.x) + s) / (w * (c.z - e.z))) * flip_handed,
         (2.0f * ((c.x * e.z) - (e.x * c.z)) - s * c.z) / (w * (c.z - e.z)),

         0.0f,
         2.0f / h,
        (2.0f * (e.y - c.y) / (h * (c.z - e.z))) * flip_handed,
         2.0f * ((c.y * e.z) - (e.y * c.z)) / (h * (c.z - e.z)),

         0.0f,
         0.0f,
        ((2.0f * (c.z * (1.0f - p) - e.z) + p * (f + n)) / ((f - n) * (c.z - e.z))) * flip_handed,
        -((c.z * (1.0f - p) - e.z) * (f + n) + 2.0f * f * n * p) / ((f - n) * (c.z - e.z)),

        0.0f,
        0.0f,
        (p / (c.z - e.z)) * flip_handed,
        (c.z * (1.0f - p) - e.z) / (c.z - e.z)

    };
}

auto create_orthographic(
    const float left,
    const float right,
    const float bottom,
    const float top,
    const float z_near,
    const float z_far
) -> mat4
{
    return mat4{
        {
            2.0f / (right - left),
            0.0f,
            0.0f,
            0.0f
        },
        {
            0.0f,
            2.0f / (top - bottom),
            0.0f,
            0.0f
        },
        {
            0.0f,
            0.0f,
            -1.0f / (z_far - z_near),
            0.0f
        },
        {
            -(right + left) / (right - left),
            -(top + bottom) / (top - bottom),
            -z_near / (z_far - z_near),
            1.0f
        }
    };
}

auto create_orthographic_centered(
    const float width,
    const float height,
    const float z_near,
    const float z_far
) -> mat4
{
    // TODO Check bottom and top
    return create_orthographic(
        -width  / 2.0f,  width  / 2.0f,
         height / 2.0f, -height / 2.0f,
        z_near, z_far
    );
}

auto create_look_at(const vec3 eye, const vec3 center, const vec3 up0) -> mat4
{
#if 0
    // This does NOT? give the same result
    return glm::lookAt(eye, center, up0);
#else
    if (eye == center)
    {
        return mat4{1};
    }

    const vec3 back  = glm::normalize(eye - center);
    const vec3 up1   = (up0 == back) ? min_axis(back) : up0;
    const vec3 right = glm::normalize(glm::cross(up1, back));
    const vec3 up    = glm::cross(back, right);

    return mat4{
        vec4{right, 0.0f},
        vec4{up,    0.0f},
        vec4{back,  0.0f},
        vec4{eye,   1.0f}
    };
#endif
}

void hsv_to_rgb(float h, float s, float v, float& r, float& g, float& b)
{
    r = 0.0f;
    g = 0.0f;
    b = 0.0f;

    // cppcheck-suppress variableScope
    float f;
    // cppcheck-suppress variableScope
    float p;
    // cppcheck-suppress variableScope
    float q;
    // cppcheck-suppress variableScope
    float t;
    // cppcheck-suppress variableScope
    int   i;

    if (s == 0.0f)
    {
        r = v;
        g = v;
        b = v;
    }
    else
    {
        if (h == 360.0f)
        {
            h = 0.0f;
        }

        h = h / 60.0f;
        i = static_cast<int>(h);
        f = h - static_cast<float>(i);
        p = v * (1.0f - s);
        q = v * (1.0f - (s * f));
        t = v * (1.0f - (s * (1.0f - f)));

        switch (i)
        {
            case 0:
            {
                r = v;
                g = t;
                b = p;
                break;
            }
            case 1:
            {
                r = q;
                g = v;
                b = p;
                break;
            }
            case 2:
            {
                r = p;
                g = v;
                b = t;
                break;
            }
            case 3:
            {
                r = p;
                g = q;
                b = v;
                break;
            }
            case 4:
            {
                r = t;
                g = p;
                b = v;
                break;
            }
            case 5:
            {
                r = v;
                g = p;
                b = q;
                break;
            }
            default:
            {
                r = 1.0f;
                g = 1.0f;
                b = 1.0f;
                break;
            }
        }
    }
}

void rgb_to_hsv(const float r, const float g, const float b, float& h, float& s, float& v)
{
    h = 0;
    s = 1.0f;
    v = 1.0f;

    float max;
    float min;

    if (r > g)
    {
        max = r;
    }
    else
    {
        max = g;
    }

    if (b > max)
    {
        max = b;
    }

    if (r < g)
    {
        min = r;
    }
    else
    {
        min = g;
    }

    if (b < min)
    {
        min = b;
    }

    float diff = max - min;
    v    = max;

    if (max < 0.0001f)
    {
        s = 0;
    }
    else
    {
        s = diff / max;
    }

    if (s == 0)
    {
        h = 0.0f;
    }
    else
    {
        float r_dist = (max - r) / diff;
        float g_dist = (max - g) / diff;
        float b_dist = (max - b) / diff;
        if (r == max)
        {
            h = b_dist - g_dist;
        }
        else if (g == max)
        {
            h = 2.0f + r_dist - b_dist;
        }
        else if (b == max)
        {
            h = 4.0f + g_dist - r_dist;
        }
        h = h * 60.0f;
        if (h < 0)
        {
            h += 360.0f;
        }
    }
}

auto srgb_to_linear(const float cs) -> float
{
    const float cs_clamped = std::min(std::max(0.0f, cs), 1.0f);
    return
        (cs_clamped <= 0.04045f)
            ? cs_clamped / 12.92f
            : std::pow(
                (cs_clamped + 0.055f) / 1.055f,
                2.4f
            );
}

auto linear_rgb_to_srgb(const float cl) -> float
{
    float res;
    if (cl > 1.0f)
    {
        res = 1.0f;
    }
    else if (cl < 0.0)
    {
        res = 0.0f;
    }
    else if (cl < 0.0031308f)
    {
        res = 12.92f * cl;
    }
    else
    {
        res = 1.055f * std::pow(cl, 0.41666f) - 0.055f;
    }
    return res;
}

auto srgb_to_linear_rgb(const vec3 srgb) -> vec3
{
    return
        vec3{
            srgb_to_linear(srgb.x),
            srgb_to_linear(srgb.y),
            srgb_to_linear(srgb.z)
        };
}

auto linear_rgb_to_srgb(const vec3 linear_rgb) -> vec3
{
    return
        vec3{
            linear_rgb_to_srgb(linear_rgb.x),
            linear_rgb_to_srgb(linear_rgb.y),
            linear_rgb_to_srgb(linear_rgb.z)
        };
}

void cartesian_to_heading_elevation(const vec3 v, float& out_elevation, float& out_heading)
{
    out_elevation = -std::asin(v.y);
    out_heading   =  std::atan2(v.x, v.z);
}

void cartesian_to_spherical_iso(const vec3 v, float& out_theta, float& out_phi)
{
    out_theta = std::acos(glm::clamp(v.z, -1.0f, 1.0f));
    out_phi   = std::atan2(v.y, v.x);
}

auto spherical_to_cartesian_iso(const float theta, const float phi) -> vec3
{
    return
        vec3{
            std::sin(theta) * std::cos(phi),
            std::sin(theta) * std::sin(phi),
            std::cos(theta)
        };
}

void calculate_bounding_volume(
    const Point_source& point_source,
    Bounding_box&       bounding_box,
    Bounding_sphere&    bounding_sphere
)
{
    bounding_box.min = vec3{std::numeric_limits<float>::max()};
    bounding_box.max = vec3{std::numeric_limits<float>::lowest()};
    bounding_sphere.center = vec3{0.0f};
    bounding_sphere.radius = 0.0f;

    glm::vec3 x_min{std::numeric_limits<float>::max()};
    glm::vec3 y_min{std::numeric_limits<float>::max()};
    glm::vec3 z_min{std::numeric_limits<float>::max()};
    glm::vec3 x_max{std::numeric_limits<float>::lowest()};
    glm::vec3 y_max{std::numeric_limits<float>::lowest()};
    glm::vec3 z_max{std::numeric_limits<float>::lowest()};

    if (point_source.point_count() == 0)
    {
        bounding_box.min = vec3{0.0f};
        bounding_box.max = vec3{0.0f};
        return;
    }
    for (size_t i = 0, end = point_source.point_count(); i < end; ++i)
    {
        const auto point = point_source.get_point(i);
        if (point.has_value())
        {
            const vec3 position = point.value();
            bounding_box.min = glm::min(bounding_box.min, position);
            bounding_box.max = glm::max(bounding_box.max, position);

            if (position.x < x_min.x) x_min = position;
            if (position.x > x_max.x) x_max = position;
            if (position.y < y_min.y) y_min = position;
            if (position.y > y_max.y) y_max = position;
            if (position.z < z_min.z) z_min = position;
            if (position.z > z_max.z) z_max = position;
        }
    }

    // Ritter's bounding sphere
    // - Pick a point x from P, search a point y in P, which has the largest distance from x;
    // - Search a point z in P, which has the largest distance from y.
    //   Set up an initial ball B, with its centre as the midpoint of y and z,
    //   the radius as half of the distance between y and z;
    // - If all points in P are within ball B, then we get a bounding sphere.
    //   Otherwise, let p be a point outside the ball, construct a new ball
    //   covering both point p and previous ball.
    //   Repeat this step until all points are covered.
    const auto x_span_v = x_max - x_min;
    const auto y_span_v = y_max - y_min;
    const auto z_span_v = z_max - z_min;
    const auto x_span   = glm::dot(x_span_v, x_span_v);
    const auto y_span   = glm::dot(y_span_v, y_span_v);
    const auto z_span   = glm::dot(z_span_v, z_span_v);

    auto dia_1    = x_min;
    auto dia_2    = x_max;
    auto max_span = x_span;
    if (y_span > max_span)
    {
        max_span = y_span;
        dia_1    = y_min;
        dia_2    = y_max;
    }
    if (z_span > max_span)
    {
        dia_1 = z_min;
        dia_2 = z_max;
    }

    auto       center = (dia_1 + dia_2) / 2.0f;
    const auto d0     = dia_2 - center;
    auto       rad_sq = glm::dot(d0, d0);
    auto       rad    = std::sqrt(rad_sq);

    for (size_t i = 0, end = point_source.point_count(); i < end; ++i)
    {
        const auto point = point_source.get_point(i);
        if (point.has_value())
        {
            const vec3 position    = point.value();
            const auto d           = position - center;
            const auto old_to_p_sq = glm::dot(d, d);
            if (old_to_p_sq > rad_sq)
            {
                const auto old_to_p = std::sqrt(old_to_p_sq);
                rad    = (rad + old_to_p) / 2.0f;
                rad_sq = rad * rad;
                const auto old_to_new = old_to_p - rad;
                center = (rad * center + old_to_new * position) / old_to_p;
            }
        }
    }
    bounding_sphere.center = center;
    bounding_sphere.radius = rad;
}

} // namespace erhe::toolkit
