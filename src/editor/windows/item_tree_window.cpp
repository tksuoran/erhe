// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "windows/item_tree_window.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_scenes.hpp"
#include "editor_settings.hpp"
#include "graphics/icon_set.hpp"
#include "operations/compound_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/item_parent_change_operation.hpp"
#include "operations/operation_stack.hpp"
#include "scene/scene_commands.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"
#include "tools/clipboard.hpp"

#include "erhe_bit/bit_helpers.hpp"
#include "erhe_defer/defer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_profile/profile.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace editor {

using Light_type = erhe::scene::Light_type;

Item_tree::Item_tree(Editor_context& context, const std::shared_ptr<erhe::Hierarchy>& root)
    : m_context{context}
    , m_filter{
        .require_all_bits_set           = erhe::Item_flags::show_in_ui,
        .require_at_least_one_bit_set   = 0,
        .require_all_bits_clear         = 0, //erhe::Item_flags::tool | erhe::Item_flags::brush,
        .require_at_least_one_bit_clear = 0
    }
    , m_root{root}
{
}

void Item_tree::set_item_filter(const erhe::Item_filter& filter)
{
    m_filter = filter;
}

void Item_tree::set_item_callback(std::function<bool(const std::shared_ptr<erhe::Item_base>&)> fun)
{
    m_item_callback = fun;
}

void Item_tree::set_hover_callback(std::function<void()> fun)
{
    m_hover_callback = fun;
}

void Item_tree::clear_selection()
{
    SPDLOG_LOGGER_TRACE(log_tree, "clear_selection()");

    m_context.selection->clear_selection();
}

void Item_tree::recursive_add_to_selection(const std::shared_ptr<erhe::Item_base>& item)
{
    m_context.selection->add_to_selection(item);
    auto hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
    if (!hierarchy) {
        return;
    }

    for (const auto& child : hierarchy->get_children()) {
        recursive_add_to_selection(child);
    }
}

