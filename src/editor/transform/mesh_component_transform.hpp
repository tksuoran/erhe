#pragma once

#include "erhe_scene/mesh.hpp"

#include <geogram/basic/numeric.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace erhe           { class Item_host; }
namespace erhe::geometry { class Geometry; }

// Generated in config/generated/mesh_transform_mode.hpp (global scope, fixed underlying
// type). Forward-declared here so this header can hold the captured mode by value without
// pulling in the generated header.
enum class Mesh_transform_mode : unsigned int;

namespace editor {

class App_context;
class Corner_texcoord_change;
class Transform_tool_shared;
class Viewport_scene_view;

// The one-scalar component edits of doc/plans/mesh_modeling.md section 4.6 (D4):
// the scalar path of Mesh_component_transform.
enum class Scalar_edit_kind : unsigned int {
    edge_slide   = 0, // the vertices of the selected edge loops slide along their rails
    vertex_slide = 1  // each selected vertex slides toward one of its neighbours
};

[[nodiscard]] auto c_str(Scalar_edit_kind kind) -> const char*;

// One step of a scalar edit (Mesh_component_transform::apply_scalar()).
class Scalar_input
{
public:
    // Edge slide: [-1, 1] when clamped, positive toward the first rail side,
    // negative toward the second. Vertex slide: [0, 1] when clamped, 1 lands
    // on the chosen neighbour.
    float     factor {0.0f};
    // Every vertex moves the same distance: the factor times the active
    // vertex's rail (edge slide) or edge (vertex slide) length.
    bool      even   {false};
    // With even: that distance is measured from the far end of each rail.
    bool      flipped{false};
    // Off: the factor is not clamped; the edge slide keeps the side of the
    // last clamped step (extrapolates) and the vertex slide neighbours freeze.
    bool      clamp  {true};
    // Vertex slide only: when non-zero and clamped, each vertex re-picks the
    // neighbour whose direction has the largest dot product with this
    // (world space) direction before the step.
    glm::vec3 drag_direction_world{0.0f};
};

// The active slide vertex projected into a viewport (pixels, the space of
// Viewport_scene_view::project_to_viewport()): the start position and, for
// edge slide, the two rail ends; for vertex slide side_a is the chosen
// neighbour and side_b equals origin. A zero rail projects onto origin.
class Slide_screen_frame
{
public:
    glm::vec2 origin{0.0f};
    glm::vec2 side_a{0.0f};
    glm::vec2 side_b{0.0f};
};

// Drives the transform gizmo when a mesh component selection (vertex/edge/face) is
// active. The selection can span multiple meshes (one Group per live selection
// entry); the gizmo anchors to the combined centroid of all selected components and
// applies its world-from-anchor delta to each group's geometry vertices through that
// group's own node transform.
//
// Two-tier update:
//   - update_anchor(): each idle frame, recompute the affected vertices + combined
//     centroid and place the gizmo anchor (sets Transform_tool_shared::component_mode).
//   - begin()/apply()/commit(): during a drag or numeric edit, snapshot each group's
//     vertices once (begin), poke moved positions into the geometry + GPU vertex buffer
//     each update (apply, like Paint_tool - visual feedback only), and on release queue a
//     Move_mesh_vertices_operation per group (wrapped in a Compound_operation when more
//     than one) that does the authoritative rebuild and provides undo/redo (commit).
//
// Scalar path (doc/editor/transform.md "Scalar edits"): begin_scalar() builds the
// slide data of one Scalar_edit_kind, apply_scalar() places the vertices for one
// factor, commit() queues the same Move_mesh_vertices_operation (carrying the
// re-interpolated corner texcoords of a slide) and cancel() restores the drag-start
// state without queueing anything. While the transform mode is edge_slide /
// vertex_slide, begin() takes the scalar path too and apply() maps the gizmo
// translation to the factor.
class Mesh_component_transform
{
public:
    auto update_anchor(App_context& context, Transform_tool_shared& shared) -> bool;
    void begin        (App_context& context);
    void apply        (App_context& context, Transform_tool_shared& shared, const glm::mat4& updated_world_from_anchor);
    void commit       (App_context& context);

