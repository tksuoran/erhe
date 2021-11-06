#include "windows/operations.hpp"
#include "tools.hpp"
#include "operations/operation_stack.hpp"
#include "operations/geometry_operations.hpp"
#include "operations/merge_operation.hpp"
#include "renderers/mesh_memory.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"
#include "tools/pointer_context.hpp"
#include "log.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/scene/mesh.hpp"

#include "imgui.h"

namespace editor
{

Operations::Operations()
    : erhe::components::Component{c_name}
{
}

Operations::~Operations() = default;

void Operations::connect()
{
    m_mesh_memory     = get<Mesh_memory    >();
    m_operation_stack = get<Operation_stack>();
    m_selection_tool  = get<Selection_tool >();
    m_scene_root      = get<Scene_root     >();
}

void Operations::initialize_component()
{
    get<Editor_tools>()->register_imgui_window(this);
}

void Operations::imgui(Pointer_context& pointer_context)
{
    if (m_selection_tool.get() == nullptr)
    {
        return;
    }

    ImGui::Begin("Tools");

    const auto button_size   = ImVec2(ImGui::GetContentRegionAvailWidth(), 0.0f);
    const auto active_action = pointer_context.priority_action;
    for (unsigned int i = 0; i < static_cast<unsigned int>(Action::count); ++i)
    {
        const auto button_action  = static_cast<Action>(i);
        const bool button_pressed = make_button(
            c_action_strings[i].data(),
            (button_action == active_action) ? Item_mode::active
                                             : Item_mode::normal,
            button_size
        );
        if (button_pressed && (active_action != button_action))
        {
            log_tools.trace("Setting priority action to {}\n", c_action_strings[i]);
            pointer_context.priority_action = button_action;
        }
    }

    Mesh_operation::Context context{
        m_mesh_memory->build_info_set,
        m_scene_root->scene(),
        m_scene_root->content_layer(),
        m_scene_root->physics_world(),
        m_selection_tool
    };
    const auto mode = m_operation_stack->can_undo()
        ? Item_mode::normal
        : Item_mode::disabled;
    if (make_button("Undo", mode, button_size))
    {
        m_operation_stack->undo();
    }
    if (make_button("Redo", mode, button_size))
    {
        m_operation_stack->redo();
    }

    if (ImGui::Button("Merge", button_size))
    {
        Merge_operation::Context merge_context{
            m_mesh_memory->build_info_set,
            m_scene_root->content_layer(),
            m_scene_root->scene(),
            m_scene_root->physics_world(),
            m_selection_tool
        };
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
    ImGui::End();
}

} // namespace editor
