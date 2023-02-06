#include "erhe/scene/transform.hpp"
#include "erhe/graphics/instance.hpp"
#include "erhe/toolkit/math_util.hpp"

namespace erhe::scene
{

using glm::vec3;
using glm::mat4;

bool Transform::s_reverse_depth = true;

Transform::Transform(const Transform& t)
    : m_matrix        {t.m_matrix}
    , m_inverse_matrix{t.m_inverse_matrix}
{
}

auto Transform::operator=(const Transform& t) -> Transform&
{
    m_matrix         = t.m_matrix;
    m_inverse_matrix = t.m_inverse_matrix;
    return *this;
}

Transform::Transform(const mat4 m)
    : m_matrix        {m}
    , m_inverse_matrix{glm::inverse(m)}
{
}

Transform::Transform(const mat4 matrix, const mat4 inverse_matrix)
    : m_matrix        {matrix}
    , m_inverse_matrix{inverse_matrix}
{
}

void Transform::set_translation(const vec3 v)
{
    m_matrix         = erhe::toolkit::create_translation<float>( v);
    m_inverse_matrix = erhe::toolkit::create_translation<float>(-v);
}

void Transform::fix_inverse()
{
    m_inverse_matrix = glm::inverse(m_matrix);
}

auto Transform::inverse(const Transform& transform) -> Transform
{
    return Transform{transform.inverse_matrix(), transform.matrix()};
}

void Transform::set_translation(const float x, const float y, const float z)
{
    m_matrix         = erhe::toolkit::create_translation<float>(glm::vec3{ x,  y,  z});
    m_inverse_matrix = erhe::toolkit::create_translation<float>(glm::vec3{-x, -y, -z});
}

auto Transform::create_translation(const float x, const float y, const float z) -> Transform
{
    return Transform{
        erhe::toolkit::create_translation<float>(glm::vec3{ x,  y,  z}),
        erhe::toolkit::create_translation<float>(glm::vec3{-x, -y, -z})
    };
}

auto Transform::create_translation(const glm::vec3& translation) -> Transform
{
    return Transform{
        erhe::toolkit::create_translation<float>( translation),
        erhe::toolkit::create_translation<float>(-translation)
    };
}

void Transform::set_rotation(const float angle_radians, const vec3& axis)
{
    m_matrix         = erhe::toolkit::create_rotation<float>( angle_radians, axis);
    m_inverse_matrix = erhe::toolkit::create_rotation<float>(-angle_radians, axis);
}

auto Transform::create_rotation(const float angle_radians, const glm::vec3& axis) -> Transform
{
    return Transform{
        erhe::toolkit::create_rotation<float>( angle_radians, axis),
        erhe::toolkit::create_rotation<float>(-angle_radians, axis)
    };
}

void Transform::set_scale(const float s)
{
    m_matrix         = erhe::toolkit::create_scale(s);
    m_inverse_matrix = erhe::toolkit::create_scale(1.0f / s);
}

auto Transform::create_scale(const float s) -> Transform
{
    return Transform{
        erhe::toolkit::create_scale<float>(s),
        erhe::toolkit::create_scale<float>(1.0f / s)
    };
}

void Transform::set_scale(const float x, const float y, const float z)
{
    const float x_scale = (x != 0.0f) ? x : 1.0f;
    const float y_scale = (y != 0.0f) ? y : 1.0f;
    const float z_scale = (z != 0.0f) ? z : 1.0f;
    m_matrix         = erhe::toolkit::create_scale<float>(x_scale, y_scale, z_scale);
    m_inverse_matrix = erhe::toolkit::create_scale<float>(1.0f / x_scale, 1.0f / y_scale, 1.0f / z_scale);
}

auto Transform::create_scale(const float x, const float y, const float z) -> Transform
{
    const float x_scale = (x != 0.0f) ? x : 1.0f;
    const float y_scale = (y != 0.0f) ? y : 1.0f;
    const float z_scale = (z != 0.0f) ? z : 1.0f;
    return Transform{
        erhe::toolkit::create_scale<float>(x_scale, y_scale, z_scale),
        erhe::toolkit::create_scale<float>(1.0f / x_scale, 1.0f / y_scale, 1.0f / z_scale)
    };
}

auto Transform::create_scale(const glm::vec3& scale) -> Transform
{
    const float x_scale = (scale.x != 0.0f) ? scale.x : 1.0f;
    const float y_scale = (scale.y != 0.0f) ? scale.y : 1.0f;
    const float z_scale = (scale.z != 0.0f) ? scale.z : 1.0f;
    return Transform{
        erhe::toolkit::create_scale<float>(x_scale, y_scale, z_scale),
        erhe::toolkit::create_scale<float>(1.0f / x_scale, 1.0f / y_scale, 1.0f / z_scale)
    };
}

void Transform::set_orthographic(const Clip_range clip_range, const float left, const float right, const float bottom, const float top)
{
    m_matrix = erhe::toolkit::create_orthographic(left, right, bottom, top, clip_range.z_near, clip_range.z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_orthographic_centered(const Clip_range clip_range, const float width, const float height)
{
    m_matrix = erhe::toolkit::create_orthographic_centered(width, height, clip_range.z_near, clip_range.z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_frustum(const Clip_range clip_range, const float left, const float right, const float bottom, const float top)
{
    m_matrix = erhe::toolkit::create_frustum(left, right, bottom, top, clip_range.z_near, clip_range.z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_frustum_simple(const Clip_range clip_range, const float width, const float height)
{
    m_matrix = erhe::toolkit::create_frustum_simple(width, height, clip_range.z_near, clip_range.z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_perspective(const Clip_range clip_range, const float fov_x, const float fov_y)
{
    m_matrix = erhe::toolkit::create_perspective(fov_x, fov_y, clip_range.z_near, clip_range.z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_perspective_xr(const Clip_range clip_range, const float fov_left, const float fov_right, const float fov_up, const float fov_down)
{
    m_matrix = erhe::toolkit::create_perspective_xr(fov_left, fov_right, fov_up, fov_down, clip_range.z_near, clip_range.z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_perspective_vertical(
    const Clip_range clip_range,
    const float      fov_y,
    const float      aspect_ratio
)
{
    m_matrix = erhe::toolkit::create_perspective_vertical(fov_y, aspect_ratio, clip_range.z_near, clip_range.z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_perspective_horizontal(
    const Clip_range clip_range,
    const float      fov_x,
    const float      aspect_ratio
)
{
    m_matrix = erhe::toolkit::create_perspective_horizontal(fov_x, aspect_ratio, clip_range.z_near, clip_range.z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set(const mat4& matrix)
{
    m_matrix         = matrix;
    m_inverse_matrix = glm::inverse(matrix);
}

void Transform::set(const mat4& matrix, const mat4& inverse_matrix)
{
    m_matrix         = matrix;
    m_inverse_matrix = inverse_matrix;
}

void Transform::catenate(const mat4& m)
{
    m_matrix         = m_matrix * m;
    m_inverse_matrix = glm::inverse(m_matrix);
}

auto operator*(const Transform& lhs, const Transform& rhs) -> Transform
{
    return Transform{
        lhs.matrix() * rhs.matrix(),
        rhs.inverse_matrix() * lhs.inverse_matrix()
    };
}

auto operator==(const Transform& lhs, const Transform& rhs) -> bool
{
    const bool matrix_equality  = lhs.matrix() == rhs.matrix();
    const bool inverse_equality = lhs.inverse_matrix() == rhs.inverse_matrix();
    return matrix_equality && inverse_equality;
}

auto operator!=(const Transform& lhs, const Transform& rhs) -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::scene
