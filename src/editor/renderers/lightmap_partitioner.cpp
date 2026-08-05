#include "renderers/lightmap_partitioner.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "renderers/lightmap_baker.hpp"
#include "renderers/lightmap_report.hpp"
#include "renderers/lightmap_streamer.hpp"
#include "renderers/lightmap_tile_io.hpp"
#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/bake_transform.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include <fmt/format.h>
#include <taskflow/taskflow.hpp>
#include <taskflow/algorithm/for_each.hpp>

#include <mutex>

namespace editor {

Lightmap_partitioner::Lightmap_partitioner(App_context& context)
    : m_context{context}
{
}

auto Lightmap_partitioner::prepare(Scene_root& scene_root, const Params& params) -> bool
{
    ERHE_PROFILE_FUNCTION();

    Lightmap_baker* const  baker  = m_context.lightmap_baker;
    Lightmap_report* const report = m_context.lightmap_report;
    if ((baker == nullptr) || (m_context.mesh_memory == nullptr)) {
        return false;
    }
    if (is_prepared()) {
        revert();
    }
    if (report != nullptr) {
        report->clear_stage(Lightmap_report::Stage::partition);
    }

    // The tile partition comes from a geometry-only split ESTIMATE of the
    // original meshes (world areas at a nominal chart coverage): no unwrap
    // or prior layout is needed, and the partitioned relayout at the end
    // re-packs every piece with its measured UVs.
    const Lightmap_baker::Estimate_split split = baker->compute_tile_split_estimate(scene_root, params.texels_per_meter);
    if (split.empty()) {
        if (report != nullptr) {
            report->add_error(Lightmap_report::Stage::partition, "prepare", "no lightmapped meshes to partition");
        }
        return false;
    }
    m_tile_tree  = split.kd_nodes;
    m_tile_count = split.tile_count;

    // Group the estimate regions by source mesh: one piece mesh per original
    // mesh, one Mesh_primitive per (source primitive, overlapped tile).
    class Mesh_group
    {
    public:
        std::shared_ptr<erhe::scene::Mesh>                  mesh;
        std::vector<const Lightmap_baker::Estimate_region*> regions;
    };
    std::vector<Mesh_group> groups;
    for (const Lightmap_baker::Estimate_region& region : split.regions) {
        if (!region.mesh) {
            continue;
        }
        auto it = std::find_if(groups.begin(), groups.end(), [&region](const Mesh_group& group) {
            return group.mesh == region.mesh;
        });
        if (it == groups.end()) {
            groups.push_back(Mesh_group{region.mesh, {}});
            it = groups.end() - 1;
        }
        it->regions.push_back(&region);
    }

    const erhe::primitive::Build_info build_info{
        .primitive_types = {
            .fill_triangles          = true,
            .fill_triangles_expanded = true,
            .edge_lines              = true,
            .corner_points           = true,
            .centroid_points         = true
        },
        .buffer_info = m_context.mesh_memory->make_primitive_buffer_info()
    };

    // ---- Phase 1 (serial, cheap): collect one task per source primitive
    // region; no geometry work here.
    class Region_task
    {
    public:
        class Piece_result
        {
        public:
            int                                         tile{-1};
            std::shared_ptr<erhe::primitive::Primitive> primitive;
        };
        std::size_t                                entry_index{0};
        std::size_t                                source_primitive_index{0};
        int                                        overflow_tile{-1};
        std::shared_ptr<erhe::geometry::Geometry>  source_geometry;
        glm::mat4                                  world_from_node{1.0f};
        std::shared_ptr<erhe::primitive::Material> material;
        erhe::primitive::Normal_style              normal_style{};
        std::string                                subject;
        std::vector<Piece_result>                  results; // phase 2 output, in piece order
    };
    std::vector<Original_entry> entries;
    std::vector<Region_task>    tasks;
    for (const Mesh_group& group : groups) {
        erhe::scene::Node* const node = group.mesh->get_node();
        if (node == nullptr) {
            continue;
        }
        const glm::mat4 world_from_node = node->world_from_node();

        const std::size_t entry_index = entries.size();
        Original_entry entry;
        entry.original_mesh           = group.mesh;
        entry.world_from_node_at_clip = world_from_node;
        entries.push_back(std::move(entry));

        const std::vector<erhe::scene::Mesh_primitive>& source_primitives = group.mesh->get_primitives();
        for (const Lightmap_baker::Estimate_region* region : group.regions) {
            if (region->primitive_index >= source_primitives.size()) {
                continue;
            }
            const erhe::scene::Mesh_primitive& source_mesh_primitive = source_primitives[region->primitive_index];
            if (!source_mesh_primitive.primitive || !source_mesh_primitive.primitive->render_shape) {
                continue;
            }
            const std::shared_ptr<erhe::primitive::Primitive_render_shape>& render_shape = source_mesh_primitive.primitive->render_shape;
            const std::shared_ptr<erhe::geometry::Geometry>& source_geometry = render_shape->get_geometry();
            if (!source_geometry) {
                continue;
            }
            tasks.push_back(
                Region_task{
                    .entry_index            = entry_index,
                    .source_primitive_index = region->primitive_index,
                    .overflow_tile          = region->tile,
                    .source_geometry        = source_geometry,
                    .world_from_node        = world_from_node,
                    .material               = source_mesh_primitive.material,
                    .normal_style           = render_shape->get_normal_style(),
                    .subject                = fmt::format("{}[{}]", group.mesh->get_name(), region->primitive_index),
                    .results                = {}
                }
            );
        }
    }

    // ---- Phase 2 (parallel): per region, world-space bake + kd clip,
    // then per-piece re-unwrap + renderable/raytrace primitive build.
    // Every task is independent: bake_transform and clip_by_tile_tree
    // reach no Geogram algorithm and are safe on distinct destinations
    // (clip_tile_tree.hpp thread-safety note; the tile tree is read-only
    // here, concurrent reads of a shared source geometry are fine),
    // make_atlas takes geogram_lock internally only around its
    // Geogram-parameterizer branch (per-facet unwraps run concurrently),
    // buffer-mesh allocation groups serialize on
    // buffer_mesh_allocation_mutex, and raytrace builds are worker-safe
    // (deferred glTF finalize does the same). Lightmap_report already
    // takes worker-thread reports (async UV unwrap failures).
    // Unwrap density: world-space geometry, so the density is the world
    // density directly (no node-scale folding). Mirror
    // Make_atlas_operation's per-facet fallback so one degenerate piece
    // does not abort the whole partition.
    const std::vector<erhe::geometry::operation::Clip_tree_node>& tile_tree = m_tile_tree;
    const auto process_region = [&params, &build_info, &tile_tree, report](Region_task& task) {
        erhe::geometry::Geometry world_geometry{fmt::format("{}.world", task.subject)};
        std::vector<erhe::geometry::operation::Clip_tile_piece> pieces;
        try {
            erhe::geometry::operation::bake_transform(
                *task.source_geometry.get(),
                world_geometry,
                erhe::geometry::to_geo_mat4f(task.world_from_node)
            );
            erhe::geometry::operation::clip_by_tile_tree(world_geometry, tile_tree, task.overflow_tile, pieces);
        } catch (const std::exception& e) {
            if (report != nullptr) {
                report->add_error(Lightmap_report::Stage::partition, task.subject, fmt::format("clip failed: {}", e.what()));
            }
            return;
        }

        for (const erhe::geometry::operation::Clip_tile_piece& piece : pieces) {
            if (!piece.geometry) {
                continue;
            }
            const std::string piece_subject = fmt::format("{}.tile{}", task.subject, piece.tile);
            std::shared_ptr<erhe::geometry::Geometry> atlas_geometry = std::make_shared<erhe::geometry::Geometry>(piece_subject);
            try {
                try {
                    erhe::geometry::operation::make_atlas(
                        *piece.geometry.get(),
                        *atlas_geometry.get(),
                        2, // lightmap UV channel (texcoord usage_index 2)
                        static_cast<double>(params.hard_angles_deg),
                        params.parameterizer,
                        params.packer,
                        static_cast<double>(params.texels_per_meter),
                        static_cast<double>(params.gutter_texels),
                        static_cast<double>(params.min_chart_texels),
                        nullptr
                    );
                } catch (const std::exception& e) {
                    if (params.parameterizer == erhe::geometry::operation::Atlas_parameterizer::per_facet) {
                        throw;
                    }
                    if (report != nullptr) {
                        report->add_warning(
                            Lightmap_report::Stage::partition,
                            piece_subject,
                            fmt::format("parameterizer failed ({}); fell back to per-facet unwrap", e.what())
                        );
                    }
                    erhe::geometry::operation::make_atlas(
                        *piece.geometry.get(),
                        *atlas_geometry.get(),
                        2,
                        static_cast<double>(params.hard_angles_deg),
                        erhe::geometry::operation::Atlas_parameterizer::per_facet,
                        params.packer,
                        static_cast<double>(params.texels_per_meter),
                        static_cast<double>(params.gutter_texels),
                        static_cast<double>(params.min_chart_texels),
                        nullptr
                    );
                }
            } catch (const std::exception& e) {
                if (report != nullptr) {
                    report->add_error(Lightmap_report::Stage::partition, piece_subject, fmt::format("unwrap failed: {}", e.what()));
                }
                continue;
            }

            std::shared_ptr<erhe::primitive::Primitive> piece_primitive = std::make_shared<erhe::primitive::Primitive>(atlas_geometry);
            const bool renderable_ok = piece_primitive->make_renderable_mesh(build_info, task.normal_style);
            const bool raytrace_ok   = renderable_ok && piece_primitive->make_raytrace();
            if (!renderable_ok || !raytrace_ok) {
                if (report != nullptr) {
                    report->add_error(
                        Lightmap_report::Stage::partition,
                        piece_subject,
                        renderable_ok ? "raytrace build failed" : "renderable mesh build failed"
                    );
                }
                continue;
            }
            task.results.push_back(Region_task::Piece_result{.tile = piece.tile, .primitive = std::move(piece_primitive)});
        }
    };
    if ((m_context.executor != nullptr) && (tasks.size() > 1)) {
        tf::Taskflow taskflow;
        taskflow.for_each_index(
            std::size_t{0}, tasks.size(), std::size_t{1},
            [&tasks, &process_region](const std::size_t i) { process_region(tasks[i]); }
        );
        m_context.executor->run(taskflow).wait();
    } else {
        for (Region_task& task : tasks) {
            process_region(task);
        }
    }

    // ---- Phase 3 (serial): assemble piece meshes in task order (identical
    // to the old serial order; ordinals count successful pieces per
    // (source mesh, source primitive) region), dropping entries with no
    // pieces.
    std::vector<std::vector<erhe::scene::Mesh_primitive>> piece_primitives_per_entry(entries.size());
    for (Region_task& task : tasks) {
        int ordinal = 0;
        for (Region_task::Piece_result& result : task.results) {
            piece_primitives_per_entry[task.entry_index].emplace_back(std::move(result.primitive), task.material);
            entries[task.entry_index].pieces.push_back(
                Piece_info{
                    .tile                   = result.tile,
                    .source_primitive_index = task.source_primitive_index,
                    .ordinal                = ordinal++
                }
            );
        }
    }
    std::size_t total_pieces = 0;
    {
        std::vector<Original_entry> kept;
        kept.reserve(entries.size());
        for (std::size_t entry_index = 0; entry_index < entries.size(); ++entry_index) {
            Original_entry& entry = entries[entry_index];
            std::vector<erhe::scene::Mesh_primitive>& piece_primitives = piece_primitives_per_entry[entry_index];
            if (piece_primitives.empty()) {
                continue;
            }
            total_pieces += piece_primitives.size();
            erhe::scene::Node* const node = entry.original_mesh->get_node();
            entry.piece_node = std::make_shared<erhe::scene::Node>(fmt::format("{}.lm", (node != nullptr) ? node->get_name() : entry.original_mesh->get_name()));
            entry.piece_mesh = std::make_shared<erhe::scene::Mesh>(fmt::format("{}.lm", entry.original_mesh->get_name()));
            entry.piece_mesh->layer_id = scene_root.layers().content()->id;
            entry.piece_mesh->set_primitives(piece_primitives);
            entry.piece_mesh->enable_flag_bits(
                erhe::Item_flags::content     |
                erhe::Item_flags::visible     |
                erhe::Item_flags::shadow_cast |
                erhe::Item_flags::show_in_ui
            );
            entry.piece_node->attach(entry.piece_mesh);
            entry.piece_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
            kept.push_back(std::move(entry));
        }
        entries = std::move(kept);
    }

    if (entries.empty()) {
        if (report != nullptr) {
            report->add_error(Lightmap_report::Stage::partition, "prepare", "no pieces produced");
        }
        m_tile_tree.clear();
        m_tile_count = 0;
        return false;
    }

    // Commit: group node with identity transform at the scene root; the
    // pieces are world-space geometry, so identity world_from_node is what
    // makes them render (and raytrace) in place.
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root.item_host_mutex};
        m_group_node = std::make_shared<erhe::scene::Node>("Lightmap Pieces");
        m_group_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
        m_group_node->set_parent(scene_root.get_scene().get_root_node());
        for (Original_entry& entry : entries) {
            entry.piece_node->set_parent(m_group_node);
            m_piece_meshes.insert(entry.piece_mesh.get());
        }
    }
    m_entries     = std::move(entries);
    m_scene_root  = &scene_root;
    m_last_params = params;
    apply_visibility();

    // Switch the baker layout over to the pieces (the partitioned branch of
    // update_layout is active now that the store is populated) and push
    // their atlas mappings (white-fallback sentinel for non-resident tiles)
    // onto the piece Mesh_primitives.
    if (baker->update_layout(scene_root, params.texels_per_meter, params.min_face_texels)) {
        baker->publish_regions();
    }
    // Manifest regions that are pieces resolve through this partition; make
    // the streamer re-apply mappings against the new piece set.
    if (m_context.lightmap_streamer != nullptr) {
        m_context.lightmap_streamer->invalidate();
    }

    log_render->info(
        "Lightmap_partitioner: {} source meshes partitioned into {} world-space pieces across {} tiles",
        m_entries.size(), total_pieces, m_tile_count
    );
    return true;
}

