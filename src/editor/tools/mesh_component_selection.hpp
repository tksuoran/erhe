#pragma once

#include "app_message.hpp"

#include "erhe_geometry/topology.hpp" // Edge_loop_delimit, Region_delimit
#include "erhe_message_bus/message_bus.hpp"

#include <geogram/basic/numeric.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <span>
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
    face   = 3,
    // Skeleton bones. Not a mesh sub-component at all, but it shares this enum
    // so exactly one selection granularity is active at a time and a viewport
    // click has a single unambiguous owner. Mesh_component_selection_tool
    // ignores it; Bone_visualization and the object Selection handle it.
    bone   = 4
};

[[nodiscard]] auto c_str(Mesh_component_mode mode) -> const char*;

// True only for the granularities that address mesh sub-components
// (vertex / edge / face). Object and bone are both "not a mesh component mode":
// object is handled by the object Selection, bone by the bone selection path.
// Guards must test this rather than `== object`, or bone would fall through into
// the mesh-component machinery.
[[nodiscard]] auto is_mesh_component_mode(Mesh_component_mode mode) -> bool;

// How a mode switch converts the selection (doc/editor/mesh_component_selection.md
// "Flush and mode conversion").
//   flush  - keep the new mode's set and derive the other two from it
//   expand - going up (vertex -> edge -> face) select every element touching the
//            old mode's selection; going down keep only the elements completely
//            surrounded by it; then flush
enum class Mode_conversion : unsigned int {
    flush  = 0,
    expand = 1
};

[[nodiscard]] auto c_str(Mode_conversion conversion) -> const char*;

// Canonical undirected edge key: (min vertex, max vertex), so the same edge
// reached from either adjacent facet maps to a single entry.
using Mesh_edge_key = std::pair<GEO::index_t, GEO::index_t>;
[[nodiscard]] auto make_edge_key(GEO::index_t a, GEO::index_t b) -> Mesh_edge_key;

// The walk a loop / ring select runs from a picked edge
// (doc/plans/mesh_modeling.md section 4.1). The edge kinds yield edges and
// apply in vertex and edge mode; face_loop yields facets and applies in face
// mode.
enum class Loop_kind : unsigned int {
    edge_loop     = 0,
    edge_ring     = 1,
    boundary_loop = 2,
    face_loop     = 3
};

[[nodiscard]] auto c_str(Loop_kind kind) -> const char*;

// How a loop select combines with the current selection.
//   replace  - clear the whole selection, then select the walked elements
//   extend   - add the walked elements
//   deselect - remove the walked elements
enum class Select_action : unsigned int {
    replace  = 0,
    extend   = 1,
    deselect = 2
};

[[nodiscard]] auto c_str(Select_action action) -> const char*;

// Runs the `kind` walk of erhe_geometry/topology.hpp from the edge edge_key
// names, filling out_elements (cleared, capacity kept) with edge indices, or
// facet indices for face_loop. The delimit applies to edge_loop only. Returns
// false, leaving out_elements empty, when the geometry lacks vertex or edge
// connectivity or edge_key is not an edge of it. Logs nothing: the callers
// decide whether a missing walk is worth a warning.
auto walk_mesh_loop(
    const erhe::geometry::Geometry&   geometry,
    Mesh_edge_key                     edge_key,
    Loop_kind                         kind,
    erhe::geometry::Edge_loop_delimit delimit,
    std::vector<GEO::index_t>&        out_elements
) -> bool;

class Mesh_component_selection;

// A set of selected component keys whose every write tells the owning
// Mesh_component_selection, so a change is announced
// (Mesh_component_selection_changed_message) by construction, whichever of the
// many editing sites (tool clicks, region select, grow / shrink, MCP,
// operations remapping the selection) made it. Reads behave like the
// std::set it wraps; a set without an owner (a detached copy) announces
// nothing.
template <typename Key>
class Component_set
{
public:
    using Set            = std::set<Key>;
    using const_iterator = typename Set::const_iterator;

    Component_set() = default;
    Component_set(const Component_set& other) = default;
    Component_set(Component_set&& other) noexcept = default;
    ~Component_set() noexcept = default;

    // Assignment replaces the keys and keeps this set's owner. A move from
    // another Component_set is the entry vector reorganizing itself (erase_if
    // in prune(), reallocation) and announces nothing; every other write does.
    auto operator=(const Component_set& other) -> Component_set&;
    auto operator=(Component_set&& other) noexcept -> Component_set&;
    auto operator=(const Set& keys) -> Component_set&;
    auto operator=(Set&& keys) -> Component_set&;

    void set_owner(Mesh_component_selection* owner) { m_owner = owner; }

