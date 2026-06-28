#pragma once

#include "app_message.hpp"

#include "erhe_message_bus/message_bus.hpp"

#include <geogram/basic/numeric.h>

#include <cstddef>
#include <limits>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace erhe::geometry { class Geometry; }
namespace erhe::scene    { class Mesh; }

namespace editor {

class App_message_bus;

// Selection granularity for the Mesh_component_selection_tool.
//   object - whole mesh / node selection (the existing Selection handles it)
//   vertex - mesh vertices (points)
//   edge   - mesh edges (vertex pairs)
//   face   - mesh facets (polygons)
enum class Mesh_component_mode {
    object = 0,
    vertex = 1,
    edge   = 2,
    face   = 3
};

[[nodiscard]] auto c_str(Mesh_component_mode mode) -> const char*;

// Canonical undirected edge key: (min vertex, max vertex), so the same edge
// reached from either adjacent facet maps to a single entry.
using Mesh_edge_key = std::pair<GEO::index_t, GEO::index_t>;
[[nodiscard]] auto make_edge_key(GEO::index_t a, GEO::index_t b) -> Mesh_edge_key;

// One mesh+primitive's selected sub-components, addressed by the Geometry the
// indices index into. The selection is "live" only while the mesh is in the
// scene and the primitive still carries this exact Geometry object. A geometry
// swap (Catmull-Clark, Conway, merge, ...) makes the entry dormant-but-retained,
// so undo - which restores the original Geometry - makes it live again
// automatically; a scene removal makes the mesh fail the in-scene test so the
// entry is simply not rendered (no ghost). Storing the Geometry identity per
// entry (rather than the indices alone) is what addresses both lifecycles
// without keeping any selection state in the content.
class Mesh_component_entry
{
public:
    std::weak_ptr<erhe::scene::Mesh>        mesh           {};
    std::size_t                             primitive_index{std::numeric_limits<std::size_t>::max()};
    std::weak_ptr<erhe::geometry::Geometry> geometry       {};
    std::set<GEO::index_t>                  vertices       {};
    std::set<GEO::index_t>                  facets         {};
    std::set<Mesh_edge_key>                 edges          {};

    [[nodiscard]] auto is_empty() const -> bool;
    void               clear();

    void add_vertex   (GEO::index_t vertex);
    void toggle_vertex(GEO::index_t vertex);
    void add_facet    (GEO::index_t facet);
    void toggle_facet (GEO::index_t facet);
    void add_edge     (GEO::index_t a, GEO::index_t b);
    void toggle_edge  (GEO::index_t a, GEO::index_t b);
};

// Editor-side, content-addressed store of mesh sub-component selections. Entries
// are keyed by per-instance content identity (mesh + primitive index + Geometry),
// so a selection survives geometry swaps and scene add/remove through undo/redo
// without being stored in the content itself (which would contaminate
// serialization, dirty the document on selection, and bleed across instances that
// share a Geometry). Owned by the editor, reachable via App_context, so both the
// object Selection (which defers to it when a component mode is active) and the
// transform tool can read it without depending on the selection tool.
class Mesh_component_selection
{
public:
    explicit Mesh_component_selection(App_message_bus& app_message_bus);

    [[nodiscard]] auto get_mode() const -> Mesh_component_mode;
    void               set_mode(Mesh_component_mode mode);

    // Entry lookup keyed by (mesh, primitive_index, geometry).
    [[nodiscard]] auto find_entry(
        const std::shared_ptr<erhe::scene::Mesh>&        mesh,
        std::size_t                                      primitive_index,
        const std::shared_ptr<erhe::geometry::Geometry>& geometry
    ) -> Mesh_component_entry*;
    auto               find_or_create_entry(
        const std::shared_ptr<erhe::scene::Mesh>&        mesh,
        std::size_t                                      primitive_index,
        const std::shared_ptr<erhe::geometry::Geometry>& geometry
    ) -> Mesh_component_entry&;

    void               clear_all();
    [[nodiscard]] auto is_empty() const -> bool;

    // Install the post-operation component selection for (mesh, primitive_index) on
    // the operation's result Geometry. Called on the main thread after a topology
    // operation swaps in new geometry, so the selection follows the change onto the
    // newly created components. Idempotent across redo (keyed by the result Geometry
    // identity via find_or_create_entry). The pre-operation entry is left in place
    // (dormant) so undo revives it automatically. No-op when all sets are empty.
    void set_after_operation(
        const std::shared_ptr<erhe::scene::Mesh>&        mesh,
        std::size_t                                      primitive_index,
        const std::shared_ptr<erhe::geometry::Geometry>& after_geometry,
        const std::set<GEO::index_t>&                    vertices,
        const std::set<GEO::index_t>&                    facets,
        const std::set<Mesh_edge_key>&                   edges
    );

    // Drop entries whose mesh or geometry has been freed, or that hold no
    // components. Dormant-but-alive entries (geometry not currently bound but
    // still referenced, e.g. retained by an undo operation) are kept, so undo can
    // make them live again.
    void               prune();

    // An entry is live when its mesh is in the scene (node attached to an item
    // host) and the primitive still carries this exact Geometry object.
    [[nodiscard]] auto is_live(const Mesh_component_entry& entry) const -> bool;

    [[nodiscard]] auto get_entries()       ->       std::vector<Mesh_component_entry>&;
    [[nodiscard]] auto get_entries() const -> const std::vector<Mesh_component_entry>&;

private:
    void on_mesh_geometry_changed(Mesh_geometry_changed_message& message);

    erhe::message_bus::Subscription<Mesh_geometry_changed_message> m_mesh_geometry_changed_subscription;
    Mesh_component_mode               m_mode   {Mesh_component_mode::object};
    std::vector<Mesh_component_entry> m_entries{};
};

} // namespace editor
