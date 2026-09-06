// Mcp_server tools for the cached-reference bug class
// (doc/import-undo-reference-clearing.md):
//
//   get_editor_references     - every cross-frame reference the editor parts
//                               cache, so "did this window let go?" is
//                               answerable headless.
//   create_library_folder     - creates a content-library folder
//                               (doc/content-library-folders.md D7).
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
#include "operations/compound_operation.hpp"
#include "operations/item_parent_change_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/operations_window.hpp"
#include "physics/physics_tool.hpp"
#include "preview/material_preview.hpp"
#include "scene/item_lookup.hpp"
#include "scene/scene_root.hpp"

#include "erhe_scene/scene.hpp"
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
#include "erhe_primitive/mesh_optimizer.hpp"
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

namespace {

// Walks a kind-scope-rooted, slash-separated folder path
// (doc/content-library-folders.md D7: "Materials/Metals") from the library's
// kind scopes without creating anything. Returns the deepest existing scope
// in out_scope and the first missing component's name in out_missing (empty
// when the whole path exists); false when the first component names no kind
// scope.
auto walk_library_folder_path(
    const Content_library&        library,
    const std::string&            folder_path,
    std::shared_ptr<erhe::Scope>& out_scope,
    std::string&                  out_missing,
    std::string&                  out_rest
) -> bool
{
    out_scope.reset();
    out_missing.clear();
    out_rest.clear();
    std::shared_ptr<erhe::Scope> current{};
    std::size_t start = 0;
    bool first = true;
    while (start < folder_path.size()) {
        const std::size_t slash = folder_path.find('/', start);
        const std::string name  = (slash == std::string::npos) ? folder_path.substr(start) : folder_path.substr(start, slash - start);
        start = (slash == std::string::npos) ? folder_path.size() : slash + 1;
        if (name.empty()) {
            continue;
        }
        std::shared_ptr<erhe::Scope> found{};
        if (first) {
            for (const uint64_t kind_type_bit : Content_library::get_kind_type_bits()) {
                const std::shared_ptr<erhe::Scope> kind_scope = library.find_scope(kind_type_bit);
                if (kind_scope && (kind_scope->get_name() == name)) {
                    found = kind_scope;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        } else {
            for (const std::shared_ptr<erhe::Hierarchy>& child : current->get_children()) {
                const std::shared_ptr<erhe::Scope> child_scope = std::dynamic_pointer_cast<erhe::Scope>(child);
                if (child_scope && (child_scope->get_name() == name)) {
                    found = child_scope;
                    break;
                }
            }
        }
        if (!found) {
            out_scope   = current;
            out_missing = name;
            out_rest    = (start < folder_path.size()) ? folder_path.substr(start) : std::string{};
            return true;
        }
        first   = false;
        current = found;
    }
    out_scope = current;
    return !first;
}

// The kind scope a prim sits under, null when it is not below one.
[[nodiscard]] auto find_kind_scope_of(const Content_library& library, const erhe::Hierarchy& prim) -> std::shared_ptr<erhe::Scope>
{
    const uint64_t kind_type_bit = library.find_scope_kind(prim);
    return (kind_type_bit != 0) ? library.find_scope(kind_type_bit) : std::shared_ptr<erhe::Scope>{};
}

} // anonymous namespace

auto Mcp_server::action_create_library_folder(const json& args) -> std::string
{
    const std::string scene_name  = args.value("scene_name", "");
    const std::string folder_path = args.value("folder_path", "");
    if (folder_path.empty()) {
        return make_error_content("'folder_path' is required");
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

    std::shared_ptr<erhe::Scope> parent{};
    std::string                  missing{};
    std::string                  rest{};
    if (!walk_library_folder_path(*library, folder_path, parent, missing, rest)) {
        return make_error_content("Folder path does not start with a resource kind scope: " + folder_path);
    }
    if (missing.empty()) {
        return make_error_content("Folder already exists: " + folder_path);
    }
    if (!rest.empty()) {
        return make_error_content("Parent folder does not exist for: " + folder_path);
    }

    // A content-library folder is a Scope (doc/content-library-folders.md D2).
    auto new_folder = std::make_shared<erhe::Scope>(missing);
    new_folder->enable_flag_bits(erhe::Item_flags::show_in_ui);
    m_context.operation_stack->queue(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = new_folder,
                .parent  = parent,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );

    return make_json_content({
        {"folder",    folder_path},
        {"folder_id", new_folder->get_id()},
        {"scene",     scene_root->get_name()}
    }).dump();
}

auto Mcp_server::action_move_library_item(const json& args) -> std::string
{
    const std::string scene_name  = args.value("scene_name", "");
    const std::string item_name   = args.value("item_name", "");
    const std::string folder_name = args.value("folder_name", "");
    const std::string folder_path = args.value("folder_path", "");
    if (item_name.empty() || (folder_name.empty() && folder_path.empty())) {
        return make_error_content("'item_name' and one of 'folder_name' / 'folder_path' are required");
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

    // A resource is a prim of the scene tree and may sit under any prim
    // (doc/usd-compatibility-plan.md C5), not only under a kind scope: a
    // USD-backed scene keeps its materials where the file put them, so the
    // lookup walks the whole tree and a resource is one whose class names a
    // library kind.
    const std::shared_ptr<erhe::scene::Node> scene_root_node = scene_root->get_scene().get_root_node();
    if (!scene_root_node) {
        return make_error_content("Scene has no root node");
    }
    std::shared_ptr<erhe::Hierarchy> found_node{};
    std::shared_ptr<erhe::Hierarchy> folder_node{};
    std::size_t                      match_count{0};
    scene_root_node->for_each<erhe::Hierarchy>(
        [&found_node, &folder_node, &match_count, &item_name, &folder_name](erhe::Hierarchy& prim) -> bool {
            const bool is_scope = (dynamic_cast<erhe::Scope*>(&prim) != nullptr);
            if (!is_scope && (prim.get_name() == item_name) && (Content_library::get_kind_type_bit(prim) != 0)) {
                if (!found_node) {
                    found_node = prim.shared_hierarchy_from_this();
                }
                ++match_count;
            }
            if (is_scope && !folder_name.empty() && (prim.get_name() == folder_name) && !folder_node) {
                folder_node = prim.shared_hierarchy_from_this();
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
    // A destination outside the kind scopes: any prim of the scene by name
    // (doc/usd-compatibility-plan.md C5 - a resource may sit under any prim).
    if (!folder_name.empty() && !folder_node) {
        const std::shared_ptr<erhe::Item_base> named = find_item_in_scene_by_name(*scene_root, folder_name);
        folder_node = std::dynamic_pointer_cast<erhe::Hierarchy>(named);
    }
    const std::shared_ptr<erhe::Scope> parent_node = std::dynamic_pointer_cast<erhe::Scope>(found_node->get_parent().lock());
    if (!parent_node) {
        return make_error_content("Library item has no parent folder: " + item_name);
    }

    std::vector<std::shared_ptr<Operation>> operations;
    // The kind scope the destination belongs to. A folder this call creates
    // is not in the tree until the queued insert runs, so its kind is the
    // kind of the scope it will be created under.
    std::shared_ptr<erhe::Scope> destination_kind_scope{};
    if (!folder_path.empty()) {
        std::string                  missing{};
        std::string                  rest{};
        std::shared_ptr<erhe::Scope> path_scope{};
        if (!walk_library_folder_path(*library, folder_path, path_scope, missing, rest) || !missing.empty() || !path_scope) {
            return make_error_content("Folder path does not exist: " + folder_path);
        }
        folder_node            = path_scope;
        destination_kind_scope = find_kind_scope_of(*library, *folder_node);
    } else if (!folder_node) {
        // Create the destination folder under the moved entry's own parent,
        // so a move never has to invent a type mapping; the insert and the
        // move undo together.
        const std::shared_ptr<erhe::Scope> new_folder = std::make_shared<erhe::Scope>(folder_name);
        new_folder->enable_flag_bits(erhe::Item_flags::show_in_ui);
        folder_node = new_folder;
        operations.push_back(
            std::make_shared<Item_insert_remove_operation>(
                Item_insert_remove_operation::Parameters{
                    .context = m_context,
                    .item    = new_folder,
                    .parent  = parent_node,
                    .mode    = Item_insert_remove_operation::Mode::insert
                }
            )
        );
        destination_kind_scope = find_kind_scope_of(*library, *parent_node);
    } else {
        destination_kind_scope = find_kind_scope_of(*library, *folder_node);
    }
    // Under a kind scope, a resource stays under the kind scope it belongs to;
    // a destination outside every kind scope is any prim of the scene, which
    // C5 allows.
    if ((destination_kind_scope != nullptr) && (destination_kind_scope != find_kind_scope_of(*library, *found_node))) {
        return make_error_content("Destination folder is of another resource kind: " + folder_node->get_name());
    }
    if ((folder_node.get() == found_node.get()) || folder_node->is_ancestor(found_node.get())) {
        return make_error_content("Destination folder is inside the moved entry: " + folder_node->get_name());
    }

    // The move is one set_parent: erhe::Hierarchy detaches from the old
    // parent and attaches to the new one inside the call, so the removal
    // note the detach records is cancelled by the attach before the frame's
    // flush - a move must not read as a removal.
    operations.push_back(
        std::make_shared<Item_parent_change_operation>(
            folder_node,
            found_node,
            std::shared_ptr<erhe::Hierarchy>{},
            std::shared_ptr<erhe::Hierarchy>{}
        )
    );
    if (operations.size() == 1) {
        m_context.operation_stack->queue(operations.front());
    } else {
        m_context.operation_stack->queue(
            std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)})
        );
    }

    return make_json_content({
        {"item",   item_name},
        {"folder", folder_node->get_name()},
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