void Lightmap_partitioner::revert()
{
    if (!is_prepared() || (m_scene_root == nullptr)) {
        m_entries.clear();
        m_piece_meshes.clear();
        m_group_node.reset();
        m_tile_tree.clear();
        m_tile_count = 0;
        return;
    }
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{m_scene_root->item_host_mutex};
        for (Original_entry& entry : m_entries) {
            if (entry.original_mesh) {
                entry.original_mesh->enable_flag_bits(erhe::Item_flags::visible);
            }
            if (entry.piece_node) {
                entry.piece_node->set_parent(std::shared_ptr<erhe::Hierarchy>{});
            }
        }
        if (m_group_node) {
            m_group_node->set_parent(std::shared_ptr<erhe::Hierarchy>{});
        }
    }
    Scene_root* const scene_root = m_scene_root;
    m_entries.clear();
    m_piece_meshes.clear();
    m_group_node.reset();
    m_tile_tree.clear();
    m_tile_count = 0;
    m_scene_root = nullptr;
    // Back to the ordinary layout derived from the original meshes. In the
    // fused workflow the originals typically have no channel-2 UVs, so this
    // yields an empty layout (originals render unlit) until the legacy
    // Generate Lightmap UVs path or a re-prepare runs.
    if ((m_context.lightmap_baker != nullptr) && (scene_root != nullptr)) {
        if (m_context.lightmap_baker->update_layout(*scene_root, m_last_params.texels_per_meter, m_last_params.min_face_texels)) {
            m_context.lightmap_baker->publish_regions();
        }
    }
    if (m_context.lightmap_streamer != nullptr) {
        m_context.lightmap_streamer->invalidate();
    }
    log_render->info("Lightmap_partitioner: partition reverted");
}

