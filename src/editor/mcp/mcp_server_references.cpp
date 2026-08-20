// Mcp_server tools for the cached-reference bug class
// (doc/import-undo-reference-clearing.md):
//
//   get_editor_references     - every cross-frame reference the editor parts
//                               cache, so "did this window let go?" is
//                               answerable headless.
//   move_library_item         - moves a content-library entry between folders,
//                               the detach-then-attach that must NOT be
//                               announced as a removal.
//   get_memory_usage          - where a loaded scene's memory actually sits, so
//                               "did dropping it free anything?" is answerable
//                               (doc/reloadable-asset-loads.md).
//   debug_set_item_tree_hover - drives the tree hover / popup pin that only
//                               ImGui interaction sets, so its release is
//                               verifiable. Test hook, same category as
//                               acquire_asset / release_asset.
//
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"

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
#include "operations/operation_stack.hpp"
#include "operations/operations_window.hpp"
#include "physics/physics_tool.hpp"
#include "preview/material_preview.hpp"
#include "scene/scene_root.hpp"
#include "texture_graph/texture_graph_window.hpp"
#include "texture_graph/graph_texture.hpp"
#include "tools/material_paint_tool.hpp"
#include "tools/selection_tool.hpp"
#include "windows/editor_windows.hpp"
#include "windows/item_tree_window.hpp"
#include "windows/properties.hpp"

#include "renderers/ray_trace_renderer.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_item/item.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/mesh.hpp"

#include <nlohmann/json.hpp>

#include <memory>
#include <mutex>
#include <string>

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

    json selection = json::array();
    if (m_context.selection != nullptr) {
        for (const std::shared_ptr<erhe::Item_base>& item : m_context.selection->get_selected_items()) {
            selection.push_back(reference_json(item));
        }
    }
    result["selection"] = selection;

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
    static_cast<void>(args);

    json result;

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

    // Driver-reported figure; Vulkan only, zeros elsewhere.
    if (m_context.graphics_device != nullptr) {
        const erhe::graphics::Memory_budget budget = m_context.graphics_device->get_memory_budget();
        result["device_memory"] = {
            {"device_local_budget", budget.device_local_budget},
            {"device_local_usage",  budget.device_local_usage}
        };
    }

    // Acceleration structures pin their primitives, so this must drop too.
    result["blas_count"] = (m_context.ray_trace_renderer != nullptr)
        ? m_context.ray_trace_renderer->get_blas_count()
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

auto Mcp_server::action_move_library_item(const json& args) -> std::string
{
    const std::string scene_name  = args.value("scene_name", "");
    const std::string item_name   = args.value("item_name", "");
    const std::string folder_name = args.value("folder_name", "");
    if (item_name.empty() || folder_name.empty()) {
        return make_error_content("'item_name' and 'folder_name' are required");
    }

    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<Content_library> library = scene_root->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library");
    }

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{library->mutex};

    std::shared_ptr<Content_library_node> found_node{};
    std::shared_ptr<Content_library_node> folder_node{};
    std::size_t                           match_count{0};
    library->root->for_each<Content_library_node>(
        [&found_node, &folder_node, &match_count, &item_name, &folder_name](Content_library_node& node) -> bool {
            if (node.item && (node.item->get_name() == item_name)) {
                if (!found_node) {
                    found_node = std::dynamic_pointer_cast<Content_library_node>(node.shared_from_this());
                }
                ++match_count;
            }
            // A folder entry carries no item, only a name.
            if (!node.item && (node.get_name() == folder_name) && !folder_node) {
                folder_node = std::dynamic_pointer_cast<Content_library_node>(node.shared_from_this());
            }
            return true;
        }
    );
    if (!found_node) {
        return make_error_content("Library item not found: " + item_name);
    }
    if (match_count > 1) {
        return make_error_content(
            "Item name '" + item_name + "' matches " + std::to_string(match_count) + " library entries"
        );
    }
    if (!folder_node) {
        // Create the destination folder under the moved entry's own top-level
        // section, so a move never has to invent a type mapping.
        const std::shared_ptr<erhe::Hierarchy> parent = found_node->get_parent().lock();
        const auto parent_node = std::dynamic_pointer_cast<Content_library_node>(parent);
        if (!parent_node) {
            return make_error_content("Library item has no parent folder: " + item_name);
        }
        folder_node = parent_node->make_folder(folder_name);
    }
    if (!folder_node) {
        return make_error_content("Could not resolve destination folder: " + folder_name);
    }

    // One set_parent: erhe::Hierarchy detaches from the old parent and attaches
    // to the new one inside this single call, so the removal note the detach
    // records is cancelled by the attach before the frame's flush - a move must
    // not read as a removal.
    found_node->set_parent(folder_node);

    return make_json_content({
        {"item",   item_name},
        {"folder", folder_name},
        {"scene",  scene_root->get_name()}
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

}
