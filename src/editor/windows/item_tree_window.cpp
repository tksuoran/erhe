// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "windows/item_tree_window.hpp"
#include "windows/inventory_slot_payload.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_scenes.hpp"
#include "app_settings.hpp"
#include "asset_browser/asset_browser.hpp"
#include "assets/asset_manager.hpp"
#include "brushes/brush.hpp"
#include "brushes/brush_thumbnail.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "geometry_graph/geometry_graph_mesh.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "graphics/icon_set.hpp"
#include "graphics/thumbnails.hpp"
#include "operations/compound_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/item_parent_change_operation.hpp"
#include "operations/item_reposition_in_parent_operation.hpp"
#include "operations/mesh_material_assign_operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/property_set_operation.hpp"
#include "prefabs/instance_structure.hpp"
#include "prefabs/prefab_library.hpp"
#include "preview/brush_preview.hpp"
#include "scene/item_lookup.hpp"
#include "scene/scene_root.hpp"
#include "tools/clipboard.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tool.hpp"
#include "windows/active_scene_highlight.hpp"
#include "windows/editor_windows.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_file/file.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_item_recorder.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_item/scope.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_property/property_value.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_utility/bit_helpers.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <optional>

#define ICON_MDI_FILTER                                   "\xf3\xb0\x88\xb2" // U+F0232
#define ICON_MDI_LINK                                     "\xf3\xb0\x8c\xb7" // U+F0337

