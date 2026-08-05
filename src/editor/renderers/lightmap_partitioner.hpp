#pragma once

#include "renderers/lightmap_baker.hpp"

#include "erhe_geometry/operation/clip_tile_tree.hpp"
#include "erhe_geometry/operation/make_atlas.hpp"

#include <glm/glm.hpp>

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace erhe::scene {
    class Mesh;
    class Node;
}

namespace editor {

class App_context;
class Scene_root;

// World-space lightmap mesh partitioner (doc/lightmap_baking_plan.md).
//
// prepare() is self-contained: it computes a geometry-only spatial split
// estimate (Lightmap_baker::compute_tile_split_estimate - no unwrap or
// prior layout needed, originals' primitives are never modified), then
// makes every lightmapped mesh/primitive instance unique and bakes it into
// world space: per instance, the node transform is baked into the geometry,
// the result is clipped against the estimated kd tile tree (clip vertices
// shared binary exact across tile boundaries - see
// erhe::geometry::operation::clip_by_tile_tree), and each piece gets fresh
// channel-2 UVs (per-piece make_atlas at world-space density). The pieces
// become Mesh_primitives of a new mesh on an identity-transform node under
// the "Lightmap Pieces" group, so each piece can carry its own
// lightmap_uv_scale_offset (per-tile residency) without renderer changes.
//
// The original meshes stay in the scene (and their primitives are retained
// here) so the partition can be reverted or re-prepared with different
// parameters. "Render with lightmaps" flips visibility between originals
// and pieces.
class Lightmap_partitioner
{
public:
    explicit Lightmap_partitioner(App_context& context);
    ~Lightmap_partitioner(); // cancels + waits an in-flight prepare job

    class Params
    {
    public:
        // Per-facet chart order keys for the piece unwraps (Reorder Charts
        // By Bake): baked-luminance keys measured on the CURRENT layout's
        // piece geometries, keyed by piece identity (source mesh, source
        // primitive index, grid tile). Clipping is deterministic for
        // unchanged sources and make_atlas preserves facet order, so keys
        // indexed by the old piece's facet ids apply to the re-clipped
        // piece. Pieces without keys unwrap in default order. Per-facet
        // parameterizer only; ignored otherwise.
        using Piece_identity = std::tuple<const erhe::scene::Mesh*, std::size_t, int>;
        using Chart_order    = std::map<Piece_identity, std::vector<float>>;

        float                                          min_face_texels  {2.0f};
        float                                          hard_angles_deg  {60.0f};
        float                                          gutter_texels    {2.0f};
        float                                          min_chart_texels {2.0f};
        erhe::geometry::operation::Atlas_parameterizer parameterizer    {erhe::geometry::operation::Atlas_parameterizer::per_facet};
        erhe::geometry::operation::Atlas_packer        packer           {erhe::geometry::operation::Atlas_packer::xatlas};
        std::shared_ptr<const Chart_order>             chart_order      {};
    };

    class Piece_info
    {
    public:
        int         tile                  {-1};
        std::size_t source_primitive_index{0};
        int         ordinal               {0}; // piece index within (source mesh, source primitive)
    };

    class Original_entry
    {
    public:
        std::shared_ptr<erhe::scene::Mesh> original_mesh;
        glm::mat4                          world_from_node_at_clip{1.0f};
        // Source geometry identity at clip time, per source primitive index:
        // a mesh operation swapping a primitive makes the pieces stale just
        // like a transform move (count_stale_sources).
        std::vector<std::pair<std::size_t, const erhe::geometry::Geometry*>> source_geometries_at_clip;
        std::shared_ptr<erhe::scene::Node> piece_node;   // identity transform
        std::shared_ptr<erhe::scene::Mesh> piece_mesh;   // one Mesh_primitive per piece
        std::vector<Piece_info>            pieces;       // parallel to piece_mesh primitives
    };

    // ---- Async prepare pipeline ----
    // request_prepare snapshots everything on the main thread (tile config,
    // geometry-only split estimate, per-region tasks) and launches the heavy
    // phase - per-region world-space bake + clip, per-piece unwrap +
    // primitive build - on the app executor. No preconditions; the old
    // partition stays live until the commit. update() (editor tick, after
    // the operation stack) commits when the job finishes: it validates the
    // job against the live scene (a source primitive swapped or a mesh
    // removed mid-flight aborts the commit and keeps the old partition;
    // transform moves are tolerated - count_stale_transforms reports them),
    // then atomically swaps the partition and runs one partitioned relayout
    // + publish. m_context.pending_async_ops is held for the whole flight,
    // so every existing async_busy gate also covers an in-flight prepare.
    // Failures/cancellation are reported via Lightmap_report
    // (Stage::partition) and get_last_prepare_result().

    class Prepare_progress
    {
    public:
        bool        in_flight       {false};
        std::size_t regions_done    {0};
        std::size_t regions_total   {0};
        bool        cancel_requested{false};
    };
    class Prepare_result
    {
    public:
        bool        committed  {false};
        std::size_t mesh_count {0};
        std::size_t piece_count{0};
        int         tile_count {0};
        std::string abort_reason; // empty on success
    };

    // Launch; false = nothing to partition / already in flight / async
    // mesh operations in flight (reported). With no executor (or a single
    // region) runs synchronously and commits before returning.
    auto request_prepare(Scene_root& scene_root, const Params& params, int tile_texture_size, int resident_tile_budget) -> bool;

    // Per-frame driver (editor tick, after the operation stack + transform
    // updates): commits or discards a finished job. No-op otherwise.
    void update();

