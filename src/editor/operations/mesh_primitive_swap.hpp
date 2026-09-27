#pragma once

#include "erhe_scene/mesh.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace erhe::geometry  { class Geometry; }
namespace erhe::primitive { class Primitive; }

namespace editor {

class App_context;

// Replaces a hosted mesh's primitives in place. The node keeps its parent and
// its position among its siblings: the raytrace instances follow the new
// primitives through the Scene_root rt-update brackets instead of a
// detach / re-attach of the node. Physics is the caller's (see
// Mesh_operation::restore_physics). The caller holds the item host mutex.
void swap_mesh_primitives(
    const std::shared_ptr<erhe::scene::Mesh>&       mesh,
    const std::vector<erhe::scene::Mesh_primitive>& primitives
);

// Shares one rebuilt Primitive of an (unchanged) Geometry across every mesh of
// the mesh's scene that references the Geometry, preserving each mesh's own
// material, so shared-geometry instances change and revert together. For each
// referencing mesh: swap_mesh_primitives(), then after_swap (when set), then
// Mesh_geometry_changed_message. The caller holds the item host mutex.
void share_rebuilt_primitive(
    App_context&                                       context,
    const std::shared_ptr<erhe::scene::Mesh>&          mesh,
    const erhe::geometry::Geometry*                    geometry,
    const std::shared_ptr<erhe::primitive::Primitive>& new_primitive,
    const std::function<void(erhe::scene::Mesh&)>&     after_swap = {}
);

} // namespace editor
