#pragma once

#include <memory>

namespace erhe::physics { class ICollision_shape; }
namespace erhe::scene   { class Mesh; class Node; }

namespace editor {

// Builds a collision shape from mesh geometry: a convex hull (usable with
// dynamic bodies) or a triangle mesh (static / kinematic bodies only). Uses
// the first mesh primitive with non-empty Geometry, creating the Geometry
// from the primitive's Triangle_soup when needed (glTF-imported meshes);
// returns nullptr when there is no usable geometry (or hull construction
// fails). Used by scene deserialization (mesh / convex hull collision shape
// rebuild) and by glTF physics import (mesh- and node-keyed collider
// geometry).
[[nodiscard]] auto build_shape_from_mesh(
    const erhe::scene::Mesh* mesh,
    bool                     convex_hull
) -> std::shared_ptr<erhe::physics::ICollision_shape>;

// Convenience wrapper: builds from the Mesh attachment of a node.
[[nodiscard]] auto build_shape_from_node_mesh(
    const erhe::scene::Node* node,
    bool                     convex_hull
) -> std::shared_ptr<erhe::physics::ICollision_shape>;

}
