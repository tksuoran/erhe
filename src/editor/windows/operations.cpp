#include "windows/operations.hpp"
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

#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/imgui_helpers.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/profile.hpp"

#include <imgui.h>

namespace editor
{

Operations::Operations()
    : erhe::components::Component{c_label}
    , Imgui_window               {c_title, c_label}
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
    require<erhe::application::Imgui_windows>();
}

void Operations::initialize_component()
{
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
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

[[nodiscard]] auto Operations::get_active_tool() const -> Tool*
{
    return m_current_active_tool;
}

void Operations::register_active_tool(Tool* tool)
{
    tool->set_enable_state(false);
    m_active_tools.push_back(tool);
}

void Operations::imgui()
{
    ERHE_PROFILE_FUNCTION

    if (m_selection_tool == nullptr)
    {
        return;
    }

    const auto button_size = ImVec2{ImGui::GetContentRegionAvail().x, 0.0f};
    for (unsigned int i = 0; i < static_cast<unsigned int>(m_active_tools.size()); ++i)
    {
        auto* tool = m_active_tools.at(i);
        const bool button_pressed = erhe::application::make_button(
            tool->description(),
            tool->is_enabled()
                ? erhe::application::Item_mode::active
                : erhe::application::Item_mode::normal,
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
        return Mesh_operation::Parameters{
            .build_info     = m_mesh_memory->build_info,
            .scene          = m_scene_root->scene(),
            .layer          = *m_scene_root->content_layer(),
            .physics_world  = m_scene_root->physics_world(),
            .selection_tool = m_selection_tool.get()
        };
    };

    const auto undo_mode = m_operation_stack->can_undo()
        ? erhe::application::Item_mode::normal
        : erhe::application::Item_mode::disabled;
    if (erhe::application::make_button("Undo", undo_mode, button_size))
    {
        m_operation_stack->undo();
    }

    const auto redo_mode = m_operation_stack->can_redo()
        ? erhe::application::Item_mode::normal
        : erhe::application::Item_mode::disabled;
    if (erhe::application::make_button("Redo", redo_mode, button_size))
    {
        m_operation_stack->redo();
    }

    const auto selected_mesh_count = count_selected_meshes();

    const auto multi_select_mode = (selected_mesh_count >= 2)
        ? erhe::application::Item_mode::normal
        : erhe::application::Item_mode::disabled;
    if (erhe::application::make_button("Attach", multi_select_mode, button_size))
    {
        m_operation_stack->push(
            std::make_shared<Attach_detach_operation>(
                Attach_detach_operation::Parameters{
                    .scene          = m_scene_root->scene(),
                    .layer          = *m_scene_root->content_layer(),
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
                Attach_detach_operation::Parameters{
                    .scene          = m_scene_root->scene(),
                    .layer          = *m_scene_root->content_layer(),
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
                Merge_operation::Parameters{
                    .build_info     = m_mesh_memory->build_info,
                    .layer          = *m_scene_root->content_layer(),
                    .scene          = m_scene_root->scene(),
                    .physics_world  = m_scene_root->physics_world(),
                    .selection_tool = m_selection_tool.get()
                }
            )
        );
    }

    const auto has_selection_mode = (selected_mesh_count >= 1)
        ? erhe::application::Item_mode::normal
        : erhe::application::Item_mode::disabled;
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
    if (make_button("Normalize", has_selection_mode, button_size))
    {
        m_operation_stack->push(
            std::make_shared<Normalize_operation>(
                mesh_context()
            )
        );
    }
    ///// if (make_button("GUI Quad", erhe::application::Item_mode::normal, button_size))
    ///// {
    /////     auto rendertarget = get<erhe::application::Imgui_windows>()->create_rendertarget(
    /////         "Gui Quad",
    /////         2048,
    /////         1024,
    /////         200.0
    /////     );
    /////     const auto placement = erhe::toolkit::create_look_at(
    /////         glm::vec3{0.0f, 1.0f, 0.0f},
    /////         glm::vec3{0.0f, 1.0f, 1.0f},
    /////         glm::vec3{0.0f, 1.0f, 0.0f}
    /////     );
    /////     rendertarget->mesh_node()->set_parent_from_node(placement);
    ///// }
}

} // namespace editor
