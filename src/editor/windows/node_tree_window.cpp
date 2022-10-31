// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "windows/node_tree_window.hpp"
#include "graphics/icon_set.hpp"
#include "editor_log.hpp"
#include "editor_scenes.hpp"

#include "operations/compound_operation.hpp"
#include "operations/node_operation.hpp"
#include "operations/operation_stack.hpp"
#include "scene/node_physics.hpp"
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
        {
            for (const auto& node : scene.root_node->children())
            {
                recursive_add_to_selection(node);
            }
        }
    }
}

template <typename T>
[[nodiscard]] auto is_in(
    const T&              item,
    const std::vector<T>& items
) -> bool
{
    return std::find(
        items.begin(),
        items.end(),
        item
    ) != items.end();
}

auto Node_tree_window::get_node_by_id(
    const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type id
) -> std::shared_ptr<erhe::scene::Node>
{
    const auto i = m_tree_nodes_last_frame.find(id);
    if (i == m_tree_nodes_last_frame.end())
    {
        //// log_tools->warn("node for id = {} not found", id);
        return {};
    }
    return i->second;
}

void Node_tree_window::move_selection_before(
    const std::shared_ptr<erhe::scene::Node>&                  target_node,
    const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type payload_id
)
{
    SPDLOG_LOGGER_TRACE(
        log_node_properties,
        "move_selection_before()"
    );

    static_cast<void>(target_node);
    static_cast<void>(payload_id);

    //// log_tools->warn(
    ////     "TODO: move_selection_before(target_node = {}, payload_id = {})",
    ////     target_node->name(),
    ////     payload_id
    //// );
}

void Node_tree_window::move_selection_after(
    const std::shared_ptr<erhe::scene::Node>&                  target_node,
    const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type payload_id
)
{
    SPDLOG_LOGGER_TRACE(
        log_node_properties,
        "move_selection_after()"
    );

    static_cast<void>(target_node);
    static_cast<void>(payload_id);

    //// log_tools->warn(
    ////     "TODO: move_selection_after(target_node = {}, payload_id = {})",
    ////     target_node->name(),
    ////     payload_id
    //// );
}

