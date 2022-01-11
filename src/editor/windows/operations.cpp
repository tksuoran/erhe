#include "windows/operations.hpp"
#include "editor_imgui_windows.hpp"
#include "log.hpp"

#include "operations/attach_detach_operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/geometry_operations.hpp"
#include "operations/merge_operation.hpp"
#include "renderers/mesh_memory.hpp"
#include "scene/scene_root.hpp"
#include "tools/pointer_context.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tool.hpp"

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
    m_pointer_context = get<Pointer_context>();
    m_scene_root      = get<Scene_root     >();
    m_selection_tool  = get<Selection_tool >();
    require<Editor_imgui_windows>();
}

void Operations::initialize_component()
{
    get<Editor_imgui_windows>()->register_imgui_window(this);
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

void Operations::register_active_tool(Tool* tool)
{
    tool->set_enable_state(false);
    m_active_tools.push_back(tool);
}

void Operations::imgui()
{
    using namespace erhe::imgui;

    if (m_selection_tool == nullptr)
    {
        return;
    }

    const auto button_size  = ImVec2(ImGui::GetContentRegionAvail().x, 0.0f);
    for (unsigned int i = 0; i < static_cast<unsigned int>(m_active_tools.size()); ++i)
    {
        auto* tool = m_active_tools.at(i);
        const bool button_pressed = make_button(
            tool->description(),
            tool->is_enabled()
                ? Item_mode::active
                : Item_mode::normal,
            button_size
        );
        if (button_pressed && (m_current_active_tool != tool))
        {
            if (m_current_active_tool != nullptr)
            {
                m_current_active_tool->set_enable_state(false);
            }
            m_current_active_tool = tool;
            m_current_active_tool->set_enable_state(true);
        }
    }

    const auto mesh_context = [this](){
        return Mesh_operation::Context{
            .build_info_set = m_mesh_memory->build_info_set,
            .scene          = m_scene_root->scene(),
            .layer          = m_scene_root->content_layer(),
            .physics_world  = m_scene_root->physics_world(),
            .selection_tool = m_selection_tool.get()
        };
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
        m_operation_stack->push(
            std::make_shared<Attach_detach_operation>(
                Attach_detach_operation::Context{
                    .scene          = m_scene_root->scene(),
                    .layer          = m_scene_root->content_layer(),
                    .attach         = true,
                    .selection_tool = m_selection_tool.get()
                }
            )
        );
    }

    if (make_button("Detach", multi_select_mode, button_size))
    {
        m_operation_stack->push(
            std::make_shared<Attach_detach_operation>(
                Attach_detach_operation::Context{
                    .scene          = m_scene_root->scene(),
                    .layer          = m_scene_root->content_layer(),
                    .attach         = false,
                    .selection_tool = m_selection_tool.get()
                }
            )
        );
    }

    if (make_button("Merge", multi_select_mode, button_size))
    {
        m_operation_stack->push(
            std::make_shared<Merge_operation>(
                Merge_operation::Context{
                    .build_info_set = m_mesh_memory->build_info_set,
                    .layer          = m_scene_root->content_layer(),
                    .scene          = m_scene_root->scene(),
                    .physics_world  = m_scene_root->physics_world(),
                    .selection_tool = m_selection_tool.get()
                }
            )
        );
    }

    const auto has_selection_mode = (selected_mesh_count >= 1)
        ? Item_mode::normal
        : Item_mode::disabled;
    if (make_button("Catmull-Clark", has_selection_mode, button_size))
    {
        m_operation_stack->push(
            std::make_shared<Catmull_clark_subdivision_operation>(
                mesh_context()
            )
        );
    }
    if (make_button("Sqrt3", has_selection_mode, button_size))
    {
        m_operation_stack->push(
            std::make_shared<Sqrt3_subdivision_operation>(
                mesh_context()
            )
        );
    }
    if (make_button("Triangulate", has_selection_mode, button_size))
    {
        m_operation_stack->push(
            std::make_shared<Triangulate_operation>(
                mesh_context()
            )
        );
    }
    if (make_button("Reverse", has_selection_mode, button_size))
    {
        m_operation_stack->push(
            std::make_shared<Reverse_operation>(
                mesh_context()
            )
        );
    }
    if (make_button("Subdivide", has_selection_mode, button_size))
    {
        m_operation_stack->push(
            std::make_shared<Subdivide_operation>(
                mesh_context()
            )
        );
    }
    if (make_button("Gyro", has_selection_mode, button_size))
    {
        m_operation_stack->push(
            std::make_shared<Gyro_operation>(
                mesh_context()
            )
        );
    }
    if (make_button("Dual", has_selection_mode, button_size))
    {
        m_operation_stack->push(
            std::make_shared<Dual_operator>(
                mesh_context()
            )
        );
    }
    if (make_button("Ambo", has_selection_mode, button_size))
    {
        m_operation_stack->push(
            std::make_shared<Ambo_operator>(
                mesh_context()
            )
        );
    }
    if (make_button("Truncate", has_selection_mode, button_size))
    {
        m_operation_stack->push(
            std::make_shared<Truncate_operator>(
                mesh_context()
            )
        );
    }
}

} // namespace editor
