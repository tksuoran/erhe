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

Transform::Transform(mat4 m)
{
    m_matrix         = m;
    m_inverse_matrix = glm::inverse(m);
}

Transform::Transform(mat4 matrix, mat4 inverse_matrix)
{
    m_matrix         = matrix;
    m_inverse_matrix = inverse_matrix;
}

void Transform::set_translation(vec3 v)
{
    m_matrix = erhe::toolkit::create_translation(v);
    m_inverse_matrix = erhe::toolkit::create_translation(-v);
}

void Transform::fix_inverse()
{
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_translation(float x, float y, float z)
{
    m_matrix = erhe::toolkit::create_translation(x, y, z);
    m_inverse_matrix = erhe::toolkit::create_translation(-x, -y, -z);
}

void Transform::set_rotation(float angle_radians, vec3 axis)
{
    m_matrix = erhe::toolkit::create_rotation(angle_radians, axis);
    m_inverse_matrix = erhe::toolkit::create_rotation(-angle_radians, axis);
}

void Transform::set_scale(float s)
{
    m_matrix = erhe::toolkit::create_scale(s);
    m_inverse_matrix = erhe::toolkit::create_scale(1.0f / s);
}

void Transform::set_scale(float x, float y, float z)
{
    m_matrix = erhe::toolkit::create_scale(x, y, z);
    float x_scale = x != 0.0f ? x : 1.0f;
    float y_scale = y != 0.0f ? y : 1.0f;
    float z_scale = z != 0.0f ? z : 1.0f;
    m_inverse_matrix = erhe::toolkit::create_scale(x_scale, y_scale, z_scale);
}

void Transform::set_projection(
    float s,          // Stereo-scopic 3D eye separation
    float p,          // Perspective (0 == parallel, 1 == perspective)
    float n, float f, // Near and far z clip depths
    float w, float h, // Width and height of viewport (at depth vz)
    vec3 v,           // Center of viewport
    vec3 e            // Center of projection (eye position)
)
{
    if constexpr (erhe::graphics::Configuration::reverse_depth)
    {
        std::swap(n, f);
    }
    m_matrix = erhe::toolkit::create_projection(s, p, n, f, w, h, v, e);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_orthographic(float left, float right, float bottom, float top, float z_near, float z_far)
{
    if constexpr (erhe::graphics::Configuration::reverse_depth)
    {
        std::swap(z_near, z_far);
    }

    m_matrix = erhe::toolkit::create_orthographic(left, right, bottom, top, z_near, z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_orthographic_centered(float width, float height, float z_near, float z_far)
{
    if constexpr (erhe::graphics::Configuration::reverse_depth)
    {
        std::swap(z_near, z_far);
    }

    m_matrix = erhe::toolkit::create_orthographic_centered(width, height, z_near, z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_frustum(float left, float right, float bottom, float top, float z_near, float z_far)
{
    if constexpr (erhe::graphics::Configuration::reverse_depth)
    {
        std::swap(z_near, z_far);
    }

    m_matrix = erhe::toolkit::create_frustum(left, right, bottom, top, z_near, z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_frustum_simple(float width, float height, float z_near, float z_far)
{
    if constexpr (erhe::graphics::Configuration::reverse_depth)
    {
        std::swap(z_near, z_far);
    }

    m_matrix = erhe::toolkit::create_frustum_simple(width, height, z_near, z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_perspective(float fov_x, float fov_y, float z_near, float z_far)
{
    if constexpr (erhe::graphics::Configuration::reverse_depth)
    {
        std::swap(z_near, z_far);
    }

    m_matrix = erhe::toolkit::create_perspective(fov_x, fov_y, z_near, z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_perspective_xr(float fov_left, float fov_right, float fov_up, float fov_down, float z_near, float z_far)
{
    if constexpr (erhe::graphics::Configuration::reverse_depth)
    {
        std::swap(z_near, z_far);
    }

    m_matrix = erhe::toolkit::create_perspective_xr(fov_left, fov_right, fov_up, fov_down, z_near, z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_perspective_vertical(float fov_y, float aspect_ratio, float z_near, float z_far)
{
    if constexpr (erhe::graphics::Configuration::reverse_depth)
    {
        std::swap(z_near, z_far);
    }

    m_matrix = erhe::toolkit::create_perspective_vertical(fov_y, aspect_ratio, z_near, z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_perspective_horizontal(float fov_x, float aspect_ratio, float z_near, float z_far)
{
    if constexpr (erhe::graphics::Configuration::reverse_depth)
    {
        std::swap(z_near, z_far);
    }

    m_matrix = erhe::toolkit::create_perspective_horizontal(fov_x, aspect_ratio, z_near, z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set(mat4 matrix)
{
    m_matrix         = matrix;
    m_inverse_matrix = glm::inverse(matrix);
}

void Transform::set(mat4 matrix, mat4 inverse_matrix)
{
    m_matrix         = matrix;
    m_inverse_matrix = inverse_matrix;
}

void Transform::catenate(mat4 m)
{
    m_matrix         = m_matrix * m;
    m_inverse_matrix = glm::inverse(m_matrix);
}

} // namespace erhe::scene
