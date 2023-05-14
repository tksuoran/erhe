// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "windows/node_tree_window.hpp"

#include "editor_log.hpp"
#include "editor_scenes.hpp"
#include "windows/node_tree_window.hpp"
#include "graphics/icon_set.hpp"
#include "operations/compound_operation.hpp"
#include "operations/insert_operation.hpp"
#include "operations/node_operation.hpp"
#include "operations/operation_stack.hpp"
#include "scene/content_library.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_commands.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/windows/log_window.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/raytrace/iscene.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <gsl/gsl>

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#   include <imgui_internal.h>
#endif

namespace editor
{

using Light_type = erhe::scene::Light_type;

Node_tree_window* g_node_tree_window{nullptr};

Node_tree_window::Node_tree_window()
    : erhe::components::Component    {c_type_name}
    , erhe::application::Imgui_window{c_title}
    , m_filter{
        .require_all_bits_set           = erhe::scene::Item_flags::show_in_ui,
        .require_at_least_one_bit_set   = 0,
        .require_all_bits_clear         = 0, //erhe::scene::Item_flags::tool | erhe::scene::Item_flags::brush,
        .require_at_least_one_bit_clear = 0
    }
{
}

Node_tree_window::~Node_tree_window() noexcept
{
    ERHE_VERIFY(g_node_tree_window == nullptr);
}

void Node_tree_window::deinitialize_component()
{
    ERHE_VERIFY(g_node_tree_window == this);
    m_tree_items.clear();
    m_tree_items_last_frame.clear();
    m_operation.reset();
    m_operations.clear();
    m_last_focus_item.reset();
    m_popup_item.reset();
    g_node_tree_window = nullptr;
}

void Node_tree_window::declare_required_components()
{
    require<Selection_tool>();
    require<erhe::application::Imgui_windows>();
}

void Node_tree_window::initialize_component()
{
    ERHE_VERIFY(g_node_tree_window == nullptr);
    erhe::application::g_imgui_windows->register_imgui_window(this, "node_tree");
    g_node_tree_window = this;
}

void Node_tree_window::clear_selection()
{
    SPDLOG_LOGGER_TRACE(log_node_properties, "clear_selection()");

    g_selection_tool->clear_selection();
}

void Node_tree_window::recursive_add_to_selection(
    const std::shared_ptr<erhe::scene::Node>& node
)
{
    g_selection_tool->add_to_selection(node);
    for (const auto& child_node : node->children()) {
        recursive_add_to_selection(child_node);
    }
}

void Node_tree_window::select_all()
{
    SPDLOG_LOGGER_TRACE(log_node_properties, "select_all()");

    g_selection_tool->clear_selection();
    const auto& scene_roots = g_editor_scenes->get_scene_roots();
    for (const auto& scene_root : scene_roots) {
        const auto& scene = scene_root->scene();
        for (const auto& node : scene.get_root_node()->children()) {
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

auto Node_tree_window::get_item_by_id(
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

void Node_tree_window::move_selection(
    const std::shared_ptr<erhe::scene::Node>&                  target_node,
    const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type payload_id,
    const Placement                                            placement
)
{
    log_node_properties->trace(
        "move_selection(anchor = {}, {})",
        target_node ? target_node->get_name() : "(empty)",
        (placement == Placement::Before_anchor)
            ? "Before_anchor"
            : "After_anchor"
    );

    Compound_operation::Parameters compound_parameters;
    const auto& selection = g_selection_tool->selection();
    const auto& drag_item = get_item_by_id(payload_id);

    std::shared_ptr<erhe::scene::Node> anchor = target_node;
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
    const std::shared_ptr<erhe::scene::Node>&              node,
    const std::vector<std::shared_ptr<erhe::scene::Item>>& selection
) -> std::shared_ptr<erhe::scene::Node>
{
    const auto& node_parent = node->parent().lock();
    if (node_parent) {
        if (is_in(node_parent, selection)) {
            return node_parent;
        }
        return get_ancestor_in(node_parent, selection);
    } else {
        return {};
    }
}

} // anonymous namespace

void Node_tree_window::reposition(
    Compound_operation::Parameters&           compound_parameters,
    const std::shared_ptr<erhe::scene::Node>& anchor_node,
    const std::shared_ptr<erhe::scene::Node>& node,
    const Placement                           placement,
    const Selection_usage                     selection_usage
)
{
    SPDLOG_LOGGER_TRACE(
        log_node_properties,
        "reposition(anchor_node = {}, node = {}, placement = {})",
        anchor_node ? anchor_node->get_name() : "(empty)",
        node ? node->get_name() : "(empty)",
        (placement == Placement::Before_anchor)
            ? "Before_anchor"
            : "After_anchor"
    );

    if (!node) {
        SPDLOG_LOGGER_WARN(log_node_properties, "Bad empty node");
        return;
    }

    if (!anchor_node) {
        SPDLOG_LOGGER_WARN(log_node_properties, "Bad empty anchor node");
        return;
    }

    // Nodes cannot be attached to themselves
    if (node == anchor_node) {
        return;
    }

    // Ancestors cannot be attached to descendants
    if (anchor_node->is_ancestor(node.get())) {
        SPDLOG_LOGGER_WARN(log_node_properties, "Ancestors cannot be attached to descendants");
        return;
    }

    if (selection_usage == Selection_usage::Selection_used) {
        const auto& selection = g_selection_tool->selection();

        // Ignore nodes if their ancestors is in selection
        const auto ancestor_in_selection = get_ancestor_in(node, selection);
        if (ancestor_in_selection) {
            SPDLOG_LOGGER_TRACE(
                log_node_properties,
                "Ignoring node {} because ancestor {} is in selection",
                node->get_name(),
                ancestor_in_selection->get_name()
            );
            return;
        }
    }

    if (anchor_node->parent().lock() != node->parent().lock()) {
        compound_parameters.operations.push_back(
            std::make_shared<Node_attach_operation>(
                anchor_node->parent().lock(),
                node,
                (placement == Placement::Before_anchor) ? anchor_node : std::shared_ptr<erhe::scene::Node>{},
                (placement == Placement::After_anchor ) ? anchor_node : std::shared_ptr<erhe::scene::Node>{}
            )
        );
        return;
    }

    compound_parameters.operations.push_back(
        std::make_shared<Node_reposition_in_parent_operation>(
            node,
            (placement == Placement::Before_anchor) ? anchor_node : std::shared_ptr<erhe::scene::Node>{},
            (placement == Placement::After_anchor ) ? anchor_node : std::shared_ptr<erhe::scene::Node>{}
        )
    );
}

void Node_tree_window::try_add_to_attach(
    Compound_operation::Parameters&           compound_parameters,
    const std::shared_ptr<erhe::scene::Node>& target_node,
    const std::shared_ptr<erhe::scene::Node>& node,
    const Selection_usage                     selection_usage
)
{
    SPDLOG_LOGGER_TRACE(
        log_node_properties,
        "try_add_to_attach(target_node = {}, node = {})",
        target_node ? target_node->get_name() : "(empty)",
        node        ? node->get_name()        : "(empty)"
    );

    if (!node) {
        SPDLOG_LOGGER_WARN(log_node_properties, "Bad empty node");
        return;
    }

    if (!target_node) {
        SPDLOG_LOGGER_WARN(log_node_properties, "Bad empty target node");
        return;
    }

    // Nodes cannot be attached to themselves
    if (node == target_node) {
        SPDLOG_LOGGER_WARN(log_node_properties, "Nodes cannot be attached to themselves");
        return;
    }

    // Ancestors cannot be attached to descendants
    if (target_node->is_ancestor(node.get())) {
        SPDLOG_LOGGER_WARN(log_node_properties, "Ancestors cannot be attached to descendants");
        return;
    }

    if (selection_usage == Selection_usage::Selection_used) {
        const auto& selection = g_selection_tool->selection();

        // Ignore nodes if their ancestors is in selection
        const auto ancestor_in_selection = get_ancestor_in(node, selection);
        if (ancestor_in_selection) {
            SPDLOG_LOGGER_TRACE(
                log_node_properties,
                "Ignoring node {} because ancestor {} is in selection",
                node->get_name(),
                ancestor_in_selection->get_name()
            );
            return;
        }
    }

    compound_parameters.operations.push_back(
        std::make_shared<Node_attach_operation>(
            target_node,
            node,
            std::shared_ptr<erhe::scene::Node>{},
            std::shared_ptr<erhe::scene::Node>{}
        )
    );
}

void Node_tree_window::attach_selection_to(
    const std::shared_ptr<erhe::scene::Node>&                  target_node,
    const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type payload_id
)
{
    SPDLOG_LOGGER_TRACE(
        log_node_properties,
        "attach_selection_to()"
    );

    //// log_tools->trace(
    ////     "attach_selection_to(target_node = {}, payload_id = {})",
    ////     target_node->get_name(),
    ////     payload_id
    //// );
    Compound_operation::Parameters compound_parameters;
    const auto& selection = g_selection_tool->selection();
    auto drag_item = get_item_by_id(payload_id);

    if (is_in(drag_item, selection)) {
        for (const auto& item : selection) {
            const auto& node = as_node(item);
            if (node) {
                try_add_to_attach(compound_parameters, target_node, node, Selection_usage::Selection_used);
            }
        }
    } else if (compound_parameters.operations.empty()) {
        const auto& drag_node = as_node(drag_item);
        if (drag_node) {
            try_add_to_attach(compound_parameters, target_node, drag_node, Selection_usage::Selection_ignored);
        }
    }

    if (!compound_parameters.operations.empty()) {
        m_operation = std::make_shared<Compound_operation>(std::move(compound_parameters));
    }
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)
void Node_tree_window::drag_and_drop_source(
    const std::shared_ptr<erhe::scene::Item>& item
)
{
    ERHE_PROFILE_FUNCTION();

    const auto id = item->get_id();
    m_tree_items.emplace(id, item);
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload(item->type_name(), &id, sizeof(id));

        const auto& selection = g_selection_tool->selection();
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

auto Node_tree_window::drag_and_drop_target(
    const std::shared_ptr<erhe::scene::Item>& item
) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (!item) {
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
    const std::string label_move_before = fmt::format("node dnd move before {}: {} {}", id, item->type_name(), item->get_name());
    const std::string label_attach_to   = fmt::format("node dnd attach to {}: {} {}",   id, item->type_name(), item->get_name());
    const std::string label_move_after  = fmt::format("node dnd move after {}: {} {}",  id, item->type_name(), item->get_name());
    const ImGuiID     imgui_id_before   = ImGui::GetID(label_move_before.c_str());
    const ImGuiID     imgui_id_attach   = ImGui::GetID(label_attach_to.c_str());
    const ImGuiID     imgui_id_after    = ImGui::GetID(label_move_after.c_str());

    // Move selection before drop target
    const auto& node = as_node(item);
    if (node) {
        const ImRect top_rect{rect_min, ImVec2{rect_max.x, y1}};
        if (ImGui::BeginDragDropTargetCustom(top_rect, imgui_id_before)) {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Node", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
            drag_and_drop_gradient_preview(x0, x1, y0, y2, ImGui::GetColorU32(ImGuiCol_DragDropTarget), 0);
            if (payload != nullptr) {
                IM_ASSERT(payload->DataSize == sizeof(erhe::toolkit::Unique_id<erhe::scene::Node>::id_type));
                const auto payload_id = *(const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type*)payload->Data;
                move_selection(node, payload_id, Placement::Before_anchor);
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
                IM_ASSERT(payload->DataSize == sizeof(erhe::toolkit::Unique_id<erhe::scene::Node>::id_type));
                const auto payload_id = *(const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type*)payload->Data;
                attach_selection_to(node, payload_id);
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
                IM_ASSERT(payload->DataSize == sizeof(erhe::toolkit::Unique_id<erhe::scene::Node>::id_type));
                const auto payload_id = *(const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type*)payload->Data;
                move_selection(node, payload_id, Placement::After_anchor);
            }
            ImGui::EndDragDropTarget();
            return true;
        }
    }
    return false;
}

void Node_tree_window::set_item_selection_terminator(
    const std::shared_ptr<erhe::scene::Item>& item
)
{
    auto& range_selection = g_selection_tool->range_selection();
    range_selection.set_terminator(item);
}

void Node_tree_window::set_item_selection(
    const std::shared_ptr<erhe::scene::Item>& item,
    const bool                                selected
)
{
    const auto& node = as_node(item);

    if (selected) {
        g_selection_tool->add_to_selection(item);
        if (node && !m_expand_attachments) {
            for (const auto& node_attachment : node->attachments()) {
                g_selection_tool->add_to_selection(node_attachment);
            }
        }
    } else {
        g_selection_tool->remove_from_selection(item);
        if (node && !m_expand_attachments) {
            for (const auto& node_attachment : node->attachments()) {
                g_selection_tool->remove_from_selection(node_attachment);
            }
        }
    }
}

void Node_tree_window::item_update_selection(
    const std::shared_ptr<erhe::scene::Item>& item
)
{
    ERHE_PROFILE_FUNCTION();

    if (!g_selection_tool) {
        SPDLOG_LOGGER_TRACE(
            log_node_properties,
            "imgui_node_update() - no selection tool"
        );
        return;
    }

    auto& range_selection = g_selection_tool->range_selection();

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
                log_node_properties,
                "click with shift down on {} {} - range select",
                item->type_name(),
                item->get_name()
            );
            set_item_selection_terminator(item);
        } else {
            const bool was_selected = item->is_selected();
            range_selection.reset();
            g_selection_tool->clear_selection();
            SPDLOG_LOGGER_TRACE(
                log_node_properties,
                "mouse button release without modifier keys on {} {} - {} selecting it",
                item->type_name(),
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
                    log_node_properties,
                    "key with shift down on {} {} - range select",
                    item->type_name(),
                    item->get_name()
                );
                set_item_selection_terminator(item);
            }
            // else
            // {
            //     SPDLOG_LOGGER_TRACE(
            //         log_node_properties,
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
                log_node_properties,
                "ctrl a pressed - select all"
            );
            select_all();
        }
    }
}

