#include "windows/operations.hpp"
#include "editor_log.hpp"

#include "operations/operation_stack.hpp"
#include "operations/geometry_operations.hpp"
#include "operations/merge_operation.hpp"
#include "operations/node_operation.hpp"
#include "renderers/mesh_memory.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tool.hpp"

#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/imgui/imgui_renderer.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

Operations::Operations()
    : erhe::components::Component{c_type_name}
    , Imgui_window               {c_title}
{
}

Operations::~Operations() noexcept
{
}

void Operations::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
}

void Operations::initialize_component()
{
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Operations::post_initialize()
{
    m_mesh_memory     = get<Mesh_memory    >();
    m_operation_stack = get<Operation_stack>();
    m_selection_tool  = get<Selection_tool >();
}

auto Operations::count_selected_meshes() const -> size_t
{
    const auto& selection = m_selection_tool->selection();
    std::size_t count = 0;
    for (const auto& node : selection)
    {
        if (is_mesh(node))
        {
            ++count;
        }
    }
    return count;
}

void Operations::set_active_tool(Tool* tool)
{
    if (m_current_active_tool == tool)
    {
        return;
    }

    if (m_current_active_tool != nullptr)
    {
        log_tools->trace("disabling tool {}", m_current_active_tool->description());
        m_current_active_tool->set_enable_state(false);
    }

    m_current_active_tool = tool;

    if (m_current_active_tool != nullptr)
    {
        log_tools->trace("enabling tool {}", m_current_active_tool->description());
        m_current_active_tool->set_enable_state(true);
    }
    else
    {
        log_tools->trace("active tool reset");
    }
}

[[nodiscard]] auto Operations::get_active_tool() const -> Tool*
{
    return m_current_active_tool;
}

void Operations::register_active_tool(Tool* tool)
{
    tool->set_enable_state(false);

    std::lock_guard<std::mutex> lock{m_mutex};
    m_active_tools.push_back(tool);
}

void Operations::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
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
        if (button_pressed)
        {
            if (get_active_tool() != tool) {
                set_active_tool(tool);
            } else {
                set_active_tool(nullptr);
            }
        }
    }

    const auto mesh_context = [this](){
        return Mesh_operation::Parameters{
            .build_info     = m_mesh_memory->build_info,
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
        const auto& node0 = as_node(m_selection_tool->selection().at(0));
        const auto& node1 = as_node(m_selection_tool->selection().at(1));
        if (node1 && node1)
        {
            m_operation_stack->push(
                std::make_shared<Node_attach_operation>(
                    node1,
                    node0,
                    std::shared_ptr<erhe::scene::Node>{},
                    std::shared_ptr<erhe::scene::Node>{}
                )
            );
        }
    }

    const auto has_selection_mode = (selected_mesh_count >= 1)
        ? erhe::application::Item_mode::normal
        : erhe::application::Item_mode::disabled;

    if (make_button("Unparent", has_selection_mode, button_size))
    {
        const auto& node0 = as_node(m_selection_tool->selection().at(0));
        if (node0)
        {
            m_operation_stack->push(
                std::make_shared<Node_attach_operation>(
                    std::shared_ptr<erhe::scene::Node>{},
                    node0,
                    std::shared_ptr<erhe::scene::Node>{},
                    std::shared_ptr<erhe::scene::Node>{}
                )
            );
        }
    }

    if (make_button("Merge", multi_select_mode, button_size))
    {
        m_operation_stack->push(
            std::make_shared<Merge_operation>(
                Merge_operation::Parameters{
                    .build_info     = m_mesh_memory->build_info,
                    .selection_tool = m_selection_tool.get()
                }
            )
        );
    }

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
    if (make_button("Join", has_selection_mode, button_size))
    {
        m_operation_stack->push(
            std::make_shared<Join_operation>(
                mesh_context()
            )
        );
    }
    if (make_button("Kis", has_selection_mode, button_size))
    {
        m_operation_stack->push(
            std::make_shared<Kis_operation>(
                mesh_context()
            )
        );
    }
    if (make_button("Meta", has_selection_mode, button_size))
    {
        m_operation_stack->push(
            std::make_shared<Meta_operation>(
                mesh_context()
            )
        );
    }
    if (make_button("Ortho", has_selection_mode, button_size))
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
    if (make_button("Reverse", has_selection_mode, button_size))
    {
        m_operation_stack->push(
            std::make_shared<Reverse_operation>(
                mesh_context()
            )
        );
    }
    //// if (make_button("GUI Quad", erhe::application::Item_mode::normal, button_size))
    //// {
    ////     Scene_builder* scene_builder = get<Scene_builder>().get();
    ////
    ////     m_imgui_renderer->at_end_of_frame(
    ////         [scene_builder](){
    ////             scene_builder->add_rendertarget_viewports(2);
    ////         }
    ////     );
    //// }
#endif
}

} // namespace editor
