// Mcp_server tools for the cached-reference bug class
// (doc/editor/import_undo_reference_clearing.md):
//
//   get_editor_references     - every cross-frame reference the editor parts
//                               cache, so "did this window let go?" is
//                               answerable headless.
//   get_memory_usage          - where a loaded scene's memory actually sits, so
//                               "did dropping it free anything?" is answerable
//                               (doc/editor/reloadable_asset_loads.md).
//   debug_set_item_tree_hover - drives the tree hover / popup pin that only
//                               ImGui interaction sets, so its release is
//                               verifiable. Test hook, same category as
//                               acquire_asset / release_asset.
//
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"
#include "mcp/mcp_process_memory.hpp"

#include "animation/animation_player.hpp"
#include "animation/animation_window.hpp"
#include "app_context.hpp"
#include "assets/asset_manager.hpp"
#include "brushes/brush.hpp"
#include "brushes/brush_tool.hpp"
#include "content_library/brdf_slice.hpp"
#include "content_library/content_library.hpp"
#include "create/create.hpp"
#include "geometry_graph/geometry_graph_window.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "grid/grid.hpp"
#include "grid/grid_tool.hpp"
#include "operations/operation_stack.hpp"
#include "operations/operations_window.hpp"
#include "physics/physics_tool.hpp"
#include "preview/material_preview.hpp"
#include "scene/four_view.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_views.hpp"

#include "erhe_scene/camera.hpp"
#include "erhe_scene/scene.hpp"
#include "texture_graph/texture_graph_window.hpp"
#include "texture_graph/graph_texture.hpp"
#include "tools/material_paint_tool.hpp"
#include "tools/selection_tool.hpp"
#include "transform/handle_enums.hpp"
#include "transform/transform_tool.hpp"
#include "windows/editor_windows.hpp"
#include "windows/item_tree_window.hpp"
#include "windows/properties.hpp"

#include "app_scenes.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/window_imgui_host.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include "erhe_graphics/texture.hpp"
#include "erhe_item/item.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_primitive/lod.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/mesh_optimizer.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/mesh.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

