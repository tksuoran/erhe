#include "windows/operations.hpp"
#include "operations/operation_stack.hpp"
#include "operations/geometry_operations.hpp"
#include "operations/merge_operation.hpp"
#include "scene/scene_manager.hpp"
#include "tools/selection_tool.hpp"
#include "tools/pointer_context.hpp"
#include "log.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/scene/mesh.hpp"

#include "imgui.h"

namespace editor
{

void Operations::connect()
{
    m_operation_stack = get<Operation_stack>();
    m_selection_tool  = get<Selection_tool>();
    m_scene_manager   = get<Scene_manager>();
}

void Operations::window(Pointer_context& pointer_context)
{
    if (m_selection_tool.get() == nullptr)
    {
        return;
    }

    ImGui::Begin("Tools");

    auto button_size = ImVec2(ImGui::GetContentRegionAvailWidth(), 0.0f);
    auto active_action = pointer_context.priority_action;
    for (unsigned int i = 0; i < static_cast<unsigned int>(Action::count); ++i)
    {
        auto button_action = static_cast<Action>(i);
        bool buttonPressed = make_button(c_action_strings[i],
                                         (button_action == active_action) ? Item_mode::active
                                                                          : Item_mode::normal,
                                         button_size);
        if (buttonPressed && (active_action != button_action)) {
            log_tools.trace("Setting priority action to {}", c_action_strings[i]);
            pointer_context.priority_action = button_action;
        }
    }

    Mesh_operation::Context context;
    context.scene_manager  = m_scene_manager;
    context.selection_tool = m_selection_tool;
    if (make_button("Undo", m_operation_stack->can_undo() ? Item_mode::normal
                                                          : Item_mode::disabled, button_size))
    {
        m_operation_stack->undo();
    }
    if (make_button("Redo", m_operation_stack->can_redo() ? Item_mode::normal
                                                          : Item_mode::disabled, button_size))
    {
        m_operation_stack->redo();
    }

    if (ImGui::Button("Merge", button_size))
    {
        Merge_operation::Context merge_context;
        merge_context.scene_manager  = m_scene_manager;
        merge_context.selection_tool = m_selection_tool;
        auto op = std::make_shared<Merge_operation>(merge_context);
        m_operation_stack->push(op);
    }
    if (ImGui::Button("Catmull-Clark", button_size))
    {
        auto op = std::make_shared<Catmull_clark_subdivision_operation>(context);
        m_operation_stack->push(op);
    }
    if (ImGui::Button("Sqrt3", button_size))
    {
        auto op = std::make_shared<Sqrt3_subdivision_operation>(context);
        m_operation_stack->push(op);
    }
    if (ImGui::Button("Triangulate", button_size))
    {
        auto op = std::make_shared<Triangulate_operation>(context);
        m_operation_stack->push(op);
    }
    if (ImGui::Button("Subdivide", button_size))
    {
        auto op = std::make_shared<Subdivide_operation>(context);
        m_operation_stack->push(op);
    }
    if (ImGui::Button("Dual", button_size))
    {
        auto op = std::make_shared<Dual_operator>(context);
        m_operation_stack->push(op);
    }
    if (ImGui::Button("Ambo", button_size))
    {
        auto op = std::make_shared<Ambo_operator>(context);
        m_operation_stack->push(op);
    }
    if (ImGui::Button("Truncate", button_size))
    {
        auto op = std::make_shared<Truncate_operator>(context);
        m_operation_stack->push(op);
    }
    // Snub is not implemented
    //
    //if (ImGui::Button("Snub"))
    //{
    //    auto op = std::make_shared<Snub_operator>(context);
    //    m_operation_stack->push(op);
    //}
    ImGui::End();
}

} // namespace editor
