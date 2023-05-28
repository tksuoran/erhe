#include "erhe/scene/transform.hpp"
#include "erhe/graphics/instance.hpp"
#include "erhe/toolkit/math_util.hpp"

#include <glm/gtx/matrix_decompose.hpp>

namespace erhe::scene
{

using glm::vec3;
using glm::mat4;


Transform::Transform(const Transform& t) = default;

auto Transform::operator=(const Transform& t) -> Transform& = default;

Transform::Transform(const mat4 m)
    : m_matrix        {m}
    , m_inverse_matrix{glm::inverse(m)}
{
}

Transform::Transform(
    const mat4 matrix,
    const mat4 inverse_matrix
)
    : m_matrix        {matrix}
    , m_inverse_matrix{inverse_matrix}
{
}

auto Transform::inverse(const Transform& transform) -> Transform
{
    return Transform{
        transform.get_inverse_matrix(),
        transform.get_matrix()
    };
}

void Transform::set_orthographic(
    const Clip_range clip_range,
    const float      left,
    const float      right,
    const float      bottom,
    const float      top
)
{
    m_matrix = erhe::toolkit::create_orthographic(left, right, bottom, top, clip_range.z_near, clip_range.z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_orthographic_centered(
    const Clip_range clip_range,
    const float      width,
    const float      height
)
{
    m_matrix = erhe::toolkit::create_orthographic_centered(width, height, clip_range.z_near, clip_range.z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_frustum(
    const Clip_range clip_range,
    const float      left,
    const float      right,
    const float      bottom,
    const float      top
)
{
    m_matrix = erhe::toolkit::create_frustum(left, right, bottom, top, clip_range.z_near, clip_range.z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_frustum_simple(
    const Clip_range clip_range,
    const float      width,
    const float      height
)
{
    m_matrix = erhe::toolkit::create_frustum_simple(width, height, clip_range.z_near, clip_range.z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_perspective(
    const Clip_range clip_range,
    const float      fov_x,
    const float      fov_y
)
{
    m_matrix = erhe::toolkit::create_perspective(fov_x, fov_y, clip_range.z_near, clip_range.z_far);
    m_inverse_matrix = glm::inverse(m_matrix);
}

void Transform::set_perspective_xr(
    const Clip_range clip_range,
    const float      fov_left,
    const float      fov_right,
    const float      fov_up,
    const float      fov_down
)
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

} // namespace erhe::scene