namespace editor {

using namespace mcp_server_detail;

namespace {

// One reported reference. Null reads back as a JSON null, so a test can tell
// "dropped" from "never set" without string matching.
[[nodiscard]] auto reference_json(const erhe::Item_base* item) -> json
{
    if (item == nullptr) {
        return nullptr;
    }
    return json{
        {"name", item->get_name()},
        {"uid",  item->get_id()},
        {"type", std::string{item->get_type_name()}}
    };
}

[[nodiscard]] auto reference_json(const std::shared_ptr<erhe::Item_base>& item) -> json
{
    return reference_json(item.get());
}

}

auto Mcp_server::query_editor_references(const json& args) -> std::string
{
    static_cast<void>(args);

    json result;

    result["animation_window"] = (m_context.animation_window != nullptr)
        ? reference_json(m_context.animation_window->get_animation())
        : json{nullptr};
    result["animation_player"] = (m_context.animation_player != nullptr)
        ? reference_json(m_context.animation_player->get_animation())
        : json{nullptr};

    json properties = json::array();
    // The primary window is owned by Editor and reached through App_context;
    // Editor_windows holds only the extra pinned ones (#252).
    const auto add_properties_window = [&properties](Properties* window) {
        if (window == nullptr) {
            return;
        }
        json target_items = json::array();
        for (const std::shared_ptr<erhe::Item_base>& item : window->get_target_items()) {
            target_items.push_back(reference_json(item));
        }
        properties.push_back({
            {"target",             reference_json(window->get_target())},
            {"target_items",       target_items},
            {"inspected_material", reference_json(window->get_inspected_material())}
        });
    };
    add_properties_window(m_context.properties);
    if (m_context.editor_windows != nullptr) {
        for (const std::shared_ptr<Properties>& window : m_context.editor_windows->get_properties_windows()) {
            add_properties_window(window.get());
        }
    }
    result["properties"] = properties;

    json brush_tool = json::object();
    if (m_context.brush_tool != nullptr) {
        brush_tool["active_brush"]        = reference_json(m_context.brush_tool->get_active_brush());
        brush_tool["drag_and_drop_brush"] = reference_json(m_context.brush_tool->get_drag_and_drop_brush());
    }
    result["brush_tool"] = brush_tool;

    result["material_paint_tool"] = (m_context.material_paint_tool != nullptr)
        ? reference_json(m_context.material_paint_tool->get_material())
        : json{nullptr};
    result["material_preview"] = (m_context.material_preview != nullptr)
        ? reference_json(m_context.material_preview->get_last_material())
        : json{nullptr};
    const Brdf_slice_rendergraph_node* const brdf_slice_node = (m_context.brdf_slice != nullptr)
        ? m_context.brdf_slice->get_node()
        : nullptr;
    result["brdf_slice"] = (brdf_slice_node != nullptr)
        ? reference_json(brdf_slice_node->get_material())
        : json{nullptr};
    result["operations_make_mesh_material"] = (m_context.operations != nullptr)
        ? reference_json(m_context.operations->get_make_mesh_material())
        : json{nullptr};
    result["create_brush"] = (m_context.create != nullptr)
        ? reference_json(m_context.create->get_brush())
        : json{nullptr};
    result["physics_tool_last_target_mesh"] = (m_context.physics_tool != nullptr)
        ? reference_json(m_context.physics_tool->get_last_target_mesh())
        : json{nullptr};

    json graph_windows = json::array();
    const auto add_geometry_window = [&graph_windows](Geometry_graph_window* window) {
        if (window != nullptr) {
            graph_windows.push_back({
                {"kind",   "geometry"},
                {"target", reference_json(window->get_target())}
            });
        }
    };
    const auto add_texture_window = [&graph_windows](Texture_graph_window* window) {
        if (window != nullptr) {
            graph_windows.push_back({
                {"kind",   "texture"},
                {"target", reference_json(window->get_target())}
            });
        }
    };
    add_geometry_window(m_context.geometry_graph_window);
    add_texture_window (m_context.texture_graph_window);
    if (m_context.editor_windows != nullptr) {
        for (const std::shared_ptr<Geometry_graph_window>& window : m_context.editor_windows->get_extra_geometry_graph_windows()) {
            add_geometry_window(window.get());
        }
        for (const std::shared_ptr<Texture_graph_window>& window : m_context.editor_windows->get_extra_texture_graph_windows()) {
            add_texture_window(window.get());
        }
    }
    result["graph_windows"] = graph_windows;

    json item_trees = json::array();
    for (Item_tree* tree : Item_tree::get_instances()) {
        if (tree == nullptr) {
            continue;
        }
        item_trees.push_back({
            {"label",            tree->get_tree_label()},
            {"hovered_item",     reference_json(tree->get_hovered_item())},
            {"popup_item",       reference_json(tree->get_popup_item())},
            {"cached_row_count", tree->get_cached_row_count()}
        });
    }
    result["item_trees"] = item_trees;

    // D6: a grid names the node whose frame it follows. The grid is owned by
    // Grid_tool and outlives every scene, so that reference is exactly the
    // cross-frame kind this query exists for.
    json grids = json::array();
    if (m_context.grid_tool != nullptr) {
        for (const std::shared_ptr<Grid>& grid : m_context.grid_tool->get_grids()) {
            if (!grid) {
                continue;
            }
            grids.push_back({
                {"name",       grid->get_name()},
                {"uid",        grid->get_id()},
                {"frame_node", reference_json(grid->get_frame_node().get())}
            });
        }
    }
    result["grids"] = grids;

    json selection = json::array();
    if (m_context.selection != nullptr) {
        for (const std::shared_ptr<erhe::Item_base>& item : m_context.selection->get_selected_items()) {
            selection.push_back(reference_json(item));
        }
    }
    result["selection"] = selection;

    // The active item is a reference of its own: it can outlive the selection
    // (doc/editor/active_item.md D2).
    if (m_context.selection != nullptr) {
        result["active_item"] = reference_json(m_context.selection->get_active_item());
    }

    // Counters, so a test can assert that an announcement happened, or that no
    // further one followed - an absence is otherwise indistinguishable from a
    // subscriber that was never wired.
    if (m_context.asset_manager != nullptr) {
        result["items_removed_announcement_count"] = m_context.asset_manager->get_items_removed_announcement_count();
        json last_uids = json::array();
        for (const std::size_t uid : m_context.asset_manager->get_last_announced_uids()) {
            last_uids.push_back(uid);
        }
        result["last_announced_uids"] = last_uids;
    }
    if (m_context.selection != nullptr) {
        result["selection_change_count"] = m_context.selection->get_selection_change_count();
    }

    return make_json_content(result).dump();
}

auto Mcp_server::query_memory_usage(const json& args) -> std::string
{
    json result;

    // process_only: just the operating system figures - cheap and lock-free
    // with respect to the scenes, for sampling the transient peak while a
    // load runs.
    if (args.value("process_only", false)) {
        result["process"] = get_process_memory_json();
        return make_json_content(result).dump();
    }

    // Mesh vertex / index pools. `capacity` only ever grows - pool blocks are
    // never destroyed - so `used` is the figure that moves when meshes are
    // released, and the release is frame-deferred.
    json pools = json::array();
    std::size_t total_capacity{0};
    std::size_t total_used    {0};
    std::size_t total_pending {0};
    if (m_context.mesh_memory != nullptr) {
        for (const erhe::scene_renderer::Mesh_memory::Pool_statistics& pool : m_context.mesh_memory->get_pool_statistics()) {
            pools.push_back({
                {"label",                 pool.label},
                {"index_pool",            pool.is_index_pool},
                {"block_count",           pool.statistics.block_count},
                {"capacity_bytes",        pool.statistics.capacity_bytes},
                {"used_bytes",            pool.statistics.used_bytes},
                {"free_bytes",            pool.statistics.free_bytes},
                {"allocation_count",      pool.statistics.allocation_count},
                {"pending_retired_bytes", pool.statistics.pending_retired_bytes}
            });
            total_capacity += pool.statistics.capacity_bytes;
            total_used     += pool.statistics.used_bytes;
            total_pending  += pool.statistics.pending_retired_bytes;
        }
    }
    result["mesh_pools"] = pools;
    result["mesh_memory"] = {
        {"capacity_bytes",        total_capacity},
        {"used_bytes",            total_used},
        {"pending_retired_bytes", total_pending}
    };

    // Textures: estimated from create info, and unlike the mesh pools this
    // really is returned to the driver when the texture dies.
    const erhe::graphics::Texture::Memory_statistics texture_statistics =
        erhe::graphics::Texture::get_memory_statistics();
    result["textures"] = {
        {"count",       texture_statistics.texture_count},
        {"byte_count",  texture_statistics.byte_count}
    };

    // GPU buffers (every erhe::graphics::Buffer, by creation usage): mesh
    // pools, ring buffers, acceleration structures and their build inputs.
    const erhe::graphics::Buffer_memory_statistics buffer_statistics = erhe::graphics::Buffer::get_memory_statistics();
    result["buffers"] = {
        {"count",                                    buffer_statistics.buffer_count},
        {"byte_count",                               buffer_statistics.byte_count},
        {"acceleration_structure_bytes",             buffer_statistics.acceleration_structure_bytes},
        {"acceleration_structure_build_input_bytes", buffer_statistics.acceleration_structure_build_input_bytes},
        {"vertex_index_bytes",                       buffer_statistics.vertex_index_bytes},
        {"other_bytes",                              buffer_statistics.other_bytes}
    };

    // Driver-reported figure; Vulkan only, zeros elsewhere.
    if (m_context.graphics_device != nullptr) {
        const erhe::graphics::Memory_budget budget = m_context.graphics_device->get_memory_budget();
        result["device_memory"] = {
            {"device_local_budget", budget.device_local_budget},
            {"device_local_usage",  budget.device_local_usage},
            {"device_local_block_bytes",      budget.device_local_block_bytes},
            {"device_local_allocation_bytes", budget.device_local_allocation_bytes}
        };
    }

    // Mesh optimization: what the optimized variants cost and bought, session
    // wide. This is where to ask rather than the log, because the geometry-path
    // optimizations land asynchronously (the deferred finalize tasks) and there
    // is no point in the log at which they are all in.
    {
        const erhe::primitive::Mesh_optimize_totals totals = erhe::primitive::get_mesh_optimize_totals();
        result["mesh_optimization"] = {
            {"primitive_count",     totals.primitive_count},
            {"measured_count",      totals.measured_count},
            {"replayed_count",      totals.replayed_count},
            {"vertex_count_before", totals.vertex_count_before},
            {"vertex_count_after",  totals.vertex_count_after},
            {"triangle_count",      totals.triangle_count},
            {"acmr_before",         totals.acmr_before()},
            {"acmr_after",          totals.acmr_after()},
            {"overdraw_before",     totals.overdraw_before()},
            {"overdraw_after",      totals.overdraw_after()},
            {"fetch_bytes_before",  totals.fetch_bytes_before},
            {"fetch_bytes_after",   totals.fetch_bytes_after},
            {"elapsed_seconds",     totals.elapsed_seconds}
        };
    }

    // Render shapes of the scene meshes and how many hold a Geometry: a
    // prepared primitive (loaded with prepared mesh data) has none until
    // something needs it (doc/editor/async_asset_loading.md "Prepared mesh
    // data"). Shapes shared by several meshes count once.
    if (m_context.app_scenes != nullptr) {
        std::unordered_set<const erhe::primitive::Primitive_shape*> shapes;
        std::size_t with_geometry = 0;
        std::size_t prepared      = 0;
        for (const std::shared_ptr<Scene_root>& scene_root : m_context.app_scenes->get_scene_roots()) {
            if (!scene_root) {
                continue;
            }
            std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root->item_host_mutex};
            for (const std::shared_ptr<erhe::scene::Mesh_layer>& mesh_layer : scene_root->get_scene().get_mesh_layers()) {
                for (const std::shared_ptr<erhe::scene::Mesh>& mesh : mesh_layer->meshes) {
                    if (!mesh) {
                        continue;
                    }
                    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
                        const erhe::primitive::Primitive* const primitive = mesh_primitive.primitive.get();
                        if ((primitive == nullptr) || !primitive->render_shape || !shapes.insert(primitive->render_shape.get()).second) {
                            continue;
                        }
                        if (primitive->render_shape->get_geometry_const()) {
                            ++with_geometry;
                        }
                        if (primitive->is_prepared()) {
                            ++prepared;
                        }
                    }
                }
            }
        }
        result["render_shapes"] = {
            {"count",         shapes.size()},
            {"with_geometry", with_geometry},
            {"prepared",      prepared}
        };
    }

    // Level of detail: the LOD chains of the scene meshes' optimized variants
    // (doc/erhe/meshoptimizer_integration.md "Level of detail"), per level
    // over the distinct primitives that have one, plus the session-wide
    // generation totals. Shared primitives count once.
    {
        const erhe::primitive::Lod_totals lod_totals = erhe::primitive::get_lod_totals();
        std::array<std::size_t, erhe::primitive::max_lod_levels> level_primitive_counts{};
        std::array<std::size_t, erhe::primitive::max_lod_levels> level_triangle_counts{};
        std::array<double,      erhe::primitive::max_lod_levels> level_error_sums{};
        std::size_t primitives_with_chain = 0;
        std::size_t lod_index_bytes       = 0;
        if (m_context.app_scenes != nullptr) {
            std::unordered_set<const erhe::primitive::Primitive*> primitives;
            for (const std::shared_ptr<Scene_root>& scene_root : m_context.app_scenes->get_scene_roots()) {
                if (!scene_root) {
                    continue;
                }
                std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root->item_host_mutex};
                for (const std::shared_ptr<erhe::scene::Mesh_layer>& mesh_layer : scene_root->get_scene().get_mesh_layers()) {
                    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : mesh_layer->meshes) {
                        if (!mesh) {
                            continue;
                        }
                        for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
                            const erhe::primitive::Primitive* const primitive = mesh_primitive.primitive.get();
                            if ((primitive == nullptr) || !primitive->optimized_render_shape || !primitives.insert(primitive).second) {
                                continue;
                            }
                            const erhe::primitive::Buffer_mesh& optimized_mesh = primitive->optimized_render_shape->get_renderable_mesh();
                            const erhe::primitive::Lod_chain&   chain          = optimized_mesh.lod_chain;
                            if (chain.is_empty()) {
                                continue;
                            }
                            ++primitives_with_chain;
                            for (std::size_t level = 0; level < chain.level_count; ++level) {
                                const erhe::primitive::Lod_level& lod_level = chain.levels[level];
                                ++level_primitive_counts[level];
                                level_triangle_counts[level] += lod_level.index_range.get_triangle_count();
                                level_error_sums[level]      += static_cast<double>(lod_level.error);
                                if (level > 0) {
                                    lod_index_bytes += lod_level.index_range.index_count * optimized_mesh.index_buffer_range.element_size;
                                }
                            }
                        }
                    }
                }
            }
        }
        json levels = json::array();
        for (std::size_t level = 0; level < erhe::primitive::max_lod_levels; ++level) {
            if (level_primitive_counts[level] == 0) {
                break;
            }
            levels.push_back({
                {"level",           level},
                {"primitive_count", level_primitive_counts[level]},
                {"triangle_count",  level_triangle_counts[level]},
                {"mean_error",      level_error_sums[level] / static_cast<double>(level_primitive_counts[level])}
            });
        }
        result["lod"] = {
            {"primitive_count",            primitives_with_chain},
            {"level_index_bytes",          lod_index_bytes},
            {"levels",                     levels},
            {"generated_primitive_count",  lod_totals.primitive_count},
            {"generated_level_count",      lod_totals.level_count},
            {"generated_index_count",      lod_totals.index_count},
            {"generation_seconds",         lod_totals.elapsed_seconds}
        };
    }

    // CPU-side mesh data of the scene meshes' primitives, per owner: the
    // render shape (source build), the optimized variant and the collision
    // shape of each distinct Primitive. Objects shared by several shapes or
    // primitives (a soup, prepared data, raytrace buffers) count once.
    // Answers "which CPU mesh copies exist after a load"
    // (doc/reference/memory_usage_2026_10.md).
    if (m_context.app_scenes != nullptr) {
        std::unordered_set<const erhe::primitive::Primitive*> primitives;
        std::unordered_set<const void*>                       counted;
        erhe::primitive::Shape_memory_statistics             render_statistics;
        erhe::primitive::Shape_memory_statistics             optimized_statistics;
        erhe::primitive::Shape_memory_statistics             collision_statistics;
        for (const std::shared_ptr<Scene_root>& scene_root : m_context.app_scenes->get_scene_roots()) {
            if (!scene_root) {
                continue;
            }
            std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root->item_host_mutex};
            for (const std::shared_ptr<erhe::scene::Mesh_layer>& mesh_layer : scene_root->get_scene().get_mesh_layers()) {
                for (const std::shared_ptr<erhe::scene::Mesh>& mesh : mesh_layer->meshes) {
                    if (!mesh) {
                        continue;
                    }
                    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
                        const erhe::primitive::Primitive* const primitive = mesh_primitive.primitive.get();
                        if ((primitive == nullptr) || !primitives.insert(primitive).second) {
                            continue;
                        }
                        if (primitive->render_shape && counted.insert(primitive->render_shape.get()).second) {
                            primitive->render_shape->add_memory_statistics(render_statistics, counted);
                        }
                        if (primitive->optimized_render_shape && counted.insert(primitive->optimized_render_shape.get()).second) {
                            primitive->optimized_render_shape->add_memory_statistics(optimized_statistics, counted);
                        }
                        if (primitive->collision_shape && counted.insert(primitive->collision_shape.get()).second) {
                            primitive->collision_shape->add_memory_statistics(collision_statistics, counted);
                        }
                    }
                }
            }
        }
        const auto shape_json = [](const erhe::primitive::Shape_memory_statistics& s) -> json {
            const std::size_t total =
                s.triangle_soup_vertex_bytes + s.triangle_soup_index_bytes + s.element_mapping_bytes +
                s.prepared_colocation_bytes + s.prepared_bvh_bytes + s.stored_bvh_bytes +
                s.raytrace_buffer_bytes + s.raytrace_facet_map_bytes + s.raytrace_bvh_node_bytes +
                s.raytrace_primitive_id_bytes + s.raytrace_precomputed_triangle_bytes + s.raytrace_pending_prebuilt_bvh_bytes;
            return json{
                {"shape_count",                         s.shape_count},
                {"geometry_count",                      s.geometry_count},
                {"triangle_soup_count",                 s.triangle_soup_count},
                {"triangle_soup_vertex_bytes",          s.triangle_soup_vertex_bytes},
                {"triangle_soup_index_bytes",           s.triangle_soup_index_bytes},
                {"triangle_soup_released_count",        s.triangle_soup_released_count},
                {"element_mapping_bytes",               s.element_mapping_bytes},
                {"prepared_data_count",                 s.prepared_data_count},
                {"prepared_colocation_bytes",           s.prepared_colocation_bytes},
                {"prepared_bvh_bytes",                  s.prepared_bvh_bytes},
                {"stored_bvh_bytes",                    s.stored_bvh_bytes},
                {"raytrace_count",                      s.raytrace_count},
                {"raytrace_proxy_count",                s.raytrace_proxy_count},
                {"raytrace_buffer_bytes",               s.raytrace_buffer_bytes},
                {"raytrace_facet_map_bytes",            s.raytrace_facet_map_bytes},
                {"raytrace_bvh_node_bytes",             s.raytrace_bvh_node_bytes},
                {"raytrace_primitive_id_bytes",         s.raytrace_primitive_id_bytes},
                {"raytrace_precomputed_triangle_bytes", s.raytrace_precomputed_triangle_bytes},
                {"raytrace_pending_prebuilt_bvh_bytes", s.raytrace_pending_prebuilt_bvh_bytes},
                {"total_bytes",                         total}
            };
        };
        // Soups released after upload (load.cpu_mesh_data) and read back on
        // demand since the process started.
        const erhe::primitive::Triangle_soup_reload_statistics reloads = erhe::primitive::get_triangle_soup_reload_statistics();
        result["cpu_mesh"] = {
            {"primitive_count",  primitives.size()},
            {"render_shapes",    shape_json(render_statistics)},
            {"optimized_shapes", shape_json(optimized_statistics)},
            {"collision_shapes", shape_json(collision_statistics)},
            {"triangle_soup_reloads", {
                {"reload_count",         reloads.reload_count},
                {"reload_failure_count", reloads.reload_failure_count},
                {"reload_bytes",         reloads.reload_bytes}
            }}
        };
    }

    // Image bytes kept on the CPU after a load: the encoded source streams
    // retained for byte-exact export (asset container parses and the scene
    // content libraries; one stream counted once) and decoded pixels not yet
    // released by image residency.
    {
        std::unordered_set<const void*> counted;
        std::size_t source_bytes        = 0;
        std::size_t decoded_pixel_bytes = 0;
        if (m_context.asset_manager != nullptr) {
            m_context.asset_manager->add_retained_image_bytes(counted, source_bytes, decoded_pixel_bytes);
        }
        if (m_context.app_scenes != nullptr) {
            for (const std::shared_ptr<Scene_root>& scene_root : m_context.app_scenes->get_scene_roots()) {
                if (scene_root && scene_root->get_content_library()) {
                    source_bytes += scene_root->get_content_library()->get_retained_image_source_bytes(counted);
                }
            }
        }
        result["cpu_images"] = {
            {"retained_source_bytes", source_bytes},
            {"decoded_pixel_bytes",   decoded_pixel_bytes}
        };
    }

    // The scene content libraries' textures by pixel format (each texture
    // counted once): which part of `textures` the loaded scenes own, and
    // what the transcode target made of them.
    if (m_context.app_scenes != nullptr) {
        std::unordered_set<const erhe::graphics::Texture*> textures;
        std::map<std::string, std::pair<std::size_t, std::size_t>> by_format; // format -> (count, bytes)
        std::size_t total_bytes = 0;
        for (const std::shared_ptr<Scene_root>& scene_root : m_context.app_scenes->get_scene_roots()) {
            const std::shared_ptr<Content_library> library = scene_root ? scene_root->get_content_library() : std::shared_ptr<Content_library>{};
            if (!library) {
                continue;
            }
            for (const std::shared_ptr<erhe::graphics::Texture>& texture : library->get_all<erhe::graphics::Texture>()) {
                if (!texture || !textures.insert(texture.get()).second) {
                    continue;
                }
                std::pair<std::size_t, std::size_t>& entry = by_format[erhe::dataformat::c_str(texture->get_pixelformat())];
                entry.first  += 1;
                entry.second += texture->get_estimated_byte_count();
                total_bytes  += texture->get_estimated_byte_count();
            }
        }
        json formats = json::array();
        for (const auto& [format, entry] : by_format) {
            formats.push_back({{"format", format}, {"count", entry.first}, {"byte_count", entry.second}});
        }
        result["scene_textures"] = {
            {"count",      textures.size()},
            {"byte_count", total_bytes},
            {"by_format",  formats}
        };
    }

    if (m_context.graphics_device != nullptr) {
        const erhe::graphics::Device_debug_state debug_state = m_context.graphics_device->get_debug_state();
        result["ring_buffers"] = {
            {"count",          debug_state.ring_buffer_count},
            {"capacity_bytes", debug_state.ring_buffer_capacity_bytes},
            {"bytes_in_use",   debug_state.ring_buffer_bytes_in_use}
        };
        // The capped upload staging of asynchronous loads
        // (doc/erhe/ring_buffer_memory.md "Upload staging cap").
        const erhe::graphics::Upload_staging_usage staging = m_context.graphics_device->get_upload_staging_usage();
        result["upload_staging"] = {
            {"capacity_bytes",   staging.capacity_byte_count},
            {"bytes_in_use",     staging.bytes_in_use},
            {"high_water_bytes", staging.high_water_bytes},
            {"refused_count",    staging.refused_count},
            {"oversize_count",   staging.oversize_count}
        };
    }

    result["process"] = get_process_memory_json();

    // Acceleration structures pin their primitives, so this must drop too:
    // one bottom level structure per traced geometry of every scene root
    // (Ray_trace_scene), shared by the ray trace, DDGI and RC consumers.
    result["blas_count"] = (m_context.app_scenes != nullptr)
        ? m_context.app_scenes->get_ray_trace_geometry_count()
        : 0;

    // Undo/redo entries, and which container records still hold a parse.
    if (m_context.operation_stack != nullptr) {
        result["undo_entry_count"] = m_context.operation_stack->get_undo_stack().size();
        result["redo_entry_count"] = m_context.operation_stack->get_redo_stack().size();
    }
    if (m_context.asset_manager != nullptr) {
        json containers = json::array();
        for (const Asset_container_info& info : m_context.asset_manager->inspect_containers()) {
            containers.push_back({
                {"path",           info.path},
                {"open_as_scene",  info.open_as_scene},
                {"material_count", info.material_count},
                {"animation_count", info.animation_count}
            });
        }
        result["containers"] = containers;
    }

    return make_json_content(result).dump();
}