namespace editor {

using Light_type = erhe::scene::Light_type;

namespace {

// Live Item_tree instances, in construction order. Raw pointers: entries are
// added and removed by the constructor / destructor below.
std::vector<Item_tree*> g_item_trees;

// True when the payload is named for an erhe item class (Item_type::c_bit_labels),
// which is how every item drag in the editor names its payload. Only such a
// payload carries an Item_base*: ImGui's own docking drag ("_IMWINDOW") carries
// an ImGuiWindow* of the same size, so the pointer must never be read before
// this check passes.
[[nodiscard]] auto is_item_payload(const ImGuiPayload* const payload) -> bool
{
    if (payload == nullptr) {
        return false;
    }
    for (uint64_t i = 1; i < erhe::Item_type::count; ++i) {
        if (payload->IsDataType(erhe::Item_type::c_bit_labels[i])) {
            return true;
        }
    }
    return false;
}

// The item the drag payload carries, when the payload is one of the item
// tree's own (the tree names a payload for the dragged item's class, see
// SetDragDropPayload below).
[[nodiscard]] auto peek_prim_payload(const ImGuiPayload* const payload_peek) -> std::shared_ptr<erhe::Item_base>
{
    if (!is_item_payload(payload_peek) || (payload_peek->Data == nullptr) || (payload_peek->DataSize != sizeof(erhe::Item_base*))) {
        return {};
    }
    erhe::Item_base* const raw = *static_cast<erhe::Item_base**>(payload_peek->Data);
    if (raw == nullptr) {
        return {};
    }
    if (!payload_peek->IsDataType(raw->get_type_name().data())) {
        return {};
    }
    return raw->shared_from_this();
}

// The content library of the scene whose tree holds the item, or null when
// the item is not hosted by a registered scene.
[[nodiscard]] auto find_owning_library(App_context& context, const std::shared_ptr<erhe::Item_base>& item) -> std::shared_ptr<Content_library>
{
    if (!item || (context.app_scenes == nullptr)) {
        return {};
    }
    erhe::Item_host* const host = item->get_item_host();
    if (host == nullptr) {
        return {};
    }
    for (const std::shared_ptr<Scene_root>& scene_root : context.app_scenes->get_scene_roots()) {
        if (static_cast<erhe::Item_host*>(scene_root.get()) == host) {
            return scene_root->get_content_library();
        }
    }
    return {};
}

// The rows that take the structural move (before / into / after), as target
// and as payload: every prim of the one object model
// (doc/erhe/usd_compatibility_design.md C5), whatever its kind. The Scene
// header row is not a prim.
[[nodiscard]] auto is_tree_prim(const std::shared_ptr<erhe::Item_base>& item) -> bool
{
    return std::dynamic_pointer_cast<erhe::Typed>(item) != nullptr;
}

}

auto Item_tree::get_instances() -> const std::vector<Item_tree*>&
{
    return g_item_trees;
}

Item_tree::Item_tree(App_context& context)
    : m_context{context}
    , m_filter{
        .require_all_bits_set           = 0,
        .require_at_least_one_bit_set   = erhe::Item_flags::show_in_ui,
        .require_all_bits_clear         = 0, //erhe::Item_flags::tool | erhe::Item_flags::brush,
        .require_at_least_one_bit_clear = 0
    }
{
    g_item_trees.push_back(this);
    if (m_context.app_message_bus != nullptr) {
        m_items_removed_subscription = m_context.app_message_bus->items_removed.subscribe(
            [this](Items_removed_message& message) {
                on_items_removed(*message.removed.get());
            }
        );
    }
}

Item_tree::~Item_tree() noexcept
{
    const auto i = std::find(g_item_trees.begin(), g_item_trees.end(), this);
    if (i != g_item_trees.end()) {
        g_item_trees.erase(i);
    }
}

void Item_tree::on_items_removed(const Removed_items& removed)
{
    // The row cache holds a shared_ptr to every listed item AND to the brush
    // of a brush row; a tree that is not rendering never rebuilds it, so drop
    // it wholesale. The hover / popup pins are reset only by the render path
    // (see imgui_tree), so they are what a hidden tree keeps alive.
    if (m_hovered_item && removed.lookup.contains(m_hovered_item.get())) {
        m_hovered_item.reset();
    }
    if (m_popup_item && removed.lookup.contains(m_popup_item.get())) {
        m_popup_item.reset();
        m_popup_id_string.clear();
        m_popup_id = 0;
    }
    for (const Flat_row& row : m_flat_rows) {
        const bool row_removed =
            (row.item  && removed.lookup.contains(row.item.get())) ||
            (row.brush && removed.lookup.contains(static_cast<const erhe::Item_base*>(row.brush.get())));
        if (row_removed) {
            clear_cached_rows();
            break;
        }
    }
}

void Item_tree::set_tree_label(const std::string_view label)
{
    m_tree_label.assign(label);
}

auto Item_tree::get_tree_label() const -> const std::string&
{
    return m_tree_label;
}

auto Item_tree::get_popup_item() const -> const std::shared_ptr<erhe::Item_base>&
{
    return m_popup_item;
}

auto Item_tree::get_cached_row_count() const -> std::size_t
{
    return m_flat_rows.size();
}

void Item_tree::debug_set_hovered_item(const std::shared_ptr<erhe::Item_base>& item)
{
    m_hovered_item = item;
    m_popup_item   = item;
}

void Item_tree::set_root(const std::shared_ptr<erhe::Hierarchy>& root)
{
    m_root = root;
    m_flat_rows_dirty = true;
}

auto Item_tree::get_root() const -> const std::shared_ptr<erhe::Hierarchy>&
{
    return m_root;
}

void Item_tree::set_header_item(const std::shared_ptr<erhe::Item_base>& item)
{
    m_header_item = item;
    m_flat_rows_dirty = true;
}

void Item_tree::set_item_filter(const erhe::Item_filter& filter)
{
    m_filter = filter;
    m_flat_rows_dirty = true;
}

void Item_tree::clear_cached_rows()
{
    m_flat_rows.clear();
    m_flat_rows_dirty = true;
}

void Item_tree::set_item_callback(std::function<bool(const std::shared_ptr<erhe::Item_base>&)> fun)
{
    m_item_callback = fun;
}

void Item_tree::set_hover_callback(std::function<void()> fun)
{
    m_hover_callback = fun;
}

void Item_tree::add_item_context_menu_callback(Context_menu_callback fun)
{
    m_item_context_menu_callbacks.push_back(std::move(fun));
}

void Item_tree::clear_selection()
{
    SPDLOG_LOGGER_TRACE(log_tree, "clear_selection()");

    // Scoped to this window's root (its scene / library); other hosts'
    // selections are left untouched.
    m_context.selection->clear_selection(m_root ? m_root->get_item_host() : nullptr);
}

void Item_tree::collect_items_recursive(
    const std::shared_ptr<erhe::Item_base>&        item,
    std::vector<std::shared_ptr<erhe::Item_base>>& out_items
)
{
    out_items.push_back(item);
    const std::shared_ptr<erhe::Hierarchy> hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
    if (!hierarchy) {
        return;
    }

    for (const std::shared_ptr<erhe::Hierarchy>& child : hierarchy->get_children()) {
        collect_items_recursive(child, out_items);
    }
}

void Item_tree::select_all()
{
    SPDLOG_LOGGER_TRACE(log_tree, "select_all()");

    // Select all within this window's root (its scene / library) only;
    // other scenes' selections are left untouched.
    if (!m_root) {
        return;
    }
    erhe::Item_host* const item_host = m_root->get_item_host();

    // The whole subtree enters the selection in ONE selection change. A
    // Selection::add_to_selection() per item closes a selection change each
    // time, and every close re-sorts and diffs the whole selection and
    // publishes a Selection_message - quadratic in the subtree size, which on
    // a large tree (the asset browser's thousands of entries) does not
    // complete.
    std::vector<std::shared_ptr<erhe::Item_base>> items;
    for (const std::shared_ptr<erhe::Item_base>& item : m_context.selection->get_selected_items()) {
        if (!m_context.selection->is_hosted_or_defined_by(*item, item_host)) {
            items.push_back(item);
        }
    }
    const std::size_t other_host_count = items.size();
    for (const std::shared_ptr<erhe::Hierarchy>& node : m_root->get_children()) {
        collect_items_recursive(node, items);
    }

    // The last item of this tree becomes the active item, which is what an
    // add per item left behind. An empty tree keeps the current active item.
    const std::shared_ptr<erhe::Item_base> active = (items.size() > other_host_count)
        ? items.back()
        : std::shared_ptr<erhe::Item_base>{};
    m_context.selection->set_selection(items, active);
}

template <typename T, typename U>
[[nodiscard]] auto is_in(const T& item, const std::vector<U>& items) -> bool
{
    return std::find(items.begin(), items.end(), item) != items.end();
}

void Item_tree::move_selection(const std::shared_ptr<erhe::Item_base>& target_node, erhe::Item_base* payload_item, const Placement placement)
{
    log_tree->trace(
        "move_selection(anchor = {}, {})",
        target_node ? target_node->get_name() : "(empty)",
        (placement == Placement::Before_anchor)
            ? "Before_anchor"
            : "After_anchor"
    );

    Compound_operation::Parameters compound_parameters;
    const auto& selection = m_context.selection->get_selected_items();
    const std::shared_ptr<erhe::Item_base> drag_item = payload_item->shared_from_this();

    std::shared_ptr<erhe::Item_base> anchor = target_node;
    if (is_in(drag_item, selection)) {
        // Dragging node which is part of the selection.
        // In this case we apply reposition to whole selection.
        if (placement == Placement::Before_anchor) {
            for (const auto& item : selection) {
                if (is_tree_prim(item)) {
                    reposition(compound_parameters, anchor, item, placement, Selection_usage::Selection_used);
                }
            }
        } else { // if (placement == Placement::After_anchor)
            for (auto i = selection.rbegin(), end = selection.rend(); i < end; ++i) {
                const auto& item = *i;
                if (is_tree_prim(item)) {
                    reposition(compound_parameters, anchor, item, placement, Selection_usage::Selection_used);
                }
            }
        }
    } else if (compound_parameters.operations.empty()) {
        // Dragging a single node which is not part of the selection.
        // In this case we ignore selection and apply operation only to dragged node.
        if (is_tree_prim(drag_item)) {
            reposition(compound_parameters, anchor, drag_item, placement, Selection_usage::Selection_ignored);
        }
    }

    if (!compound_parameters.operations.empty()) {
        m_operation = std::make_shared<Compound_operation>(std::move(compound_parameters));
    }
}

namespace {

[[nodiscard]] auto get_ancestor_in(const std::shared_ptr<erhe::Item_base>& item, const std::vector<std::shared_ptr<erhe::Item_base>>& selection) -> std::shared_ptr<erhe::Item_base>
{
    const auto hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
    if (!hierarchy) {
        return {};
    }

    const auto& parent = hierarchy->get_parent().lock();
    if (parent) {
        if (is_in(parent, selection)) {
            return parent;
        }
        return get_ancestor_in(parent, selection);
    } else {
        return {};
    }
}

} // anonymous namespace

void Item_tree::reposition(
    Compound_operation::Parameters&         compound_parameters,
    const std::shared_ptr<erhe::Item_base>& anchor,
    const std::shared_ptr<erhe::Item_base>& item,
    const Placement                         placement,
    const Selection_usage                   selection_usage
)
{
    SPDLOG_LOGGER_TRACE(
        log_tree,
        "reposition(anchor_node = {}, node = {}, placement = {})",
        anchor ? anchor->get_name() : "(empty)",
        item ? item->get_name() : "(empty)",
        (placement == Placement::Before_anchor)
            ? "Before_anchor"
            : "After_anchor"
    );

    if (!item) {
        SPDLOG_LOGGER_WARN(log_tree, "Bad empty item");
        return;
    }

    if (!anchor) {
        SPDLOG_LOGGER_WARN(log_tree, "Bad empty anchor");
        return;
    }

    // Nodes cannot be attached to themselves
    if (item == anchor) {
        return;
    }

    if (!is_tree_prim(item) || !is_tree_prim(anchor)) {
        return;
    }

    const auto hierarchy        = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
    const auto anchor_hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(anchor);
    if (!hierarchy || !anchor_hierarchy) {
        return;
    }

    // Ancestors cannot be attached to descendants
    if (anchor_hierarchy->is_ancestor(hierarchy.get())) {
        SPDLOG_LOGGER_WARN(log_tree, "Ancestors cannot be moved as child of descendant");
        return;
    }

    if (selection_usage == Selection_usage::Selection_used) {
        const auto& selection = m_context.selection->get_selected_items();

        // Ignore nodes if their ancestors is in selection
        const auto ancestor_in_selection = get_ancestor_in(item, selection);
        if (ancestor_in_selection) {
            SPDLOG_LOGGER_TRACE(
                log_tree,
                "Ignoring node {} because ancestor {} is in selection",
                item->get_name(),
                ancestor_in_selection->get_name()
            );
            return;
        }
    }

    // A reference instance protects its structure (doc/erhe/usd_compatibility_design.md
    // X2): an item inside an instance is not reparented, and nothing is
    // inserted under a carrier or inside one.
    const std::optional<std::string> item_refusal = instance_structure_refusal(*item);
    if (item_refusal.has_value()) {
        log_tree->info("Move refused: {}", item_refusal.value());
        return;
    }
    const std::shared_ptr<erhe::Hierarchy> anchor_parent = anchor_hierarchy->get_parent().lock();
    if (anchor_parent) {
        const std::optional<std::string> parent_refusal = instance_child_refusal(*anchor_parent);
        if (parent_refusal.has_value()) {
            log_tree->info("Move refused: {}", parent_refusal.value());
            return;
        }
    }

    if (anchor_hierarchy->get_parent().lock() != hierarchy->get_parent().lock()) {
        compound_parameters.operations.push_back(
            std::make_shared<Item_parent_change_operation>(
                anchor_hierarchy->get_parent().lock(),
                hierarchy,
                (placement == Placement::Before_anchor) ? anchor_hierarchy : std::shared_ptr<erhe::Hierarchy>{},
                (placement == Placement::After_anchor ) ? anchor_hierarchy : std::shared_ptr<erhe::Hierarchy>{}
            )
        );
        return;
    }

    compound_parameters.operations.push_back(
        std::make_shared<Item_reposition_in_parent_operation>(
            hierarchy,
            (placement == Placement::Before_anchor) ? anchor_hierarchy : std::shared_ptr<erhe::Hierarchy>{},
            (placement == Placement::After_anchor ) ? anchor_hierarchy : std::shared_ptr<erhe::Hierarchy>{}
        )
    );
}

void Item_tree::try_add_to_attach(
    Compound_operation::Parameters&         compound_parameters,
    const std::shared_ptr<erhe::Item_base>& target,
    const std::shared_ptr<erhe::Item_base>& item,
    const Selection_usage                   selection_usage
)
{
    SPDLOG_LOGGER_TRACE(
        log_tree,
        "try_add_to_attach(target = {}, item = {})",
        target ? target->get_name() : "(empty)",
        item   ? item->get_name()   : "(empty)"
    );

    if (!item) {
        SPDLOG_LOGGER_WARN(log_tree, "Bad empty item");
        return;
    }

    if (!target) {
        SPDLOG_LOGGER_WARN(log_tree, "Bad empty target");
        return;
    }

    // Nodes cannot be attached to themselves
    if (item == target) {
        SPDLOG_LOGGER_WARN(log_tree, "Nodes cannot be moved as child of themselves");
        return;
    }

    if (!is_tree_prim(item) || !is_tree_prim(target)) {
        return;
    }

    const auto hierarchy        = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
    const auto target_hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(target);
    if (!hierarchy || !target_hierarchy) {
        return;
    }

    // Ancestors cannot be attached to descendants
    if (target_hierarchy->is_ancestor(hierarchy.get())) {
        SPDLOG_LOGGER_WARN(log_tree, "Ancestors cannot be moved to child of descendant");
        return;
    }

    if (selection_usage == Selection_usage::Selection_used) {
        const auto& selection = m_context.selection->get_selected_items();

        // Ignore item if their ancestors is in selection
        const auto ancestor_in_selection = get_ancestor_in(item, selection);
        if (ancestor_in_selection) {
            SPDLOG_LOGGER_TRACE(
                log_tree,
                "Ignoring item '{}' because ancestor '{}' is in selection",
                item->describe(),
                ancestor_in_selection->describe()
            );
            return;
        }
    }

    const std::optional<std::string> item_refusal = instance_structure_refusal(*item);
    if (item_refusal.has_value()) {
        log_tree->info("Move refused: {}", item_refusal.value());
        return;
    }
    const std::optional<std::string> target_refusal = instance_child_refusal(*target_hierarchy);
    if (target_refusal.has_value()) {
        log_tree->info("Move refused: {}", target_refusal.value());
        return;
    }

    compound_parameters.operations.push_back(
        std::make_shared<Item_parent_change_operation>(
            target_hierarchy,
            hierarchy,
            std::shared_ptr<erhe::Hierarchy>{},
            std::shared_ptr<erhe::Hierarchy>{}
        )
    );
}

void Item_tree::attach_selection_to(const std::shared_ptr<erhe::Item_base>& target, erhe::Item_base* payload_item)
{
    SPDLOG_LOGGER_TRACE(log_tree, "attach_selection_to()");

    //// log_tools->trace(
    ////     "attach_selection_to(target_node = {}, payload_id = {})",
    ////     target_node->get_name(),
    ////     payload_id
    //// );
    Compound_operation::Parameters compound_parameters;
    const auto& selection = m_context.selection->get_selected_items();
    const std::shared_ptr<erhe::Item_base> drag_item = payload_item->shared_from_this();

    if (is_in(drag_item, selection)) {
        for (const auto& item : selection) {
            try_add_to_attach(compound_parameters, target, item, Selection_usage::Selection_used);
        }
    } else if (compound_parameters.operations.empty()) {
        try_add_to_attach(compound_parameters, target, drag_item, Selection_usage::Selection_ignored);
    }

    if (!compound_parameters.operations.empty()) {
        m_operation = std::make_shared<Compound_operation>(std::move(compound_parameters));
    }
}

void Item_tree::drag_and_drop_source(const std::shared_ptr<erhe::Item_base>& item)
{
    ERHE_PROFILE_SCOPE("drag_and_drop_source"); // named zone: no per-row callstack capture

    // log_tree_frame->trace("DnD source: '{}'", item->describe());

    if (erhe::imgui::begin_drag_drop_source(ImGuiDragDropFlags_SourceAllowNullID)) {
        erhe::Item_base* item_raw = item.get();
        ImGui::SetDragDropPayload(item->get_type_name().data(), &item_raw, sizeof(item_raw));

        const auto& selection = m_context.selection->get_selected_items();
        if (is_in(item, selection)) {
            for (const auto& selection_item : selection) {
                item_icon_and_text(selection_item);
            }
        } else {
            item_icon_and_text(item);
        }
        ImGui::EndDragDropSource();
    }
}

namespace {

void drag_and_drop_rectangle_preview(const ImRect rect)
{
    const auto* g       = ImGui::GetCurrentContext();
    const auto* window  = g->CurrentWindow;
    const auto& payload = g->DragDropPayload;

    if (payload.Preview) {
        window->DrawList->AddRect(
            rect.Min - ImVec2{0.0f, 3.5f},
            rect.Max + ImVec2{0.0f, 3.5f},
            ImGui::GetColorU32(ImGuiCol_DragDropTarget),
            0.0f,
            2.0f,
            ImDrawFlags_None
        );
    }
}

void drag_and_drop_gradient_preview(
    const float x0,
    const float x1,
    const float y0,
    const float y1,
    const ImU32 top,
    const ImU32 bottom
)
{
    const auto* g       = ImGui::GetCurrentContext();
    const auto* window  = g->CurrentWindow;
    const auto& payload = g->DragDropPayload;

    if (payload.Preview) {
        window->DrawList->AddRectFilledMultiColor(
            ImVec2{x0, y0},
            ImVec2{x1, y1},
            top,
            top,
            bottom,
            bottom
        );
    }
}

// The world transform of a new child prim with an identity local transform
// under `parent`: the world transform of the nearest Xformable at or above
// `parent` (a prim without a transform, such as a Scope or a Material, passes
// its ancestor's transform through), identity when there is none.
[[nodiscard]] auto world_from_new_child(const erhe::Hierarchy& parent) -> glm::mat4
{
    std::shared_ptr<const erhe::Hierarchy> prim = std::static_pointer_cast<const erhe::Hierarchy>(parent.shared_from_this());
    while (prim) {
        const std::shared_ptr<const erhe::scene::Node> node = std::dynamic_pointer_cast<const erhe::scene::Node>(prim);
        if (node) {
            return node->world_from_node();
        }
        prim = prim->get_parent().lock();
    }
    return glm::mat4{1.0f};
}

// glTF asset dropped from the Asset browser onto the scene hierarchy:
// parse the file once through the prefab library (cached app-wide) and
// insert an instance clone under the chosen parent prim, with an identity
// local transform, as one undoable operation (mirrors the viewport drop).
void instantiate_gltf_prefab(
    App_context&                            context,
    Asset_file_gltf&                        gltf,
    Scene_root&                             scene_root,
    const std::shared_ptr<erhe::Hierarchy>& parent,
    const std::size_t                       index_in_parent
)
{
    if (!parent) {
        return;
    }
    const std::filesystem::path* source_path = gltf.get_source_path();
    if ((source_path == nullptr) || (context.prefab_library == nullptr)) {
        return;
    }
    // Asynchronous prefab load (doc/editor/async_asset_loading_design.md step 7): the
    // drop returns immediately and the instance appears once the template is
    // ready. Scene_root is enable_shared_from_this, so the callback can hold
    // it weakly and bail if the scene closed meanwhile.
    const std::weak_ptr<Scene_root> weak_scene_root = scene_root.shared_from_this();
    const std::filesystem::path     path            = *source_path;
    context.prefab_library->get_or_load_async(
        path,
        [&context, weak_scene_root, path, parent, index_in_parent](const std::shared_ptr<Prefab>& prefab) {
            if (!prefab) {
                log_tree->warn("Dropped glTF could not be loaded as a prefab: {}", path.string());
                return;
            }
            const std::shared_ptr<Scene_root> target = weak_scene_root.lock();
            if (!target || !parent->get_item_host()) {
                return; // scene closed, or the drop parent left the scene
            }
            instantiate_prefab(context, prefab, *target, world_from_new_child(*parent), parent, index_in_parent);
        }
    );
}

}

// The geometry of one tree row as a drop target: its rect, the split lines of
// the before / into / after zones, and the ImGui ids of the zones.
class Item_tree_drop_row
{
public:
    ImVec2  rect_min {};
    ImVec2  rect_max {};
    float   x0       {0.0f};
    float   x1       {0.0f};
    float   y0       {0.0f};
    float   y1       {0.0f};
    float   y2       {0.0f};
    float   y3       {0.0f};
    ImGuiID id_top   {0};
    ImGuiID id_center{0};
    ImGuiID id_bottom{0};
};

namespace {

enum class Drop_zone : unsigned int {
    before = 0, // sibling before the row
    into,       // last child of the row
    after       // sibling after the row
};

// Which of the three zones a drop target offers.
class Drop_zones
{
public:
    bool before{true};
    bool into  {true};
    bool after {true};
};

// The before / into / after drop target of a row: top third, middle third
// and bottom third, each with its own preview. `on_drop(Drop_zone)` runs when
// a payload of `payload_type` is delivered. True while the row is the
// hovered drop target.
template <typename On_drop>
[[nodiscard]] auto three_zone_drop_target(
    const Item_tree_drop_row& row,
    const char* const         payload_type,
    const Drop_zones          zones,
    On_drop&&                 on_drop
) -> bool
{
    if (zones.before) {
        const ImRect top_rect{row.rect_min, ImVec2{row.rect_max.x, row.y1}};
        if (ImGui::BeginDragDropTargetCustom(top_rect, row.id_top)) {
            drag_and_drop_gradient_preview(row.x0, row.x1, row.y0, row.y2, ImGui::GetColorU32(ImGuiCol_DragDropTarget), 0);
            if (ImGui::AcceptDragDropPayload(payload_type, ImGuiDragDropFlags_AcceptNoDrawDefaultRect) != nullptr) {
                on_drop(Drop_zone::before);
            }
            ImGui::EndDragDropTarget();
            return true;
        }
    }

    if (zones.into) {
        const ImRect middle_rect{ImVec2{row.rect_min.x, row.y1}, ImVec2{row.rect_max.x, row.y2}};
        if (ImGui::BeginDragDropTargetCustom(middle_rect, row.id_center)) {
            drag_and_drop_rectangle_preview(middle_rect);
            if (ImGui::AcceptDragDropPayload(payload_type, ImGuiDragDropFlags_AcceptNoDrawDefaultRect) != nullptr) {
                on_drop(Drop_zone::into);
            }
            ImGui::EndDragDropTarget();
            return true;
        }
    }

    if (zones.after) {
        const ImRect bottom_rect{ImVec2{row.rect_min.x, row.y2}, row.rect_max};
        if (ImGui::BeginDragDropTargetCustom(bottom_rect, row.id_bottom)) {
            drag_and_drop_gradient_preview(row.x0, row.x1, row.y1, row.y3, 0, ImGui::GetColorU32(ImGuiCol_DragDropTarget));
            if (ImGui::AcceptDragDropPayload(payload_type, ImGuiDragDropFlags_AcceptNoDrawDefaultRect) != nullptr) {
                on_drop(Drop_zone::after);
            }
            ImGui::EndDragDropTarget();
            return true;
        }
    }
    return false;
}

// The whole-row drop target of an action: `on_drop()` runs when a payload of
// `payload_type` is delivered. True while the row is the hovered drop target.
template <typename On_drop>
[[nodiscard]] auto whole_row_drop_target(
    const Item_tree_drop_row& row,
    const char* const         payload_type,
    On_drop&&                 on_drop
) -> bool
{
    const ImRect rect{row.rect_min, row.rect_max};
    if (ImGui::BeginDragDropTargetCustom(rect, row.id_center)) {
        drag_and_drop_rectangle_preview(rect);
        if (ImGui::AcceptDragDropPayload(payload_type, ImGuiDragDropFlags_AcceptNoDrawDefaultRect) != nullptr) {
            on_drop();
        }
        ImGui::EndDragDropTarget();
        return true;
    }
    return false;
}

// Material dropped onto a brush: a brush with the brush's geometry and that
// material joins the brush's parent, unless the library already has one.
void fork_brush_with_material(
    Content_library&                                  library,
    const std::shared_ptr<Brush>&                     target_brush,
    const std::shared_ptr<erhe::primitive::Material>& material
)
{
    const std::shared_ptr<erhe::geometry::Geometry> original_geometry = target_brush->get_geometry();
    if (!original_geometry) {
        // A brush whose geometry preparation failed has nothing to fork: a
        // null geometry would match every other failed brush.
        log_brush->warn("Brush '{}' has no geometry: not forked with material '{}'", target_brush->get_name(), material->get_name());
        return;
    }
    for (const std::shared_ptr<Brush>& existing_brush : library.get_all<Brush>()) {
        if ((existing_brush->get_geometry() == original_geometry) && (existing_brush->get_material() == material)) {
            return;
        }
    }
    const std::shared_ptr<Brush> forked = target_brush->make_with_material(material);
    const std::shared_ptr<erhe::Hierarchy> brush_parent = target_brush->get_parent().lock();
    if (brush_parent) {
        forked->set_parent(brush_parent);
    } else {
        library.add(forked);
    }
}

} // anonymous namespace

auto Item_tree::material_assign_drop_target(
    const Item_tree_drop_row&                         row,
    const std::shared_ptr<erhe::scene::Node>&         node,
    const std::shared_ptr<erhe::primitive::Material>& material,
    const char* const                                 payload_type
) -> std::optional<bool>
{
    const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(node.get());
    if (!mesh || mesh->get_primitives().empty()) {
        return std::nullopt;
    }
    return whole_row_drop_target(
        row,
        payload_type,
        [this, &mesh, &material]() {
            queue_mesh_material_assign_to_all_primitives(m_context, mesh, material);
        }
    );
}

auto Item_tree::brush_drop_target(
    const Item_tree_drop_row&                         row,
    const std::shared_ptr<erhe::Hierarchy>&           prim,
    const std::shared_ptr<Brush>&                     brush,
    const std::shared_ptr<erhe::primitive::Material>& material,
    const char* const                                 payload_type
) -> std::optional<bool>
{
    Scene_root* const scene_root = find_scene_root_for_item(m_context, *prim);
    if (scene_root == nullptr) {
        return std::nullopt;
    }
    const auto insert_brush_instance = [this, &brush, &material, scene_root](
        const std::shared_ptr<erhe::Hierarchy>& parent,
        const std::size_t                       index_in_parent
    ) {
        if (!parent) {
            return;
        }
        // Material priority: payload material (inventory slot) > brush material > default
        std::shared_ptr<erhe::primitive::Material> brush_material = material ? material : brush->get_material();
        if (!brush_material) {
            brush_material = get_default_material(m_context, *scene_root);
        }
        if (!brush_material) {
            return;
        }
        place_brush_in_scene(
            m_context,
            *brush,
            *scene_root,
            world_from_new_child(*parent), // identity local transform under the chosen parent
            brush_material,
            1.0,
            erhe::physics::Motion_mode::e_dynamic,
            parent,
            index_in_parent
        );
    };
    // The brush instance joins the tree beside the row (its parent) or under
    // it (any prim parents any prim, doc/erhe/usd_compatibility_design.md C5). A
    // reference instance protects its structure (X2): a zone is offered only
    // where the parent it inserts under accepts children.
    const std::shared_ptr<erhe::Hierarchy> prim_parent = prim->get_parent().lock();
    return three_zone_drop_target(
        row,
        payload_type,
        Drop_zones{
            .before = prim_parent && !refuses_instance_child(*prim_parent),
            .into   = !refuses_instance_child(*prim),
            .after  = prim_parent && !refuses_instance_child(*prim_parent)
        },
        [&prim, &prim_parent, &insert_brush_instance](const Drop_zone zone) {
            switch (zone) {
                case Drop_zone::before: insert_brush_instance(prim_parent, prim->get_index_in_parent()); break;
                case Drop_zone::into:   insert_brush_instance(prim, std::numeric_limits<std::size_t>::max()); break;
                case Drop_zone::after:  insert_brush_instance(prim_parent, prim->get_index_in_parent() + 1); break;
            }
        }
    );
}

auto Item_tree::move_drop_target(
    const Item_tree_drop_row&               row,
    const std::shared_ptr<erhe::Item_base>& item,
    const std::shared_ptr<erhe::Item_base>& payload_prim
) -> bool
{
    return three_zone_drop_target(
        row,
        payload_prim->get_type_name().data(),
        Drop_zones{},
        [this, &item, &payload_prim](const Drop_zone zone) {
            switch (zone) {
                case Drop_zone::before: move_selection     (item, payload_prim.get(), Placement::Before_anchor); break;
                case Drop_zone::into:   attach_selection_to(item, payload_prim.get()); break;
                case Drop_zone::after:  move_selection     (item, payload_prim.get(), Placement::After_anchor); break;
            }
        }
    );
}

auto Item_tree::action_drop_target(
    const Item_tree_drop_row&               row,
    const std::shared_ptr<erhe::Item_base>& item,
    const std::shared_ptr<erhe::Item_base>& payload_prim
) -> std::optional<bool>
{
    const char* const payload_type = payload_prim->get_type_name().data();

    const std::shared_ptr<erhe::scene::Node>         node           = std::dynamic_pointer_cast<erhe::scene::Node>(item);
    const std::shared_ptr<Brush>                     target_brush   = std::dynamic_pointer_cast<Brush>(item);
    const std::shared_ptr<erhe::primitive::Material> material       = std::dynamic_pointer_cast<erhe::primitive::Material>(payload_prim);
    const std::shared_ptr<Brush>                     brush          = std::dynamic_pointer_cast<Brush>(payload_prim);
    const std::shared_ptr<Graph_mesh>                graph_mesh     = std::dynamic_pointer_cast<Graph_mesh>(payload_prim);
    const std::shared_ptr<Content_library>           target_library = find_owning_library(m_context, item);

    // Graph Mesh onto a scene node: source the node's mesh from the graph by
    // writing the node's Geometry_graph_mesh.graph_mesh value - same bind
    // logic as Properties and MCP set_node_graph_mesh. The graph mesh comes
    // from the node's own scene content library; the scene file resolves the
    // binding by name in that library on load, so a cross-scene bind would
    // not survive a save/load round-trip.
    if (graph_mesh && node && target_library && target_library->has_item(*graph_mesh)) {
        return whole_row_drop_target(
            row,
            payload_type,
            [&node, &graph_mesh]() {
                // Writing the value materializes the asset's latest bake
                // immediately; a never-baked asset applies on its first
                // evaluation push.
                set_geometry_graph_mesh(*node.get(), graph_mesh);
            }
        );
    }

    // Material onto a node holding a mesh: assign it to every primitive.
    if (material && node) {
        const std::optional<bool> assign = material_assign_drop_target(row, node, material, payload_type);
        if (assign.has_value()) {
            return assign;
        }
    }

    // Material onto a brush of this scene: fork the brush with that material.
    if (material && target_brush && target_library && target_library->has_item(*target_brush)) {
        return whole_row_drop_target(
            row,
            payload_type,
            [&target_library, &target_brush, &material]() {
                fork_brush_with_material(*target_library, target_brush, material);
            }
        );
    }

    // Brush onto a prim: place a brush instance before / under / after it.
    const std::shared_ptr<erhe::Hierarchy> prim = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
    if (brush && prim) {
        const std::optional<bool> placement = brush_drop_target(row, prim, brush, {}, payload_type);
        if (placement.has_value()) {
            return placement;
        }
    }

    // Material from another scene's library: copy it into this scene's
    // library, as a child of the row.
    if (material && target_library && !target_library->has_item(*material) && (m_context.asset_manager != nullptr)) {
        const std::shared_ptr<erhe::Hierarchy> parent     = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
        Scene_root* const                      scene_root = find_scene_root_for_item(m_context, *item);
        if (parent && (scene_root != nullptr) && !refuses_instance_child(*parent)) {
            return whole_row_drop_target(
                row,
                payload_type,
                [this, &material, &parent, scene_root]() {
                    const std::shared_ptr<erhe::primitive::Material> new_material =
                        m_context.asset_manager->create<erhe::primitive::Material>(*scene_root, *material);
                    m_context.operation_stack->queue(
                        std::make_shared<Item_insert_remove_operation>(
                            Item_insert_remove_operation::Parameters{
                                .context = m_context,
                                .item    = new_material,
                                .parent  = parent,
                                .mode    = Item_insert_remove_operation::Mode::insert
                            }
                        )
                    );
                }
            );
        }
    }

    return std::nullopt;
}

auto Item_tree::drag_and_drop_target(const std::shared_ptr<erhe::Item_base>& item) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (!item) {
        log_tree_frame->trace("DnD target item is empty");
        return false;
    }

