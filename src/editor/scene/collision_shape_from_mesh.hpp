#pragma once

#include <memory>

namespace erhe::physics { class ICollision_shape; }
namespace erhe::scene   { class Node; }

namespace editor {

// Builds a collision shape from the mesh geometry attached to a node: a convex
// hull (usable with dynamic bodies) or a triangle mesh (static / kinematic
// bodies only). Uses the first mesh primitive with non-empty Geometry; returns
// nullptr when the node has no usable geometry (or hull construction fails).
// Used by scene deserialization (mesh / convex hull collision shape rebuild)
// and by glTF physics import (collider geometry referencing a mesh node).
[[nodiscard]] auto build_shape_from_node_mesh(
    const erhe::scene::Node* node,
    bool                     convex_hull
) -> std::shared_ptr<erhe::physics::ICollision_shape>;

}
