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

    // The tile partition comes from the ORIGINAL meshes: refresh the baker
    // layout so the kd tree and the packed tiles agree.
    if (!baker->update_layout(scene_root, params.texels_per_meter, params.min_face_texels)) {
        if (report != nullptr) {
            report->add_error(Lightmap_report::Stage::partition, "prepare", "atlas layout failed - nothing to partition");
        }
        return false;
    }
    const Lightmap_baker::Atlas_layout& layout = baker->get_layout();
    if (layout.kd_nodes.empty() || layout.regions.empty()) {
        if (report != nullptr) {
            report->add_error(Lightmap_report::Stage::partition, "prepare", "empty layout - nothing to partition");
        }
        return false;
    }
    m_tile_tree  = layout.kd_nodes;
    m_tile_count = layout.get_tile_count();

    // Group the layout regions by source mesh: one piece mesh per original
    // mesh, one Mesh_primitive per (source primitive, overlapped tile).
    class Mesh_group
    {
    public:
        std::shared_ptr<erhe::scene::Mesh>              mesh;
        std::vector<const Lightmap_baker::Instance_region*> regions;
    };
    std::vector<Mesh_group> groups;
    for (const Lightmap_baker::Instance_region& region : layout.regions) {
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

    std::vector<Original_entry> entries;
    std::size_t total_pieces = 0;
    for (const Mesh_group& group : groups) {
        erhe::scene::Node* const node = group.mesh->get_node();
        if (node == nullptr) {
            continue;
        }
        const glm::mat4 world_from_node = node->world_from_node();

        Original_entry entry;
        entry.original_mesh           = group.mesh;
        entry.world_from_node_at_clip = world_from_node;

        std::vector<erhe::scene::Mesh_primitive> piece_primitives;
        const std::vector<erhe::scene::Mesh_primitive>& source_primitives = group.mesh->get_primitives();
        for (const Lightmap_baker::Instance_region* region : group.regions) {
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
            const std::string subject = fmt::format("{}[{}]", group.mesh->get_name(), region->primitive_index);

            // World-space bake + kd clip. Both create GEO attributes, whose
            // pool allocation is process-global (erhe::geometry::geogram_lock).
            erhe::geometry::Geometry world_geometry{fmt::format("{}.world", subject)};
            std::vector<erhe::geometry::operation::Clip_tile_piece> pieces;
            try {
                const std::lock_guard<std::recursive_mutex> geogram_guard{erhe::geometry::geogram_lock()};
                erhe::geometry::operation::bake_transform(
                    *source_geometry.get(),
                    world_geometry,
                    erhe::geometry::to_geo_mat4f(world_from_node)
                );
                erhe::geometry::operation::clip_by_tile_tree(world_geometry, m_tile_tree, region->tile, pieces);
            } catch (const std::exception& e) {
                if (report != nullptr) {
                    report->add_error(Lightmap_report::Stage::partition, subject, fmt::format("clip failed: {}", e.what()));
                }
                continue;
            }

            int ordinal = 0;
            for (const erhe::geometry::operation::Clip_tile_piece& piece : pieces) {
                if (!piece.geometry) {
                    continue;
                }
                // Per-piece re-unwrap: world-space geometry, so the density
                // is the world density directly (no node-scale folding).
                // Mirror Make_atlas_operation's per-facet fallback so one
                // degenerate piece does not abort the whole partition.
                std::shared_ptr<erhe::geometry::Geometry> atlas_geometry = std::make_shared<erhe::geometry::Geometry>(
                    fmt::format("{}.tile{}", subject, piece.tile)
                );
                const std::string piece_subject = atlas_geometry->get_name();
                try {
                    const std::lock_guard<std::recursive_mutex> geogram_guard{erhe::geometry::geogram_lock()};
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
                const bool renderable_ok = piece_primitive->make_renderable_mesh(build_info, render_shape->get_normal_style());
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
                piece_primitives.emplace_back(piece_primitive, source_mesh_primitive.material);
                entry.pieces.push_back(
                    Piece_info{
                        .tile                   = piece.tile,
                        .source_primitive_index = region->primitive_index,
                        .ordinal                = ordinal++
                    }
                );
            }
        }
        if (piece_primitives.empty()) {
            continue;
        }
        total_pieces += piece_primitives.size();

        entry.piece_node = std::make_shared<erhe::scene::Node>(fmt::format("{}.lm", node->get_name()));
        entry.piece_mesh = std::make_shared<erhe::scene::Mesh>(fmt::format("{}.lm", group.mesh->get_name()));
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
        entries.push_back(std::move(entry));
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
    // Back to the ordinary layout derived from the original meshes.
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