void Node_tree_window::item_popup_menu(
    const std::shared_ptr<erhe::scene::Item>& item
)
{
    const auto& node       = as_node(item);
    Scene_root* scene_root = reinterpret_cast<Scene_root*>(item->get_item_host());
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
        auto parent_node = node->parent().lock();

        bool close{false};
        if (ImGui::BeginMenu("Create")) {
            if (ImGui::MenuItem("Empty Node")) {
                m_operations.push_back(
                    [this, scene_root, parent_node]
                    ()
                    {
                        g_scene_commands->create_new_empty_node(parent_node.get());
                    }
                );
                close = true;
            }
            if (ImGui::MenuItem("Camera")) {
                m_operations.push_back(
                    [this, scene_root, parent_node]
                    ()
                    {
                        g_scene_commands->create_new_camera(parent_node.get());
                    }
                );
                close = true;
            }
            if (ImGui::MenuItem("Light")) {
                m_operations.push_back(
                    [this, scene_root, parent_node]
                    ()
                    {
                        g_scene_commands->create_new_light(parent_node.get());
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

void Node_tree_window::item_icon(
    const std::shared_ptr<erhe::scene::Item>& item
)
{
    std::optional<glm::vec2> icon;

    glm::vec4 color = item->get_wireframe_color();
    const auto& node = as_node(item);
    if (node) {
        icon = g_icon_set->icons.node;
    }
    const auto& mesh = as_mesh(item);
    if (mesh) {
        icon = g_icon_set->icons.mesh;
    }
    const auto& light = as_light(item);
    if (light) {
        color = glm::vec4{light->color, 1.0f};
        switch (light->type) {
            //using enum erhe::scene::Light_type;
            case erhe::scene::Light_type::spot:        icon = g_icon_set->icons.spot_light; break;
            case erhe::scene::Light_type::directional: icon = g_icon_set->icons.directional_light; break;
            case erhe::scene::Light_type::point:       icon = g_icon_set->icons.point_light; break;
            default: break;
        }
    }
    const auto& camera = as_camera(item);
    if (camera) {
        icon = g_icon_set->icons.camera;
    }
    const auto& scene = as_scene(item);
    if (scene) {
        icon = g_icon_set->icons.scene;
    }

    if (icon.has_value()) {
        const auto& icon_rasterization = get_scale_value() < 1.5f
            ? g_icon_set->get_small_rasterization()
            : g_icon_set->get_large_rasterization();
        icon_rasterization.icon(icon.value(), color);
    }
}

auto Node_tree_window::item_icon_and_text(
    const std::shared_ptr<erhe::scene::Item>& item,
    const bool                                update
) -> Tree_node_state
{
    ERHE_PROFILE_FUNCTION();

    item_icon(item);

    const auto& node = as_node(item);
    if (!m_expand_attachments && node) {
        for (const auto& node_attachment : node->attachments()) {
            item_icon(node_attachment);
        }
    }

    const auto& scene = as_scene(item);

    bool is_leaf = true;
    if (node && (node->child_count(m_filter) > 0)) {
        is_leaf = false;
    }
    if (scene && scene->get_root_node()->child_count(m_filter) > 0) {
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
        log_node_properties->info("{} {}", item->get_label(), message);
    }

    const bool is_item_toggled_open = ImGui::IsItemToggledOpen();
    if (is_item_toggled_open) {
        m_toggled_open = true;
    }
    if (!m_toggled_open) {
        item_popup_menu(item);

        if (update) {
            ERHE_PROFILE_SCOPE("update");
            const bool consumed_by_drag_and_drop = drag_and_drop_target(item);
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

void Node_tree_window::imgui_item_node(
    const std::shared_ptr<erhe::scene::Item>& item
)
{
    ERHE_PROFILE_FUNCTION();

    if (!m_filter(item->get_flag_bits())) {
        //// log_node_properties->info("filtered {}", item->describe());
        return;
    }

    if (g_selection_tool != nullptr) {
        g_selection_tool->range_selection().entry(item, m_expand_attachments);
    }

    const auto tree_node_state = item_icon_and_text(item, true);
    if (tree_node_state.is_open) {
        {
            const auto& node = as_node(item);
            if (node) {
                if (m_expand_attachments) {
                    const float attachment_indent = 15.0f; // TODO
                    ImGui::Indent(attachment_indent);
                    for (const auto& node_attachment : node->attachments()) {
                        imgui_item_node(node_attachment);
                    }
                    ImGui::Unindent(attachment_indent);
                }
                for (const auto& child_node : node->children()) {
                    imgui_item_node(child_node);
                }
            }
        }
        {
            const auto& scene = as_scene(item);
            if (scene) {
                for (const auto& node : scene->get_root_node()->children()) {
                    imgui_item_node(node);
                }
            }
        }
        if (tree_node_state.need_tree_pop) {
            ImGui::TreePop();
        }
    }
}
#endif

void Node_tree_window::on_begin()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2{0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2{3.0f, 3.0f});
#endif
}

void Node_tree_window::on_end()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::PopStyleVar(2);
#endif
}

auto Node_tree_window::expand_attachments() const -> bool
{
    return m_expand_attachments;
}

void Node_tree_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION();

#if 1 //// TODO
    ImGui::Checkbox("Expand Attachments", &m_expand_attachments);
    ImGui::Checkbox("Show All",           &m_show_all);
    if (m_show_all) {
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

    if (g_selection_tool) {
        g_selection_tool->range_selection().begin();
    }

    // TODO Handle cross scene drags and drops
#if 1 //// TODO
    if (ImGui::Button("Create Scene"))
    {
        auto content_library = std::make_shared<Content_library>();
        content_library->materials.make("Default");
        auto scene_root = std::make_shared<Scene_root>(
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

        g_editor_scenes->register_scene_root(scene_root);
    }
#endif
    const auto& scene_roots = g_editor_scenes->get_scene_roots();
    for (const auto& scene_root : scene_roots) {
        const auto& scene = scene_root->get_shared_scene();
        imgui_item_node(scene);
    }

    for (const auto& fun : m_operations) {
        fun();
    }
    m_operations.clear();

    if (m_operation) {
        g_operation_stack->push(m_operation);
        m_operation.reset();
    }

    if (g_selection_tool) {
        g_selection_tool->range_selection().end(m_expand_attachments);
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_toggled_open = false;
    }

    g_editor_scenes->sanity_check();
#endif
}

}