auto Mcp_server::action_free_undone_loads(const json& args) -> std::string
{
    static_cast<void>(args);
    if (m_context.operation_stack == nullptr) {
        return make_error_content("Operation stack not available");
    }
    // Shadows the same-named editor command deliberately: the command returns
    // only success, and a caller (or a test) needs the counts.
    const Operation_stack::Free_undone_loads_result result = m_context.operation_stack->free_undone_loads();
    return make_json_content({
        {"released_count",  result.released_count},
        {"discarded_count", result.discarded_count}
    }).dump();
}

auto Mcp_server::action_debug_set_item_tree_hover(const json& args) -> std::string
{
    const std::string tree_label = args.value("tree", "");
    const std::string item_name  = args.value("item_name", "");
    const bool        clear      = args.value("clear", false);

    Item_tree* found_tree = nullptr;
    json       labels     = json::array();
    for (Item_tree* tree : Item_tree::get_instances()) {
        if (tree == nullptr) {
            continue;
        }
        labels.push_back(tree->get_tree_label());
        if (tree->get_tree_label() == tree_label) {
            found_tree = tree;
        }
    }
    if (found_tree == nullptr) {
        return make_error_content("Item tree not found: '" + tree_label + "'; available: " + labels.dump());
    }

    if (clear) {
        found_tree->debug_set_hovered_item({});
        return make_json_content({{"tree", tree_label}, {"hovered", nullptr}}).dump();
    }

    // Resolve by name against the tree's own root, so the hook can only pin
    // something the tree really lists.
    std::shared_ptr<erhe::Item_base> found_item{};
    const std::shared_ptr<erhe::Hierarchy>& root = found_tree->get_root();
    if (root) {
        root->for_each<erhe::Hierarchy>(
            [&found_item, &item_name](erhe::Hierarchy& item) -> bool {
                if (!found_item && (item.get_name() == item_name)) {
                    found_item = item.shared_from_this();
                }
                return true;
            }
        );
    }
    if (!found_item) {
        return make_error_content("Item not found in tree '" + tree_label + "': " + item_name);
    }
    found_tree->debug_set_hovered_item(found_item);
    return make_json_content({
        {"tree",    tree_label},
        {"hovered", found_item->get_name()},
        {"uid",     found_item->get_id()}
    }).dump();
}

