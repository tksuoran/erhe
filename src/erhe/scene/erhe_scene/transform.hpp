#pragma once

#include <glm/glm.hpp>

namespace erhe::scene {

class Clip_range
{
public:
    float z_near;
    float z_far;
};

class Transform
{
public:
    Transform() = default;
    explicit Transform(glm::mat4 m);
    Transform(glm::mat4 matrix, glm::mat4 inverse_matrix);
    Transform(const Transform& t);
    auto operator=(const Transform& t) -> Transform&;

    [[nodiscard]] auto get_matrix        () const -> const glm::mat4& { return m_matrix; }
    [[nodiscard]] auto get_inverse_matrix() const -> const glm::mat4& { return m_inverse_matrix; }

    [[nodiscard]] static auto inverse(const Transform& transform) -> Transform;

    void set_orthographic          (Clip_range clip_range, float left,     float right,        float bottom, float top);
    void set_orthographic_centered (Clip_range clip_range, float width,    float height);
    void set_frustum               (Clip_range clip_range, float left,     float right,        float bottom, float top);
    void set_frustum_simple        (Clip_range clip_range, float width,    float height);
    void set_perspective           (Clip_range clip_range, float fov_x,    float fov_y);
    void set_perspective_xr        (Clip_range clip_range, float fov_left, float fov_right,    float fov_up, float fov_down);
    void set_perspective_vertical  (Clip_range clip_range, float fov_y,    float aspect_ratio);
    void set_perspective_horizontal(Clip_range clip_range, float fov_x,    float aspect_ratio);

protected:
    glm::mat4 m_matrix        {1.0f};
    glm::mat4 m_inverse_matrix{1.0f};
};

Transform operator*(const Transform& lhs, const Transform& rhs);

} // namespace erhe::scene

