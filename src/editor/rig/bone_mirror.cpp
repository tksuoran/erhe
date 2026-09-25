#include "rig/bone_mirror.hpp"

namespace editor {

auto get_mirror_x_matrix() -> glm::mat4
{
    return glm::mat4{
        glm::vec4{-1.0f, 0.0f, 0.0f, 0.0f},
        glm::vec4{ 0.0f, 1.0f, 0.0f, 0.0f},
        glm::vec4{ 0.0f, 0.0f, 1.0f, 0.0f},
        glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f}
    };
}

auto mirror_vector_x(const glm::vec3& v) -> glm::vec3
{
    return glm::vec3{-v.x, v.y, v.z};
}

auto mirror_rotation_x(const glm::quat& q) -> glm::quat
{
    return glm::quat{q.w, q.x, -q.y, -q.z};
}

auto mirror_trs_x(const erhe::scene::Trs_transform& transform) -> erhe::scene::Trs_transform
{
    return erhe::scene::Trs_transform{
        mirror_vector_x  (transform.get_translation()),
        mirror_rotation_x(transform.get_rotation()),
        transform.get_scale()
    };
}

auto mirror_local_transform(const glm::mat4& parent_map, const glm::mat4& local) -> erhe::scene::Trs_transform
{
    return erhe::scene::Trs_transform{parent_map * local * get_mirror_x_matrix()};
}

auto get_mirror_plane_matrix(const glm::mat4& frame) -> glm::mat4
{
    return frame * get_mirror_x_matrix() * glm::inverse(frame);
}

auto mirror_ik_limits(const glm::vec3& limit_min, const glm::vec3& limit_max) -> Ik_limit_range
{
    return Ik_limit_range{
        .min = glm::vec3{limit_min.x, -limit_max.y, -limit_max.z},
        .max = glm::vec3{limit_max.x, -limit_min.y, -limit_min.z}
    };
}

auto mirror_pole_angle(const float angle) -> float
{
    return -angle;
}

}