    [[nodiscard]] auto begin   () const -> const_iterator { return m_keys.begin(); }
    [[nodiscard]] auto end     () const -> const_iterator { return m_keys.end(); }
    [[nodiscard]] auto size    () const -> std::size_t    { return m_keys.size(); }
    [[nodiscard]] auto empty   () const -> bool           { return m_keys.empty(); }
    [[nodiscard]] auto contains(const Key& key) const -> bool           { return m_keys.contains(key); }
    [[nodiscard]] auto find    (const Key& key) const -> const_iterator { return m_keys.find(key); }
    [[nodiscard]] auto get     () const -> const Set&     { return m_keys; }
    operator const Set&() const { return m_keys; }

    auto insert(const Key& key) -> bool;
    auto erase (const Key& key) -> bool;
    void clear ();

private:
    void changed();

    Set                       m_keys {};
    Mesh_component_selection* m_owner{nullptr};
};

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
    Component_set<GEO::index_t>             vertices       {};
    Component_set<GEO::index_t>             facets         {};
    Component_set<Mesh_edge_key>            edges          {};

    [[nodiscard]] auto is_empty() const -> bool;
    void               clear();
    void               set_owner(Mesh_component_selection* owner);

    void add_vertex   (GEO::index_t vertex);
    void toggle_vertex(GEO::index_t vertex);
    void add_facet    (GEO::index_t facet);
    void toggle_facet (GEO::index_t facet);
    void add_edge     (GEO::index_t a, GEO::index_t b);
    void toggle_edge  (GEO::index_t a, GEO::index_t b);
};

// One (mesh, primitive, Geometry) the selection commands act on (select all).
class Mesh_component_target
{
public:
    std::shared_ptr<erhe::scene::Mesh>        mesh           {};
    std::size_t                               primitive_index{0};
    std::shared_ptr<erhe::geometry::Geometry> geometry       {};
};

// Appends one target per primitive of `mesh` that component selection can
// address: scene content, not skinned, not lock_edit, no separate collision
// shape, and a Geometry already published on its raytrace shape (never built
// here). Returns the number of targets appended.
auto append_mesh_component_targets(
    const std::shared_ptr<erhe::scene::Mesh>& mesh,
    std::vector<Mesh_component_target>&       out_targets
) -> std::size_t;

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
    // Publishes Mesh_component_mode_changed_message when the mode actually
    // changes (Bone_visualization gates proxy visibility / pickability on it).
    // Switching into a component mode converts every live entry first (flush:
    // keep the new mode's set and derive the rest; expand: see
    // Mode_conversion), so subscribers see the converted selection.
    void               set_mode(Mesh_component_mode mode, Mode_conversion conversion = Mode_conversion::flush);

    // Derive, for every live entry, the two sets other than the current mode's
    // from the current mode's set: vertex mode keeps the vertices, edges are
    // those with both vertices selected, facets those with all vertices
    // selected; edge mode keeps the edges, vertices are their end points,
    // facets those with all edges selected; face mode keeps the facets and
    // selects their edges and vertices. No-op outside component modes. Every
    // selection command calls it after its write.
    void               flush();

    // Select every element of every target in the current mode, then flush.
    void               select_all(std::span<const Mesh_component_target> targets);
    // Deselect everything (every entry, live or not).
    void               select_none();
    // Replace the current mode's set of every live entry with its complement,
    // then flush.
    void               invert();
    // Add the connected region (erhe::geometry::walk_connected_region) grown
    // from the vertices of every selected element of every live entry, then
    // flush. Face mode adds the facets whose vertices all lie in the region.
    // Entries whose Geometry lacks connectivity are skipped with a warning.
    void               select_linked_from_selection(erhe::geometry::Region_delimit delimit = erhe::geometry::Region_delimit::none);
    // Add the connected region grown from seed_vertices of one target, as
    // select_linked_from_selection does for each entry. Returns false (and
    // changes nothing) when the target's Geometry lacks connectivity.
    auto               select_linked(
        const Mesh_component_target&   target,
        std::span<const GEO::index_t>  seed_vertices,
        erhe::geometry::Region_delimit delimit = erhe::geometry::Region_delimit::none
    ) -> bool;

    // Loop / ring select (doc/plans/mesh_modeling.md section 4.2): runs the
    // `kind` walk from edge_key on the target's Geometry and applies `action`
    // to the current mode's set of the target's entry, then flushes. Vertex
    // mode takes the walked edges' vertices, edge mode the walked edges, face
    // mode the walked facets; the edge kinds apply in vertex and edge mode,
    // face_loop in face mode only. Returns the number of walked elements, 0
    // (changing nothing) when the kind does not apply in the current mode, the
    // walk is empty, or the Geometry lacks connectivity (logged as a warning).
    auto select_loop(
        const Mesh_component_target&      target,
        Mesh_edge_key                     edge_key,
        Loop_kind                         kind,
        Select_action                     action,
        erhe::geometry::Edge_loop_delimit delimit
    ) -> std::size_t;
    // True when the walk select_loop would run is non-empty and every element
    // it would select is already selected in the target's entry.
    [[nodiscard]] auto is_loop_selected(
        const Mesh_component_target&      target,
        Mesh_edge_key                     edge_key,
        Loop_kind                         kind,
        erhe::geometry::Edge_loop_delimit delimit
    ) -> bool;

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

    // Blender-style Select More / Select Less. Expand (grow) or contract (shrink)
    // the selection by one ring of border components, in the current mode, for
    // every live entry. Grow adds the immediate neighbors of the selection's
    // boundary; shrink drops the components that lie on that boundary. No-op in
    // object mode and for non-live entries. Like the other selection mutators
    // (clear_all / set_after_operation), these are not undoable; the change is
    // announced through Component_set (Mesh_component_selection_changed_message).
    void grow();
    void shrink();

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
    // The same test for a (mesh, primitive index, Geometry) held elsewhere.
    [[nodiscard]] auto is_live(
        const std::shared_ptr<erhe::scene::Mesh>&        mesh,
        std::size_t                                      primitive_index,
        const std::shared_ptr<erhe::geometry::Geometry>& geometry
    ) const -> bool;

    [[nodiscard]] auto get_entries()       ->       std::vector<Mesh_component_entry>&;
    [[nodiscard]] auto get_entries() const -> const std::vector<Mesh_component_entry>&;

    // Called by Component_set on every write, and by the entry list edits
    // (clear_all, prune). Queues one Mesh_component_selection_changed_message
    // per message bus update however many writes happen before it.
    void on_components_changed();

