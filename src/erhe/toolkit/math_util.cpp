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

    mat4 result;
    result[0][0] = x;
    result[1][0] = 0;
    result[2][0] = a;
    result[3][0] = 0;
    result[0][1] = 0;
    result[1][1] = y;
    result[2][1] = b;
    result[3][1] = 0;
    result[0][2] = 0;
    result[1][2] = 0;
    result[2][2] = c; // inf far: zero to one = -1.0         minus one to one = -1.0
    result[3][2] = d; // inf far: zero to one = -1.0 * near  minus one to one = -2.0 * near
    result[0][3] = 0;
    result[1][3] = 0;
    result[2][3] = -1.0f;
    result[3][3] = 0;
    return result;
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
    const float x =  (2.0f  * z_near        ) / (right  - left  );
    const float y =  (2.0f  * z_near        ) / (top    - bottom);
    const float a =  (right + left          ) / (right  - left  );
    const float b =  (top   + bottom        ) / (top    - bottom);
  //const float c = -1.0f;           -- negative one to one
    const float c = -1.0f;           // zero to one
  //const float d = -2.0f * z_near;  -- negative one to one
    const float d = -z_near;         // zero to one

    mat4 result;
    result[0][0] = x;
    result[1][0] = 0;
    result[2][0] = a;
    result[3][0] = 0;
    result[0][1] = 0;
    result[1][1] = y;
    result[2][1] = b;
    result[3][1] = 0;
    result[0][2] = 0;
    result[1][2] = 0;
    result[2][2] = c;
    result[3][2] = d;
    result[0][3] = 0;
    result[1][3] = 0;
    result[2][3] = -1.0f;
    result[3][3] = 0;
    return result;
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
    const vec3  v,                // Center of viewport
    const vec3  e                 // Center of projection (eye position) 
) -> mat4
{
    mat4 result;
    result[0][0] =  2.0f / w;
    result[0][1] =  0.0f;
    result[0][2] = (2.0f * (e.x - v.x) + s) / (w * (v.z - e.z));
    result[0][3] = (2.0f * ((v.x * e.z) - (e.x * v.z)) - s * v.z) / (w * (v.z - e.z));

    result[1][0] = 0.0f;
    result[1][1] = 2.0f / h;
    result[1][2] = 2.0f * (e.y - v.y) / (h * (v.z - e.z));
    result[1][3] = 2.0f * ((v.y * e.z) - (e.y * v.z)) / (h * (v.z - e.z));

    result[2][0] =  0.0f;
    result[2][1] =  0.0f;
    result[2][2] = (2.0f * (v.z * (1.0f - p) - e.z) + p * (f + n)) / ((f - n) * (v.z - e.z));
    result[2][3] = -((v.z * (1.0f - p) - e.z) * (f + n) + 2.0f * f * n * p) / ((f - n) * (v.z - e.z));

    result[3][0] = 0.0f;
    result[3][1] = 0.0f;
    result[3][2] = p / (v.z - e.z);
    result[3][3] = (v.z * (1.0f - p) - e.z) / (v.z - e.z);

    // Changes handedness
    result[0][2] = -result[0][2];
    result[1][2] = -result[1][2];
    result[2][2] = -result[2][2];
    result[3][2] = -result[3][2];
    return result;
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
    mat4 result;
    result[0][0] = 2.0f / (right - left);
    result[1][0] = 0.0f;
    result[2][0] = 0.0f;
    result[3][0] = -(right + left) / (right - left);
    result[0][1] = 0.0f;
    result[1][1] = 2.0f / (top - bottom);
    result[2][1] = 0.0f;
    result[3][1] = -(top + bottom) / (top - bottom);
    result[0][2] = 0.0f;
    result[1][2] = 0.0f;
    result[2][2] = -1.0f / (z_far - z_near);
    result[3][2] = -z_near / (z_far - z_near);
    result[0][3] = 0.0f;
    result[1][3] = 0.0f;
    result[2][3] = 0.0f;
    result[3][3] = 1.0f;
    return result;
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

    mat4 result;
    result[0] = vec4{right, 0.0f};
    result[1] = vec4{up,    0.0f};
    result[2] = vec4{back,  0.0f};
    result[3] = vec4{eye, 1.0f};
    return result;
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

} // namespace erhe::toolkit