    // Restores the drag-start state of the active edit (positions, the before
    // primitive of an extruded or forked group - which makes the pre-edit
    // selection entry live again - and the GPU buffers) and queues nothing.
    // No-op when no edit is active.
    void cancel       (App_context& context);

    // Scalar path. begin_scalar() returns false (and leaves no edit active)
    // when the selection cannot slide: edge slide needs every selected vertex
    // on one or two selected edges and every selected edge manifold or
    // boundary; vertex slide needs a neighbour for every selected vertex. The
    // active slide vertex is the first one until select_active_slide_vertex()
    // picks another.
    auto begin_scalar (App_context& context, Scalar_edit_kind kind) -> bool;
    void apply_scalar (App_context& context, const Scalar_input& input);

    // Pointer drags: the slide vertex nearest to position_in_viewport becomes
    // the active one, and edge slide loops whose projected rail direction
    // opposes the active vertex's swap sides. False when nothing projects.
    auto select_active_slide_vertex(const Viewport_scene_view& view, glm::vec2 position_in_viewport) -> bool;
    // Vertex slide: each vertex picks the neighbour whose projected direction
    // has the largest dot product with drag_delta_in_viewport.
    void pick_vertex_slide_neighbours(const Viewport_scene_view& view, glm::vec2 drag_delta_in_viewport);
    [[nodiscard]] auto get_active_slide_screen_frame(const Viewport_scene_view& view, Slide_screen_frame& out) const -> bool;

    [[nodiscard]] auto is_active       () const -> bool { return m_active; }
    [[nodiscard]] auto is_scalar_active() const -> bool { return m_active && m_scalar; }
    [[nodiscard]] auto get_scalar_kind () const -> Scalar_edit_kind { return m_scalar_kind; }
    [[nodiscard]] auto get_slide_vertex_count() const -> std::size_t { return m_slide_vertices.size(); }
    [[nodiscard]] auto get_slide_loop_count  () const -> std::size_t { return m_slide_loop_swapped.size(); }
    // Slide / moved vertices whose position differs from the drag start.
    [[nodiscard]] auto count_moved_vertices  () const -> std::size_t;
    // True when the active edit has a group whose mesh is hosted by item_host.
    [[nodiscard]] auto references_item_host  (const erhe::Item_host* item_host) const -> bool;

private:
    // One selected mesh+primitive's affected vertices and the transforms needed to
    // move them. The geometry is held by shared_ptr for the duration of the drag so
    // it stays the same object (keeping the selection entry live).
    class Group
    {
    public:
        std::weak_ptr<erhe::scene::Mesh>          mesh;
        std::size_t                               primitive_index{0};
        std::shared_ptr<erhe::geometry::Geometry> geometry;       // the fork after fork-on-edit
        glm::mat4                                 world_from_node{1.0f};
        glm::mat4                                 node_from_world{1.0f};
        std::vector<GEO::index_t>                 vertices;     // unique affected
        std::vector<glm::vec3>                    before_local; // captured at begin(), parallel to vertices

        // The Primitive begin() bracketed with an optimization hold (null when
        // none was taken). Released via release_optimization_hold() on exactly
        // this object at commit(); fork/extrude transfer it to the swapped-in
        // primitive mid-drag.
        std::shared_ptr<erhe::primitive::Primitive> held_primitive;

        // Extrude-along-normal only: per moved vertex, the unit direction it slides along,
        // in WORLD space (parallel to `vertices`) - its disjoint subset's average normal
        // (group-normal mode) or its own original vertex normal (vertex-normal mode). The
        // drag slides each vertex along its own direction by a shared scalar amount instead
        // of applying the gizmo delta. Empty for plain move / plain extrude.
        std::vector<glm::vec3>                    move_directions;

