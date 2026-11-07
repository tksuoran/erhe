#pragma once

#include "erhe_primitive/lod.hpp"
#include "erhe_scene/mesh.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
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

// The primitives one mesh held before an in-place edit swapped them.
class Retained_mesh_primitives
{
public:
    std::shared_ptr<erhe::scene::Mesh>       mesh;
    std::vector<erhe::scene::Mesh_primitive> primitives;
};

// Shares one rebuilt Primitive of an (unchanged) Geometry across every mesh of
// the mesh's scene that references the Geometry, preserving each mesh's own
// material, so shared-geometry instances change and revert together. For each
// referencing mesh: swap_mesh_primitives(), then after_swap (when set), then
// Mesh_geometry_changed_message. out_retained (when set) receives each
// mesh's primitives from before its swap, in swap order, for
// restore_mesh_primitives(). The caller holds the item host mutex.
void share_rebuilt_primitive(
    App_context&                                       context,
    const std::shared_ptr<erhe::scene::Mesh>&          mesh,
    const erhe::geometry::Geometry*                    geometry,
    const std::shared_ptr<erhe::primitive::Primitive>& new_primitive,
    const std::function<void(erhe::scene::Mesh&)>&     after_swap   = {},
    std::vector<Retained_mesh_primitives>*             out_retained = nullptr
);

// The undo of share_rebuilt_primitive(): swaps every retained mesh back to the
// primitives it held, then after_swap (when set, with the index into
// `retained`), then Mesh_geometry_changed_message - the notifications of the
// edit. The caller holds the item host mutex.
void restore_mesh_primitives(
    App_context&                                                 context,
    const std::vector<Retained_mesh_primitives>&                 retained,
    const std::function<void(erhe::scene::Mesh&, std::size_t)>& after_swap = {}
);

// The LOD request (Primitive::lod_settings) of the mesh's primitive built from
// `geometry`, for an edit that rebuilds that primitive as a new Primitive: the
// new one takes the request over before it is built, so its build (or the
// re-optimization after the edit) regenerates the chain
// (doc/erhe/meshoptimizer_integration.md "Level of detail"). nullopt when no
// primitive of the mesh is built from the geometry or it has no request. The
// caller holds the item host mutex.
[[nodiscard]] auto get_geometry_lod_settings(
    const erhe::scene::Mesh&        mesh,
    const erhe::geometry::Geometry* geometry
) -> std::optional<erhe::primitive::Lod_settings>;

} // namespace editor
