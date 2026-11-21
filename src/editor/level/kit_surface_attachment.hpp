#pragma once

#include "level/kit_piece.hpp"
#include "level/surface_base_frame.hpp"

#include "erhe_math/aabb.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <optional>

// The place of a surface attachment on its support, free of the scene (doc/editor/level_tools.md "Surface attachments").
namespace editor {

// The point of the piece that lies on the surface, in its own node space: the center of the bounds face the snap rests
// (leading along the direction without orient, the -Y face with orient), or the origin with the anchor rest.
[[nodiscard]] auto get_surface_rest_point(
    const erhe::math::Aabb& piece_bounds,
    const glm::quat&        rotation,
    const glm::vec3&        scale,
    const glm::vec3&        direction,
    Surface_orient          orient,
    Surface_snap_rest       rest
) -> glm::vec3;

// The record's position for the piece's local pose (in the support's node space): the rest point as a proportion of the
// support's bounds, 0 on an axis where they have no extent; none for invalid bounds.
[[nodiscard]] auto make_surface_attachment_position(
    const erhe::math::Aabb& support_bounds,
    const erhe::math::Aabb& piece_bounds,
    const Surface_pose&     local_pose,
    const glm::vec3&        direction,
    Surface_orient          orient,
    Surface_snap_rest       rest
) -> std::optional<glm::dvec3>;

// S3.2 carry: the piece's local translation that puts its rest point at the record's proportion of the support's bounds
// (the bounds' value on an axis without extent), rotation and scale kept; none for invalid bounds.
[[nodiscard]] auto carry_surface_attachment(
    const Kit_surface_attachment& attachment,
    const erhe::math::Aabb&       support_bounds,
    const erhe::math::Aabb&       piece_bounds,
    const glm::quat&              rotation,
    const glm::vec3&              scale
) -> std::optional<glm::vec3>;

} // namespace editor
