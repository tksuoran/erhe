#pragma once

#include "erhe_geometry/operation/clip_tile_tree.hpp"
#include "erhe_geometry/operation/make_atlas.hpp"

#include <glm/glm.hpp>

#include <memory>
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

    class Params
    {
    public:
        float                                          texels_per_meter {8.0f};
        float                                          min_face_texels  {2.0f};
        float                                          hard_angles_deg  {60.0f};
        float                                          gutter_texels    {2.0f};
        float                                          min_chart_texels {2.0f};
        erhe::geometry::operation::Atlas_parameterizer parameterizer    {erhe::geometry::operation::Atlas_parameterizer::per_facet};
        erhe::geometry::operation::Atlas_packer        packer           {erhe::geometry::operation::Atlas_packer::xatlas};
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
        std::shared_ptr<erhe::scene::Node> piece_node;   // identity transform
        std::shared_ptr<erhe::scene::Mesh> piece_mesh;   // one Mesh_primitive per piece
        std::vector<Piece_info>            pieces;       // parallel to piece_mesh primitives
    };

    // Blocking (driven from the main thread by the Lightmap window / MCP);
    // no preconditions - the kd tile tree comes from a geometry-only split
    // estimate, and the final partitioned relayout re-packs the pieces with
    // their measured UVs. The heavy phase - per-region world-space bake +
    // clip, per-piece unwrap + primitive build - runs in parallel on the
    // app executor (only the scene commit and final relayout stay serial).
    // An existing partition is reverted before re-preparing. Failures are
    // reported via Lightmap_report (Stage::partition); returns false when
    // nothing could be partitioned.
    auto prepare(Scene_root& scene_root, const Params& params) -> bool;

    // Destroys the piece nodes, restores original mesh visibility and
    // clears the store.
    void revert();

    // ON: piece meshes render (all of them - non-resident tiles fall back
    // to white), originals hidden. OFF: originals render, pieces hidden.
    void set_render_with_lightmaps(bool enabled);
    [[nodiscard]] auto get_render_with_lightmaps() const -> bool { return m_render_with_lightmaps; }

    [[nodiscard]] auto is_prepared() const -> bool { return !m_entries.empty(); }
    // How many original meshes moved since the clip (their pieces are stale
    // world-space geometry until re-prepare). Queried by the Lightmap
    // window when its World-Space Tiles section is drawn.
    [[nodiscard]] auto count_stale_transforms() const -> std::size_t;
    [[nodiscard]] auto is_piece_mesh(const erhe::scene::Mesh* mesh) const -> bool
    {
        return m_piece_meshes.contains(mesh);
    }
    [[nodiscard]] auto get_entries() const -> const std::vector<Original_entry>& { return m_entries; }
    [[nodiscard]] auto get_scene_root() const -> Scene_root* { return m_scene_root; }
    // The kd tile tree the pieces were clipped against (the split estimate
    // computed at prepare time) and its tile count.
    [[nodiscard]] auto get_tile_tree() const -> const std::vector<erhe::geometry::operation::Clip_tree_node>& { return m_tile_tree; }
    [[nodiscard]] auto get_tile_count() const -> int { return m_tile_count; }

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
    void apply_visibility();

    App_context&                                            m_context;
    Scene_root*                                             m_scene_root{nullptr};
    std::vector<Original_entry>                             m_entries;
    std::unordered_set<const erhe::scene::Mesh*>            m_piece_meshes;
    std::shared_ptr<erhe::scene::Node>                      m_group_node;
    std::vector<erhe::geometry::operation::Clip_tree_node>  m_tile_tree;
    int                                                     m_tile_count{0};
    bool                                                    m_render_with_lightmaps{false};
    Params                                                  m_last_params{};
};

} // namespace editor