    const ImGuiPayload* const payload_peek = ImGui::GetDragDropPayload();
    if (payload_peek == nullptr) {
        return false;
    }

    const ImVec2 rect_min = ImGui::GetItemRectMin();
    const ImVec2 rect_max = ImGui::GetItemRectMax();
    const float  height   = rect_max.y - rect_min.y;

    const auto        id           = item->get_id();
    const std::string label_top    = fmt::format("node dnd top {}: {} {}",    id, item->get_type_name(), item->get_name());
    const std::string label_center = fmt::format("node dnd center {}: {} {}", id, item->get_type_name(), item->get_name());
    const std::string label_bottom = fmt::format("node dnd bottom {}: {} {}", id, item->get_type_name(), item->get_name());

    const Item_tree_drop_row row{
        .rect_min  = rect_min,
        .rect_max  = rect_max,
        .x0        = rect_min.x,
        .x1        = rect_max.x,
        .y0        = rect_min.y,
        .y1        = rect_min.y + (0.3f * height),
        .y2        = rect_max.y - (0.3f * height),
        .y3        = rect_max.y,
        .id_top    = ImGui::GetID(label_top.c_str()),
        .id_center = ImGui::GetID(label_center.c_str()),
        .id_bottom = ImGui::GetID(label_bottom.c_str())
    };

