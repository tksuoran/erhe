#include "rig/bone_roll.hpp"

#include <glm/gtx/quaternion.hpp>

#include <cmath>

namespace editor {

namespace {

// Below this squared length a direction or projection is treated as zero.
constexpr float c_degenerate_length2 = 1.0e-12f;

// Projections onto the plane perpendicular to the bone axis shorter than
// this fraction of the unprojected (unit) vector are parallel to the axis.
constexpr float c_parallel_length = 1.0e-4f;

} // anonymous namespace

auto get_roll_axis_vector(const Roll_axis axis) -> glm::vec3
{
    switch (axis) {
        case Roll_axis::x: return glm::vec3{1.0f, 0.0f, 0.0f};
        case Roll_axis::z: return glm::vec3{0.0f, 0.0f, 1.0f};
        default:           return glm::vec3{1.0f, 0.0f, 0.0f};
    }
}

auto compute_roll_angle(
    const glm::quat& world_rotation,
    const glm::vec3& local_tail,
    const glm::vec3& local_axis,
    const glm::vec3& reference
) -> std::optional<float>
{
    if ((glm::dot(local_tail, local_tail) < c_degenerate_length2) || (glm::dot(reference, reference) < c_degenerate_length2)) {
        return std::nullopt;
    }
    const glm::vec3 bone_axis = glm::normalize(world_rotation * glm::normalize(local_tail));
    const glm::vec3 aimed     = glm::normalize(world_rotation * glm::normalize(local_axis));
    const glm::vec3 target    = glm::normalize(reference);
    const glm::vec3 aimed_in_plane  = aimed  - (glm::dot(aimed,  bone_axis) * bone_axis);
    const glm::vec3 target_in_plane = target - (glm::dot(target, bone_axis) * bone_axis);
    if ((glm::length(aimed_in_plane) < c_parallel_length) || (glm::length(target_in_plane) < c_parallel_length)) {
        return std::nullopt;
    }
    const float sine   = glm::dot(glm::cross(aimed_in_plane, target_in_plane), bone_axis);
    const float cosine = glm::dot(aimed_in_plane, target_in_plane);
    return std::atan2(sine, cosine);
}

auto make_roll_change(const glm::vec3& local_tail, const float angle) -> std::optional<glm::quat>
{
    if (glm::dot(local_tail, local_tail) < c_degenerate_length2) {
        return std::nullopt;
    }
    return glm::angleAxis(angle, glm::normalize(local_tail));
}

auto compute_align_change(
    const glm::quat& world_rotation,
    const glm::vec3& local_tail,
    const glm::quat& active_world_rotation,
    const glm::vec3& active_local_tail
) -> std::optional<glm::quat>
{
    if ((glm::dot(local_tail, local_tail) < c_degenerate_length2) || (glm::dot(active_local_tail, active_local_tail) < c_degenerate_length2)) {
        return std::nullopt;
    }
    // Q takes the bone's own axis onto the active bone's local axis, so
    // (active_world_rotation * Q) * tail points along the active bone's tail.
    const glm::quat axis_map = glm::rotation(glm::normalize(local_tail), glm::normalize(active_local_tail));
    const glm::quat new_world_rotation = active_world_rotation * axis_map;
    return glm::normalize(glm::inverse(world_rotation) * new_world_rotation);
}

auto apply_frame_change(
    const erhe::scene::Trs_transform& local,
    const glm::quat&                  parent_change,
    const glm::quat&                  own_change,
    const Frame_change_head           head
) -> erhe::scene::Trs_transform
{
    const glm::quat undo_parent = glm::inverse(parent_change);
    const glm::vec3 translation = (head == Frame_change_head::on_parent_tail)
        ? local.get_translation()
        : (undo_parent * local.get_translation());
    return erhe::scene::Trs_transform{
        translation,
        glm::normalize(undo_parent * local.get_rotation() * own_change),
        local.get_scale()
    };
}

}
