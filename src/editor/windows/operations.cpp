#include "windows/operations.hpp"
#include "log.hpp"
#include "tools.hpp"
#include "operations/attach_detach_operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/geometry_operations.hpp"
#include "operations/merge_operation.hpp"
#include "renderers/mesh_memory.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"
#include "tools/pointer_context.hpp"

#include "erhe/imgui/imgui_helpers.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/scene/mesh.hpp"

#include <imgui.h>

namespace editor
{

Operations::Operations()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_title}
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

auto Operations::count_selected_meshes() const -> size_t
{
    const auto& selection = m_selection_tool->selection();
    size_t count = 0;
    for (const auto& node : selection)
    {
        if (is_mesh(node))
        {
            ++count;
        }
    }
    return count;
}

void Operations::imgui(Pointer_context& pointer_context)
{
    using namespace erhe::imgui;

    if (m_selection_tool == nullptr)
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
            (button_action == active_action)
                ? Item_mode::active
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

    const auto undo_mode = m_operation_stack->can_undo()
        ? Item_mode::normal
        : Item_mode::disabled;
    if (make_button("Undo", undo_mode, button_size))
    {
        m_operation_stack->undo();
    }

    const auto redo_mode = m_operation_stack->can_redo()
        ? Item_mode::normal
        : Item_mode::disabled;
    if (make_button("Redo", redo_mode, button_size))
    {
        m_operation_stack->redo();
    }

    const auto selected_mesh_count = count_selected_meshes();

    const auto multi_select_mode = (selected_mesh_count >= 2)
        ? Item_mode::normal
        : Item_mode::disabled;
    if (make_button("Attach", multi_select_mode, button_size))
    {
        Attach_detach_operation::Context attach_context{
            m_scene_root->scene(),
            m_scene_root->content_layer(),
            m_selection_tool,
            true
        };
        auto op = std::make_shared<Attach_detach_operation>(attach_context);
        m_operation_stack->push(op);
    }

    if (make_button("Detach", multi_select_mode, button_size))
    {
        Attach_detach_operation::Context detach_context{
            m_scene_root->scene(),
            m_scene_root->content_layer(),
            m_selection_tool,
            false
        };
        auto op = std::make_shared<Attach_detach_operation>(detach_context);
        m_operation_stack->push(op);
    }

    if (make_button("Merge", multi_select_mode, button_size))
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

    const auto has_selection_mode = (selected_mesh_count >= 1)
        ? Item_mode::normal
        : Item_mode::disabled;
    if (make_button("Catmull-Clark", has_selection_mode, button_size))
    {
        auto op = std::make_shared<Catmull_clark_subdivision_operation>(context);
        m_operation_stack->push(op);
    }
    if (make_button("Sqrt3", has_selection_mode, button_size))
    {
        auto op = std::make_shared<Sqrt3_subdivision_operation>(context);
        m_operation_stack->push(op);
    }
    if (make_button("Triangulate", has_selection_mode, button_size))
    {
        auto op = std::make_shared<Triangulate_operation>(context);
        m_operation_stack->push(op);
    }
    if (make_button("Reverse", has_selection_mode, button_size))
    {
        auto op = std::make_shared<Reverse_operation>(context);
        m_operation_stack->push(op);
    }
    if (make_button("Subdivide", has_selection_mode, button_size))
    {
        auto op = std::make_shared<Subdivide_operation>(context);
        m_operation_stack->push(op);
    }
    if (make_button("Gyro", has_selection_mode, button_size))
    {
        auto op = std::make_shared<Gyro_operation>(context);
        m_operation_stack->push(op);
    }
    if (make_button("Dual", has_selection_mode, button_size))
    {
        auto op = std::make_shared<Dual_operator>(context);
        m_operation_stack->push(op);
    }
    if (make_button("Ambo", has_selection_mode, button_size))
    {
        auto op = std::make_shared<Ambo_operator>(context);
        m_operation_stack->push(op);
    }
    if (make_button("Truncate", has_selection_mode, button_size))
    {
        auto op = std::make_shared<Truncate_operator>(context);
        m_operation_stack->push(op);
    }
    ImGui::End();
}

} // namespace editor
