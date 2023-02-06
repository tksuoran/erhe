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

    [[nodiscard]] auto matrix         () const -> const glm::mat4& { return m_matrix; }
    [[nodiscard]] auto inverse_matrix () const -> const glm::mat4& { return m_inverse_matrix; }

    void set_translation(const glm::vec3 v);
    void fix_inverse    ();
    void set_translation(float x, float y, float z);
    void set_rotation   (float angle_radians, const glm::vec3& axis);
    void set_scale      (float s);
    void set_scale      (float x, float y, float z);

    void set_orthographic          (const Clip_range clip_range, float left,     float right,        float bottom, float top);
    void set_orthographic_centered (const Clip_range clip_range, float width,    float height);
    void set_frustum               (const Clip_range clip_range, float left,     float right,        float bottom, float top);
    void set_frustum_simple        (const Clip_range clip_range, float width,    float height);
    void set_perspective           (const Clip_range clip_range, float fov_x,    float fov_y);
    void set_perspective_xr        (const Clip_range clip_range, float fov_left, float fov_right,    float fov_up, float fov_down);
    void set_perspective_vertical  (const Clip_range clip_range, float fov_y,    float aspect_ratio);
    void set_perspective_horizontal(const Clip_range clip_range, float fov_x,    float aspect_ratio);
    void set                       (const glm::mat4& matrix);
    void set                       (const glm::mat4& matrix, const glm::mat4& inverse_matrix);
    void catenate                  (const glm::mat4& m);

    static auto inverse           (const Transform& transform)                 -> Transform;
    static auto create_translation(float x, float y, float z)                  -> Transform;
    static auto create_translation(const glm::vec3& translation)               -> Transform;
    static auto create_rotation   (float angle_radians, const glm::vec3& axis) -> Transform;
    static auto create_scale      (float s)                                    -> Transform;
    static auto create_scale      (float x, float y, float z)                  -> Transform;
    static auto create_scale      (const glm::vec3& scale)                     -> Transform;

    static bool s_reverse_depth;

private:
    glm::mat4 m_matrix        {1.0f};
    glm::mat4 m_inverse_matrix{1.0f};
};

auto operator* (const Transform& lhs, const Transform& rhs) -> Transform;
auto operator==(const Transform& lhs, const Transform& rhs) -> bool;
auto operator!=(const Transform& lhs, const Transform& rhs) -> bool;

} // namespace erhe::scene
