// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "windows/node_tree_window.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_scenes.hpp"
#include "editor_settings.hpp"
#include "windows/node_tree_window.hpp"
#include "graphics/icon_set.hpp"
#include "operations/compound_operation.hpp"
#include "operations/node_operation.hpp"
#include "operations/operation_stack.hpp"
#include "scene/content_library.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_commands.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/imgui/imgui_windows.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/raytrace/iscene.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/skin.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#   include <imgui_internal.h>
#endif

namespace editor
{

using Light_type = erhe::scene::Light_type;

Item_tree_window::Item_tree_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Node Tree", "node_tree"}
    , m_context                {context}
    , m_filter{
        .require_all_bits_set           = erhe::scene::Item_flags::show_in_ui,
        .require_at_least_one_bit_set   = 0,
        .require_all_bits_clear         = 0, //erhe::scene::Item_flags::tool | erhe::scene::Item_flags::brush,
        .require_at_least_one_bit_clear = 0
    }
{
}

void Item_tree_window::clear_selection()
{
    SPDLOG_LOGGER_TRACE(log_tree, "clear_selection()");

    m_context.selection->clear_selection();
}

void Item_tree_window::recursive_add_to_selection(
    const std::shared_ptr<erhe::scene::Item>& item
)
{
    m_context.selection->add_to_selection(item);
    for (const auto& child : item->get_children()) {
        recursive_add_to_selection(child);
    }
}

void Item_tree_window::select_all()
{
    SPDLOG_LOGGER_TRACE(log_tree, "select_all()");

    m_context.selection->clear_selection();
    const auto& scene_roots = m_context.editor_scenes->get_scene_roots();
    for (const auto& scene_root : scene_roots) {
        const auto& scene = scene_root->get_scene();
        for (const auto& node : scene.get_root_node()->get_children()) {
            recursive_add_to_selection(node);
        }
    }
}

template <typename T, typename U>
[[nodiscard]] auto is_in(
    const T&              item,
    const std::vector<U>& items
) -> bool
{
    return std::find(
        items.begin(),
        items.end(),
        item
    ) != items.end();
}

auto Item_tree_window::get_item_by_id(
    const erhe::toolkit::Unique_id<erhe::scene::Item>::id_type id
) const -> std::shared_ptr<erhe::scene::Item>
{
    const auto i = m_tree_items_last_frame.find(id);
    if (i == m_tree_items_last_frame.end()) {
        //// log_tools->warn("node for id = {} not found", id);
        return {};
    }
    return i->second;
}

void Item_tree_window::move_selection(
    const std::shared_ptr<erhe::scene::Item>& target_node,
    const std::size_t                         payload_id,
    const Placement                           placement
)
{
    log_tree->trace(
        "move_selection(anchor = {}, {})",
        target_node ? target_node->get_name() : "(empty)",
        (placement == Placement::Before_anchor)
            ? "Before_anchor"
            : "After_anchor"
    );

    Compound_operation::Parameters compound_parameters;
    const auto& selection = m_context.selection->get_selection();
    const auto& drag_item = get_item_by_id(payload_id);

    std::shared_ptr<erhe::scene::Item> anchor = target_node;
    if (is_in(drag_item, selection)) {
        // Dragging node which is part of the selection.
        // In this case we apply reposition to whole selection.
        if (placement == Placement::Before_anchor) {
            for (const auto& item : selection) {
                const auto& node = as_node(item);
                if (node) {
                    reposition(compound_parameters, anchor, node, placement, Selection_usage::Selection_used);
                }
            }
        } else { // if (placement == Placement::After_anchor)
            for (auto i = selection.rbegin(), end = selection.rend(); i < end; ++i) {
                const auto& item = *i;
                const auto& node = as_node(item);
                if (node) {
                    reposition(compound_parameters, anchor, node, placement, Selection_usage::Selection_used);
                }
            }
        }
    } else if (compound_parameters.operations.empty()) {
        // Dragging a single node which is not part of the selection.
        // In this case we ignore selection and apply operation only to dragged node.
        const auto& drag_node = as_node(drag_item);
        if (drag_node) {
            reposition(compound_parameters, anchor, drag_node, placement, Selection_usage::Selection_ignored);
        }
    }

    if (!compound_parameters.operations.empty()) {
        m_operation = std::make_shared<Compound_operation>(std::move(compound_parameters));
    }
}

namespace {

[[nodiscard]] auto get_ancestor_in(
    const std::shared_ptr<erhe::scene::Item>&              item,
    const std::vector<std::shared_ptr<erhe::scene::Item>>& selection
) -> std::shared_ptr<erhe::scene::Item>
{
    const auto& item_parent = item->get_parent().lock();
    if (item_parent) {
        if (is_in(item_parent, selection)) {
            return item_parent;
        }
        return get_ancestor_in(item_parent, selection);
    } else {
        return {};
    }
}

} // anonymous namespace

void Item_tree_window::reposition(
    Compound_operation::Parameters&           compound_parameters,
    const std::shared_ptr<erhe::scene::Item>& anchor,
    const std::shared_ptr<erhe::scene::Item>& item,
    const Placement                           placement,
    const Selection_usage                     selection_usage
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

    // Ancestors cannot be attached to descendants
    if (anchor->is_ancestor(item.get())) {
        SPDLOG_LOGGER_WARN(log_tree, "Ancestors cannot be moved as child of descendant");
        return;
    }

    if (selection_usage == Selection_usage::Selection_used) {
        const auto& selection = m_context.selection->get_selection();

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

    if (anchor->get_parent().lock() != item->get_parent().lock()) {
        compound_parameters.operations.push_back(
            std::make_shared<Item_parent_change_operation>(
                anchor->get_parent().lock(),
                item,
                (placement == Placement::Before_anchor) ? anchor : std::shared_ptr<erhe::scene::Node>{},
                (placement == Placement::After_anchor ) ? anchor : std::shared_ptr<erhe::scene::Node>{}
            )
        );
        return;
    }

    compound_parameters.operations.push_back(
        std::make_shared<Item_reposition_in_parent_operation>(
            item,
            (placement == Placement::Before_anchor) ? anchor : std::shared_ptr<erhe::scene::Node>{},
            (placement == Placement::After_anchor ) ? anchor : std::shared_ptr<erhe::scene::Node>{}
        )
    );
}

void Item_tree_window::try_add_to_attach(
    Compound_operation::Parameters&           compound_parameters,
    const std::shared_ptr<erhe::scene::Item>& target,
    const std::shared_ptr<erhe::scene::Item>& item,
    const Selection_usage                     selection_usage
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

    // Ancestors cannot be attached to descendants
    if (target->is_ancestor(item.get())) {
        SPDLOG_LOGGER_WARN(log_tree, "Ancestors cannot be moved to child of descendant");
        return;
    }

    if (selection_usage == Selection_usage::Selection_used) {
        const auto& selection = m_context.selection->get_selection();

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

    compound_parameters.operations.push_back(
        std::make_shared<Item_parent_change_operation>(
            target,
            item,
            std::shared_ptr<erhe::scene::Item>{},
            std::shared_ptr<erhe::scene::Item>{}
        )
    );
}

void Item_tree_window::attach_selection_to(
    const std::shared_ptr<erhe::scene::Item>& target,
    const std::size_t                         payload_id
)
{
    SPDLOG_LOGGER_TRACE(
        log_tree,
        "attach_selection_to()"
    );

    //// log_tools->trace(
    ////     "attach_selection_to(target_node = {}, payload_id = {})",
    ////     target_node->get_name(),
    ////     payload_id
    //// );
    Compound_operation::Parameters compound_parameters;
    const auto& selection = m_context.selection->get_selection();
    auto drag_item = get_item_by_id(payload_id);

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

#if defined(ERHE_GUI_LIBRARY_IMGUI)
void Item_tree_window::drag_and_drop_source(
    const std::shared_ptr<erhe::scene::Item>& item
)
{
    ERHE_PROFILE_FUNCTION();

    log_tree_frame->trace("DnD source: '{}'", item->describe());

    const auto id = item->get_id();
    m_tree_items.emplace(id, item);
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload(item->get_type_name(), &id, sizeof(id));

        const auto& selection = m_context.selection->get_selection();
        if (is_in(item, selection)) {
            for (const auto& selection_item : selection) {
                item_icon_and_text(selection_item, false);
            }
        } else {
            item_icon_and_text(item, false);
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
            0,
            2.0f
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

}

auto Item_tree_window::drag_and_drop_target(
    const std::shared_ptr<erhe::scene::Item>& item
) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (!item) {
        log_tree_frame->trace("DnD item is empty");
        return false;
    }

    const auto  rect_min = ImGui::GetItemRectMin();
    const auto  rect_max = ImGui::GetItemRectMax();
    const float height   = rect_max.y - rect_min.y;
    const float y0       = rect_min.y;
    const float y1       = rect_min.y + 0.3f * height;
    const float y2       = rect_max.y - 0.3f * height;
    const float y3       = rect_max.y;
    const float x0       = rect_min.x;
    const float x1       = rect_max.x;

    const auto        id                = item->get_id();
    const std::string label_move_before = fmt::format("node dnd move before {}: {} {}", id, item->get_type_name(), item->get_name());
    const std::string label_attach_to   = fmt::format("node dnd attach to {}: {} {}",   id, item->get_type_name(), item->get_name());
    const std::string label_move_after  = fmt::format("node dnd move after {}: {} {}",  id, item->get_type_name(), item->get_name());
    const ImGuiID     imgui_id_before   = ImGui::GetID(label_move_before.c_str());
    const ImGuiID     imgui_id_attach   = ImGui::GetID(label_attach_to.c_str());
    const ImGuiID     imgui_id_after    = ImGui::GetID(label_move_after.c_str());

    // Move selection before drop target
    const auto& node = as_node(item);
    if (node) {
        log_tree_frame->trace("Dnd item is Node: {}", node->describe());
        const ImRect top_rect{rect_min, ImVec2{rect_max.x, y1}};
        if (ImGui::BeginDragDropTargetCustom(top_rect, imgui_id_before)) {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Node", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
            drag_and_drop_gradient_preview(x0, x1, y0, y2, ImGui::GetColorU32(ImGuiCol_DragDropTarget), 0);
            if (payload != nullptr) {
                log_tree_frame->trace("Dnd payload is Node (top rect)");
                IM_ASSERT(payload->DataSize == sizeof(erhe::toolkit::Unique_id<erhe::scene::Node>::id_type));
                const auto payload_id = *(const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type*)payload->Data;
                move_selection(node, payload_id, Placement::Before_anchor);
            } else {
                log_tree_frame->trace("Dnd payload is not Node (top rect)");
            }
            ImGui::EndDragDropTarget();
            return true;
        }

        // Attach selection to target
        const ImRect middle_rect{ImVec2{rect_min.x, y1}, ImVec2{rect_max.x, y2}};
        if (ImGui::BeginDragDropTargetCustom(middle_rect, imgui_id_attach)) {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Node", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
            drag_and_drop_rectangle_preview(middle_rect);
            if (payload != nullptr) {
                log_tree_frame->trace("Dnd payload is Node (middle rect)");
                IM_ASSERT(payload->DataSize == sizeof(erhe::toolkit::Unique_id<erhe::scene::Node>::id_type));
                const auto payload_id = *(const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type*)payload->Data;
                attach_selection_to(node, payload_id);
            } else {
                log_tree_frame->trace("Dnd payload is not Node (middle rect)");
            }
            ImGui::EndDragDropTarget();
            return true;
        }

        // Move selection after drop target
        const ImRect bottom_rect{ImVec2{rect_min.x, y2}, rect_max};
        if (ImGui::BeginDragDropTargetCustom(bottom_rect, imgui_id_after)) {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Node", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
            drag_and_drop_gradient_preview(x0, x1, y1, y3, 0, ImGui::GetColorU32(ImGuiCol_DragDropTarget));
            if (payload != nullptr) {
                log_tree_frame->trace("Dnd payload is Node (bottom rect)");
                IM_ASSERT(payload->DataSize == sizeof(erhe::toolkit::Unique_id<erhe::scene::Node>::id_type));
                const auto payload_id = *(const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type*)payload->Data;
                move_selection(node, payload_id, Placement::After_anchor);
            } else {
                log_tree_frame->trace("Dnd payload is not Node (bottom rect)");
            }
            ImGui::EndDragDropTarget();
            return true;
        }
    } else {
        log_tree_frame->trace("Dnd item is not Node: {}", item->describe());
    }
    return false;
}

void Item_tree_window::set_item_selection_terminator(
    const std::shared_ptr<erhe::scene::Item>& item
)
{
    auto& range_selection = m_context.selection->range_selection();
    range_selection.set_terminator(item);
}

void Item_tree_window::set_item_selection(
    const std::shared_ptr<erhe::scene::Item>& item,
    const bool                                selected
)
{
    if (selected) {
        m_context.selection->add_to_selection(item);
    } else {
        m_context.selection->remove_from_selection(item);
    }
}

void Item_tree_window::item_update_selection(
    const std::shared_ptr<erhe::scene::Item>& item
)
{
    ERHE_PROFILE_FUNCTION();

    auto& range_selection = m_context.selection->range_selection();

    drag_and_drop_source(item);

    const bool shift_down     = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
    const bool ctrl_down      = ImGui::IsKeyDown(ImGuiKey_LeftCtrl ) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
    const bool mouse_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    const bool focused        = ImGui::IsItemFocused();
    const bool hovered        = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

    if (hovered && mouse_released) {
        if (ctrl_down) {
            range_selection.reset();
            if (item->is_selected()) {
                set_item_selection(item, false);
            } else {
                set_item_selection(item, true);
                set_item_selection_terminator(item);
            }
        } else if (shift_down) {
            SPDLOG_LOGGER_TRACE(
                log_tree,
                "click with shift down on {} {} - range select",
                item->get_type_name(),
                item->get_name()
            );
            set_item_selection_terminator(item);
        } else {
            const bool was_selected = item->is_selected();
            range_selection.reset();
            m_context.selection->clear_selection();
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

    if (focused) {
        if (item != m_last_focus_item.lock()) {
            if (shift_down) {
                SPDLOG_LOGGER_TRACE(
                    log_tree,
                    "key with shift down on {} {} - range select",
                    item->get_type_name(),
                    item->get_name()
                );
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

    // CTRL-A to select all
    if (ctrl_down) {
        const bool a_pressed = ImGui::IsKeyPressed(ImGuiKey_A);
        if (a_pressed) {
            SPDLOG_LOGGER_TRACE(
                log_tree,
                "ctrl a pressed - select all"
            );
            select_all();
        }
    }
}

void Item_tree_window::item_popup_menu(
    const std::shared_ptr<erhe::scene::Item>& item
)
{
    const auto& node       = as_node(item);
    Scene_root* scene_root = static_cast<Scene_root*>(item->get_item_host());
    if (!node || (scene_root == nullptr))
    {
        return;
    }

    if (
        ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
        !m_popup_item
    ) {
        m_popup_item = item;
        m_popup_id_string = fmt::format("{}##Node{}-popup-menu", item->get_name(), item->get_id());
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
        auto parent_node = node->get_parent_node();

        bool close{false};
        if (ImGui::BeginMenu("Create")) {
            if (ImGui::MenuItem("Empty Node")) {
                m_operations.push_back(
                    [this, scene_root, parent_node]() {
                        m_context.scene_commands->create_new_empty_node(parent_node.get());
                    }
                );
                close = true;
            }
            if (ImGui::MenuItem("Camera")) {
                m_operations.push_back(
                    [this, scene_root, parent_node]() {
                        m_context.scene_commands->create_new_camera(parent_node.get());
                    }
                );
                close = true;
            }
            if (ImGui::MenuItem("Light")) {
                m_operations.push_back(
                    [this, scene_root, parent_node]() {
                        m_context.scene_commands->create_new_light(parent_node.get());
                    }
                );
                close = true;
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        ImGui::MenuItem("Cut");
        ImGui::MenuItem("Copy");
        ImGui::MenuItem("Paste");
        ImGui::MenuItem("Duplicate");
        ImGui::MenuItem("Delete");

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

void Item_tree_window::item_icon(
    const std::shared_ptr<erhe::scene::Item>& item
)
{
    std::optional<glm::vec2> icon;

    glm::vec4 color = item->get_wireframe_color();
    auto& icons = m_context.icon_set->icons;
    const auto& node = as_node(item);
    if (node) {
        icon = icons.node;
    }
    const auto& mesh = as_mesh(item);
    if (mesh) {
        icon = icons.mesh;
    }
    const auto& skin = as_skin(item);
    if (skin) {
        icon = icons.skin;
    }
    const auto& node_raytrace = as_raytrace(item);
    if (node_raytrace) {
        icon = icons.raytrace;
    }
    const auto& node_physics = as_physics(item);
    if (node_physics) {
        icon = icons.physics;
    }
    if (is_bone(item)) {
        icon = icons.bone;
    }
    const auto& light = as_light(item);
    if (light) {
        color = glm::vec4{light->color, 1.0f};
        switch (light->type) {
            //using enum erhe::scene::Light_type;
            case erhe::scene::Light_type::spot:        icon = icons.spot_light; break;
            case erhe::scene::Light_type::directional: icon = icons.directional_light; break;
            case erhe::scene::Light_type::point:       icon = icons.point_light; break;
            default: break;
        }
    }
    const auto& camera = as_camera(item);
    if (camera) {
        icon = icons.camera;
    }
    const auto& scene = as_scene(item);
    if (scene) {
        icon = icons.scene;
    }

    if (icon.has_value()) {
        const auto& icon_rasterization = get_scale_value() < 1.5f
            ? m_context.icon_set->get_small_rasterization()
            : m_context.icon_set->get_large_rasterization();
        icon_rasterization.icon(icon.value(), color);
    }
}

auto Item_tree_window::item_icon_and_text(
    const std::shared_ptr<erhe::scene::Item>& item,
    const bool                                update
) -> Tree_node_state
{
    ERHE_PROFILE_FUNCTION();

    item_icon(item);

    const auto& node = as_node(item);
    if (!m_context.editor_settings->node_tree_expand_attachments && node) {
        for (const auto& node_attachment : node->get_attachments()) {
            item_icon(node_attachment);
        }
    }

    const auto& scene = as_scene(item);

    bool is_leaf = true;
    if (node && (node->get_child_count(m_filter) > 0)) {
        is_leaf = false;
    }
    if (scene && scene->get_root_node()->get_child_count(m_filter) > 0) {
        is_leaf = false;
    }

    const ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_SpanAvailWidth |
        (scene
            ? ImGuiTreeNodeFlags_DefaultOpen
            : ImGuiTreeNodeFlags_None
        ) |
        (is_leaf
            ? (ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Leaf)
            : ImGuiTreeNodeFlags_OpenOnArrow
        ) |
        (update && item->is_selected()
            ? ImGuiTreeNodeFlags_Selected
            : ImGuiTreeNodeFlags_None
        );

    const bool item_node_open = ImGui::TreeNodeEx(
        item->get_label().c_str(),
        flags
    );

#if 0
    std::stringstream ss;
    if (ImGui::IsItemClicked()) ss << "clicked ";
    if (ImGui::IsItemToggledOpen()) ss << "toggled_open ";
    if (ImGui::IsItemToggledSelection()) ss << "toggled_selection ";
    //if (ImGui::IsItemHovered()) ss << "hovered ";
    //if (ImGui::IsItemActive()) ss << "active ";
    if (ImGui::IsItemActivated()) ss << "activated ";
    if (ImGui::IsItemDeactivated()) ss << "deactivated ";
    //if (ImGui::IsItemFocused()) ss << "focused ";
    //if (ImGui::IsItemVisible()) ss << "visible ";
    if (ImGui::IsItemEdited()) ss << "edited ";
    const std::string message = ss.str();
    if (!message.empty())
    {
        log_tree->info("{} {}", item->get_label(), message);
    }
#endif

    const bool is_item_toggled_open = ImGui::IsItemToggledOpen();
    if (is_item_toggled_open) {
        m_toggled_open = true;
    }
    if (!m_toggled_open) {
        item_popup_menu(item);

        if (update) {
            ERHE_PROFILE_SCOPE("update");
            const bool consumed_by_drag_and_drop = ImGui::IsDragDropActive() && drag_and_drop_target(item);
            if (!consumed_by_drag_and_drop) {
                item_update_selection(item);
            }
        }
    }
    //// log_frame->info("{} - is_leaf = {}", item->get_label(), is_leaf);

    return Tree_node_state{
        .is_open       = item_node_open,
        .need_tree_pop = !is_leaf
    };
}

void Item_tree_window::imgui_item_node(
    const std::shared_ptr<erhe::scene::Item>& item
)
{
    ERHE_PROFILE_FUNCTION();

    if (!m_filter(item->get_flag_bits())) {
        //// log_tree->info("filtered {}", item->describe());
        return;
    }

    m_context.selection->range_selection().entry(item);

    const auto tree_node_state = item_icon_and_text(item, true);
    if (tree_node_state.is_open) {
        const auto& node = as_node(item);
        if (node) {
            if (m_context.editor_settings->node_tree_expand_attachments) {
                const float attachment_indent = 15.0f; // TODO
                ImGui::Indent(attachment_indent);
                for (const auto& node_attachment : node->get_attachments()) {
                    imgui_item_node(node_attachment);
                }
                ImGui::Unindent(attachment_indent);
            }
            for (const auto& child_node : node->get_children()) {
                imgui_item_node(child_node);
            }
        }
        if (tree_node_state.need_tree_pop) {
            ImGui::TreePop();
        }
    }
}
#endif

void Item_tree_window::on_begin()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2{0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2{3.0f, 3.0f});
#endif
}

void Item_tree_window::on_end()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::PopStyleVar(2);
#endif
}

void Item_tree_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION();

#if 1 //// TODO
    ImGui::Checkbox("Expand Attachments", &m_context.editor_settings->node_tree_expand_attachments);
    ImGui::Checkbox("Show All",           &m_context.editor_settings->node_tree_show_all);
    if (m_context.editor_settings->node_tree_show_all) {
        m_filter = erhe::scene::Item_filter{
            .require_all_bits_set           = 0,
            .require_at_least_one_bit_set   = 0,
            .require_all_bits_clear         = 0, //erhe::scene::Item_flags::tool | erhe::scene::Item_flags::brush,
            .require_at_least_one_bit_clear = 0
        };
    } else {
        m_filter = erhe::scene::Item_filter{
            .require_all_bits_set           = erhe::scene::Item_flags::show_in_ui,
            .require_at_least_one_bit_set   = 0,
            .require_all_bits_clear         = 0, //erhe::scene::Item_flags::tool | erhe::scene::Item_flags::brush,
            .require_at_least_one_bit_clear = 0
        };
    }
#endif

    m_tree_items_last_frame = m_tree_items;
    m_tree_items.clear();

    m_context.selection->range_selection().begin();

    // TODO Handle cross scene drags and drops
#if 1 //// TODO
    if (ImGui::Button("Create Scene")) {
        auto content_library = std::make_shared<Content_library>();
        content_library->materials.make("Default");
        auto scene_root = std::make_shared<Scene_root>(
            *m_context.scene_message_bus,
            content_library,
            "new scene"
        );

        using Item_flags = erhe::scene::Item_flags;

        auto camera_node = std::make_shared<erhe::scene::Node>("Camera Node");
        auto camera = std::make_shared<erhe::scene::Camera>("Camera");
        camera_node->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
        camera     ->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
        camera_node->attach    (camera);
        camera_node->set_parent(scene_root->get_hosted_scene()->get_root_node());
        camera_node->set_world_from_node(
            erhe::toolkit::create_look_at(
                glm::vec3{0.0f, 1.0f, 1.0f},
                glm::vec3{0.0f, 1.0f, 0.0f},
                glm::vec3{0.0f, 1.0f, 0.0f}
            )
        );

        auto light_node = std::make_shared<erhe::scene::Node>("Light Node");
        auto light = std::make_shared<erhe::scene::Light>("Light");
        light_node->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
        light     ->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
        light     ->layer_id  = scene_root->layers().light()->id;
        light_node->attach    (light);
        light_node->set_parent(scene_root->get_hosted_scene()->get_root_node());
        light_node->set_world_from_node(
            erhe::toolkit::create_look_at(
                glm::vec3{0.0f, 3.0f, 0.0f},
                glm::vec3{0.0f, 0.0f, 0.0f},
                glm::vec3{0.0f, 0.0f, 1.0f}
            )
        );

        m_context.editor_scenes->register_scene_root(scene_root);
    }
#endif
    const auto& scene_roots = m_context.editor_scenes->get_scene_roots();
    for (const auto& scene_root : scene_roots) {
        const auto& scene = scene_root->get_scene();
        for (const auto& node : scene.get_root_node()->get_children()) {
            imgui_item_node(node);
        }
    }

    for (const auto& fun : m_operations) {
        fun();
    }
    m_operations.clear();

    if (m_operation) {
        m_context.operation_stack->push(m_operation);
        m_operation.reset();
    }

    m_context.selection->range_selection().end();

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_toggled_open = false;
    }

    m_context.editor_scenes->sanity_check();
#endif
}

}