private:
    void on_mesh_geometry_changed(Mesh_geometry_changed_message& message);
    // Adds the region grown from m_seed_vertices to the entry's current mode set.
    auto add_linked_region(
        Mesh_component_entry&           entry,
        const erhe::geometry::Geometry& geometry,
        erhe::geometry::Region_delimit  delimit
    ) -> bool;

    erhe::message_bus::Subscription<Mesh_geometry_changed_message>             m_mesh_geometry_changed_subscription;
    erhe::message_bus::Subscription<Mesh_component_selection_changed_message> m_selection_changed_subscription;
    App_message_bus&                  m_app_message_bus;
    Mesh_component_mode               m_mode   {Mesh_component_mode::object};
    std::vector<Mesh_component_entry> m_entries{};
    bool                              m_change_pending{false};

    // Runs walk_mesh_loop into m_loop_elements when `kind` applies in the
    // current mode; logs a warning when the Geometry lacks connectivity.
    auto walk_loop(
        const Mesh_component_target&      target,
        Mesh_edge_key                     edge_key,
        Loop_kind                         kind,
        erhe::geometry::Edge_loop_delimit delimit
    ) -> bool;

    // Loop select scratch (cleared at use, capacity kept).
    std::vector<GEO::index_t>         m_loop_elements{};

    // Select linked scratch (cleared at use, capacity kept).
    std::vector<GEO::index_t>         m_seed_vertices  {};
    std::vector<GEO::index_t>         m_region_vertices{};
    std::vector<std::uint8_t>         m_region_marks   {};
};

template <typename Key>
auto Component_set<Key>::operator=(const Component_set& other) -> Component_set&
{
    if (this != &other) {
        m_keys = other.m_keys;
        changed();
    }
    return *this;
}

template <typename Key>
auto Component_set<Key>::operator=(Component_set&& other) noexcept -> Component_set&
{
    if (this != &other) {
        m_keys = std::move(other.m_keys);
    }
    return *this;
}

template <typename Key>
auto Component_set<Key>::operator=(const Set& keys) -> Component_set&
{
    m_keys = keys;
    changed();
    return *this;
}

template <typename Key>
auto Component_set<Key>::operator=(Set&& keys) -> Component_set&
{
    m_keys = std::move(keys);
    changed();
    return *this;
}

template <typename Key>
auto Component_set<Key>::insert(const Key& key) -> bool
{
    const bool inserted = m_keys.insert(key).second;
    if (inserted) {
        changed();
    }
    return inserted;
}

template <typename Key>
auto Component_set<Key>::erase(const Key& key) -> bool
{
    const bool erased = (m_keys.erase(key) != 0);
    if (erased) {
        changed();
    }
    return erased;
}

template <typename Key>
void Component_set<Key>::clear()
{
    if (!m_keys.empty()) {
        m_keys.clear();
        changed();
    }
}

template <typename Key>
void Component_set<Key>::changed()
{
    if (m_owner != nullptr) {
        m_owner->on_components_changed();
    }
}

} // namespace editor
