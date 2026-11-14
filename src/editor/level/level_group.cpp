#include "level/level_group.hpp"

#include <cmath>

namespace editor {

namespace {

[[nodiscard]] auto plane_normal_of(const Kit_mirror_plane plane) -> glm::vec3
{
    return (plane == Kit_mirror_plane::z) ? glm::vec3{0.0f, 0.0f, 1.0f} : glm::vec3{1.0f, 0.0f, 0.0f};
}

// I - 2 n n^T for a unit n.
[[nodiscard]] auto make_reflection(const glm::vec3& n) -> glm::mat3
{
    glm::mat3 m{1.0f};
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            m[column][row] -= 2.0f * n[row] * n[column];
        }
    }
    return m;
}

} // anonymous namespace

auto c_str(const Mirror_axis axis) -> const char*
{
    switch (axis) {
        case Mirror_axis::x: return "x";
        case Mirror_axis::y: return "y";
        case Mirror_axis::z: return "z";
        default:             return "?";
    }
}

auto get_axis_vector(const Mirror_axis axis) -> glm::vec3
{
    switch (axis) {
        case Mirror_axis::y: return glm::vec3{0.0f, 1.0f, 0.0f};
        case Mirror_axis::z: return glm::vec3{0.0f, 0.0f, 1.0f};
        case Mirror_axis::x:
        default:             return glm::vec3{1.0f, 0.0f, 0.0f};
    }
}

auto mirror_pose(
    const Group_pose&                       pose,
    const glm::vec3&                        plane_point,
    const glm::vec3&                        plane_normal,
    const std::span<const Kit_mirror_plane> symmetry_planes
) -> Mirrored_pose
{
    Mirrored_pose result;
    result.image = symmetry_planes.empty() ? Mirror_image::not_exact : Mirror_image::exact;
    float best = -1.0f;
    for (const Kit_mirror_plane plane : symmetry_planes) {
        const float alignment = std::abs(glm::dot(pose.rotation * plane_normal_of(plane), plane_normal));
        if (alignment > (best + 1.0e-6f)) {
            best         = alignment;
            result.plane = plane;
        }
    }
    const glm::mat3 world_reflection = make_reflection(plane_normal);
    const glm::mat3 local_reflection = make_reflection(plane_normal_of(result.plane));
    // Two reflections make a proper rotation.
    const glm::mat3 rotation = world_reflection * glm::mat3_cast(pose.rotation) * local_reflection;
    result.pose.position = plane_point + (world_reflection * (pose.position - plane_point));
    result.pose.rotation = glm::normalize(glm::quat_cast(rotation));
    result.pose.scale    = pose.scale;
    return result;
}

auto get_group_origin(const erhe::math::Aabb& bounds) -> glm::vec3
{
    const glm::vec3 center = bounds.center();
    return glm::vec3{center.x, bounds.min.y, center.z};
}

} // namespace editor