    // Request cancellation of the in-flight job; update() discards the
    // results on completion (the old partition is kept). No-op when idle.
    void cancel_prepare();

    // Blocking convenience (MCP wait mode, tests): request_prepare + wait +
    // commit; true = committed.
    auto prepare(Scene_root& scene_root, const Params& params, int tile_texture_size, int resident_tile_budget) -> bool;

    [[nodiscard]] auto is_prepare_in_flight   () const -> bool { return m_prepare_job != nullptr; }
    [[nodiscard]] auto get_prepare_progress   () const -> Prepare_progress;
    [[nodiscard]] auto get_last_prepare_result() const -> const Prepare_result& { return m_last_prepare_result; }

    // Destroys the piece nodes, restores original mesh visibility and
    // clears the store.
    void revert();

    // ON: piece meshes render (all of them - non-resident tiles fall back
    // to white), originals hidden. OFF: originals render, pieces hidden.
    void set_render_with_lightmaps(bool enabled);
    [[nodiscard]] auto get_render_with_lightmaps() const -> bool { return m_render_with_lightmaps; }

    [[nodiscard]] auto is_prepared() const -> bool { return !m_entries.empty(); }
    // How many original meshes moved OR had a source primitive's geometry
    // swapped since the clip (their pieces are stale world-space geometry
    // until re-prepare). Queried by the Lightmap window's World-Space Tiles
    // section and by its auto-re-prepare debounce.
    [[nodiscard]] auto count_stale_sources() const -> std::size_t;
    // FNV hash of the stale inputs (world transforms + source geometry
    // pointers) - the auto-re-prepare debounce fires once this stops
    // changing (the drag / operation settled).
    [[nodiscard]] auto get_source_state_hash() const -> uint64_t;
    [[nodiscard]] auto is_piece_mesh(const erhe::scene::Mesh* mesh) const -> bool
    {
        return m_piece_meshes.contains(mesh);
    }
    [[nodiscard]] auto get_entries() const -> const std::vector<Original_entry>& { return m_entries; }
    [[nodiscard]] auto get_scene_root() const -> Scene_root* { return m_scene_root; }
    // The kd tile tree the pieces were clipped against (the grid split
    // computed at prepare time), its tile count, and the per-tile grid
    // metadata (key, cell bounds, nominal texel density) the partitioned
    // relayout re-instantiates its tiles from.
    [[nodiscard]] auto get_tile_tree() const -> const std::vector<erhe::geometry::operation::Clip_tree_node>& { return m_tile_tree; }
    [[nodiscard]] auto get_tile_count() const -> int { return m_tile_count; }
    [[nodiscard]] auto get_tile_descs() const -> const std::vector<Lightmap_baker::Tile>& { return m_tile_descs; }

    // Resolves a manifest piece identity (SOURCE mesh identity + tile +
    // ordinal, see Lightmap_tile_io) to the live piece mesh and its
    // Mesh_primitive index. The original mesh is matched by node index path
    // when one is given (unique even for duplicate names), falling back to
    // node path + mesh name. Returns {nullptr, 0} when unresolved.
    [[nodiscard]] auto find_piece(
        const std::string& node_path,
        const std::string& node_index_path,
        const std::string& mesh_name,
        std::size_t        source_primitive_index,
        int                tile,
        int                ordinal
    ) const -> std::pair<erhe::scene::Mesh*, std::size_t>;

    // Drops all references without touching the scene - the scene is being
    // closed (see AGENTS.md "Scene-hosted references in editor parts").
    void on_scene_closed(const Scene_root* scene_root);

private:
    class Prepare_job; // cpp-local: snapshot + taskflow + progress atomics

    void apply_visibility();
    // Factored halves of revert(), reused by commit_prepare(): restore the
    // originals' visibility and detach the piece/group nodes (locks the
    // scene's item_host_mutex itself) / reset the store members
    // (m_last_params is kept - revert()'s trailing relayout uses it).
    void teardown_scene_state();
    void clear_store();
    // Empty string = the live scene still matches the job's snapshot.
    [[nodiscard]] auto validate_job_against_scene(const Prepare_job& job) const -> std::string;
    // Consume the finished job: validate -> assemble -> swap the partition
    // -> one relayout + publish + streamer invalidate. Keeps the old
    // partition on abort. Pre: m_prepare_job set, its future ready.
    void commit_prepare();
    // Cancel / scene-close path: report, record the result, release the
    // pending counter and drop the job.
    void finish_prepare_discard(const char* reason);

    App_context&                                            m_context;
    Scene_root*                                             m_scene_root{nullptr};
    std::vector<Original_entry>                             m_entries;
    std::unordered_set<const erhe::scene::Mesh*>            m_piece_meshes;
    std::shared_ptr<erhe::scene::Node>                      m_group_node;
    std::vector<erhe::geometry::operation::Clip_tree_node>  m_tile_tree;
    int                                                     m_tile_count{0};
    std::vector<Lightmap_baker::Tile>                       m_tile_descs;
    bool                                                    m_render_with_lightmaps{false};
    Params                                                  m_last_params{};
    // Job lifetime invariant: the taskflow lambdas capture the raw
    // Prepare_job*, so the job must NEVER be destroyed before its future is
    // ready. The only consumption sites - update() (ready-checked),
    // on_scene_closed() and the destructor (both cancel + wait first) -
    // uphold this; keep it that way.
    std::unique_ptr<Prepare_job>                            m_prepare_job;
    Prepare_result                                          m_last_prepare_result{};
};

} // namespace editor