void Lightmap_partitioner::set_render_with_lightmaps(const bool enabled)
{
    if (m_render_with_lightmaps == enabled) {
        return;
    }
    m_render_with_lightmaps = enabled;
    apply_visibility();
}

void Lightmap_partitioner::apply_visibility()
{
    if (!is_prepared() || (m_scene_root == nullptr)) {
        return;
    }
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{m_scene_root->item_host_mutex};
    for (Original_entry& entry : m_entries) {
        if (entry.original_mesh) {
            if (m_render_with_lightmaps) {
                entry.original_mesh->disable_flag_bits(erhe::Item_flags::visible);
            } else {
                entry.original_mesh->enable_flag_bits(erhe::Item_flags::visible);
            }
        }
        if (entry.piece_mesh) {
            if (m_render_with_lightmaps) {
                entry.piece_mesh->enable_flag_bits(erhe::Item_flags::visible);
            } else {
                entry.piece_mesh->disable_flag_bits(erhe::Item_flags::visible);
            }
        }
    }
}

auto Lightmap_partitioner::count_stale_transforms() const -> std::size_t
{
    std::size_t stale = 0;
    for (const Original_entry& entry : m_entries) {
        const erhe::scene::Node* const node = entry.original_mesh ? entry.original_mesh->get_node() : nullptr;
        if (node == nullptr) {
            continue;
        }
        if (node->world_from_node() != entry.world_from_node_at_clip) {
            ++stale;
        }
    }
    return stale;
}

