#include "windows/node_tree_window.hpp"
#include "editor_imgui_windows.hpp"
#include "graphics/icon_set.hpp"
#include "log.hpp"

#include "operations/compound_operation.hpp"
#include "operations/node_operation.hpp"
#include "operations/operation_stack.hpp"

#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"

#include "tools/selection_tool.hpp"

#include "windows/log_window.hpp"

#include "erhe/graphics/texture.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"

#include <gsl/gsl>
#include <imgui.h>
#include <imgui_internal.h>

namespace editor
{

using Light_type = erhe::scene::Light_type;

Node_tree_window::Node_tree_window()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_title}
{
}

Node_tree_window::~Node_tree_window() = default;

void Node_tree_window::connect()
{
    m_scene_root     = get    <Scene_root    >();
    m_selection_tool = require<Selection_tool>();
    m_icon_set       = get    <Icon_set      >();
    require<Editor_imgui_windows>();
    Expects(m_scene_root != nullptr);
    Expects(m_icon_set   != nullptr);
}

void Node_tree_window::initialize_component()
{
    get<Editor_imgui_windows>()->register_imgui_window(this);
}

void Node_tree_window::clear_selection()
{
    m_selection_tool->clear_selection();
}

void Node_tree_window::select_all()
{
    log_tools.warn("TODO: select_all\n");
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
        log_tools.warn("node for id = {} not found\n", id);
        return {};
    }
    return i->second;
}

void Node_tree_window::move_selection_before(
    const std::shared_ptr<erhe::scene::Node>&                  target_node,
    const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type payload_id
)
{
    log_tools.warn(
        "TODO: move_selection_before(target_node = {}, payload_id = {})\n",
        target_node->name(),
        payload_id
    );
}

void Node_tree_window::move_selection_after(
    const std::shared_ptr<erhe::scene::Node>&                  target_node,
    const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type payload_id
)
{
    log_tools.warn(
        "TODO: move_selection_after(target_node = {}, payload_id = {})\n",
        target_node->name(),
        payload_id
    );
}

void Node_tree_window::try_add_to_attach(
    Compound_operation::Parameters&           compound_parameters,
    const std::shared_ptr<erhe::scene::Node>& target_node,
    const std::shared_ptr<erhe::scene::Node>& node
)
{
    // Nodes cannot be attached to themselves
    // Ancestors cannot be attached to descendants
    if (
        (node == target_node) ||
        (target_node->is_ancestor(node.get()))
    )
    {
        return;
    }

    // Ignore nodes if their parent is also in selection
    const auto& node_parent = node->parent().lock();
    if (node_parent)
    {
        const auto& selection = m_selection_tool->selection();
        if (is_in(node_parent, selection))
        {
            return;
        }
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
    log_tools.trace(
        "attach_selection_to(target_node = {}, payload_id = {})\n",
        target_node->name(),
        payload_id
    );
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

void Node_tree_window::drag_and_drop_source(
    const std::shared_ptr<erhe::scene::Node>& node
)
{
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

template<typename T>
void apply_permutation_in_place(
    std::vector<T>&         v,
    const std::vector<int>& p
)
{
   for (size_t i = 0 ; i < v.size(); ++i)
   {
        const size_t curr = i;
        const size_t next = p[curr];
        while (next != i)
        {
            swap(v[curr], v[next]);
            p[curr] = curr;
            curr = next;
            next = p[next];
        }
        p[curr] = curr;
   }
}

}

auto Node_tree_window::drag_and_drop_target(
    const std::shared_ptr<erhe::scene::Node>& node
) -> bool
{
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

void Node_tree_window::imgui_node_update(const std::shared_ptr<erhe::scene::Node>& node)
{
    if (!m_selection_tool)
    {
        return;
    }

    auto& range_selection = m_selection_tool->range_selection();

    drag_and_drop_source(node);

    const bool shift_down     = ImGui::IsKeyDown(ImGuiKey_ModShift);
    const bool ctrl_down      = ImGui::IsKeyDown(ImGuiKey_ModCtrl);
    const bool up_pressed     = ImGui::IsKeyPressed(ImGuiKey_UpArrow);
    const bool down_pressed   = ImGui::IsKeyPressed(ImGuiKey_DownArrow);
    const bool home_pressed   = ImGui::IsKeyPressed(ImGuiKey_Home);
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
                log_node_properties.trace("click with ctrl down on selected node {} - deselecting\n", node->name());
                m_selection_tool->remove_from_selection(node);
            }
            else
            {
                log_node_properties.trace("click with ctrl down on node {} - selecting\n", node->name());
                range_selection.set_terminator(node);
            }
        }
        else if (shift_down)
        {
            log_node_properties.trace("click with shift down on node {} - range select\n", node->name());
            range_selection.set_terminator(node);
        }
        else
        {
            log_node_properties.trace("click without modifier keys on node {} - selecting it\n", node->name());
            const bool was_selected = node->is_selected();
            range_selection.reset();
            m_selection_tool->clear_selection();
            if (!was_selected)
            {
                m_selection_tool->add_to_selection(node);
                range_selection.set_terminator(node);
            }
        }
    }

    if (focused)
    {
        if (up_pressed || down_pressed || home_pressed || down_pressed)
        {
            if (shift_down)
            {
                log_node_properties.trace("key with shift down on node {} - range select\n", node->name());
                range_selection.set_terminator(node);
            }
            else
            {
                log_node_properties.trace("key without modifier key on node {} - clearing range select and select\n", node->name());
                range_selection.reset();
                range_selection.set_terminator(node);
            }
        }
    }

    // CTRL-A to select all
    if (hovered || focused)
    {
        const bool a_pressed = ImGui::IsKeyPressed(ImGuiKey_A);
        if (ctrl_down && a_pressed)
        {
            log_node_properties.trace("ctrl a pressed - select all\n");
            select_all();
        }
    }
}

auto Node_tree_window::node_items(
    const std::shared_ptr<erhe::scene::Node>& node,
    const bool                                update
) -> bool
{
    if (is_empty(node))
    {
        m_icon_set->icon(*node);
    }
    else if (is_mesh(node))
    {
        m_icon_set->icon(*as_mesh(node));
    }
    else if (is_light(node)) // note light is before camera - light is also camera
    {
        m_icon_set->icon(*as_light(node));
    }
    else if (is_camera(node))
    {
        m_icon_set->icon(*as_camera(node));
    }
    //else if (is_physics(node))
    //{
    //    //log_tools.info("P {}\n", node->name());
    //    m_icon_set->icon(*node);
    //}
    else
    {
        m_icon_set->icon(*node);
    }
    //if (update)
    //{
    //    imgui_node_update(node);
    //}

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

    const auto label     = fmt::format("{} {}##{}", node->name(), node->get_id(), node->get_id());
    const auto node_open = ImGui::TreeNodeEx(label.c_str(), node_flags);

    if (update)
    {
        drag_and_drop_target(node);
        imgui_node_update(node);
    }

    return node_open;
}

void Node_tree_window::imgui_tree_node(
    const std::shared_ptr<erhe::scene::Node>& node
)
{
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

void Node_tree_window::on_begin()
{
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2{0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2{3.0f, 3.0f});
}

void Node_tree_window::on_end()
{
    ImGui::PopStyleVar(2);
}

void Node_tree_window::imgui()
{
    const auto& scene = m_scene_root->scene();
    m_tree_nodes_last_frame = m_tree_nodes;
    m_tree_nodes.clear();

    if (m_selection_tool)
    {
        m_selection_tool->range_selection().begin();
    }

    for (const auto& node : scene.root_node->children())
    {
        imgui_tree_node(node);
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
}

}
