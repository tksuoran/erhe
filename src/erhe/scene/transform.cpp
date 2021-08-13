#include "erhe/scene/transform.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/toolkit/math_util.hpp"

namespace erhe::scene
{

using glm::vec3;
using glm::mat4;

Transform::Transform(const Transform& t)
{
    m_matrix         = t.m_matrix;
    m_inverse_matrix = t.m_inverse_matrix;
}

auto Transform::operator=(const Transform& t) -> Transform&
{
    m_matrix         = t.m_matrix;
    m_inverse_matrix = t.m_inverse_matrix;
    return *this;
}

Transform::Transform(const mat4 m)
{
    m_matrix         = m;
    m_inverse_matrix = glm::inverse(m);
}

Transform::Transform(const mat4 matrix, const mat4 inverse_matrix)
{
    m_matrix         = matrix;
    m_inverse_matrix = inverse_matrix;
}

void Transform::set_translation(const vec3 v)
{
    m_matrix = erhe::toolkit::create_translation(v);
    m_inverse_matrix = erhe::toolkit::create_translation(-v);
}

void Transform::fix_inverse()
{
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_translation(const float x, const float y, const float z)
{
    m_matrix         = erhe::toolkit::create_translation(x, y, z);
    m_inverse_matrix = erhe::toolkit::create_translation(-x, -y, -z);
}

void Transform::set_rotation(const float angle_radians, const vec3 axis)
{
    m_matrix = erhe::toolkit::create_rotation(angle_radians, axis);
    m_inverse_matrix = erhe::toolkit::create_rotation(-angle_radians, axis);
}

void Transform::set_scale(const float s)
{
    m_matrix = erhe::toolkit::create_scale(s);
    m_inverse_matrix = erhe::toolkit::create_scale(1.0f / s);
}

void Transform::set_scale(const float x, const float y, const float z)
{
    m_matrix = erhe::toolkit::create_scale(x, y, z);
    float x_scale = x != 0.0f ? x : 1.0f;
    float y_scale = y != 0.0f ? y : 1.0f;
    float z_scale = z != 0.0f ? z : 1.0f;
    m_inverse_matrix = erhe::toolkit::create_scale(x_scale, y_scale, z_scale);
}

void Transform::set_projection(
    const float s,                // Stereo-scopic 3D eye separation
    const float p,                // Perspective (0 == parallel, 1 == perspective)
          float n,       float f, // Near and far z clip depths
    const float w, const float h, // Width and height of viewport (at depth vz)
    const vec3 v,                 // Center of viewport
    const vec3 e                  // Center of projection (eye position)
)
{
    if constexpr (erhe::graphics::Configuration::reverse_depth)
    {
        std::swap(n, f);
    }
    m_matrix = erhe::toolkit::create_projection(s, p, n, f, w, h, v, e);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_orthographic(const float left, const float right, const float bottom, const float top, float z_near, float z_far)
{
    if constexpr (erhe::graphics::Configuration::reverse_depth)
    {
        std::swap(z_near, z_far);
    }

    m_matrix = erhe::toolkit::create_orthographic(left, right, bottom, top, z_near, z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_orthographic_centered(const float width, const float height, float z_near, float z_far)
{
    if constexpr (erhe::graphics::Configuration::reverse_depth)
    {
        std::swap(z_near, z_far);
    }

    m_matrix = erhe::toolkit::create_orthographic_centered(width, height, z_near, z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_frustum(const float left, const float right, const float bottom, const float top, float z_near, float z_far)
{
    if constexpr (erhe::graphics::Configuration::reverse_depth)
    {
        std::swap(z_near, z_far);
    }

    m_matrix = erhe::toolkit::create_frustum(left, right, bottom, top, z_near, z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_frustum_simple(const float width, const float height, float z_near, float z_far)
{
    if constexpr (erhe::graphics::Configuration::reverse_depth)
    {
        std::swap(z_near, z_far);
    }

    m_matrix = erhe::toolkit::create_frustum_simple(width, height, z_near, z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_perspective(const float fov_x, const float fov_y, float z_near, float z_far)
{
    if constexpr (erhe::graphics::Configuration::reverse_depth)
    {
        std::swap(z_near, z_far);
    }

    m_matrix = erhe::toolkit::create_perspective(fov_x, fov_y, z_near, z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_perspective_xr(const float fov_left, const float fov_right, const float fov_up, const float fov_down, float z_near, float z_far)
{
    if constexpr (erhe::graphics::Configuration::reverse_depth)
    {
        std::swap(z_near, z_far);
    }

    m_matrix = erhe::toolkit::create_perspective_xr(fov_left, fov_right, fov_up, fov_down, z_near, z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_perspective_vertical(const float fov_y, const float aspect_ratio, float z_near, float z_far)
{
    if constexpr (erhe::graphics::Configuration::reverse_depth)
    {
        std::swap(z_near, z_far);
    }

    m_matrix = erhe::toolkit::create_perspective_vertical(fov_y, aspect_ratio, z_near, z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_perspective_horizontal(const float fov_x, const float aspect_ratio, float z_near, float z_far)
{
    if constexpr (erhe::graphics::Configuration::reverse_depth)
    {
        std::swap(z_near, z_far);
    }

    m_matrix = erhe::toolkit::create_perspective_horizontal(fov_x, aspect_ratio, z_near, z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set(const mat4 matrix)
{
    m_matrix         = matrix;
    m_inverse_matrix = glm::inverse(matrix);
}

void Transform::set(const mat4 matrix, const mat4 inverse_matrix)
{
    m_matrix         = matrix;
    m_inverse_matrix = inverse_matrix;
}

void Transform::catenate(const mat4 m)
{
    m_matrix         = m_matrix * m;
    m_inverse_matrix = glm::inverse(m_matrix);
}

} // namespace erhe::scene