    // glTF asset dragged from the Asset browser: extract the payload once;
    // accepted below by the Scene header row and by scene node rows.
    std::shared_ptr<Asset_file_gltf> gltf_asset{};
    if (payload_peek->IsDataType(Asset_file_gltf::static_type_name.data())) {
        erhe::Item_base* payload_item_base = *(static_cast<erhe::Item_base**>(payload_peek->Data));
        gltf_asset = std::dynamic_pointer_cast<Asset_file_gltf>(payload_item_base->shared_from_this());
    }

    // Dropping a glTF asset on the Scene header row inserts the prefab
    // instance as the last child of the scene root node.
    if (gltf_asset) {
        const std::shared_ptr<erhe::scene::Scene> scene_item = std::dynamic_pointer_cast<erhe::scene::Scene>(item);
        if (scene_item) {
            const std::shared_ptr<erhe::scene::Node> root_node = scene_item->get_root_node();
            Scene_root* scene_root = root_node ? static_cast<Scene_root*>(root_node->get_item_host()) : nullptr;
            if (scene_root == nullptr) {
                return false;
            }
            return whole_row_drop_target(
                row,
                Asset_file_gltf::static_type_name.data(),
                [this, &gltf_asset, scene_root, &root_node]() {
                    instantiate_gltf_prefab(m_context, *gltf_asset, *scene_root, root_node, std::numeric_limits<std::size_t>::max());
                }
            );
        }
    }

    if (!is_tree_prim(item)) {
        return false;
    }

    // A tree row dropped on a tree row: move wins, the modifier acts. The drop
    // is a structural move (before / as last child / after); while Alt is held
    // the action this payload has on this row is offered instead, and the
    // move stays offered where no action applies.
    const std::shared_ptr<erhe::Item_base> payload_prim = peek_prim_payload(payload_peek);
    if (payload_prim && is_tree_prim(payload_prim)) {
        const bool alt_down = ImGui::GetIO().KeyAlt;
        if (alt_down) {
            const std::optional<bool> action = action_drop_target(row, item, payload_prim);
            if (action.has_value()) {
                return action.value();
            }
        }
        return move_drop_target(row, item, payload_prim);
    }

    // The payloads that are not tree rows act on the row regardless of
    // modifier: an inventory slot and a glTF asset. A brush or a prefab
    // instance joins the tree beside or under any prim; a material assignment
    // needs a node holding a mesh.
    const std::shared_ptr<erhe::Hierarchy> prim = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
    if (!prim) {
        return false;
    }

    if (payload_peek->IsDataType(c_inventory_slot_payload_type)) {
        // An inventory slot can define a brush, a material, or both. When it
        // defines both, the brush wins: a new node is created and the slot
        // material is applied to its mesh.
        const Slot_drag_payload& slot_payload = *static_cast<const Slot_drag_payload*>(payload_peek->Data);
        const std::shared_ptr<Brush> slot_brush = (slot_payload.brush != nullptr)
            ? std::dynamic_pointer_cast<Brush>(slot_payload.brush->shared_from_this())
            : std::shared_ptr<Brush>{};
        const std::shared_ptr<erhe::primitive::Material> slot_material = (slot_payload.material != nullptr)
            ? std::dynamic_pointer_cast<erhe::primitive::Material>(slot_payload.material->shared_from_this())
            : std::shared_ptr<erhe::primitive::Material>{};
        if (slot_brush) {
            return brush_drop_target(row, prim, slot_brush, slot_material, c_inventory_slot_payload_type).value_or(false);
        }
        const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        if (slot_material && node) {
            return material_assign_drop_target(row, node, slot_material, c_inventory_slot_payload_type).value_or(false);
        }
        return false;
    }

    if (gltf_asset) {
        // glTF asset dropped onto a prim: instantiate as a prefab, as sibling
        // before / child of / sibling after the drop target.
        Scene_root* const scene_root = find_scene_root_for_item(m_context, *prim);
        if (scene_root == nullptr) {
            return false;
        }
        // A reference instance protects its structure
        // (doc/erhe/usd_compatibility_design.md X2): no prim is added under a
        // carrier or inside one, so the child zone is not offered there, and
        // the sibling zones are not offered when the prim's own parent
        // refuses children.
        const std::shared_ptr<erhe::Hierarchy> prim_parent = prim->get_parent().lock();
        const bool refuse_child_of_prim   = refuses_instance_child(*prim);
        const bool refuse_child_of_parent = !prim_parent || refuses_instance_child(*prim_parent);
        return three_zone_drop_target(
            row,
            Asset_file_gltf::static_type_name.data(),
            Drop_zones{
                .before = !refuse_child_of_parent,
                .into   = !refuse_child_of_prim,
                .after  = !refuse_child_of_parent
            },
            [this, &gltf_asset, scene_root, &prim, &prim_parent](const Drop_zone zone) {
                switch (zone) {
                    case Drop_zone::before: instantiate_gltf_prefab(m_context, *gltf_asset, *scene_root, prim_parent, prim->get_index_in_parent()); break;
                    case Drop_zone::into:   instantiate_gltf_prefab(m_context, *gltf_asset, *scene_root, prim, std::numeric_limits<std::size_t>::max()); break;
                    case Drop_zone::after:  instantiate_gltf_prefab(m_context, *gltf_asset, *scene_root, prim_parent, prim->get_index_in_parent() + 1); break;
                }
            }
        );
    }

    return false;
}

void Item_tree::set_item_selection_terminator(const std::shared_ptr<erhe::Item_base>& item)
{
    auto& range_selection = m_context.selection->range_selection();
    range_selection.set_terminator(item);
    m_range_selection_edited = true; // feed range selection entries this frame
}

void Item_tree::set_item_selection(const std::shared_ptr<erhe::Item_base>& item, bool selected)
{
    if (selected) {
        m_context.selection->add_to_selection(item);
    } else {
        m_context.selection->remove_from_selection(item);
    }
}

void Item_tree::item_update_selection(const std::shared_ptr<erhe::Item_base>& item, const bool hovered_in_folded_subtree)
{
    ERHE_PROFILE_SCOPE("item_update_selection"); // named zone: no per-row callstack capture

    auto& range_selection = m_context.selection->range_selection();

    drag_and_drop_source(item);

    const bool shift_down          = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
    const bool ctrl_down           = ImGui::IsKeyDown(ImGuiKey_LeftCtrl ) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
    const bool mouse_down          = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool mouse_released      = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    const bool focused             = ImGui::IsItemFocused();
    const bool hovered             = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
    const bool was_selected        = item->is_selected();
    const bool non_mouse_activated = ImGui::IsItemActivated() && !mouse_released && !mouse_down;

    if (hovered) {
        m_hovered_item = item;
    }
    if (!shift_down) {
        m_shift_down_range_selection_started = false;
    }

    // Graph hovering highlights like viewport hovering: the geometry graph
    // window maintains Item_flags::hovered_in_graph on the scene node
    // referenced by the hovered graph node (plus child/ancestor companions).
    const bool hovered_in_graph = erhe::utility::test_bit_set(item->get_flag_bits(), erhe::Item_flags::hovered_in_graph);
    if (item->is_hovered() || hovered_in_graph || hovered_in_folded_subtree) {
        const ImVec2 rect_min = ImGui::GetItemRectMin();
        const ImVec2 rect_max = ImGui::GetItemRectMax();
        const ImRect rect{rect_min, rect_max};
        const auto* g       = ImGui::GetCurrentContext();
        const auto* window  = g->CurrentWindow;

        window->DrawList->AddRect(
            rect.Min - ImVec2{0.0f, 2.0f},
            rect.Max + ImVec2{0.0f, 2.0f},
            ImGui::GetColorU32(ImVec4{0.0f, 0.5f, 1.0f, 1.0f}),
            0.0f,
            1.0f,
            ImDrawFlags_None
        );
    }

    std::shared_ptr<erhe::Item_base> last_focus_item = m_last_focus_item.lock();
    bool have_last_focus_item = last_focus_item.operator bool();
    bool focus_change = have_last_focus_item && focused && (last_focus_item != item);

    if (focus_change) {
        SPDLOG_LOGGER_TRACE(
            log_tree,
            "focus change from {} to {}: {}, {}, {}",
            last_focus_item->get_name(),
            item->get_name(),
            shift_down ? "shift" : "no shift",
            m_shift_down_range_selection_started ? "range already started" : "range not yet started",
            have_last_focus_item ? "have last focus item" : "no last focus item"
        );
        if (shift_down && !m_shift_down_range_selection_started && have_last_focus_item) {
            m_shift_down_range_selection_started = true;
            range_selection.reset();
            set_item_selection_terminator(last_focus_item);
            SPDLOG_LOGGER_TRACE(
                log_tree,
                "nav with shift: resetting range {}, {} last {}, {}",
                item->get_type_name(),
                item->get_name(),
                last_focus_item->get_type_name(),
                last_focus_item->get_name()
            );
        }
    }

    if (non_mouse_activated || (hovered && mouse_released)) {
        if (ctrl_down) {
            range_selection.reset(item->get_item_host());
            if (item->is_selected()) {
                // Ctrl-click on a selected row that is not the active item
                // makes it active and leaves the selection alone; on the
                // active row it deselects, and it stays active
                // (doc/editor/active_item.md D3.4).
                if (m_context.selection->get_active_item() != item) {
                    m_context.selection->set_active_item(item);
                } else {
                    set_item_selection(item, false);
                }
            } else {
                set_item_selection(item, true);
                set_item_selection_terminator(item);
            }
        } else if (shift_down) {
            SPDLOG_LOGGER_TRACE(log_tree, "click with shift down on {} {} - range select", item->get_type_name(), item->get_name());
            set_item_selection_terminator(item);
        } else {
            // Plain click deselects within the clicked item's host (its scene,
            // or the non-hosted bucket for library items) only; other scenes'
            // selections are left untouched.
            range_selection.reset(item->get_item_host());
            m_context.selection->clear_selection(item->get_item_host());
            SPDLOG_LOGGER_TRACE(
                log_tree,
                "mouse button release without modifier keys on {} {} - {} selecting it",
                item->get_type_name(),
                item->get_name(),
                was_selected ? "de" : ""
            );
            if (!was_selected) {
                set_item_selection(item, true);
                set_item_selection_terminator(item);
            }
        }
    }

    // Issue #252: double-click opens the item's editor (a Graph Mesh / Graph
    // Texture -> its graph editor, a Scene -> a new viewport). Runs alongside
    // the single-click selection above, which is fine - select-then-open is
    // the natural result. Editor_windows self-defers the actual window
    // creation, so this is safe from inside ImGui iteration.
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if ((m_context.editor_windows != nullptr) && Editor_windows::item_has_editor(item)) {
            m_context.editor_windows->open_editor_for_item(item);
        }
    }

    if (focused) {
        if (item != m_last_focus_item.lock()) {
            if (shift_down) {
                SPDLOG_LOGGER_TRACE(log_tree, "key with shift down on {} {} - range select", item->get_type_name(), item->get_name());
                set_item_selection_terminator(item);
            }
            // else
            // {
            //     SPDLOG_LOGGER_TRACE(
            //         log_tree,
            //         "key without modifier key on node {} - clearing range select and select",
            //         node->get_name()
            //     );
            //     range_selection.reset();
            //     range_selection.set_terminator(node);
            // }
        }
        m_last_focus_item = item;
    }
}

