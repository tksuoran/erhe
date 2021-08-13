#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <stdexcept>

namespace erhe::scene
{

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
    void set_projection (const float s,                 // Stereo-scopic 3D eye separation
                         const float p,                 // Perspective (0 == parallel, 1 == perspective)
                               float n,       float f,  // Near and far z clip depths
                         const float w, const float h,  // Width and height of viewport (at depth vz)
                         const glm::vec3 v,
                         const glm::vec3 e);

    void set_orthographic          (const float left,     const float right,        const float bottom, const float top, float z_near, float z_far);
    void set_orthographic_centered (const float width,    const float height,       const float z_near, const float z_far);
    void set_frustum               (const float left,     const float right,        const float bottom, const float top, float z_near, float z_far);
    void set_frustum_simple        (const float width,    const float height,       const float z_near, const float z_far);
    void set_perspective           (const float fov_x,    const float fov_y,        const float z_near, const float z_far);
    void set_perspective_xr        (const float fov_left, const float fov_right,    const float fov_up, const float fov_down, float z_near, float z_far);
    void set_perspective_vertical  (const float fov_y,    const float aspect_ratio, const float z_near, const float z_far);
    void set_perspective_horizontal(const float fov_x,    const float aspect_ratio, const float z_near, const float z_far);
    void set                       (const glm::mat4 matrix);
    void set                       (const glm::mat4 matrix, const glm::mat4 inverse_matrix);
    void catenate                  (const glm::mat4 m);

private:
    glm::mat4 m_matrix        {1.0f};
    glm::mat4 m_inverse_matrix{1.0f};
};

} // namespace erhe::scene
