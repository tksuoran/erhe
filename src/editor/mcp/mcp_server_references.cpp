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

#include "renderers/ray_trace_renderer.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/window_imgui_host.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
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

// debug_imgui_mouse - queues pointer events into the desktop window's ImGui
// context, so a headless run can exercise ImGui interactions that only the
// pointer drives (docking splitters, window resize grips).
auto Mcp_server::action_debug_imgui_mouse(const json& args) -> std::string
{
    if (m_context.imgui_windows == nullptr) {
        return make_error_content("ImGui windows are not available");
    }
    const std::shared_ptr<erhe::imgui::Window_imgui_host> host = m_context.imgui_windows->get_window_imgui_host();
    if (!host) {
        return make_error_content("The desktop window ImGui host is not available");
    }
    if (!args.contains("x") || !args.contains("y")) {
        return make_error_content("x and y are required");
    }
    const float x = args["x"].get<float>();
    const float y = args["y"].get<float>();
    ImGuiIO& io = host->get_imgui_context()->IO;
    io.AddMousePosEvent(x, y);
    if (args.contains("pressed")) {
        const int  button  = args.value("button", 0);
        const bool pressed = args["pressed"].get<bool>();
        if ((button < 0) || (button >= ImGuiMouseButton_COUNT)) {
            return make_error_content("button is out of range");
        }
        io.AddMouseButtonEvent(button, pressed);
    }
    return make_json_content({{"x", x}, {"y", y}}).dump();
}

namespace {

[[nodiscard]] auto describe_four_view(const Four_view& four_view) -> json
{
    static constexpr const char* c_axis_names[Four_view::axis_count] = { "top", "front", "right" };
    const std::shared_ptr<Scene_root> scene_root = four_view.get_scene_root();
    const glm::vec3 focus = four_view.get_focus();
    json cameras = json::array();
    for (std::size_t i = 0; i < Four_view::axis_count; ++i) {
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