// debug_set_transform_hover - forces the transform gizmo's hovered handle,
// which otherwise only the pointer sets, so a headless run can capture the
// hover highlight.
auto Mcp_server::action_debug_set_transform_hover(const json& args) -> std::string
{
    Transform_tool* transform_tool = m_context.transform_tool;
    if (transform_tool == nullptr) {
        return make_error_content("Transform tool is not available");
    }
    const int handle_value = args.value("handle", 0);
    if ((handle_value < 0) || (handle_value > static_cast<int>(Handle::e_handle_rotate_free))) {
        return make_error_content("handle is out of range");
    }
    const Handle handle = static_cast<Handle>(handle_value);
    transform_tool->debug_set_hover_handle(handle);
    return make_json_content({{"handle", handle_value}, {"name", c_str(handle)}}).dump();
}

namespace {

[[nodiscard]] auto describe_four_view(const Four_view& four_view) -> json
{
    static constexpr const char* c_axis_names[Four_view::axis_count + 1] = { "top", "front", "right", "perspective" };
    const std::shared_ptr<Scene_root> scene_root = four_view.get_scene_root();
    const glm::vec3 focus = four_view.get_focus();
    json cameras = json::array();
    for (std::size_t i = 0; i < (Four_view::axis_count + 1); ++i) {
        const std::shared_ptr<erhe::scene::Camera> camera = four_view.get_camera(static_cast<Four_view_axis>(i));
        if (!camera) {
            continue;
        }
        const glm::vec3 position = glm::vec3{camera->position_in_world()};
        cameras.push_back({
            {"axis",         c_axis_names[i]},
            {"id",           camera->get_id()},
            {"name",         camera->get_name()},
            {"position",     {position.x, position.y, position.z}},
            {"ortho_height", camera->projection()->ortho_height}
        });
    }
    return json{
        {"scene",       scene_root ? scene_root->get_name() : std::string{}},
        {"focus",       {focus.x, focus.y, focus.z}},
        {"view_height", four_view.get_view_height()},
        {"cameras",     cameras}
    };
}

} // anonymous namespace

// open_four_view - Scene_views::open_four_view() (see doc/editor/four_view.md)
auto Mcp_server::action_open_four_view(const json&) -> std::string
{
    if (m_context.scene_views == nullptr) {
        return make_error_content("Scene views are not available");
    }
    const Four_view* const four_view = m_context.scene_views->open_four_view();
    if (four_view == nullptr) {
        return make_error_content("There is no viewport showing a scene to start the four view from");
    }
    return make_json_content(describe_four_view(*four_view)).dump();
}

auto Mcp_server::query_four_views(const json&) -> std::string
{
    if (m_context.scene_views == nullptr) {
        return make_error_content("Scene views are not available");
    }
    json four_views = json::array();
    for (const std::unique_ptr<Four_view>& four_view : m_context.scene_views->get_four_views()) {
        four_views.push_back(describe_four_view(*four_view));
    }
    return make_json_content({{"four_views", four_views}}).dump();
}

}