        // Fork-on-edit (set the first time this group is forked during the drag).
        bool                          forked{false};
        erhe::scene::Mesh_primitive   fork_before;  // shared primitive (for the Fork op's undo)
        erhe::scene::Mesh_primitive   fork_after;   // forked primitive (for the Fork op's redo)

        // Extrude-on-first-move (set the first time this group is extruded during the
        // drag). The extrude builds a new Geometry (topology change), so `geometry` and
        // `vertices` are redirected to it and the commit swaps the whole primitive
        // (not an in-place vertex move).
        bool                          extruded{false};
        erhe::scene::Mesh_primitive   extrude_before; // original primitive (for undo)
        erhe::scene::Mesh_primitive   extrude_after;  // extruded primitive (for redo)

        // Scalar path: this group's slide vertices are
        // m_slide_vertices[slide_offset + i], parallel to `vertices`.
        std::size_t                   slide_offset{0};
    };

    // One vertex of a scalar edit. Edge slide: side[0] / side[1] are the
    // world-space offsets from the start position to the two rail ends (zero:
    // no rail on that side) and loop indexes m_slide_loop_swapped. Vertex
    // slide: the candidate neighbours are the world-space offsets
    // m_slide_neighbours[neighbour_begin, neighbour_begin + neighbour_count)
    // and side[0] is the chosen one.
    class Slide_vertex
    {
    public:
        std::size_t  group          {0};
        GEO::index_t vertex         {0};
        glm::vec3    start_world    {0.0f};
        glm::vec3    side[2]        {glm::vec3{0.0f}, glm::vec3{0.0f}};
        std::size_t  loop           {0};
        std::size_t  neighbour_begin{0};
        std::size_t  neighbour_count{0};
    };

    // Resolve the live component-selection entries into editable groups (one per
    // single-geometry, in-scene mesh). Returns false when no editable target exists.
    auto gather(App_context& context) -> bool;

    // Snapshot the drag-start state of every group (transforms, before_local)
    // and take the optimization holds. Shared by begin() and begin_scalar().
    void capture_start();

    // Builds the slide data of one group (section 4.6) from its live selection
    // entry and redirects group.vertices to the slide vertices; false when the
    // selection cannot slide.
    auto build_edge_slide  (App_context& context, std::size_t group_index) -> bool;
    auto build_vertex_slide(App_context& context, std::size_t group_index) -> bool;
    auto build_scalar      (App_context& context, Scalar_edit_kind kind) -> bool;
    // World-space orientation of the loops against the active vertex's rails.
    void align_slide_loops_world();
    // The factor a gizmo translation maps to (slide transform modes).
    [[nodiscard]] auto factor_from_translation(const glm::vec3& translation) -> float;
    // Every vertex slide vertex picks the neighbour nearest in direction.
    void pick_vertex_slide_neighbours_world(const glm::vec3& direction);

    // Fork-on-first-move of every group whose geometry is shared (fork mode).
    void fork_shared_groups(App_context& context);
    // Writes one vertex position into the geometry and the GPU buffers.
    void write_vertex(App_context& context, const Group& group, GEO::index_t vertex, const glm::vec3& local_position);
    // Re-samples the corner texcoords of the facets around this group's slid
    // vertices at their new positions (section 4.6 "correct UVs") into out.
    void collect_corrected_texcoords(const Group& group, std::vector<Corner_texcoord_change>& out);

    // True if any mesh OTHER than `mesh` references `geometry` in the scene.
    [[nodiscard]] auto is_geometry_shared(App_context& context, const std::shared_ptr<erhe::scene::Mesh>& mesh, const erhe::geometry::Geometry* geometry) const -> bool;

    // Deep-copy this group's geometry onto a new primitive for its mesh only, swap
    // it in, redirect the group + its component-selection entry to the fork.
    void fork_group(App_context& context, Group& group);