void Item_tree::item_popup_menu(const std::shared_ptr<erhe::Item_base>& item)
{
    const auto& hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
    // Scene rows (the Hierarchy window header item, #240) are not Hierarchy
    // items but get a popup too; only the context menu callbacks apply to them
    // (e.g. the scene "Close" entry), not the clipboard entries below.
    const bool is_scene = static_cast<bool>(std::dynamic_pointer_cast<erhe::scene::Scene>(item));
    if (!hierarchy && !is_scene) {
        return;
    }

    // In XR the context menu opens on the right-button press edge (the
    // trigger pull) so it appears immediately; desktop keeps the
    // conventional open-on-release.
    const bool context_menu_click = m_context.OpenXR
        ? ImGui::IsMouseClicked(ImGuiMouseButton_Right)
        : ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    if (
        context_menu_click &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
        !m_popup_item
    ) {
        log_tree->debug("Item context menu OPEN: '{}' (ImGui right-button {} over hovered item)", item->get_name(), m_context.OpenXR ? "press" : "release");
        m_popup_item = item;
        m_popup_id_string = fmt::format("{}##{}-popup-menu", item->get_name(), item->get_id());
        m_popup_id = ImGui::GetID(m_popup_id_string.c_str());
        ImGui::OpenPopupEx(
            m_popup_id,
            ImGuiPopupFlags_MouseButtonRight
        );
    }

    if ((m_popup_item != item) || m_popup_id_string.empty()) {
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f, 10.0f});
    const bool begin_popup_context_item = ImGui::BeginPopupEx(
        m_popup_id,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings
    );
    if (begin_popup_context_item) {
        bool close{false};

        if (!m_item_context_menu_callbacks.empty()) {
            for (const Context_menu_callback& cb : m_item_context_menu_callbacks) {
                cb(item, m_operations, close);
            }
            ImGui::Separator();
        }

        // Like the clipboard entries, Rename on a selected row acts on the
        // selection: with several items selected, the one added last.
        if (ImGui::MenuItem("Rename", "F2")) {
            begin_rename(item->is_selected() ? get_rename_target() : item);
        }

        if (hierarchy) {
        // The clipboard entries apply to every prim of the tree - node,
        // scope, kind scope or resource alike (doc/erhe/usd_compatibility_design.md
        // C5). A copy is a clone, so a kind that is
        // erhe::Item_kind::not_clonable (a texture, a brush, a graph asset)
        // is not copied, cut or duplicated; a subtree clone leaves such
        // descendants out.
        const bool is_prim   = is_tree_prim(item);
        const bool clonable  = item->is_clonable();
        const bool can_copy  = is_prim && clonable;
        // Structure protection (doc/erhe/usd_compatibility_design.md X2): an item
        // inside a reference instance is not removed, and nothing is
        // inserted under a carrier or inside one.
        const std::shared_ptr<erhe::Hierarchy> item_parent = hierarchy->get_parent().lock();
        const bool can_cut       = can_copy && !is_instance_structure_protected(*item);
        const bool can_duplicate = can_copy && item_parent && !refuses_instance_child(*item_parent);
        if (!can_cut) {
            ImGui::BeginDisabled();
        }
        if (ImGui::MenuItem("Cut")) {
            if (item->is_selected()) {
                m_context.selection->cut_selection();
            } else {
                m_context.clipboard->set_contents(item);
                auto op = std::make_shared<Item_insert_remove_operation>(
                    Item_insert_remove_operation::Parameters{
                        .context = m_context,
                        .item    = hierarchy,
                        .parent  = hierarchy->get_parent().lock(),
                        .mode    = Item_insert_remove_operation::Mode::remove,
                    }
                );
                m_context.operation_stack->queue(op);
            }
        }
        if (!can_cut) {
            ImGui::EndDisabled();
        }
        if (!can_copy) {
            ImGui::BeginDisabled();
        }
        if (ImGui::MenuItem("Copy")) {
            if (item->is_selected()) {
                m_context.selection->copy_selection();
            } else {
                m_context.clipboard->set_contents(item->clone());
            }
        }
        if (!can_copy) {
            ImGui::EndDisabled();
        }

        // Paste inserts the clipboard contents as the last children of the
        // row's prim.
        const std::shared_ptr<erhe::Hierarchy>& paste_target = hierarchy;
        const std::vector<std::shared_ptr<erhe::Item_base>>& clipboard_contents = m_context.clipboard->get_contents();
        const bool can_paste = is_prim && !clipboard_contents.empty() && !refuses_instance_child(*paste_target);
        if (!can_paste) {
            ImGui::BeginDisabled();
        }
        if (ImGui::MenuItem("Paste")) {
            m_context.clipboard->try_paste(paste_target, paste_target->get_child_count());
        }
        if (!can_paste) {
            ImGui::EndDisabled();
        }

        if (!can_duplicate) {
            ImGui::BeginDisabled();
        }
        if (ImGui::MenuItem("Duplicate")) {
            if (item->is_selected()) {
                m_context.selection->duplicate_selection();
            } else {
                // Clones keep the source name; a duplicate wants a
                // distinguishing name, so rename the duplicate root here.
                const std::shared_ptr<erhe::Hierarchy> duplicate = std::dynamic_pointer_cast<erhe::Hierarchy>(hierarchy->clone());
                if (duplicate) {
                    duplicate->set_name(hierarchy->get_name() + " Copy");
                }
                auto op = std::make_shared<Item_insert_remove_operation>(
                    Item_insert_remove_operation::Parameters{
                        .context         = m_context,
                        .item            = duplicate,
                        .parent          = hierarchy->get_parent().lock(),
                        .mode            = Item_insert_remove_operation::Mode::insert,
                        .index_in_parent = hierarchy->get_index_in_parent() + 1
                    }
                );
                m_context.operation_stack->queue(op);
             }
        }
        if (!can_duplicate) {
            ImGui::EndDisabled();
        }

        if (ImGui::MenuItem("Delete")) {
            if (item->is_selected()) {
                m_context.selection->delete_selection();
            } else {
                // Same recursive delete as delete_selection: a bare remove
                // operation would re-parent the item's children to the
                // grandparent instead of deleting them (and orphan a prefab
                // instance's sealed interior).
                m_context.selection->delete_items({item});
            }
        }
        ImGui::Separator();
        // The M1 namespace path (doc/erhe/usd_compatibility_design.md), the form the
        // MCP tools and the ERHE_scene entries address items by; a root is
        // named by its name.
        if (ImGui::MenuItem("Copy Path")) {
            const std::string path = hierarchy->get_reference_path();
            ImGui::SetClipboardText(path.c_str());
        }
        } // if (hierarchy)

        ImGui::EndPopup();
        if (close) {
            m_popup_item.reset();
            m_popup_id_string.clear();
            m_popup_id = 0;
        }
    } else {
        m_popup_item.reset();
        m_popup_id_string.clear();
        m_popup_id = 0;
    }
    ImGui::PopStyleVar(1);
}

auto Item_tree::find_row_index(const erhe::Item_base* const item) const -> std::optional<std::size_t>
{
    if (item == nullptr) {
        return {};
    }
    for (std::size_t i = 0, end = m_flat_rows.size(); i < end; ++i) {
        if (m_flat_rows[i].item.get() == item) {
            return i;
        }
    }
    return {};
}

auto Item_tree::get_rename_target() const -> std::shared_ptr<erhe::Item_base>
{
    // The selection keeps items in the order they were added, so the last
    // one shown in this tree is the item added to the selection last.
    const std::vector<std::shared_ptr<erhe::Item_base>>& selection = m_context.selection->get_selected_items();
    for (auto i = selection.rbegin(), end = selection.rend(); i != end; ++i) {
        if (find_row_index(i->get()).has_value()) {
            return *i;
        }
    }
    return {};
}

void Item_tree::begin_rename(const std::shared_ptr<erhe::Item_base>& item)
{
    if (!item || !find_row_index(item.get()).has_value()) {
        return;
    }
    m_rename_item   = item;
    m_rename_buffer = item->get_name();
    m_rename_error.clear();
    // Focus is requested for a few frames: a rename started from the context
    // menu is submitted while the popup still holds focus.
    m_rename_focus_frames = 3;
    m_rename_was_active   = false;
}

void Item_tree::end_rename()
{
    m_rename_item.reset();
    m_rename_buffer.clear();
    m_rename_error.clear();
    m_rename_focus_frames = 0;
    m_rename_was_active   = false;
}

auto Item_tree::try_commit_rename() -> bool
{
    const std::shared_ptr<erhe::Item_base> item = m_rename_item.lock();
    if (!item || (m_rename_buffer == item->get_name())) {
        return true;
    }
    if (m_rename_buffer.empty()) {
        m_rename_error = "Name must not be empty";
        return false;
    }
    // The name property's validator refuses a name a sibling already holds
    // (doc/erhe/usd_compatibility_design.md M2); the operation makes the
    // rename undoable like the Properties window row.
    const erhe::property::Property_value after{m_rename_buffer};
    if (!item->validate_value(erhe::Item_base::name_property.get(), after, m_rename_error)) {
        return false;
    }
    log_tree->info("Rename '{}' -> '{}'", item->get_name(), m_rename_buffer);
    m_context.operation_stack->queue(
        std::make_shared<Property_set_operation>(
            item,
            erhe::Item_base::name_property.get(),
            std::optional<erhe::property::Property_value>{erhe::property::Property_value{item->get_name()}},
            std::optional<erhe::property::Property_value>{after}
        )
    );
    return true;
}

