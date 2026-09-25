#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace erhe::scene {
    class Xformable; using Node = Xformable;
}

namespace editor {

// The math of Bind (rigid), doc/plans/rigging/skeleton_editing.md R18: the
// bones' rest frames the inverse bind matrices come from, and the rigid
// weighting of a vertex to the nearest bone segment. Pure functions over
// nodes and glm; the verb that builds the skin is rig/rigid_skin.hpp.

// A bone's head-to-tail segment in world space at rest.
class Bone_segment
{
public:
    glm::vec3 head{0.0f};
    glm::vec3 tail{0.0f};
};

// Squared distance from `point` to the segment [head, tail]; the distance to
// the head when the segment has zero length.
[[nodiscard]] auto get_point_segment_distance_squared(const glm::vec3& point, const Bone_segment& segment) -> float;

// The index of the segment nearest to `point` (get_point_segment_distance_
// squared); the lowest index among equally near segments. `segments` must
// not be empty.
[[nodiscard]] auto find_nearest_bone_segment(std::span<const Bone_segment> segments, const glm::vec3& point) -> std::size_t;

// The world transforms of `joints` at rest, anchored the way
// erhe::scene::get_bind_pose_parent_from_node reads them back once a skin
// with the inverses of these as its inverse bind matrices lists the joints
// (and its mesh sits at world identity): a joint's rest world is the frame
// above it times its own Rig.rest_* transform, where the frame above is the
// rest world of its nearest ancestor among `joints` carried down by the
// current local transforms of the nodes between, else the parent's current
// world transform (identity without a parent). So the bind pose the skin
// implies is exactly each joint's Rig.rest_* transform.
[[nodiscard]] auto get_rest_world_transforms(const std::vector<std::shared_ptr<erhe::scene::Node>>& joints) -> std::vector<glm::mat4>;

// The segments of `joints` at rest: head = the rest world origin, tail =
// the rest world transform of the joint's Rig.tail.
[[nodiscard]] auto get_rest_bone_segments(
    const std::vector<std::shared_ptr<erhe::scene::Node>>& joints,
    std::span<const glm::mat4>                             rest_world_transforms
) -> std::vector<Bone_segment>;

} // namespace editor
