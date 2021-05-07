#include "windows/operations.hpp"
#include "editor.hpp"
#include "operations/operation_stack.hpp"
#include "operations/geometry_operations.hpp"
#include "scene/scene_manager.hpp"
#include "tools/selection_tool.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/scene/mesh.hpp"

#include "imgui.h"

namespace editor
{

Operations::Operations(Editor&                                 editor,
                       const std::shared_ptr<Operation_stack>& operation_stack,
                       const std::shared_ptr<Selection_tool>&  selection_tool,
                       const std::shared_ptr<Scene_manager>&   scene_manager)
    : m_editor         {editor}
    , m_operation_stack{operation_stack}
    , m_selection_tool {selection_tool}
    , m_scene_manager  {scene_manager}
{
}


void Operations::window(Pointer_context&)
{
    if (m_selection_tool.get() == nullptr)
    {
        return;
    }

    ImGui::Begin("Operations");

    auto activeAction = m_editor.get_priority_action();
    for (unsigned int i = 0; i < static_cast<unsigned int>(Editor::Action::count); ++i)
    {
        auto buttonAction = static_cast<Editor::Action>(i);
        bool isActive = buttonAction == activeAction;
        if (isActive)
        {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.3f, 0.4, 0.8f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.5, 0.9f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.5f, 0.6, 1.0f, 1.0f));
        }
        if (ImGui::Button(Editor::c_action_strings[i]) && (activeAction != buttonAction)) {
            m_editor.set_priority_action(buttonAction);
        }
        if (isActive)
        {
            ImGui::PopStyleColor(3);
        }
    }

    Mesh_operation::Context context;
    context.scene_manager  = m_scene_manager;
    context.selection_tool = m_selection_tool;
    if (m_operation_stack->can_undo() && ImGui::Button("Undo"))
    {
        m_operation_stack->undo();
    }
    if (m_operation_stack->can_redo() && ImGui::Button("Redo"))
    {
        m_operation_stack->redo();
    }

    if (ImGui::Button("Catmull-Clark Subdivision"))
    {
        auto op = std::make_shared<Catmull_clark_subdivision_operation>(context);
        m_operation_stack->push(op);
    }
    if (ImGui::Button("Sqrt3 Subdivision"))
    {
        auto op = std::make_shared<Sqrt3_subdivision_operation>(context);
        m_operation_stack->push(op);
    }
    if (ImGui::Button("Triangulate"))
    {
        auto op = std::make_shared<Triangulate_operation>(context);
        m_operation_stack->push(op);
    }
    if (ImGui::Button("Subdivide"))
    {
        auto op = std::make_shared<Subdivide_operation>(context);
        m_operation_stack->push(op);
    }
    if (ImGui::Button("Dual"))
    {
        auto op = std::make_shared<Dual_operator>(context);
        m_operation_stack->push(op);
    }
    if (ImGui::Button("Ambo"))
    {
        auto op = std::make_shared<Ambo_operator>(context);
        m_operation_stack->push(op);
    }
    if (ImGui::Button("Truncate"))
    {
        auto op = std::make_shared<Truncate_operator>(context);
        m_operation_stack->push(op);
    }
    if (ImGui::Button("Snub"))
    {
        auto op = std::make_shared<Snub_operator>(context);
        m_operation_stack->push(op);
    }
    ImGui::End();
}

} // namespace editor