void Item_tree::imgui_rename_field(const ImVec2& position, const float width)
{
    ImGui::SetCursorScreenPos(position);
    if (m_rename_focus_frames > 0) {
        if (!ImGui::IsRectVisible(ImVec2{position.x, position.y + ImGui::GetFrameHeight()})) {
            ImGui::SetScrollHereY(0.5f);
        }
        ImGui::SetKeyboardFocusHere();
        --m_rename_focus_frames;
    }
    ImGui::SetNextItemWidth(width);
    const bool enter_pressed = ImGui::InputText(
        "##item_tree_rename",
        &m_rename_buffer,
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll
    );
    if (ImGui::IsItemActive()) {
        m_rename_was_active   = true;
        m_rename_focus_frames = 0;
    }
    if (!m_rename_error.empty()) {
        ImGui::SetTooltip("%s", m_rename_error.c_str());
    }

    if (enter_pressed) {
        // Enter deactivates the field; a refused name keeps it open.
        if (try_commit_rename()) {
            end_rename();
        } else {
            m_rename_focus_frames = 3;
            m_rename_was_active   = false;
        }
    } else if (ImGui::IsItemDeactivated()) {
        // Escape cancels; clicking elsewhere commits, dropping a refused name.
        if (!ImGui::IsKeyPressed(ImGuiKey_Escape) && !try_commit_rename()) {
            log_tree->warn("Rename to '{}' refused: {}", m_rename_buffer, m_rename_error);
        }
        end_rename();
    } else if (!m_rename_was_active && (m_rename_focus_frames == 0)) {
        end_rename(); // focus never arrived
    }
}

void Item_tree::root_popup_menu()
{
    if (!m_root) {
        return;
    }

    const bool window_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup);
    // Press-edge in XR, release on desktop - see item_popup_menu(). Only the
    // opening click needs the window hovered: once open, the popup is
    // submitted every frame. While a menu entry is held down it is the active
    // item, and IsWindowHovered() reports the tree window as not hovered, so
    // a popup submitted only while hovered never sees the release that
    // activates the entry.
    const bool context_menu_click = m_context.OpenXR
        ? ImGui::IsMouseClicked(ImGuiMouseButton_Right)
        : ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    if (
        window_hovered &&
        context_menu_click &&
        !m_popup_item
    ) {
        log_tree->debug("Root context menu OPEN: '{}' (ImGui right-button {} over hovered window)", m_root->get_name(), m_context.OpenXR ? "press" : "release");
        m_popup_item = m_root;
        m_popup_id_string = fmt::format("{}##{}-popup-menu", m_root->get_name(), m_root->get_id());
        m_popup_id = ImGui::GetID(m_popup_id_string.c_str());
        ImGui::OpenPopupEx(
            m_popup_id,
            ImGuiPopupFlags_MouseButtonRight
        );
    }

    if ((m_popup_item != m_root) || m_popup_id_string.empty()) {
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f, 10.0f});
    const bool begin_popup_context_item = ImGui::BeginPopupEx(
        m_popup_id,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings
    );
    if (begin_popup_context_item) {
        bool close{false};

        if (!m_item_context_menu_callbacks.empty()) {
            for (const Context_menu_callback& cb : m_item_context_menu_callbacks) {
                cb(m_root, m_operations, close);
            }
            ImGui::Separator();
        }

        const std::vector<std::shared_ptr<erhe::Item_base>>& clipboard_contents = m_context.clipboard->get_contents();
        const bool clipboard_is_empty = clipboard_contents.empty();
        if (clipboard_is_empty) {
            ImGui::BeginDisabled();
        }
        if (ImGui::MenuItem("Paste")) {
            const auto& hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(m_root);
            if (hierarchy) {
                m_context.clipboard->try_paste(hierarchy, hierarchy->get_child_count());
            }
        }
        if (clipboard_is_empty) {
            ImGui::EndDisabled();
        }

        ImGui::EndPopup();
        if (close) {
            m_popup_item.reset();
            m_popup_id_string.clear();
            m_popup_id = 0;
        }
    } else {
        m_popup_item.reset();
        m_popup_id_string.clear();
        m_popup_id = 0;
    }
    ImGui::PopStyleVar(1);
}

namespace {

// A resource prim is its own label item.
[[nodiscard]] auto get_label_item(const std::shared_ptr<erhe::Item_base>& item) -> const std::shared_ptr<erhe::Item_base>&
{
    return item;
}

// The hierarchy accent of the active item: the pale yellow the viewport
// outline uses for it (Selection_outline_style::active_highlight_*, tone
// mapped), so the two presentations name the same item in the same color.
constexpr ImVec4 c_active_item_accent{1.0f, 0.94f, 0.5f, 1.0f};

// RGB interpolation towards accent, keeping the alpha of color.
[[nodiscard]] auto tint_color(const ImVec4& color, const ImVec4& accent, const float t) -> ImVec4
{
    return ImVec4{
        color.x + (accent.x - color.x) * t,
        color.y + (accent.y - color.y) * t,
        color.z + (accent.z - color.z) * t,
        color.w
    };
}

}

void Item_tree::imgui_row(const Flat_row& row)
{
    // Named zone instead of ERHE_PROFILE_FUNCTION(): the latter captures a
    // 32-frame callstack per zone (ZoneScopedS), which costs microseconds and
    // would dominate the measurement of this per-row function.
    ERHE_PROFILE_SCOPE("row");

    {
        ERHE_PROFILE_SCOPE("table_row");
        ImGui::TableNextRow(ImGuiTableRowFlags_None);
        ImGui::TableSetColumnIndex(0);
    }

    const ImVec2 row_pos   = ImGui::GetCursorScreenPos();
    const float  row_right = row_pos.x + ImGui::GetContentRegionAvail().x;

    const bool is_selected = row.item->is_selected();
    // The active item of the selection (doc/editor/active_item.md D5) accents
    // its row: a brighter header while it is selected, a tinted label while it
    // is not. The bit is read off the item, so no Selection lookup per row.
    const bool is_active_item = erhe::utility::test_bit_set(row.item->get_flag_bits(), erhe::Item_flags::active_item);

    const ImGuiTreeNodeFlags flags =
        row.tree_node_flags |
        (is_selected ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None);

    // Tree node with an empty visible label; the icon and the label text are drawn manually
    // below, after the interaction handlers - those must see the tree node as the last item.
    bool is_open = false;
    {
        ERHE_PROFILE_SCOPE("tree_node");
        const bool accent_header = is_active_item && is_selected;
        if (accent_header) {
            ImGui::PushStyleColor(
                ImGuiCol_Header,
                tint_color(ImGui::GetStyleColorVec4(ImGuiCol_Header), ImVec4{1.0f, 1.0f, 1.0f, 1.0f}, 0.35f)
            );
        }
        is_open = ImGui::TreeNodeEx(row.debug_label.data(), flags, "%s", "");
        if (accent_header) {
            ImGui::PopStyleColor();
        }
        // The row's visible text is drawn below with the draw list, so ImGui
        // was handed an empty label; name the item for the item recorder so
        // the get_imgui_* MCP queries can address the row by what it shows.
        erhe::imgui::set_item_debug_label(row.label_text);
    }

    bool consumed = false;
    if (m_item_callback) {
        ERHE_PROFILE_SCOPE("callback");
        consumed = m_item_callback(row.item);
    }

    {
        ERHE_PROFILE_SCOPE("interact");
        const bool is_item_toggled_open = ImGui::IsItemToggledOpen();
        if (is_item_toggled_open) {
            m_toggled_open = true;
            m_flat_rows_dirty = true; // open/close changes the visible row set
        }
        if (!m_toggled_open) {
            item_popup_menu(row.item);

            if (!consumed) {
                ERHE_PROFILE_SCOPE("update");
                // A closed row hides its subtree, so when the viewport-hovered
                // node is inside (descendant_hovered_in_viewport, maintained
                // by Hover_tool) - or the graph-hovered node is
                // (child_hovered_in_graph, maintained by the geometry graph
                // window) - this row is its closest visible ancestor and
                // takes over the hover highlight.
                const bool hovered_in_folded_subtree =
                    !is_open &&
                    erhe::utility::test_any_rhs_bits_set(
                        row.item->get_flag_bits(),
                        erhe::Item_flags::descendant_hovered_in_viewport | erhe::Item_flags::child_hovered_in_graph
                    );
                item_update_selection(row.item, hovered_in_folded_subtree);
            }
        }
    }

    // Row visuals from the precomputed layout. The icon glyphs and the label
    // are passive decorations (the tree node owns all row interaction), so
    // they are emitted directly into the draw list, skipping per-item ImGui
    // submission entirely.
    {
        ERHE_PROFILE_SCOPE("decor");
        ImDrawList* const draw_list = ImGui::GetWindowDrawList();
        const ImGuiStyle& style     = ImGui::GetStyle();

        // An inactive item and everything below it is out of the scene
        // (doc/erhe/usd_compatibility_design.md X2): the row stays, drawn dim.
        const bool     dimmed     = !row.item->is_active();
        const ImGuiCol text_color = dimmed ? ImGuiCol_TextDisabled : ImGuiCol_Text;
        // An unselected active item has no header to brighten, so its label
        // carries the accent instead - this is the one place it is visible.
        const ImU32 label_color = (is_active_item && !is_selected)
            ? ImGui::GetColorU32(tint_color(ImGui::GetStyleColorVec4(text_color), c_active_item_accent, 0.8f))
            : ImGui::GetColorU32(text_color);

        bool thumbnail_drawn = false;
        if (row.brush && m_context.thumbnails) {
            ImGui::SameLine();
            // Tier 2 (doc/editor/brushes.md G3): the shared
            // helper requests the geometry and draws the preview only once the
            // brush is ready, spinning in the icon square until then. The
            // spinner goes on the row's own draw list at the icon position, so
            // the row keeps the height it has with an icon.
            const Brush_thumbnail_placement placement{
                .size     = m_cached_icon_font_size,
                .top_left = ImVec2{row_pos.x + row.icon_x_offset, row_pos.y + m_icon_y_offset}
            };
            thumbnail_drawn = (draw_brush_thumbnail(m_context, row.brush, placement) != Brush_thumbnail_result::icon);
        }
        if (!thumbnail_drawn && (row.primary_icon.code != nullptr)) {
            const Row_icon& icon  = row.primary_icon;
            const glm::vec4 color =
                (icon.live_color_material != nullptr) ? glm::vec4{icon.live_color_material->get_base_color(), 1.0f} :
                (icon.live_color_light    != nullptr) ? glm::vec4{icon.live_color_light->get_color(), 1.0f} :
                icon.color;
            draw_list->AddText(
                icon.font,
                m_cached_icon_font_size,
                ImVec2{row_pos.x + row.icon_x_offset, row_pos.y + m_icon_y_offset},
                dimmed
                    ? ImGui::GetColorU32(ImGuiCol_TextDisabled)
                    : ImGui::GetColorU32(ImVec4{color.x, color.y, color.z, color.w}),
                icon.code
            );
        }

        // Feature icons: one per attached value group the row's node carries
        // (doc/erhe/property_system.md section 4.23). They are read
        // here, per visible row per frame, because a group is taken up or
        // dropped by a property write, which the flattened row cache does not
        // see; each entry costs one key-property read and one glyph
        // measurement, and the row's own stack holds them.
        const Icon_set& icon_set = *m_context.icon_set;
        std::array<const Icon_set::Feature_icon*, Flat_row::max_right_icon_count> feature_icons{};
        std::size_t feature_icon_count = 0;
        float       feature_icons_width = 0.0f;
        if (erhe::is<erhe::scene::Node>(row.item.get())) {
            const erhe::scene::Node& node = *static_cast<const erhe::scene::Node*>(row.item.get());
            for (const Icon_set::Feature_icon& feature_icon : icon_set.get_feature_icons()) {
                if (feature_icon_count == feature_icons.size()) {
                    break; // out of slots; remaining feature icons are dropped
                }
                if ((feature_icon.icon.code == nullptr) || !feature_icon.carries(node)) {
                    continue;
                }
                if (feature_icon_count > 0) {
                    feature_icons_width += style.ItemSpacing.x;
                }
                feature_icons[feature_icon_count] = &feature_icon;
                ++feature_icon_count;
                feature_icons_width += icon_set.get_icon_width(feature_icon.icon);
            }
        }

        const float right_icons_width = row.right_icons_width + feature_icons_width +
            (((row.right_icon_count > 0) && (feature_icon_count > 0)) ? style.ItemSpacing.x : 0.0f);
        float icons_start_x = row_right - right_icons_width;
        if ((row.right_icon_count > 0) || (feature_icon_count > 0)) {
            // Never draw the right-aligned icons over a long label
            const float label_end_x = row_pos.x + row.label_x_offset + row.label_width;
            icons_start_x = std::max(icons_start_x, label_end_x + style.ItemInnerSpacing.x);
        }

        // Label, clipped so it does not run under the right-aligned icons.
        // A row being renamed shows the edit field in place of the label.
        const ImVec4 label_clip{row_pos.x, row_pos.y, icons_start_x - style.ItemInnerSpacing.x, row_pos.y + ImGui::GetFrameHeight()};
        // Owner comparison: no weak_ptr lock per row.
        const bool renaming = !m_rename_item.owner_before(row.item) && !row.item.owner_before(m_rename_item);
        if (renaming) {
            // Frame padding outside the label position keeps the text in place
            const float field_x = row_pos.x + row.label_x_offset - style.FramePadding.x;
            imgui_rename_field(
                ImVec2{field_x, row_pos.y},
                std::max(label_clip.z - field_x, 8.0f * ImGui::GetFontSize())
            );
        } else {
            draw_list->AddText(
                ImGui::GetFont(),
                ImGui::GetFontSize(),
                ImVec2{row_pos.x + row.label_x_offset, row_pos.y + m_label_y_offset},
                label_color,
                row.label_text.data(),
                row.label_text.data() + row.label_text.size(),
                0.0f,
                ((row.right_icon_count > 0) || (feature_icon_count > 0)) ? &label_clip : nullptr
            );
        }

        // R5.8 reference badge suffix: dim defining-container name after the
        // label, sharing the label's clip so it never runs under the icons.
        if (!row.reference_suffix.empty() && !renaming) {
            draw_list->AddText(
                ImGui::GetFont(),
                ImGui::GetFontSize(),
                ImVec2{row_pos.x + row.label_x_offset + row.label_width + style.ItemInnerSpacing.x, row_pos.y + m_label_y_offset},
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                row.reference_suffix.data(),
                row.reference_suffix.data() + row.reference_suffix.size(),
                0.0f,
                ((row.right_icon_count > 0) || (feature_icon_count > 0)) ? &label_clip : nullptr
            );
        }

        float feature_icon_x = icons_start_x;
        for (std::size_t i = 0; i < feature_icon_count; ++i) {
            const Icon_set::Item_icon& icon = feature_icons[i]->icon;
            draw_list->AddText(
                icon.font,
                m_cached_icon_font_size,
                ImVec2{feature_icon_x, row_pos.y + m_icon_y_offset},
                ImGui::GetColorU32(ImVec4{icon.color.x, icon.color.y, icon.color.z, icon.color.w}),
                icon.code
            );
            feature_icon_x += icon_set.get_icon_width(icon) + style.ItemSpacing.x;
        }

        const float cached_icons_start_x = icons_start_x + feature_icons_width +
            (((row.right_icon_count > 0) && (feature_icon_count > 0)) ? style.ItemSpacing.x : 0.0f);
        for (std::size_t i = 0; i < row.right_icon_count; ++i) {
            const Row_icon& icon  = row.right_icons[i];
            const glm::vec4 color =
                (icon.live_color_material != nullptr) ? glm::vec4{icon.live_color_material->get_base_color(), 1.0f} :
                (icon.live_color_light    != nullptr) ? glm::vec4{icon.live_color_light->get_color(), 1.0f} :
                icon.color;
            draw_list->AddText(
                icon.font,
                m_cached_icon_font_size,
                ImVec2{cached_icons_start_x + icon.x_offset, row_pos.y + m_icon_y_offset},
                ImGui::GetColorU32(ImVec4{color.x, color.y, color.z, color.w}),
                icon.code
            );
        }
    }
}