auto Lightmap_partitioner::find_piece(
    const std::string& node_path,
    const std::string& node_index_path,
    const std::string& mesh_name,
    const std::size_t  source_primitive_index,
    const int          tile,
    const int          ordinal
) const -> std::pair<erhe::scene::Mesh*, std::size_t>
{
    for (const Original_entry& entry : m_entries) {
        if (!entry.original_mesh || !entry.piece_mesh) {
            continue;
        }
        if (entry.original_mesh->get_name() != mesh_name) {
            continue;
        }
        const erhe::scene::Node* const node = entry.original_mesh->get_node();
        if (!node_index_path.empty()) {
            if (Lightmap_tile_io::node_index_path(node) != node_index_path) {
                continue;
            }
        } else if (Lightmap_tile_io::node_path(node) != node_path) {
            continue;
        }
        for (std::size_t piece_index = 0; piece_index < entry.pieces.size(); ++piece_index) {
            const Piece_info& piece = entry.pieces[piece_index];
            if ((piece.tile == tile) && (piece.source_primitive_index == source_primitive_index) && (piece.ordinal == ordinal)) {
                return {entry.piece_mesh.get(), piece_index};
            }
        }
    }
    return {nullptr, 0};
}

void Lightmap_partitioner::on_scene_closed(const Scene_root* scene_root)
{
    if ((m_scene_root == nullptr) || (m_scene_root != scene_root)) {
        return;
    }
    // The scene is going away - drop every reference without mutating it.
    m_entries.clear();
    m_piece_meshes.clear();
    m_group_node.reset();
    m_tile_tree.clear();
    m_tile_count = 0;
    m_scene_root = nullptr;
}

} // namespace editor