namespace {

[[nodiscard]] auto get_ancestor_in(
    const std::shared_ptr<erhe::scene::Node>&              node,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& selection
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

void Node_tree_window::try_add_to_attach(
    Compound_operation::Parameters&           compound_parameters,
    const std::shared_ptr<erhe::scene::Node>& target_node,
    const std::shared_ptr<erhe::scene::Node>& node
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

    compound_parameters.operations.push_back(
        std::make_shared<Node_attach_operation>(
            target_node,
            node
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
    auto drag_node = get_node_by_id(payload_id);

    if (is_in(drag_node, selection))
    {
        for (const auto& node : selection)
        {
            try_add_to_attach(compound_parameters, target_node, node);
        }
    }
    else if (compound_parameters.operations.empty())
    {
        try_add_to_attach(compound_parameters, target_node, drag_node);
    }

    if (!compound_parameters.operations.empty())
    {
        m_drag_and_drop_operation = std::make_shared<Compound_operation>(std::move(compound_parameters));
    }
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)
void Node_tree_window::drag_and_drop_source(
    const std::shared_ptr<erhe::scene::Node>& node
)
{
    ERHE_PROFILE_FUNCTION

    const auto node_id = node->get_id();
    m_tree_nodes.emplace(node_id, node);
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        ImGui::SetDragDropPayload("Node", &node_id, sizeof(node_id));

        const auto& selection = m_selection_tool->selection();
        if (is_in(node, selection))
        {
            for (const auto& selection_node : selection)
            {
                node_items(selection_node, false);
            }
        }
        else
        {
            node_items(node, false);
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
    const std::shared_ptr<erhe::scene::Node>& node
) -> bool
{
    ERHE_PROFILE_FUNCTION

    const auto  rect_min = ImGui::GetItemRectMin();
    const auto  rect_max = ImGui::GetItemRectMax();
    const float height   = rect_max.y - rect_min.y;
    const float y0       = rect_min.y;
    const float y1       = rect_min.y + 0.3f * height;
    const float y2       = rect_max.y - 0.3f * height;
    const float y3       = rect_max.y;
    const float x0       = rect_min.x;
    const float x1       = rect_max.x;

    const auto        node_id           = node->get_id();
    const std::string label_move_before = fmt::format("node dnd move before {}", node_id);
    const std::string label_attach_to   = fmt::format("node dnd attach to {}",   node_id);
    const std::string label_move_after  = fmt::format("node dnd move after {}",  node_id);
    const ImGuiID     imgui_id_before   = ImGui::GetID(label_move_before.c_str());
    const ImGuiID     imgui_id_attach   = ImGui::GetID(label_attach_to.c_str());
    const ImGuiID     imgui_id_after    = ImGui::GetID(label_move_after.c_str());

    // Move selection before drop target
    const ImRect top_rect{rect_min, ImVec2{rect_max.x, y1}};
    if (ImGui::BeginDragDropTargetCustom(top_rect, imgui_id_before))
    {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Node", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
        drag_and_drop_gradient_preview(x0, x1, y0, y2, ImGui::GetColorU32(ImGuiCol_DragDropTarget), 0);
        if (payload != nullptr)
        {
            IM_ASSERT(payload->DataSize == sizeof(erhe::toolkit::Unique_id<erhe::scene::Node>::id_type));
            const auto payload_id = *(const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type*)payload->Data;
            move_selection_before(node, payload_id);
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
            move_selection_after(node, payload_id);
        }
        ImGui::EndDragDropTarget();
        return true;
    }
    return false;
}

void Node_tree_window::imgui_node_update(
    const std::shared_ptr<erhe::scene::Node>& node
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

    drag_and_drop_source(node);

    const bool shift_down     = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
    const bool ctrl_down      = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
    const bool mouse_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    const bool focused        = ImGui::IsItemFocused();
    const bool hovered        = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

    if (hovered && mouse_released)
    {
        if (ctrl_down)
        {
            range_selection.reset();
            if (node->is_selected())
            {
                SPDLOG_LOGGER_TRACE(
                    log_node_properties,
                    "click with ctrl down on selected node {} - deselecting",
                    node->name()
                );
                m_selection_tool->remove_from_selection(node);
            }
            else
            {
                SPDLOG_LOGGER_TRACE(
                    log_node_properties,
                    "click with ctrl down on node {} - selecting",
                    node->name()
                );
                m_selection_tool->add_to_selection(node);
                range_selection.set_terminator(node);
            }
        }
        else if (shift_down)
        {
            SPDLOG_LOGGER_TRACE(
                log_node_properties,
                "click with shift down on node {} - range select",
                node->name()
            );
            range_selection.set_terminator(node);
        }
        else
        {
            const bool was_selected = node->is_selected();
            range_selection.reset();
            m_selection_tool->clear_selection();
            SPDLOG_LOGGER_TRACE(
                log_node_properties,
                "mouse button release without modifier keys on node {} - {} selecting it",
                node->name(),
                was_selected ? "de" : ""
            );
            if (!was_selected)
            {
                m_selection_tool->add_to_selection(node);
                range_selection.set_terminator(node);
            }
        }
    }

    if (focused)
    {
        if (node != m_last_focus_node.lock())
        {
            if (shift_down)
            {
                SPDLOG_LOGGER_TRACE(
                    log_node_properties,
                    "key with shift down on node {} - range select",
                    node->name()
                );
                range_selection.set_terminator(node);
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
        m_last_focus_node = node;
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

auto Node_tree_window::node_items(
    const std::shared_ptr<erhe::scene::Node>& node,
    const bool                                update
) -> bool
{
    ERHE_PROFILE_FUNCTION

    {
        ERHE_PROFILE_SCOPE("icon");

        std::optional<glm::vec2> icon;

        if (is_empty(node))
        {
            icon = m_icon_set->icons.node;
        }
        else if (is_mesh(node))
        {
            icon = m_icon_set->icons.mesh;
        }
        else if (is_light(node))
        {
            switch (as_light(node)->type)
            {
                //using enum erhe::scene::Light_type;
                case erhe::scene::Light_type::spot:        icon = m_icon_set->icons.spot_light; break;
                case erhe::scene::Light_type::directional: icon = m_icon_set->icons.directional_light; break;
                case erhe::scene::Light_type::point:       icon = m_icon_set->icons.point_light; break;
                default: return {};
            }
        }
        else if (is_camera(node))
        {
            icon = m_icon_set->icons.camera;
        }
        //else if (is_physics(node))
        //{
        //    //log_tools->info("P {}", node->name());
        //    m_icon_set->icon(*node);
        //}
        else
        {
            icon = m_icon_set->icons.node;
        }

        if (icon.has_value())
        {
            const auto& icon_rasterization = get_scale_value() < 1.5f
                ? m_icon_set->get_small_rasterization()
                : m_icon_set->get_large_rasterization();
            icon_rasterization.icon(icon.value());
        }
    }

    const auto child_count = node->child_count();
    const bool is_leaf = (child_count == 0);

    const ImGuiTreeNodeFlags parent_flags{ImGuiTreeNodeFlags_OpenOnArrow      | ImGuiTreeNodeFlags_OpenOnDoubleClick};
    const ImGuiTreeNodeFlags leaf_flags  {ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Leaf             };

    const ImGuiTreeNodeFlags node_flags{0
        | ImGuiTreeNodeFlags_SpanAvailWidth
        | (is_leaf
            ? leaf_flags
            : parent_flags)
        | (update && node->is_selected()
            ? ImGuiTreeNodeFlags_Selected
            : ImGuiTreeNodeFlags_None)};

    bool node_open;
    {
        ERHE_PROFILE_SCOPE("TreeNodeEx");
        node_open = ImGui::TreeNodeEx(node->label().c_str(), node_flags);
        // const std::string label = fmt::format(
        //     "{}{}",
        //     node->is_selected() ? "S." : "",
        //     node->label().c_str()
        // );
        //node_open = ImGui::TreeNodeEx(label.c_str(), node_flags);
    }

    if (update)
    {
        ERHE_PROFILE_SCOPE("update");
        const bool consumed_by_drag_and_drop = drag_and_drop_target(node);;
        if (!consumed_by_drag_and_drop)
        {
            imgui_node_update(node);
        }
    }

    return node_open;
}

void Node_tree_window::imgui_tree_node(
    const std::shared_ptr<erhe::scene::Node>& node
)
{
    ERHE_PROFILE_FUNCTION

    if (m_selection_tool)
    {
        m_selection_tool->range_selection().entry(node);
    }

    const auto node_open = node_items(node, true);

    if (node_open)
    {
        for (const auto& child_node : node->children())
        {
            imgui_tree_node(child_node);
        }

        if (node->child_count() > 0)
        {
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

void Node_tree_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    m_tree_nodes_last_frame = m_tree_nodes;

    {
        ERHE_PROFILE_SCOPE("map clear");
        m_tree_nodes.clear();
    }

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
                imgui_tree_node(node);
            }
        }
    }

    if (m_drag_and_drop_operation)
    {
        get<Operation_stack>()->push(m_drag_and_drop_operation);
        m_drag_and_drop_operation.reset();
    }

    if (m_selection_tool)
    {
        m_selection_tool->range_selection().end();
    }
#endif
}

}
