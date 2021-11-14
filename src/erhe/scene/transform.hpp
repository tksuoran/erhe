#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <stdexcept>

namespace erhe::scene
{

class Clip_range
{
public:
    float z_near;
    float z_far;
};

class Transform
{
public:
    Transform         () = default;
    explicit Transform(const glm::mat4 m);
    Transform         (const glm::mat4 matrix, const glm::mat4 inverse_matrix);
    Transform         (const Transform& t);
    auto operator=    (const Transform& t) -> Transform&;

    auto matrix         () const -> glm::mat4 { return m_matrix; }
    auto inverse_matrix () const -> glm::mat4 { return m_inverse_matrix; }
    void set_translation(const glm::vec3 v);
    void fix_inverse    ();
    void set_translation(const float x, const float y, const float z);
    void set_rotation   (const float angle_radians, const glm::vec3 axis);
    void set_scale      (const float s);
    void set_scale      (const float x, const float y, const float z);

    void set_orthographic          (const Clip_range clip_range, const float left,     const float right,        const float bottom, const float top);
    void set_orthographic_centered (const Clip_range clip_range, const float width,    const float height);
    void set_frustum               (const Clip_range clip_range, const float left,     const float right,        const float bottom, const float top);
    void set_frustum_simple        (const Clip_range clip_range, const float width,    const float height);
    void set_perspective           (const Clip_range clip_range, const float fov_x,    const float fov_y);
    void set_perspective_xr        (const Clip_range clip_range, const float fov_left, const float fov_right,    const float fov_up, const float fov_down);
    void set_perspective_vertical  (const Clip_range clip_range, const float fov_y,    const float aspect_ratio);
    void set_perspective_horizontal(const Clip_range clip_range, const float fov_x,    const float aspect_ratio);
    void set                       (const glm::mat4 matrix);
    void set                       (const glm::mat4 matrix, const glm::mat4 inverse_matrix);
    void catenate                  (const glm::mat4 m);

    static auto inverse           (const Transform& transform)                      -> const Transform;
    static auto create_translation(const float x, const float y, const float z)     -> const Transform;
    static auto create_translation(const glm::vec3 translation)                     -> const Transform;
    static auto create_rotation   (const float angle_radians, const glm::vec3 axis) -> const Transform;
    static auto create_scale      (const float s)                                   -> const Transform;
    static auto create_scale      (const float x, const float y, const float z)     -> const Transform;
    static auto create_scale      (const glm::vec3 scale)                           -> const Transform;

    static bool s_reverse_depth;

private:
    glm::mat4 m_matrix        {1.0f};
    glm::mat4 m_inverse_matrix{1.0f};
};

auto operator*(const Transform& lhs, const Transform& rhs) -> Transform;

} // namespace erhe::scene
