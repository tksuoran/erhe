#include "level/kit_surface_attachment.hpp"

namespace editor {

namespace {

// The rest point's offset from the piece's origin in the support's node space; double, so a carry at unchanged bounds
// gives the translation back.
[[nodiscard]] auto get_rest_offset(
    const erhe::math::Aabb& piece_bounds,
    const glm::quat&        rotation,
    const glm::vec3&        scale,
    const glm::vec3&        direction,
    const Surface_orient    orient,
    const Surface_snap_rest rest
) -> glm::dvec3
{
    const glm::dvec3 point{get_surface_rest_point(piece_bounds, rotation, scale, direction, orient, rest)};
    return glm::dquat{rotation} * (glm::dvec3{scale} * point);
}

} // anonymous namespace

auto get_surface_rest_point(
    const erhe::math::Aabb& piece_bounds,
    const glm::quat&        rotation,
    const glm::vec3&        scale,
    const glm::vec3&        direction,
    const Surface_orient    orient,
    const Surface_snap_rest rest
) -> glm::vec3
{
    if (orient == Surface_orient::on) {
        return get_base_center(piece_bounds, get_orient_base_frame(scale));
    }
    if (rest == Surface_snap_rest::anchor) {
        return glm::vec3{0.0f};
    }
    return get_base_center(piece_bounds, get_surface_base_frame(rotation, scale, direction));
}

auto make_surface_attachment_position(
    const erhe::math::Aabb& support_bounds,
    const erhe::math::Aabb& piece_bounds,
    const Surface_pose&     local_pose,
    const glm::vec3&        direction,
    const Surface_orient    orient,
    const Surface_snap_rest rest
) -> std::optional<glm::dvec3>
{
    if (!support_bounds.is_valid_3d() || !piece_bounds.is_valid_3d()) {
        return {};
    }
    const glm::dvec3 rest_point = glm::dvec3{local_pose.position} + get_rest_offset(piece_bounds, local_pose.rotation, local_pose.scale, direction, orient, rest);
    glm::dvec3 position{0.0};
    for (int axis = 0; axis < 3; ++axis) {
        const double low    = static_cast<double>(support_bounds.min[axis]);
        const double extent = static_cast<double>(support_bounds.max[axis]) - low;
        position[axis] = (extent > 0.0) ? ((rest_point[axis] - low) / extent) : 0.0;
    }
    return position;
}

auto carry_surface_attachment(
    const Kit_surface_attachment& attachment,
    const erhe::math::Aabb&       support_bounds,
    const erhe::math::Aabb&       piece_bounds,
    const glm::quat&              rotation,
    const glm::vec3&              scale
) -> std::optional<glm::vec3>
{
    if (!support_bounds.is_valid_3d() || !piece_bounds.is_valid_3d()) {
        return {};
    }
    const glm::dvec3 offset = get_rest_offset(piece_bounds, rotation, scale, attachment.direction, attachment.orient, attachment.rest);
    glm::dvec3 rest_point{0.0};
    for (int axis = 0; axis < 3; ++axis) {
        const double low    = static_cast<double>(support_bounds.min[axis]);
        const double extent = static_cast<double>(support_bounds.max[axis]) - low;
        rest_point[axis] = (extent > 0.0) ? (low + (attachment.position[axis] * extent)) : low;
    }
    return glm::vec3{rest_point - offset};
}

} // namespace editor