// Simple icon + name line used for the drag-and-drop payload preview
void Item_tree::item_icon_and_text(const std::shared_ptr<erhe::Item_base>& item)
{
    m_context.icon_set->item_icon(item, m_ui_scale); // ends with SameLine()
    const std::shared_ptr<erhe::Item_base>& label_item = get_label_item(item);
    const std::string& name = label_item->get_name();
    ImGui::TextUnformatted(name.c_str(), name.c_str() + name.size());
}

auto Item_tree::should_show(const std::shared_ptr<erhe::Item_base>& item) -> Show_mode
{
    const bool show_by_type = m_filter(item->get_flag_bits());
    const bool show_by_name = m_text_filter.PassFilter(item->get_name().c_str());
    if (show_by_type && show_by_name) {
        return Show_mode::Show;
    }

    const auto& hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
    if (hierarchy) {
        for (const auto& child_node : hierarchy->get_children()) {
            if (should_show(child_node) != Show_mode::Hide) {
                return Show_mode::Show_expanded;
            }
        }
    }

    return Show_mode::Hide;
}

void Item_tree::flatten_visible_rows(const std::shared_ptr<erhe::Item_base>& item, const float indent)
{
    // Special handling for invisible parents (scene root)
    if (erhe::utility::test_bit_set(item->get_flag_bits(), erhe::Item_flags::invisible_parent)) {
        const auto& hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
        if (hierarchy) {
            for (const auto& child_node : hierarchy->get_children()) {
                flatten_visible_rows(child_node, indent);
            }
        }
        return;
    }

    ERHE_PROFILE_SCOPE("flatten_visible_rows"); // named zone: no per-call callstack capture

    const Show_mode show = should_show(item);
    if (show == Show_mode::Hide) {
        //// log_tree->info("filtered {}", item->describe());
        return;
    }
    const bool force_expand = (show == Show_mode::Show_expanded);

    const auto& hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
    // A SEALED prefab instance root (a glTF template) hides its interior:
    // the subtree is prefab content, editable only by opening the prefab's
    // own scene, so the row renders as a leaf. A USD-backed instance is not
    // sealed (doc/erhe/usd_compatibility_design.md X2): its interior is listed,
    // selectable and editable, and only its structure is protected.
    const bool is_sealed_instance_root = is_sealed_instance_carrier(*item.get());
    bool is_leaf = true;
    if (hierarchy && !is_sealed_instance_root) {
        if (hierarchy->get_child_count(m_filter) > 0) {
            is_leaf = false;
        }
    }

    const bool expand =
        erhe::utility::test_bit_set(item->get_flag_bits(), erhe::Item_flags::expand) ||
        force_expand;

    const std::shared_ptr<erhe::Item_base>& label_item   = get_label_item(item);
    const erhe::utility::Debug_label        debug_label  = label_item->get_debug_label(); // "<name>##<id>" - tree node id

    {
        // NOTE: this reference must not be held across the recursive calls
        // below; they can reallocate m_flat_rows.
        Flat_row& row = m_flat_rows.emplace_back();
        row.item        = item;
        row.indent      = indent;
        row.debug_label = debug_label;
        row.label_text  = std::string_view{label_item->get_name()};

        row.tree_node_flags =
            ImGuiTreeNodeFlags_SpanAvailWidth |
            ImGuiTreeNodeFlags_NoTreePushOnOpen | // rows are flat; indentation comes from the row list
            (expand ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None) |
            (is_leaf ? ImGuiTreeNodeFlags_Leaf : ImGuiTreeNodeFlags_OpenOnArrow);

        Icon_set&         icon_set = *m_context.icon_set;
        const ImGuiStyle& style    = ImGui::GetStyle();

        // Primary visual: brush thumbnail (drawn live) or a resolved icon
        if (m_context.thumbnails != nullptr) {
            row.brush = std::dynamic_pointer_cast<Brush>(item);
        }
        float primary_width = m_cached_icon_font_size; // thumbnails are square
        if (!row.brush) {
            const Icon_set::Item_icon icon = icon_set.get_item_icon(item);
            row.primary_icon = Row_icon{.font = icon.font, .code = icon.code, .color = icon.color, .live_color_light = icon.live_color_light, .live_color_material = icon.live_color_material};
            primary_width = icon_set.get_icon_width(icon);
        }
        // The header row (the Scene item) never collapses and has no arrow,
        // so its icon takes the arrow's place; the rows below it keep the
        // arrow space, which indents them one level relative to the header.
        row.icon_x_offset  = (item == m_header_item) ? style.FramePadding.x : m_icon_x_offset;
        row.label_x_offset = row.icon_x_offset + primary_width + style.ItemInnerSpacing.x;
        row.label_width    = ImGui::CalcTextSize(row.label_text.data(), row.label_text.data() + row.label_text.size()).x;

        // R5.8 reference badge: a content-library REFERENCE entry (a listing
        // of an asset defined elsewhere) shows a link glyph and a dim suffix
        // naming its defining container. The path comes from the library's
        // recorded asset_key / gltf_source; a resource carrying neither falls
        // back to the manager's key (file-scope for path-bound containers).
        const std::shared_ptr<Content_library> row_library = find_owning_library(m_context, item);
        const Resource_metadata* const row_metadata = row_library ? row_library->find_metadata(*item) : nullptr;
        Scene_root* const row_scene_root = (row_library != nullptr) ? dynamic_cast<Scene_root*>(item->get_item_host()) : nullptr;
        const bool row_is_external =
            (row_metadata != nullptr) &&
            (row_scene_root != nullptr) &&
            (Content_library::get_kind_type_bit(*item) != 0) &&
            !row_scene_root->is_asset_definition(*item);
        if (row_is_external) {
            std::string container_path;
            if (row_metadata->asset_key.has_value() && !row_metadata->asset_key->path.empty()) {
                container_path = row_metadata->asset_key->path;
            } else if (row_metadata->gltf_source.has_value() && !row_metadata->gltf_source->gltf_path.empty()) {
                container_path = row_metadata->gltf_source->gltf_path;
            } else if (m_context.asset_manager != nullptr) {
                const Asset_key key = m_context.asset_manager->make_key(*item);
                if (key.scope == Asset_scope::file) {
                    container_path = key.path;
                }
            }
            row.reference_suffix = container_path.empty()
                ? std::string{"(reference)"}
                : fmt::format("({})", erhe::file::to_string(std::filesystem::path{container_path}.filename()));
            row.reference_suffix_width = ImGui::CalcTextSize(row.reference_suffix.data(), row.reference_suffix.data() + row.reference_suffix.size()).x;
            if (row.right_icon_count < Flat_row::max_right_icon_count) {
                row.right_icons[row.right_icon_count] = Row_icon{
                    .font  = icon_set.material_design,
                    .code  = ICON_MDI_LINK,
                    .color = glm::vec4{0.55f, 0.65f, 0.9f, 1.0f},
                };
                ++row.right_icon_count;
                row.right_icons_width = icon_set.get_icon_width(Icon_set::Item_icon{.font = icon_set.material_design, .code = ICON_MDI_LINK});
            }
        }

    }

    // ImGui treats leaf tree nodes as always open (TreeNodeUpdateNextOpen); for
    // non-leaf rows the open state lives in ImGui per-window storage, keyed by
    // the same ID and with the same default imgui_row passes to TreeNodeEx.
    bool is_open = true;
    if (!is_leaf) {
        const ImGuiID id = ImGui::GetID(debug_label.data());
        is_open = ImGui::GetStateStorage()->GetInt(id, expand ? 1 : 0) != 0;
    }
    if (!is_open) {
        return;
    }

    if (hierarchy && !is_sealed_instance_root) {
        const float indent_spacing = ImGui::GetStyle().IndentSpacing;
        for (const auto& child_node : hierarchy->get_children()) {
            flatten_visible_rows(child_node, indent + indent_spacing);
        }
    }
}