void Item_tree::select_all()
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
    const auto& selection = m_context.selection->get_selection();
    const std::shared_ptr<erhe::Item_base> drag_item = payload_item->shared_from_this();

    std::shared_ptr<erhe::Item_base> anchor = target_node;
    if (is_in(drag_item, selection)) {
        // Dragging node which is part of the selection.
        // In this case we apply reposition to whole selection.
        if (placement == Placement::Before_anchor) {
            for (const auto& item : selection) {
                const auto& node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
                if (node) {
                    reposition(compound_parameters, anchor, node, placement, Selection_usage::Selection_used);
                }
            }
        } else { // if (placement == Placement::After_anchor)
            for (auto i = selection.rbegin(), end = selection.rend(); i < end; ++i) {
                const auto& item = *i;
                const auto& node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
                if (node) {
                    reposition(compound_parameters, anchor, node, placement, Selection_usage::Selection_used);
                }
            }
        }
    } else if (compound_parameters.operations.empty()) {
        // Dragging a single node which is not part of the selection.
        // In this case we ignore selection and apply operation only to dragged node.
        const auto& drag_node = std::dynamic_pointer_cast<erhe::scene::Node>(drag_item);
        if (drag_node) {
            reposition(compound_parameters, anchor, drag_node, placement, Selection_usage::Selection_ignored);
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
    const auto& selection = m_context.selection->get_selection();
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
    ERHE_PROFILE_FUNCTION();

    log_tree_frame->trace("DnD source: '{}'", item->describe());

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        erhe::Item_base* item_raw = item.get();
        ImGui::SetDragDropPayload(item->get_type_name().data(), &item_raw, sizeof(item_raw));

        const auto& selection = m_context.selection->get_selection();
        if (is_in(item, selection)) {
            for (const auto& selection_item : selection) {
                item_icon_and_text(selection_item, 0);
            }
        } else {
            item_icon_and_text(item, 0);
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

auto Item_tree::drag_and_drop_target(const std::shared_ptr<erhe::Item_base>& item) -> bool
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
    const auto& node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
    if (node) {
        log_tree_frame->trace("Dnd item is Node: {}", node->describe());
        const ImRect top_rect{rect_min, ImVec2{rect_max.x, y1}};
        if (ImGui::BeginDragDropTargetCustom(top_rect, imgui_id_before)) {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Node", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
            drag_and_drop_gradient_preview(x0, x1, y0, y2, ImGui::GetColorU32(ImGuiCol_DragDropTarget), 0);
            if (payload != nullptr) {
                log_tree_frame->trace("Dnd payload is Node (top rect)");
                IM_ASSERT(payload->DataSize == sizeof(erhe::Item_base*));
                erhe::Item_base* payload_item = *(static_cast<erhe::Item_base**>(payload->Data));
                move_selection(node, payload_item, Placement::Before_anchor);
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
                IM_ASSERT(payload->DataSize == sizeof(erhe::Item_base*));
                erhe::Item_base* payload_item = *(static_cast<erhe::Item_base**>(payload->Data));
                attach_selection_to(node, payload_item);
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
                IM_ASSERT(payload->DataSize == sizeof(erhe::Item_base*));
                erhe::Item_base* payload_item = *(static_cast<erhe::Item_base**>(payload->Data));
                move_selection(node, payload_item, Placement::After_anchor);
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

void Item_tree::set_item_selection_terminator(const std::shared_ptr<erhe::Item_base>& item)
{
    auto& range_selection = m_context.selection->range_selection();
    range_selection.set_terminator(item);
}

void Item_tree::set_item_selection(const std::shared_ptr<erhe::Item_base>& item, bool selected)
{
    if (selected) {
        m_context.selection->add_to_selection(item);
    } else {
        m_context.selection->remove_from_selection(item);
    }
}

void Item_tree::item_update_selection(const std::shared_ptr<erhe::Item_base>& item)
{
    ERHE_PROFILE_FUNCTION();

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

    if (!shift_down) {
        m_shift_down_range_selection_started = false;
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
            range_selection.reset();
            if (item->is_selected()) {
                set_item_selection(item, false);
            } else {
                set_item_selection(item, true);
                set_item_selection_terminator(item);
            }
        } else if (shift_down) {
            SPDLOG_LOGGER_TRACE(log_tree, "click with shift down on {} {} - range select", item->get_type_name(), item->get_name());
            set_item_selection_terminator(item);
        } else {
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

    // CTRL-A to select all
    if (ctrl_down) {
        const bool a_pressed = ImGui::IsKeyPressed(ImGuiKey_A);
        if (a_pressed) {
            SPDLOG_LOGGER_TRACE(log_tree, "ctrl a pressed - select all");
            select_all();
        }
    }
}

void Item_tree::item_popup_menu(const std::shared_ptr<erhe::Item_base>& item)
{
    const auto& node       = std::dynamic_pointer_cast<erhe::scene::Node>(item);
    Scene_root* scene_root = static_cast<Scene_root*>(item->get_item_host());
    if (!node || (scene_root == nullptr)) {
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
        const auto& hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
        const bool selected_or_hierarchy = item->is_selected() || hierarchy;
        if (!selected_or_hierarchy) {
            ImGui::BeginDisabled();
        }
        if (ImGui::MenuItem("Cut")) {
            if (item->is_selected()) {
                m_context.selection->cut_selection();
            } else {
                ERHE_VERIFY(hierarchy);
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
        if (ImGui::MenuItem("Copy")) {
            if (item->is_selected()) {
                m_context.selection->copy_selection();
            } else {
                m_context.clipboard->set_contents(item->clone());
            }
        }

        if (!selected_or_hierarchy) {
            ImGui::EndDisabled();
        }

        const std::vector<std::shared_ptr<erhe::Item_base>>& clipboard_contents = m_context.clipboard->get_contents();
        const bool clipboard_is_empty = clipboard_contents.empty();
        if (clipboard_is_empty || !hierarchy) {
            ImGui::BeginDisabled();
        }
        if (ImGui::MenuItem("Paste")) {
            m_context.clipboard->try_paste(hierarchy, hierarchy->get_child_count());
        }
        if (clipboard_is_empty || !hierarchy) {
            ImGui::EndDisabled();
        }

        if (!selected_or_hierarchy) {
            ImGui::BeginDisabled();
        }

        if (ImGui::MenuItem("Duplicate")) {
            if (item->is_selected()) {
                m_context.selection->duplicate_selection();
            } else {
                ERHE_VERIFY(hierarchy);
                auto op = std::make_shared<Item_insert_remove_operation>(
                    Item_insert_remove_operation::Parameters{
                        .context         = m_context,
                        .item            = std::dynamic_pointer_cast<erhe::Hierarchy>(hierarchy->clone()),
                        .parent          = hierarchy->get_parent().lock(),
                        .mode            = Item_insert_remove_operation::Mode::insert,
                        .index_in_parent = hierarchy->get_index_in_parent() + 1
                    }
                );
                m_context.operation_stack->queue(op);
             }
        }
        if (ImGui::MenuItem("Delete")) {
            if (item->is_selected()) {
                m_context.selection->delete_selection();
            } else {
                ERHE_VERIFY(hierarchy);
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
        if (!selected_or_hierarchy) {
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

void Item_tree::root_popup_menu()
{
    const auto& node       = std::dynamic_pointer_cast<erhe::scene::Node>(m_root);
    Scene_root* scene_root = static_cast<Scene_root*>(m_root->get_item_host());
    if (!node || (scene_root == nullptr)) {
        return;
    }

    static bool opened = false;
    if (
        ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        !m_popup_item
    ) {
        m_popup_item = m_root;
        m_popup_id_string = fmt::format("{}##Node{}-popup-menu", m_root->get_name(), m_root->get_id());
        m_popup_id = ImGui::GetID(m_popup_id_string.c_str());
        ImGui::OpenPopupEx(
            m_popup_id,
            ImGuiPopupFlags_MouseButtonRight
        );
        opened = true;
    }

    if ((m_popup_item != m_root) || m_popup_id_string.empty()) {
        if (opened) {
            opened = false;
        }
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
        const auto& hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(m_root);
        
        const std::vector<std::shared_ptr<erhe::Item_base>>& clipboard_contents = m_context.clipboard->get_contents();
        const bool clipboard_is_empty = clipboard_contents.empty();
        if (clipboard_is_empty) {
            ImGui::BeginDisabled();
        }
        if (ImGui::MenuItem("Paste")) {
            m_context.clipboard->try_paste(hierarchy, hierarchy->get_child_count());
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

auto Item_tree::item_icon_and_text(const std::shared_ptr<erhe::Item_base>& item, const unsigned int visual_flags) -> Tree_node_state
{
    ERHE_PROFILE_FUNCTION();

    using namespace erhe::bit;
    bool update       = test_all_rhs_bits_set(visual_flags, item_visual_flag_update);
    bool force_expand = test_all_rhs_bits_set(visual_flags, item_visual_flag_force_expand);
    bool table_row    = test_all_rhs_bits_set(visual_flags, item_visual_flag_table_row);

    if (table_row) {
        ImGui::PushID(m_row++);
        ImGui::TableNextRow(ImGuiTableRowFlags_None);
        ImGui::TableSetColumnIndex(0);
    }
    ERHE_DEFER( if (table_row) ImGui::PopID(); );

    m_context.icon_set->item_icon(item, m_ui_scale);

    const auto& node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
    if (!m_context.editor_settings->node_tree_expand_attachments && node) {
        for (const auto& node_attachment : node->get_attachments()) {
            m_context.icon_set->item_icon(node_attachment, m_ui_scale);
        }
    }
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN); 

    const auto& content_library_node = std::dynamic_pointer_cast<Content_library_node>(item);
    const auto& hierarchy            = std::dynamic_pointer_cast<erhe::Hierarchy     >(item);
    const auto& scene                = std::dynamic_pointer_cast<erhe::scene::Scene  >(item);

    bool is_leaf = true;
    if (hierarchy && (hierarchy->get_child_count(m_filter) > 0)) {
        is_leaf = false;
    }
    if (scene && scene->get_root_node() && scene->get_root_node()->get_child_count(m_filter) > 0) {
        is_leaf = false;
    }
    if (scene || content_library_node) {
        force_expand = true;
    }

    ////bool is_last_selected = false;
    ////if (!item->is_selected()) {
    ////    std::shared_ptr<erhe::Item_base> item_for_last_selected = (content_library_node && content_library_node->item) ? content_library_node->item : item;
    ////    std::shared_ptr<erhe::Item_base> last_selected_item = m_context.selection->get_last_selected(item_for_last_selected->get_type() );
    ////    if (item_for_last_selected == last_selected_item) {
    ////        is_last_selected = true;
    ////         ImGuiStyle& style = ImGui::GetCurrentContext()->Style;
    ////         ImVec4 not_selected_color  = style.Colors[ImGuiCol_WindowBg];
    ////         ImVec4 selected_color      = style.Colors[ImGuiCol_Header];
    ////         ImVec4 last_selected_color{
    ////             0.5f * (not_selected_color.x + selected_color.x),
    ////             0.5f * (not_selected_color.y + selected_color.y),
    ////             0.5f * (not_selected_color.z + selected_color.z),
    ////             0.5f * (not_selected_color.w + selected_color.w)
    ////         };
    ////         ImGui::PushStyleColor(ImGuiCol_Header, last_selected_color);
    ////    }
    ////}

    const ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_SpanAvailWidth |
        ImGuiTreeNodeFlags_SpanAllColumns |
        (force_expand ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None) |
        (is_leaf
            ? (ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Leaf)
            : ImGuiTreeNodeFlags_OpenOnArrow
        ) |
        (update && (item->is_selected()) ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None);
        ////(update && (item->is_selected() || is_last_selected) ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None);

    const bool item_node_open = ImGui::TreeNodeEx(item->get_label().c_str(), flags);
    //// if (is_last_selected) {
    ////     ImGui::PopStyleColor();
    //// }

    const bool consumed = m_item_callback ? m_item_callback(item) : false;

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
            if (!consumed) {
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

auto Item_tree::should_show(const std::shared_ptr<erhe::Item_base>& item) -> Show_mode
{
    const bool show_by_type = m_filter(item->get_flag_bits());
    const bool show_by_name = m_text_filter.PassFilter(item->get_name().c_str());
    if (show_by_type && show_by_name) {
        return Show_mode::Show;
    }

    bool show_by_attachments = false;
    const auto& node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
    if (node) {
        for (const auto& node_attachment : node->get_attachments()) {
            if (should_show(node_attachment) != Show_mode::Hide) {
                show_by_attachments = true;
                break;
            }
        }
    }
    if (show_by_attachments) {
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

void Item_tree::imgui_item_node(const std::shared_ptr<erhe::Item_base>& item)
{
    // Special handling for invisible parents (scene root)
    if (erhe::bit::test_all_rhs_bits_set(item->get_flag_bits(), erhe::Item_flags::invisible_parent)) {
        const auto& hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
        if (hierarchy) {
            for (const auto& child_node : hierarchy->get_children()) {
                imgui_item_node(child_node);
            }
        }
        return;
    }

    ERHE_PROFILE_FUNCTION();

    const Show_mode show = should_show(item);
    if (show == Show_mode::Hide) {
        //// log_tree->info("filtered {}", item->describe());
        return;
    }

    m_context.selection->range_selection().entry(item);

    unsigned int flags = item_visual_flag_update | item_visual_flag_table_row;
    if (show == Show_mode::Show_expanded) {
        flags |= item_visual_flag_force_expand;
    }
    const auto tree_node_state = item_icon_and_text(item, flags);
    if (tree_node_state.is_open) {
        if (m_context.editor_settings->node_tree_expand_attachments) {
            const auto& node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
            if (node) {
                const float attachment_indent = 15.0f; // TODO
                ImGui::Indent(attachment_indent);
                for (const auto& node_attachment : node->get_attachments()) {
                    imgui_item_node(node_attachment);
                }
                ImGui::Unindent(attachment_indent);
            }
        }
        const auto& hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
        if (hierarchy) {
            for (const auto& child_node : hierarchy->get_children()) {
                imgui_item_node(child_node);
            }
        }
        if (tree_node_state.need_tree_pop) {
            ImGui::TreePop();
        }
    }
}

void Item_tree::imgui_tree(float ui_scale)
{
    ERHE_PROFILE_FUNCTION();

    ///ImGuiStyle& style = ImGui::GetCurrentContext()->Style;
    ///ImVec4 not_selected_color     = style.Colors[ImGuiCol_WindowBg];
    ///ImVec4 selected_color         = style.Colors[ImGuiCol_Selected];
    ///ImVec4 selected_hovered_color = style.Colors[ImGuiCol_SelectedHovered];
    ///ImVec4 selected_active_color  = style.Colors[ImGuiCol_SelectedActive];
    ///ImGui::PushStyleColor(ImGuiCol_Header, last_selected_color);

    m_ui_scale = ui_scale;
    m_row = 0;

    const std::size_t root_id = m_root->get_id();
    const int table_id = static_cast<int>(root_id);
    ImGui::PushID(table_id);
    ERHE_DEFER( ImGui::PopID(); );

    m_text_filter.Draw("Filter:", -FLT_MIN);

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{0.0f, 0.0f});
    ERHE_DEFER( ImGui::PopStyleVar(1); );

    bool table_visible = ImGui::BeginTable("##", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoClip);
    if (!table_visible) {
        return;
    }

    ImGui::TableSetupColumn("icons", /*ImGuiTableColumnFlags_IndentDisable |*/ ImGuiTableColumnFlags_NoClip | ImGuiTableColumnFlags_WidthFixed,   50.0f);
    ImGui::TableSetupColumn("entry", ImGuiTableColumnFlags_IndentEnable  | ImGuiTableColumnFlags_WidthStretch,  1.0f);

#if 0 //// TODO
    ImGui::Checkbox("Expand Attachments", &m_context.editor_settings->node_tree_expand_attachments);
    ImGui::Checkbox("Show All",           &m_context.editor_settings->node_tree_show_all);
#endif

    m_context.selection->range_selection().begin();

    // TODO Handle cross scene drags and drops
#if 0 //// TODO
    if (ImGui::Button("Create Scene")) {
        auto content_library = std::make_shared<Content_library>();
        content_library->materials.make("Default");
        auto scene_root = std::make_shared<Scene_root>(
            *m_context.scene_message_bus,
            content_library,
            "new scene"
        );

        using Item_flags = erhe::Item_flags;

        auto camera_node = std::make_shared<erhe::scene::Node>("Camera Node");
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

        auto light_node = std::make_shared<erhe::scene::Node>("Light Node");
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

        m_context.editor_scenes->register_scene_root(scene_root);
    }
#endif
    //// const auto& scene_roots = m_context.editor_scenes->get_scene_roots();
    //// for (const auto& scene_root : scene_roots) {
    ////     const auto& scene = scene_root->get_scene();
    ////     for (const auto& node : scene.get_root_node()->get_children()) {
    ////         imgui_item_node(node);
    ////     }
    //// }
    imgui_item_node(m_root);

    for (const auto& fun : m_operations) {
        fun();
    }
    m_operations.clear();

    if (m_operation) {
        m_context.operation_stack->queue(m_operation);
        m_operation.reset();
    }

    m_context.selection->range_selection().end();

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_toggled_open = false;
    }

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
        if (m_hover_callback) {
            m_hover_callback();
        }
        root_popup_menu();
    }

    ImGui::EndTable();

    //// m_context.editor_scenes->sanity_check();
}

////////////////////////////

Item_tree_window::Item_tree_window(
    erhe::imgui::Imgui_renderer&            imgui_renderer,
    erhe::imgui::Imgui_windows&             imgui_windows,
    Editor_context&                         context,
    const std::string_view                  window_title,
    const std::string_view                  ini_label,
    const std::shared_ptr<erhe::Hierarchy>& root
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, window_title, ini_label}
    , Item_tree{context, root}
{
}

void Item_tree_window::on_begin()
{
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2{0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2{3.0f, 3.0f});
}

void Item_tree_window::on_end()
{
    ImGui::PopStyleVar(2);
}

void Item_tree_window::imgui()
{
    ERHE_PROFILE_FUNCTION();

    imgui_tree(get_scale_value());
}

}
