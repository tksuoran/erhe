// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "windows/node_tree_window.hpp"
#include "graphics/icon_set.hpp"
#include "editor_log.hpp"
#include "editor_scenes.hpp"

#include "operations/compound_operation.hpp"
#include "operations/insert_operation.hpp"
#include "operations/node_operation.hpp"
#include "operations/operation_stack.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_commands.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/windows/log_window.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/toolkit/profile.hpp"

#include <gsl/gsl>

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#   include <imgui_internal.h>
#endif

namespace editor
{

using Light_type = erhe::scene::Light_type;

Node_tree_window::Node_tree_window()
    : erhe::components::Component    {c_type_name}
    , erhe::application::Imgui_window{c_title}
    , m_filter{
        .require_all_bits_set           = erhe::scene::Scene_item_flags::content,
        .require_at_least_one_bit_set   = 0,
        .require_all_bits_clear         = erhe::scene::Scene_item_flags::tool | erhe::scene::Scene_item_flags::brush,
        .require_at_least_one_bit_clear = 0
    }
{
}

Node_tree_window::~Node_tree_window() noexcept
{
}

void Node_tree_window::declare_required_components()
{
    m_selection_tool = require<Selection_tool>();
    require<erhe::application::Imgui_windows>();
}

void Node_tree_window::initialize_component()
{
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Node_tree_window::post_initialize()
{
    m_editor_scenes = get<Editor_scenes>();
    m_icon_set      = get<Icon_set     >();
}

void Node_tree_window::clear_selection()
{
    SPDLOG_LOGGER_TRACE(log_node_properties, "clear_selection()");

    m_selection_tool->clear_selection();
}

void Node_tree_window::recursive_add_to_selection(
    const std::shared_ptr<erhe::scene::Node>& node
)
{
    m_selection_tool->add_to_selection(node);
    for (const auto& child_node : node->children())
    {
        recursive_add_to_selection(child_node);
    }
}

void Node_tree_window::select_all()
{
    SPDLOG_LOGGER_TRACE(log_node_properties, "select_all()");

    m_selection_tool->clear_selection();
    const auto& scene_roots = m_editor_scenes->get_scene_roots();
    for (const auto& scene_root : scene_roots)
    {
        const auto& scene = scene_root->scene();
        for (const auto& node : scene.root_node->children())
        {
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
    const erhe::toolkit::Unique_id<erhe::scene::Scene_item>::id_type id
) const -> std::shared_ptr<erhe::scene::Scene_item>
{
    const auto i = m_tree_items_last_frame.find(id);
    if (i == m_tree_items_last_frame.end())
    {
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
        target_node ? target_node->name() : "(empty)",
        (placement == Placement::Before_anchor)
            ? "Before_anchor"
            : "After_anchor"
    );

    const auto parent = target_node->parent().lock();

    Compound_operation::Parameters compound_parameters;
    const auto& selection = m_selection_tool->selection();
    const auto& drag_item = get_item_by_id(payload_id);

    std::shared_ptr<erhe::scene::Node> anchor = target_node;
    if (is_in(drag_item, selection))
    {
        // Dragging node which is part of the selection.
        // In this case we apply reposition to whole selection.
        if (placement == Placement::Before_anchor)
        {
            for (const auto& item : selection)
            {
                const auto& node = as_node(item);
                if (node)
                {
                    reposition(compound_parameters, anchor, node, placement, Selection_usage::Selection_used);
                }
            }
        }
        else // if (placement == Placement::After_anchor)
        {
            for (auto i = selection.rbegin(), end = selection.rend(); i < end; ++i)
            {
                const auto& item = *i;
                const auto& node = as_node(item);
                if (node)
                {
                    reposition(compound_parameters, anchor, node, placement, Selection_usage::Selection_used);
                }
            }
        }
    }
    else if (compound_parameters.operations.empty())
    {
        // Dragging a single node which is not part of the selection.
        // In this case we ignore selection and apply operation only to dragged node.
        const auto& drag_node = as_node(drag_item);
        if (drag_node)
        {
            reposition(compound_parameters, anchor, drag_node, placement, Selection_usage::Selection_ignored);
        }
    }

    if (!compound_parameters.operations.empty())
    {
        m_operation = std::make_shared<Compound_operation>(std::move(compound_parameters));
    }
}

namespace {

[[nodiscard]] auto get_ancestor_in(
    const std::shared_ptr<erhe::scene::Node>&                    node,
    const std::vector<std::shared_ptr<erhe::scene::Scene_item>>& selection
) -> std::shared_ptr<erhe::scene::Node>
{
    const auto& node_parent = node->parent().lock();
    if (node_parent)
    {
        if (is_in(node_parent, selection))
        {
            return node_parent;
        }
        return get_ancestor_in(node_parent, selection);
    }
    else
    {
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
        anchor_node ? anchor_node->name() : "(empty)",
        node ? node->name() : "(empty)",
        (placement == Placement::Before_anchor)
            ? "Before_anchor"
            : "After_anchor"
    );

    if (!node)
    {
        SPDLOG_LOGGER_WARN(log_node_properties, "Bad empty node");
        return;
    }

    if (!anchor_node)
    {
        SPDLOG_LOGGER_WARN(log_node_properties, "Bad empty anchor node");
        return;
    }

    // Nodes cannot be attached to themselves
    if (node == anchor_node)
    {
        return;
    }

    // Ancestors cannot be attached to descendants
    if (anchor_node->is_ancestor(node.get()))
    {
        SPDLOG_LOGGER_WARN(log_node_properties, "Ancestors cannot be attached to descendants");
        return;
    }

    if (selection_usage == Selection_usage::Selection_used)
    {
        const auto& selection = m_selection_tool->selection();

        // Ignore nodes if their ancestors is in selection
        const auto ancestor_in_selection = get_ancestor_in(node, selection);
        if (ancestor_in_selection)
        {
            SPDLOG_LOGGER_TRACE(
                log_node_properties,
                "Ignoring node {} because ancestor {} is in selection",
                node->name(),
                ancestor_in_selection->name()
            );
            return;
        }
    }

    if (anchor_node->parent().lock() != node->parent().lock())
    {
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
        target_node ? target_node->name() : "(empty)",
        node        ? node->name() : "(empty)"
    );

    if (!node)
    {
        SPDLOG_LOGGER_WARN(log_node_properties, "Bad empty node");
        return;
    }

    if (!target_node)
    {
        SPDLOG_LOGGER_WARN(log_node_properties, "Bad empty target node");
        return;
    }

    // Nodes cannot be attached to themselves
    if (node == target_node)
    {
        SPDLOG_LOGGER_WARN(log_node_properties, "Nodes cannot be attached to themselves");
        return;
    }

    // Ancestors cannot be attached to descendants
    if (target_node->is_ancestor(node.get()))
    {
        SPDLOG_LOGGER_WARN(log_node_properties, "Ancestors cannot be attached to descendants");
        return;
    }

    if (selection_usage == Selection_usage::Selection_used)
    {
        const auto& selection = m_selection_tool->selection();

        // Ignore nodes if their ancestors is in selection
        const auto ancestor_in_selection = get_ancestor_in(node, selection);
        if (ancestor_in_selection)
        {
            SPDLOG_LOGGER_TRACE(
                log_node_properties,
                "Ignoring node {} because ancestor {} is in selection",
                node->name(),
                ancestor_in_selection->name()
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
    ////     target_node->name(),
    ////     payload_id
    //// );
    Compound_operation::Parameters compound_parameters;
    const auto& selection = m_selection_tool->selection();
    auto drag_item = get_item_by_id(payload_id);

    if (is_in(drag_item, selection))
    {
        for (const auto& item : selection)
        {
            const auto& node = as_node(item);
            if (node)
            {
                try_add_to_attach(compound_parameters, target_node, node, Selection_usage::Selection_used);
            }
        }
    }
    else if (compound_parameters.operations.empty())
    {
        const auto& drag_node = as_node(drag_item);
        if (drag_node)
        {
            try_add_to_attach(compound_parameters, target_node, drag_node, Selection_usage::Selection_ignored);
        }
    }

    if (!compound_parameters.operations.empty())
    {
        m_operation = std::make_shared<Compound_operation>(std::move(compound_parameters));
    }
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)
void Node_tree_window::drag_and_drop_source(
    const std::shared_ptr<erhe::scene::Scene_item>& item
)
{
    ERHE_PROFILE_FUNCTION

    const auto id = item->get_id();
    m_tree_items.emplace(id, item);
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        ImGui::SetDragDropPayload("Scene_item", &id, sizeof(id));

        const auto& selection = m_selection_tool->selection();
        if (is_in(item, selection))
        {
            for (const auto& selection_item : selection)
            {
                item_icon_and_text(selection_item, false);
            }
        }
        else
        {
            item_icon_and_text(item, false);
        }
        ImGui::EndDragDropSource();
    }
}

namespace {

static inline ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs)
{
    return ImVec2{lhs.x + rhs.x, lhs.y + rhs.y};
}

static inline ImVec2 operator-(const ImVec2& lhs, const ImVec2& rhs)
{
    return ImVec2{lhs.x - rhs.x, lhs.y - rhs.y};
}

void drag_and_drop_rectangle_preview(const ImRect rect)
{
    const auto* g       = ImGui::GetCurrentContext();
    const auto* window  = g->CurrentWindow;
    const auto& payload = g->DragDropPayload;

    if (payload.Preview)
    {
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

    if (payload.Preview)
    {
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
    const std::shared_ptr<erhe::scene::Scene_item>& item
) -> bool
{
    ERHE_PROFILE_FUNCTION

    if (!item)
    {
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
    const std::string label_move_before = fmt::format("node dnd move before {}: {} {}", id, item->type_name(), item->name());
    const std::string label_attach_to   = fmt::format("node dnd attach to {}: {} {}",   id, item->type_name(), item->name());
    const std::string label_move_after  = fmt::format("node dnd move after {}: {} {}",  id, item->type_name(), item->name());
    const ImGuiID     imgui_id_before   = ImGui::GetID(label_move_before.c_str());
    const ImGuiID     imgui_id_attach   = ImGui::GetID(label_attach_to.c_str());
    const ImGuiID     imgui_id_after    = ImGui::GetID(label_move_after.c_str());

    // Move selection before drop target
    const auto& node = as_node(item);
    if (node)
    {
        const ImRect top_rect{rect_min, ImVec2{rect_max.x, y1}};
        if (ImGui::BeginDragDropTargetCustom(top_rect, imgui_id_before))
        {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Node", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
            drag_and_drop_gradient_preview(x0, x1, y0, y2, ImGui::GetColorU32(ImGuiCol_DragDropTarget), 0);
            if (payload != nullptr)
            {
                IM_ASSERT(payload->DataSize == sizeof(erhe::toolkit::Unique_id<erhe::scene::Node>::id_type));
                const auto payload_id = *(const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type*)payload->Data;
                move_selection(node, payload_id, Placement::Before_anchor);
            }
            ImGui::EndDragDropTarget();
            return true;
        }

        // Attach selection to target
        const ImRect middle_rect{ImVec2{rect_min.x, y1}, ImVec2{rect_max.x, y2}};
        if (ImGui::BeginDragDropTargetCustom(middle_rect, imgui_id_attach))
        {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Node", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
            drag_and_drop_rectangle_preview(middle_rect);
            if (payload != nullptr)
            {
                IM_ASSERT(payload->DataSize == sizeof(erhe::toolkit::Unique_id<erhe::scene::Node>::id_type));
                const auto payload_id = *(const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type*)payload->Data;
                attach_selection_to(node, payload_id);
            }
            ImGui::EndDragDropTarget();
            return true;
        }

        // Move selection after drop target
        const ImRect bottom_rect{ImVec2{rect_min.x, y2}, rect_max};
        if (ImGui::BeginDragDropTargetCustom(bottom_rect, imgui_id_after))
        {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Node", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
            drag_and_drop_gradient_preview(x0, x1, y1, y3, 0, ImGui::GetColorU32(ImGuiCol_DragDropTarget));
            if (payload != nullptr)
            {
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

void Node_tree_window::imgui_item_update(
    const std::shared_ptr<erhe::scene::Scene_item>& item
)
{
    ERHE_PROFILE_FUNCTION

    if (!m_selection_tool)
    {
        SPDLOG_LOGGER_TRACE(
            log_node_properties,
            "imgui_node_update() - no selection tool"
        );
        return;
    }

    auto& range_selection = m_selection_tool->range_selection();

    drag_and_drop_source(item);

    const bool shift_down     = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
    const bool ctrl_down      = ImGui::IsKeyDown(ImGuiKey_LeftCtrl ) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
    const bool mouse_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    const bool focused        = ImGui::IsItemFocused();
    const bool hovered        = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

    if (hovered && mouse_released)
    {
        if (ctrl_down)
        {
            range_selection.reset();
            if (item->is_selected())
            {
                SPDLOG_LOGGER_TRACE(
                    log_node_properties,
                    "click with ctrl down on selected {} {} - deselecting",
                    item->type_name(),
                    item->name()
                );
                m_selection_tool->remove_from_selection(item);
            }
            else
            {
                SPDLOG_LOGGER_TRACE(
                    log_node_properties,
                    "click with ctrl down on {} {} - selecting",
                    item->type_name(),
                    item->name()
                );
                m_selection_tool->add_to_selection(item);
                range_selection.set_terminator(item);
            }
        }
        else if (shift_down)
        {
            SPDLOG_LOGGER_TRACE(
                log_node_properties,
                "click with shift down on {} {} - range select",
                item->type_name(),
                item->name()
            );
            range_selection.set_terminator(item);
        }
        else
        {
            const bool was_selected = item->is_selected();
            range_selection.reset();
            m_selection_tool->clear_selection();
            SPDLOG_LOGGER_TRACE(
                log_node_properties,
                "mouse button release without modifier keys on {} {} - {} selecting it",
                item->type_name(),
                item->name(),
                was_selected ? "de" : ""
            );
            if (!was_selected)
            {
                m_selection_tool->add_to_selection(item);
                range_selection.set_terminator(item);
            }
        }
    }

    if (focused)
    {
        if (item != m_last_focus_item.lock())
        {
            if (shift_down)
            {
                SPDLOG_LOGGER_TRACE(
                    log_node_properties,
                    "key with shift down on {} {} - range select",
                    item->type_name(),
                    item->name()
                );
                range_selection.set_terminator(item);
            }
            // else
            // {
            //     SPDLOG_LOGGER_TRACE(
            //         log_node_properties,
            //         "key without modifier key on node {} - clearing range select and select",
            //         node->name()
            //     );
            //     range_selection.reset();
            //     range_selection.set_terminator(node);
            // }
        }
        m_last_focus_item = item;
    }

    // CTRL-A to select all
    if (ctrl_down)
    {
        const bool a_pressed = ImGui::IsKeyPressed(ImGuiKey_A);
        if (a_pressed)
        {
            SPDLOG_LOGGER_TRACE(
                log_node_properties,
                "ctrl a pressed - select all"
            );
            select_all();
        }
    }
}

void Node_tree_window::item_popup_menu(
    const std::shared_ptr<erhe::scene::Scene_item>& item
)
{
    if (
        ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
        !m_popup_item
    )
    {
        m_popup_item = item;
        m_popup_id_string = fmt::format("{}##Node{}-popup-menu", item->name(), item->get_id());
        m_popup_id = ImGui::GetID(m_popup_id_string.c_str());
        ImGui::OpenPopupEx(
            m_popup_id,
            ImGuiPopupFlags_MouseButtonRight
        );
    }
    if ((m_popup_item != item) || m_popup_id_string.empty())
    {
        return;
    }
    const bool begin_popup_context_item = ImGui::BeginPopupEx(
        m_popup_id,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings
    );
    if (begin_popup_context_item)
    {
        const auto& node       = as_node(item);
        Scene_root* scene_root = reinterpret_cast<Scene_root*>(item->get_scene_host());
        if (node && (scene_root != nullptr))
        {
            auto parent_node = node->parent().lock();

            bool close{false};
            if (ImGui::Button("Create New Empty Node"))
            {
                m_operations.push_back(
                    [this, scene_root, parent_node]
                    ()
                    {
                        m_scene_commands->create_new_empty_node(parent_node.get());
                    }
                );
                close = true;
            }
            if (ImGui::Button("Create New Camera"))
            {
                m_operations.push_back(
                    [this, scene_root, parent_node]
                    ()
                    {
                        m_scene_commands->create_new_camera(parent_node.get());
                    }
                );
                close = true;
            }
            if (ImGui::Button("Create New Light"))
            {
                m_operations.push_back(
                    [this, scene_root, parent_node]
                    ()
                    {
                        m_scene_commands->create_new_light(parent_node.get());
                    }
                );
                close = true;
            }
            ImGui::EndPopup();
            if (close) {
                m_popup_item.reset();
                m_popup_id_string.clear();
                m_popup_id = 0;
            }
        }
    }
    else
    {
        m_popup_item.reset();
        m_popup_id_string.clear();
        m_popup_id = 0;
    }
}

auto Node_tree_window::item_icon_and_text(
    const std::shared_ptr<erhe::scene::Scene_item>& item,
    const bool                                      update
) -> Node_state
{
    ERHE_PROFILE_FUNCTION

    std::optional<glm::vec2> icon;

    glm::vec4 color = item->get_wireframe_color();
    const auto& node = as_node(item);
    if (node)
    {
        icon = m_icon_set->icons.node;
    }
    const auto& mesh = as_mesh(item);
    if (mesh)
    {
        icon = m_icon_set->icons.mesh;
    }
    const auto& light = as_light(item);
    if (light)
    {
        color = glm::vec4{light->color, 1.0f};
        switch (light->type)
        {
            //using enum erhe::scene::Light_type;
            case erhe::scene::Light_type::spot:        icon = m_icon_set->icons.spot_light; break;
            case erhe::scene::Light_type::directional: icon = m_icon_set->icons.directional_light; break;
            case erhe::scene::Light_type::point:       icon = m_icon_set->icons.point_light; break;
            default: break;
        }
    }
    const auto& camera = as_camera(node);
    if (camera)
    {
        icon = m_icon_set->icons.camera;
    }

    if (icon.has_value())
    {
        const auto& icon_rasterization = get_scale_value() < 1.5f
            ? m_icon_set->get_small_rasterization()
            : m_icon_set->get_large_rasterization();
        icon_rasterization.icon(icon.value(), color);
    }

    const bool is_leaf          = !node || (node->child_count(m_filter) == 0);
    const bool only_attachments = node && (node->child_count(m_filter) == 0) && (node->attachment_count(m_filter) > 0);

    const ImGuiTreeNodeFlags parent_flags{ImGuiTreeNodeFlags_OpenOnArrow      | ImGuiTreeNodeFlags_OpenOnDoubleClick};
    const ImGuiTreeNodeFlags leaf_flags  {ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Leaf             };

    const ImGuiTreeNodeFlags item_flags{0
        | ImGuiTreeNodeFlags_SpanAvailWidth
        | (is_leaf ? leaf_flags : parent_flags)
        | (only_attachments ? ImGuiTreeNodeFlags_DefaultOpen : 0)
        | (update && item->is_selected()
            ? ImGuiTreeNodeFlags_Selected
            : ImGuiTreeNodeFlags_None)};

    bool item_node_open;
    {
        ERHE_PROFILE_SCOPE("TreeNodeEx");
        item_node_open = ImGui::TreeNodeEx(
            item->label().c_str(),
            item_flags
        );
        // const std::string label = fmt::format(
        //     "{}{}",
        //     node->is_selected() ? "S." : "",
        //     node->label().c_str()
        // );
        //node_open = ImGui::TreeNodeEx(label.c_str(), node_flags);
    }

    //int mouse_button = (popup_flags & ImGuiPopupFlags_MouseButtonMask_);
    item_popup_menu(item);

    if (update)
    {
        ERHE_PROFILE_SCOPE("update");
        const bool consumed_by_drag_and_drop = drag_and_drop_target(item);
        if (!consumed_by_drag_and_drop)
        {
            imgui_item_update(item);
        }
    }

    return Node_state
    {
        .is_open   = item_node_open,
        .is_parent = !is_leaf
    };
}

void Node_tree_window::imgui_item_node(
    const std::shared_ptr<erhe::scene::Scene_item>& item
)
{
    ERHE_PROFILE_FUNCTION

    if (!m_filter(item->get_flag_bits()))
    {
        log_node_properties->info("filtered {}", item->describe());
        return;
    }

    if (m_selection_tool)
    {
        m_selection_tool->range_selection().entry(item);
    }

    const auto node_state = item_icon_and_text(item, true);
    if (node_state.is_open)
    {
        const auto& node = as_node(item);
        if (node)
        {
            //for (const auto& child_node : node->children())
            //{
            //    imgui_item_node(child_node);
            //}
            ImGui::Indent(20.0f); // TODO
            for (const auto& node_attachment : node->attachments())
            {
                imgui_item_node(node_attachment);
            }
            ImGui::Unindent(20.0f);
        }
    }
    if (node_state.is_parent)
    {
        ImGui::TreePop();
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

void Node_tree_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    m_tree_items_last_frame = m_tree_items;
    m_tree_items.clear();

    if (m_selection_tool)
    {
        m_selection_tool->range_selection().begin();
    }

    // TODO Handle cross scene drags and drops
    const auto& scene_roots = m_editor_scenes->get_scene_roots();
    for (const auto& scene_root : scene_roots)
    {
        const auto& scene = scene_root->scene();
        {
            ERHE_PROFILE_SCOPE("nodes");
            for (const auto& node : scene.root_node->children())
            {
                imgui_item_node(node);
            }
        }
    }

    for (const auto& fun : m_operations)
    {
        fun();
    }
    m_operations.clear();

    if (m_operation)
    {
        get<Operation_stack>()->push(m_operation);
        m_operation.reset();
    }

    if (m_selection_tool)
    {
        m_selection_tool->range_selection().end();
    }

    get<Editor_scenes>()->sanity_check();
#endif
}

}
