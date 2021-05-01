#include "erhe/toolkit/math_util.hpp"
#include "erhe/log/log.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <stdexcept>

namespace erhe::toolkit
{

using glm::mat4;
using glm::vec2;
using glm::vec3;
using glm::vec4;

namespace
{
    constexpr float epsilon         {1e-6f};
    constexpr float pi_minus_epsilon{glm::pi<float>() - epsilon};
}

auto unproject(mat4  world_from_clip,
               vec3  window,
               float depth_range_near,
               float depth_range_far,
               float viewport_x,
               float viewport_y,
               float viewport_width,
               float viewport_height)
-> vec3
{
    float viewport_center_x = viewport_x + viewport_width  * 0.5f;
    float viewport_center_y = viewport_y + viewport_height * 0.5f;
    float s                 = depth_range_far - depth_range_near;
    float b                 = depth_range_near;

    vec4 ndc;
    ndc.x = (window.x - viewport_center_x) / (viewport_width * 0.5f);
    ndc.y = (window.y - viewport_center_y) / (viewport_height * 0.5f);
    ndc.z = (window.z - b) / s;
    ndc.w = 1.0f;

    vec4 world_homogeneous = world_from_clip * ndc;
    if (world_homogeneous.w == 0.0f)
    {
        FATAL("w singularity");
    }

    return vec3(world_homogeneous.x / world_homogeneous.w,
                world_homogeneous.y / world_homogeneous.w,
                world_homogeneous.z / world_homogeneous.w);
}

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
auto project_to_screen_space(mat4  clip_from_world,
                             vec3  position_in_world,
                             float depth_range_near,
                             float depth_range_far,
                             float viewport_x,
                             float viewport_y,
                             float viewport_width,
                             float viewport_height)
-> vec3
{
    float viewport_center_x = viewport_x + viewport_width  * 0.5f;
    float viewport_center_y = viewport_y + viewport_height * 0.5f;
    float s                 = depth_range_far - depth_range_near;
    float b                 = depth_range_near;

    vec4 clip = clip_from_world * vec4(position_in_world, 1.0f);

    vec3 ndc(clip.x / clip.w,
             clip.y / clip.w,
             clip.z / clip.w);

    return vec3(viewport_width  * 0.5f * ndc.x + viewport_center_x,
                viewport_height * 0.5f * ndc.y + viewport_center_y,
                s * ndc.z + b);
}

auto create_frustum(float left, float right, float bottom, float top, float z_near, float z_far)
-> mat4
{
    float x =  (2.0f  * z_near        ) / (right  - left  );
    float y =  (2.0f  * z_near        ) / (top    - bottom);
    float a =  (right + left          ) / (right  - left  );
    float b =  (top   + bottom        ) / (top    - bottom);
  //float c = -(z_far + z_near        ) / (z_far  - z_near); -- negative one to one
    float c =   z_far                   / (z_near - z_far ); // zero to one
  //float d = -(2.0f  * z_far * z_near) / (z_far  - z_near); -- negative one to one
    float d = -(z_far * z_near        ) / (z_far  - z_near); // zero to one

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

auto create_frustum_simple(float width, float height, float z_near, float z_far)
-> mat4
{
    return create_frustum(-width / 2.0f, width / 2.0f, -height / 2.0f, height / 2.0f, z_near, z_far);
}

auto create_perspective(float fov_x, float fov_y, float z_near, float z_far)
-> mat4
{
    fov_y = std::max(fov_y, epsilon);
    fov_y = std::min(fov_y, pi_minus_epsilon);
    fov_x = std::max(fov_x, epsilon);
    fov_x = std::min(fov_x, pi_minus_epsilon);
    auto  tan_x_half_angle = std::tan(fov_x * 0.5f);
    auto  tan_y_half_angle = std::tan(fov_y * 0.5f);
    float width            = 2.0f * z_near * tan_x_half_angle;
    float height           = 2.0f * z_near * tan_y_half_angle;
    return create_frustum_simple(width, height, z_near, z_far);
}

auto create_perspective_vertical(float fov_y, float aspect_ratio, float z_near, float z_far)
-> mat4
{
    fov_y = std::max(fov_y, epsilon);
    fov_y = std::min(fov_y, pi_minus_epsilon);
    //float tan_half_angle = std::tan(fov_y * 0.5f);
    //float height         = 2.0f * z_near * tan_half_angle;
    //float width          = height * aspect_ratio;

    //mat4 result = create_frustum_simple(width, height, z_near, z_far);
    mat4 result = glm::perspective(fov_y, aspect_ratio, z_near, z_far);
    return result;
}

auto create_perspective_horizontal(float fov_x, float aspect_ratio, float z_near, float z_far)
-> mat4
{
    fov_x = std::max(fov_x, epsilon);
    fov_x = std::min(fov_x, pi_minus_epsilon);
    float tan_half_angle = std::tan(fov_x * 0.5f);
    float width          = 2.0f * z_near * tan_half_angle;
    float height         = width / aspect_ratio;

    return create_frustum_simple(width, height, z_near, z_far);
}

// http://and-what-happened.blogspot.com/p/just-formulas.html
// The projection produced by this formula has x, y and z extents of -1:+1.
// The perspective control value p is not restricted to integer values.
// The view plane is defined by z.
// Objects on the view plane will have a homogeneous w value of 1.0 after the transform.
auto create_projection(float s,          // Stereo-scopic 3D eye separation
                       float p,          // Perspective (0 == parallel, 1 == perspective)
                       float n, float f, // Near and z_far z clip depths
                       float w, float h, // Width and height of viewport (at depth vz)
                       vec3  v,          // Center of viewport
                       vec3  e)          // Center of projection (eye position)
-> mat4
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

auto create_orthographic(float left, float right, float bottom, float top, float z_near, float z_far)
-> mat4
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

auto create_orthographic_centered(float width, float height, float z_near, float z_far)
-> mat4
{
    // TODO Check bottom and top
    return create_orthographic(-width / 2, width / 2, height / 2, -height / 2, z_near, z_far);
}

auto create_translation(vec2 t)
-> mat4
{
    mat4 result;
    result[0][0] = 1.0f;
    result[1][0] = 0.0f;
    result[2][0] = 0.0f;
    result[3][0] = t.x;
    result[0][1] = 0.0f;
    result[1][1] = 1.0f;
    result[2][1] = 0.0f;
    result[3][1] = t.y;
    result[0][2] = 0.0f;
    result[1][2] = 0.0f;
    result[2][2] = 1.0f;
    result[3][2] = 0.0f;
    result[0][3] = 0.0f;
    result[1][3] = 0.0f;
    result[2][3] = 0.0f;
    result[3][3] = 1.0f;
    return result;
}

auto create_translation(glm::vec3 t)
-> mat4
{
    mat4 result;
    result[0][0] = 1.0f;
    result[1][0] = 0.0f;
    result[2][0] = 0.0f;
    result[3][0] = t.x;
    result[0][1] = 0.0f;
    result[1][1] = 1.0f;
    result[2][1] = 0.0f;
    result[3][1] = t.y;
    result[0][2] = 0.0f;
    result[1][2] = 0.0f;
    result[2][2] = 1.0f;
    result[3][2] = t.z;
    result[0][3] = 0.0f;
    result[1][3] = 0.0f;
    result[2][3] = 0.0f;
    result[3][3] = 1.0f;
    return result;
}

auto create_translation(float x, float y, float z)
-> mat4
{
    mat4 result;
    result[0][0] = 1.0f;
    result[1][0] = 0.0f;
    result[2][0] = 0.0f;
    result[3][0] = x;
    result[0][1] = 0.0f;
    result[1][1] = 1.0f;
    result[2][1] = 0.0f;
    result[3][1] = y;
    result[0][2] = 0.0f;
    result[1][2] = 0.0f;
    result[2][2] = 1.0f;
    result[3][2] = z;
    result[0][3] = 0.0f;
    result[1][3] = 0.0f;
    result[2][3] = 0.0f;
    result[3][3] = 1.0f;
    return result;
}

auto create_rotation(float angle_radians, vec3 axis)
-> mat4
{
    float rsin = std::sin(angle_radians);
    float rcos = std::cos(angle_radians);

    float u = axis.x;
    float v = axis.y;
    float w = axis.z;

    mat4 result;

    // Set the first row
    result[0][0] =      rcos + u * u * (1 - rcos);
    result[1][0] = -w * rsin + v * u * (1 - rcos);
    result[2][0] =  v * rsin + w * u * (1 - rcos);
    result[3][0] = 0;

    // Set the second row
    result[0][1] =  w * rsin + u * v * (1 - rcos);
    result[1][1] =      rcos + v * v * (1 - rcos);
    result[2][1] = -u * rsin + w * v * (1 - rcos);
    result[3][1] = 0;

    // Set the third row
    result[0][2] = -v * rsin + u * w * (1 - rcos);
    result[1][2] =  u * rsin + v * w * (1 - rcos);
    result[2][2] =      rcos + w * w * (1 - rcos);
    result[3][2] = 0;

    // Set the fourth row
    result[0][3] = 0.0f;
    result[1][3] = 0.0f;
    result[2][3] = 0.0f;
    result[3][3] = 1.0f;
    return result;
}

auto create_scale(float x, float y, float z)
-> mat4
{
    mat4 result;
    result[0][0] = x;
    result[1][0] = 0.0f;
    result[2][0] = 0.0f;
    result[3][0] = 0.0f;
    result[0][1] = 0.0f;
    result[1][1] = y;
    result[2][1] = 0.0f;
    result[3][1] = 0.0f;
    result[0][2] = 0.0f;
    result[1][2] = 0.0f;
    result[2][2] = z;
    result[3][2] = 0.0f;
    result[0][3] = 0.0f;
    result[1][3] = 0.0f;
    result[2][3] = 0.0f;
    result[3][3] = 1.0f;
    return result;
}

auto create_scale(float s)
-> mat4
{
    mat4 result;
    result[0][0] = s;
    result[1][0] = 0.0f;
    result[2][0] = 0.0f;
    result[3][0] = 0.0f;
    result[0][1] = 0.0f;
    result[1][1] = s;
    result[2][1] = 0.0f;
    result[3][1] = 0.0f;
    result[0][2] = 0.0f;
    result[1][2] = 0.0f;
    result[2][2] = s;
    result[3][2] = 0.0f;
    result[0][3] = 0.0f;
    result[1][3] = 0.0f;
    result[2][3] = 0.0f;
    result[3][3] = 1.0f;
    return result;
}

auto create_look_at(vec3 eye, vec3 center, vec3 up0)
-> mat4
{
#if 0
    // This does NOT give the same result
    return glm::lookAt(eye, center, up0);
#else
    if (eye == center)
    {
        return mat4_identity;
    }

    vec3 back = glm::normalize(eye - center);
    if (up0 == back)
    {
        up0 = min_axis(back);
    }

    vec3 right = glm::normalize(glm::cross(up0, back));
    vec3 up    = glm::cross(back, right);

    mat4 result;
    result[0] = vec4(right, 0.0f);
    result[1] = vec4(up,    0.0f);
    result[2] = vec4(back,  0.0f);
    result[3] = vec4(eye, 1.0f);
    return result;
#endif
}

auto degrees_to_radians(float degrees)
-> float
{
    return degrees * glm::pi<float>() / 180.0f;
}

void hsv_to_rgb(float h, float s, float v, float& r, float& g, float& b)
{
    r = 0.0f;
    g = 0.0f;
    b = 0.0f;
    float f;
    float p;
    float q;
    float t;
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

void rgb_to_hsv(float r, float g, float b, float& h, float& s, float& v)
{
    h = 0;
    s = 1.0f;
    v = 1.0f;
    float diff;
    float r_dist;
    float g_dist;
    float b_dist;
    float undefined = 0;

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

    diff = max - min;
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
        h = undefined;
    }
    else
    {
        r_dist = (max - r) / diff;
        g_dist = (max - g) / diff;
        b_dist = (max - b) / diff;
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

auto srgb_to_linear(float cs)
-> float
{
    float cl = 0.0f;

    cs = std::max(0.0f, cs);
    cs = std::min(cs, 1.0f);

    if (cs <= 0.04045f)
    {
        cl = cs / 12.92f;
    }
    else
    {
        cl = std::pow((cs + 0.055f) / 1.055f, 2.4f);
    }

    return cl;
}

auto linear_rgb_to_srgb(float cl)
-> float
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

auto srgb_to_linear_rgb(vec3 srgb)
-> vec3
{
    return vec3{srgb_to_linear(srgb.x),
                srgb_to_linear(srgb.y),
                srgb_to_linear(srgb.z)};
}

auto linear_rgb_to_srgb(vec3 linear_rgb)
-> vec3
{
    return vec3{linear_rgb_to_srgb(linear_rgb.x),
                linear_rgb_to_srgb(linear_rgb.y),
                linear_rgb_to_srgb(linear_rgb.z)};
}

void cartesian_to_heading_elevation(vec3 v, float& out_elevation, float& out_heading)
{
    out_elevation = -std::asin(v.y);
    out_heading   =  std::atan2(v.x, v.z);
}

void cartesian_to_spherical_iso(vec3 v, float& out_theta, float& out_phi)
{
    out_theta = std::acos(v.z);
    out_phi   = std::atan2(v.y, v.x);
}

auto spherical_to_cartesian_iso(float theta, float phi)
-> vec3
{
    return vec3{std::sin(theta) * std::cos(phi),
                std::sin(theta) * std::sin(phi),
                std::cos(theta)};
}

auto closest_points(glm::vec3 P0, glm::vec3 P1, glm::vec3 Q0, glm::vec3 Q1, glm::vec3& out_PC, glm::vec3& out_QC)
-> bool
{
    glm::vec3 u  = P1 - P0;
    glm::vec3 v  = Q1 - Q0;
    glm::vec3 w0 = P0 - Q0;
    float     a  = glm::dot(u, u);
    float     b  = glm::dot(u, v);
    float     c  = glm::dot(v, v);
    float     d  = glm::dot(u, w0);
    float     e  = glm::dot(v, w0);
    float     denominator = (a * c) - (b * b);

    if (denominator < 0.000001f)
    {
        return false;
    }

    float sC = ((b * e) - (c * d)) / denominator;
    float tC = ((a * e) - (b * d)) / denominator;
    out_PC = P0 + sC * u;
    out_QC = Q0 + tC * v;

    return true;
}

auto closest_points(glm::vec2 P0, glm::vec2 P1, glm::vec2 Q0, glm::vec2 Q1, glm::vec2& out_PC, glm::vec2& out_QC)
-> bool
{
    glm::vec2 u  = P1 - P0;
    glm::vec2 v  = Q1 - Q0;
    glm::vec2 w0 = P0 - Q0;
    float     a  = glm::dot(u, u);
    float     b  = glm::dot(u, v);
    float     c  = glm::dot(v, v);
    float     d  = glm::dot(u, w0);
    float     e  = glm::dot(v, w0);
    float     denominator = (a * c) - (b * b);

    if (denominator < epsilon)
    {
        return false;
    }

    float sC = ((b * e) - (c * d)) / denominator;
    float tC = ((a * e) - (b * d)) / denominator;
    out_PC = P0 + sC * u;
    out_QC = Q0 + tC * v;

    return true;
}

auto closest_point(glm::vec2 P0, glm::vec2 P1, glm::vec2 Q, glm::vec2& out_PC)
-> bool
{
    glm::vec2 u = P1 - P0;
    if (dot(u, u) < epsilon)
    {
        return false;
    }
    float t = glm::dot(u, Q - P0) / dot(u, u);
    out_PC = P0 + t * u;
    return true;
}

auto closest_point(glm::vec3 P0, glm::vec3 P1, glm::vec3 Q, glm::vec3& out_PC)
-> bool
{
    glm::vec3 u = P1 - P0;
    if (dot(u, u) < epsilon)
    {
        return false;
    }
    float t = glm::dot(u, Q - P0) / dot(u, u);
    out_PC = P0 + t * u;
    return true;
}

auto line_point_distance(glm::vec2 P0, glm::vec2 P1, glm::vec2 Q, float& distance)
-> bool
{
    glm::vec2 PC;
    if (!closest_point(P0, P1, Q, PC))
    {
        return false;
    }
    distance = glm::distance(Q, PC);
    return true;
}

auto line_point_distance(glm::vec3 P0, glm::vec3 P1, glm::vec3 Q, float& distance)
-> bool
{
    glm::vec3 PC;
    if (!closest_point(P0, P1, Q, PC))
    {
        return false;
    }
    distance = glm::distance(Q, PC);
    return true;
}

auto intersect_plane(glm::vec3 n, glm::vec3 p0, glm::vec3 l0, glm::vec3 l, float& t) -> bool
{
    float denominator = glm::dot(n, l);
    if (std::abs(denominator) < epsilon)
    {
        return false;
    }
    t = glm::dot(p0 - l0, n) / denominator;
    return true;
}

auto project_point_to_plane(glm::vec3 n, glm::vec3 p, glm::vec3& q) -> bool
{
    // Q = P - Pn_v
    // v = P - S
    // Pn_v = dot(v, n) / dot(n, n)
    //glm::vec3 v           = q - p;
    float     nominator   = dot(n, q - p);
    float     denominator = dot(n, n);
    if (std::abs(denominator) < epsilon)
    {
        return false;
    }
    float     t            = nominator / denominator;
    glm::vec3 Pn_v         = t * n;
    glm::vec3 projected_q1 = q - Pn_v;

    q = projected_q1;
    return true;
}

} // namespace erhe::toolkit