void Item_tree::imgui_tree(float ui_scale)
{
    ERHE_PROFILE_FUNCTION();

    if ((m_filter.require_at_least_one_bit_set & erhe::Item_flags::show_in_ui) != 0) {
        m_filter.require_at_least_one_bit_set = m_context.developer_mode
            ? (erhe::Item_flags::show_in_ui | erhe::Item_flags::show_in_developer_ui)
            : erhe::Item_flags::show_in_ui;
    }

    m_hovered_item.reset();

    if (!m_root) {
        return;
    }

    m_ui_scale = ui_scale;

    const std::size_t root_id = m_root->get_id();
    const int table_id = static_cast<int>(root_id);
    ImGui::PushID(table_id);
    ERHE_DEFER( ImGui::PopID(); );

    ImGui::PushFont(
        m_context.imgui_renderer->material_design_font(),
        m_context.imgui_renderer->get_imgui_settings().scale_factor *
        m_context.imgui_renderer->get_imgui_settings().font_size
    );
    ImGui::TextUnformatted(ICON_MDI_FILTER);
    ImGui::PopFont();
    ImGui::SameLine();
    if (m_text_filter.Draw("##Filter", -FLT_MIN)) {
        m_flat_rows_dirty = true;
    }

    // Ctrl+A selects everything in this tree. ImGui::Shortcut() routes the
    // chord to the focused window and lets the active item take it first, so
    // this runs once per frame, for the one focused tree, and the filter
    // input above keeps Ctrl+A as its own select-all while it is being
    // edited. It also runs before the row loop, so select_all() never mutates
    // the selection while the rows are being submitted.
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A)) {
        SPDLOG_LOGGER_TRACE(log_tree, "ctrl a pressed - select all");
        select_all();
    }

    // F2 renames the item added to the selection last (see Ctrl+A above for
    // the routing); a rename field being edited takes the key itself.
    if (ImGui::Shortcut(ImGuiKey_F2)) {
        begin_rename(get_rename_target());
    }

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{0.0f, 0.0f});
    ERHE_DEFER( ImGui::PopStyleVar(1); );

    bool table_visible = ImGui::BeginTable("##", 1, ImGuiTableFlags_RowBg);
    if (!table_visible) {
        return;
    }

    ImGui::TableSetupColumn("entry", ImGuiTableColumnFlags_WidthStretch);

#if 0 //// TODO
    ImGui::Checkbox("Show All", &m_context.app_settings->node_tree_show_all);
#endif

    m_context.selection->range_selection().begin();

    // TODO Handle cross scene drags and drops
#if 0 //// TODO
    if (ImGui::Button("Create Scene")) {
        auto content_library = std::make_shared<Content_library>();
        content_library->make<erhe::primitive::Material>("Default");
        const bool enable_physics = m_context.editor_settings->physics.static_enable;
        auto scene_root = std::make_shared<Scene_root>(
            nullptr,
            content_library,
            "new scene",
            enable_physics,
            nullptr,
            erhe::scene_renderer::Material_set_create_info{}
        );

        using Item_flags = erhe::Item_flags;

        auto camera_node = std::make_shared<erhe::scene::Xform>("Camera Node");
        auto camera = std::make_shared<erhe::scene::Camera>("Camera");
        camera_node->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
        camera     ->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
        camera_node->attach    (camera);
        camera_node->set_parent(scene_root->get_hosted_scene()->get_root_node());
        camera_node->set_world_from_node(
            erhe::math::create_look_at(
                glm::vec3{0.0f, 1.0f, 1.0f},
                glm::vec3{0.0f, 1.0f, 0.0f},
                glm::vec3{0.0f, 1.0f, 0.0f}
            )
        );

        auto light_node = std::make_shared<erhe::scene::Xform>("Light Node");
        auto light = std::make_shared<erhe::scene::Light>("Light");
        light_node->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
        light     ->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
        light     ->layer_id  = scene_root->layers().light()->id;
        light_node->attach    (light);
        light_node->set_parent(scene_root->get_hosted_scene()->get_root_node());
        light_node->set_world_from_node(
            erhe::math::create_look_at(
                glm::vec3{0.0f, 3.0f, 0.0f},
                glm::vec3{0.0f, 0.0f, 0.0f},
                glm::vec3{0.0f, 0.0f, 1.0f}
            )
        );

        m_context.app_scenes->register_scene_root(scene_root);
    }
#endif
    // Flatten the visible tree into uniform-height rows, then submit only the
    // on-screen range. The flattened list is cached across frames: it is
    // rebuilt when the item mutation serial moves (any hierarchy, name or
    // non-transient flag change anywhere), or when this tree's own inputs
    // change (open/close toggle, text filter, root, item filter, indent
    // spacing). flatten_visible_rows() must run inside the BeginTable scope
    // so its ImGui::GetID() calls match the TreeNodeEx IDs.
    const uint64_t item_mutation_serial = erhe::get_item_mutation_serial();
    const float    indent_spacing       = ImGui::GetStyle().IndentSpacing;
    const float    font_size            = ImGui::GetFontSize();
    const float    icon_font_size       = m_context.icon_set->get_icon_font_size();
    const bool rebuild =
        m_flat_rows_dirty ||
        (m_last_mutation_serial != item_mutation_serial) ||
        !(m_cached_filter == m_filter) ||
        (m_cached_indent_spacing != indent_spacing) ||
        (m_cached_font_size != font_size) ||
        (m_cached_icon_font_size != icon_font_size);
    if (rebuild) {
        ERHE_PROFILE_SCOPE("flatten");
        m_flat_rows_dirty           = false;
        m_last_mutation_serial      = item_mutation_serial;
        m_cached_filter             = m_filter;
        m_cached_indent_spacing     = indent_spacing;
        m_cached_font_size          = font_size;
        m_cached_icon_font_size     = icon_font_size;
        // Row layout constants: the empty-label tree node advances the cursor
        // by FontSize + 2 * FramePadding.x (+ ItemSpacing.x); icon glyphs are
        // vertically centered in the frame height, the label sits at the tree
        // node text baseline.
        const ImGuiStyle& style = ImGui::GetStyle();
        m_icon_x_offset  = font_size + 2.0f * style.FramePadding.x + style.ItemSpacing.x;
        m_icon_y_offset  = std::max(0.0f, (ImGui::GetFrameHeight() - icon_font_size) * 0.5f);
        m_label_y_offset = style.FramePadding.y;
        m_flat_rows.clear();
        // Optional header (the selectable Scene item, issue #240) is flattened
        // first. The root's rows are flattened at the same indent: the header
        // row draws its icon where the other rows reserve the arrow, so the
        // root's rows read as its children, one level deeper.
        if (m_header_item) {
            flatten_visible_rows(m_header_item, 0.0f);
        }
        flatten_visible_rows(m_root, 0.0f);
    }

    {
        ERHE_PROFILE_SCOPE("rows");
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_flat_rows.size()), -1.0f);
        // The row being renamed is always submitted: its edit field must keep
        // existing (and keep keyboard focus) while scrolled out of view. A
        // row that left the tree (deleted, filtered, parent folded) ends it.
        if (!m_rename_item.expired()) {
            const std::optional<std::size_t> rename_row = find_row_index(m_rename_item.lock().get());
            if (rename_row.has_value()) {
                clipper.IncludeItemByIndex(static_cast<int>(rename_row.value()));
            } else {
                end_rename();
            }
        }
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const Flat_row& row = m_flat_rows[i];
                // Indent must be applied before the row's TableNextRow(), which
                // snapshots the window indent for the cell start position.
                if (row.indent > 0.0f) {
                    ImGui::Indent(row.indent);
                }
                imgui_row(row);
                if (row.indent > 0.0f) {
                    ImGui::Unindent(row.indent);
                }
            }
        }
        clipper.End();
    }

    for (const auto& fun : m_operations) {
        fun();
    }
    m_operations.clear();

    if (m_operation) {
        m_context.operation_stack->queue(m_operation);
        m_operation.reset();
    }

    // Range selection entries are needed only on frames where this tree set a
    // terminator (range_selection.end() ignores them otherwise). Feed every
    // visible row in top-to-bottom order, which end() requires.
    if (m_range_selection_edited) {
        auto& range_selection = m_context.selection->range_selection();
        for (const Flat_row& row : m_flat_rows) {
            range_selection.entry(row.item);
        }
        m_range_selection_edited = false;
    }
    m_context.selection->range_selection().end();

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_toggled_open = false;
    }

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
        if (m_hover_callback) {
            m_hover_callback();
        }
    }
    root_popup_menu();

    // Clear stale popup state when the popup item was removed from the tree
    // (e.g. after Delete) and ImGui already closed the popup.
    if (m_popup_item && (m_popup_id != 0) && !ImGui::IsPopupOpen(m_popup_id, ImGuiPopupFlags_None)) {
        m_popup_item.reset();
        m_popup_id_string.clear();
        m_popup_id = 0;
    }

    ImGui::EndTable();

    //// m_context.app_scenes->sanity_check();
}

auto Item_tree::get_hovered_item() const -> const std::shared_ptr<erhe::Item_base>&
{
    return m_hovered_item;
}

////////////////////////////

Item_tree_window::Item_tree_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 context,
    const std::string_view       window_title,
    const std::string_view       ini_label
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, window_title, ini_label}
    , Item_tree{context}
{
    // Identifies this tree in get_editor_references / debug_set_item_tree_hover.
    set_tree_label(window_title);
}

auto Item_tree_window::get_tree_scene_root() const -> Scene_root*
{
    if (!m_is_scene_hierarchy) {
        return nullptr;
    }
    const std::shared_ptr<erhe::Hierarchy>& root = get_root();
    if (!root) {
        return nullptr;
    }
    return dynamic_cast<Scene_root*>(root->get_item_host());
}

void Item_tree_window::on_begin()
{
    m_active_scene_tint_count = push_active_scene_window_tint(m_context, get_tree_scene_root());
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2{0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2{3.0f, 3.0f});
}

void Item_tree_window::on_end()
{
    ImGui::PopStyleVar(2);
    if (m_active_scene_tint_count > 0) {
        ImGui::PopStyleColor(m_active_scene_tint_count);
        m_active_scene_tint_count = 0;
    }
}

void Item_tree_window::set_scene_hierarchy(const bool value)
{
    m_is_scene_hierarchy = value;
}

auto Item_tree_window::is_scene_hierarchy() const -> bool
{
    return m_is_scene_hierarchy;
}

void Item_tree_window::hidden()
{
    // Drop cached rows so a hidden tree does not keep deleted items alive
    // through the cached shared_ptrs; rebuilt when the window is shown again.
    clear_cached_rows();
}

void Item_tree_window::imgui()
{
    ERHE_PROFILE_FUNCTION();

    // Giving this scene's hierarchy window focus makes its scene the active
    // scene (edge-triggered: selection changes elsewhere may activate another
    // scene while this window stays focused, and must not be fought).
    if (m_is_scene_hierarchy) {
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (focused && !m_was_focused) {
            Scene_root* const scene_root = get_tree_scene_root();
            if (scene_root != nullptr) {
                m_context.selection->set_active_scene_root(scene_root->shared_from_this());
            }
        }
        m_was_focused = focused;
    }

    imgui_tree(get_scale_value());

    if (ImGui::IsWindowHovered()) {
        m_context.app_message_bus->hover_tree_node.queue_message(
            Hover_tree_node_message{
                .item = get_hovered_item()
            }
        );
    }

}

}