    // Build an extruded copy of this group's geometry (duplicate the selection
    // boundary, bridge with new faces), swap it onto a new primitive for this mesh,
    // and redirect the group + its component-selection entry + its moved-vertex set to
    // the extruded copy. Modeled on fork_group(); deferred to the first real move. In a
    // normal extrude mode (group / vertex), also fills group.move_directions (world-space
    // per-vertex normals) so apply() can slide each vertex along its own normal.
    void extrude_group(App_context& context, Group& group);

    void enqueue_gpu_position(App_context& context, const Group& group, GEO::index_t vertex, const glm::vec3& local_position);

    // Patch the edge-line vertex buffer endpoints for the edges incident to this moved
    // vertex, so the content wide-line renderer (which reads that separate buffer, not
    // the main vertex buffer) follows the drag live instead of snapping only on commit.
    void enqueue_gpu_edge_line_positions(App_context& context, const Group& group, GEO::index_t vertex, const glm::vec3& local_position);

    // Recompute and re-upload the content + smooth normal attributes for the faces
    // incident to this group's moved vertices, so involved faces (and the new faces
    // created by extrude) are shaded with valid normals during the active drag, matching
    // what the commit rebuild produces (so there is no shading pop on release).
    // Normal_source::stored_attributes instead writes what the primitive builder
    // wrote from the geometry's normal attributes: cancel() restores the built state.
    enum class Normal_source : unsigned int { live_positions = 0, stored_attributes = 1 };
    void update_group_normals(App_context& context, Group& group, Normal_source source);

    std::vector<Group>  m_groups;        // persistent scratch, cleared per gather()
    bool                m_active{false};
    bool                m_extrude{false};       // captured at begin(): mode is any Extrude (plain / group / vertex)
    Mesh_transform_mode m_transform_mode{};     // captured at begin(): the exact transform mode (value-inits to move)
    bool                m_scalar{false};        // the edit runs the scalar path (begin_scalar(), or a slide transform mode)
    Scalar_edit_kind    m_scalar_kind{Scalar_edit_kind::edge_slide};
    std::size_t         m_slide_active{0};      // index into m_slide_vertices
    unsigned int        m_slide_last_side{0};   // edge slide: the side of the last clamped step
    bool                m_slide_neighbours_picked{false}; // vertex slide: a pick has run

    // Scalar path data, cleared and refilled per begin_scalar() (capacity kept).
    std::vector<Slide_vertex>  m_slide_vertices;
    std::vector<glm::vec3>     m_slide_neighbours;
    std::vector<std::uint8_t>  m_slide_loop_swapped;
    // Scalar path build scratch (cleared at use, capacity kept).
    std::vector<std::pair<GEO::index_t, GEO::index_t>> m_slide_edge_pairs;      // selected edges, both directions, sorted
    std::vector<GEO::index_t>                          m_slide_unique_vertices; // sorted
    std::vector<std::uint8_t>                          m_slide_visited;         // parallel to m_slide_unique_vertices
    std::vector<GEO::index_t>                          m_slide_loop_vertices;   // one loop, in walk order
    std::vector<GEO::index_t>                          m_slide_loop_facets;     // two side facets per loop edge
    // Commit-time "correct UVs" scratch.
    std::vector<std::pair<GEO::index_t, std::size_t>>  m_texcoord_lookup;       // (vertex, index in group), sorted
    std::vector<glm::vec2>                             m_texcoord_polygon;
    std::vector<float>                                 m_texcoord_weights;

    // Per-frame scratch for update_group_normals(), kept across frames so the live
    // normal update performs no steady-state heap allocation (cleared, capacity kept).
    std::vector<GEO::index_t>                 m_normal_scratch_facets;
    std::unordered_map<GEO::index_t, glm::vec3> m_normal_smooth_cache;
};

}
