#include "windows/operations.hpp"

#include "editor_context.hpp"
#include "operations/operation_stack.hpp"
#include "operations/geometry_operations.hpp"
#include "operations/merge_operation.hpp"
#include "operations/node_operation.hpp"
#include "renderers/mesh_memory.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/imgui/imgui_helpers.hpp"
#include "erhe/imgui/imgui_renderer.hpp"
#include "erhe/imgui/imgui_windows.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

Operations::Operations(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context
)
    : Imgui_window{imgui_renderer, imgui_windows, "Operations", "operations"}
    , m_context   {editor_context}
{
}

// Special rule to count meshes as selected even when the node that
// contains the mesh is seletected and mesh itself is not selected.
auto Operations::count_selected_meshes() const -> size_t
{
    const auto& selection = m_context.selection->get_selection();
    std::size_t count = 0;
    for (const auto& item : selection) {
        auto node = as_node(item);
        if (node) {
            for (const auto& attachment : node->get_attachments()) {
                if (is_mesh(attachment)) {
                    ++count;
                }
            }
        } else if (is_mesh(item)) {
            ++count;
        }
    }
    return count;
}


void Operations::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION();

    const auto button_size = ImVec2{ImGui::GetContentRegionAvail().x, 0.0f};
    auto& selection       = *m_context.selection;
    auto& operation_stack = *m_context.operation_stack;

    //// for (unsigned int i = 0; i < static_cast<unsigned int>(m_active_tools.size()); ++i) {
    ////     auto* tool = m_active_tools.at(i);
    ////     const bool button_pressed = erhe::imgui::make_button(
    ////         tool->description(),
    ////         tool->is_active()
    ////             ? erhe::imgui::Item_mode::active
    ////             : erhe::imgui::Item_mode::normal,
    ////         button_size
    ////     );
    ////     if (button_pressed) {
    ////         if (get_active_tool() != tool) {
    ////             set_active_tool(tool);
    ////         } else {
    ////             set_active_tool(nullptr);
    ////         }
    ////     }
    //// }

    const auto mesh_context = [this](){
        return Mesh_operation::Parameters{
            .context = m_context,
            .build_info{
                .primitive_types = {.fill_triangles = true, .edge_lines = true, .corner_points = true, .centroid_points = true },
                .buffer_info     = m_context.mesh_memory->buffer_info
            }
        };
    };

    const auto undo_mode = operation_stack.can_undo()
        ? erhe::imgui::Item_mode::normal
        : erhe::imgui::Item_mode::disabled;
    if (erhe::imgui::make_button("Undo", undo_mode, button_size)) {
        operation_stack.undo();
    }

    const auto redo_mode = operation_stack.can_redo()
        ? erhe::imgui::Item_mode::normal
        : erhe::imgui::Item_mode::disabled;
    if (erhe::imgui::make_button("Redo", redo_mode, button_size)) {
        operation_stack.redo();
    }

    const auto selected_mesh_count = count_selected_meshes();
    const auto selected_node_count = selection.count<erhe::scene::Node>();
    const auto multi_select_meshes = (selected_mesh_count >= 2)
        ? erhe::imgui::Item_mode::normal
        : erhe::imgui::Item_mode::disabled;
    const auto multi_select_nodes  = (selected_node_count >= 2)
        ? erhe::imgui::Item_mode::normal
        : erhe::imgui::Item_mode::disabled;
    if (erhe::imgui::make_button("Attach", multi_select_nodes, button_size)) {
        const auto& node0 = selection.get<erhe::scene::Node>(0);
        const auto& node1 = selection.get<erhe::scene::Node>(1);
        if (node0 && node1) {
            operation_stack.push(
                std::make_shared<Item_parent_change_operation>(
                    node1,
                    node0,
                    std::shared_ptr<erhe::scene::Node>{},
                    std::shared_ptr<erhe::scene::Node>{}
                )
            );
        }
    }

    const auto has_selection_mode = (selected_mesh_count >= 1)
        ? erhe::imgui::Item_mode::normal
        : erhe::imgui::Item_mode::disabled;

    if (make_button("Unparent", has_selection_mode, button_size)) {
        const auto& node0 = selection.get<erhe::scene::Node>();
        if (node0) {
            m_context.operation_stack->push(
                std::make_shared<Item_parent_change_operation>(
                    std::shared_ptr<erhe::scene::Node>{},
                    node0,
                    std::shared_ptr<erhe::scene::Node>{},
                    std::shared_ptr<erhe::scene::Node>{}
                )
            );
        }
    }

    if (make_button("Merge", multi_select_meshes, button_size)) {
        m_context.operation_stack->push(
            std::make_shared<Merge_operation>(
                Merge_operation::Parameters{
                    .context = m_context,
                    .build_info{
                        .primitive_types{ .fill_triangles = true, .edge_lines = true, .corner_points = true, .centroid_points = true },
                        .buffer_info    = m_context.mesh_memory->buffer_info
                    }
                }
            )
        );
    }

    if (make_button("Catmull-Clark", has_selection_mode, button_size)) {
        m_context.operation_stack->push(
            std::make_shared<Catmull_clark_subdivision_operation>(
                mesh_context()
            )
        );
    }
    if (make_button("Sqrt3", has_selection_mode, button_size)) {
        m_context.operation_stack->push(
            std::make_shared<Sqrt3_subdivision_operation>(
                mesh_context()
            )
        );
    }
    if (make_button("Triangulate", has_selection_mode, button_size)) {
        m_context.operation_stack->push(
            std::make_shared<Triangulate_operation>(
                mesh_context()
            )
        );
    }
    if (make_button("Join", has_selection_mode, button_size)) {
        m_context.operation_stack->push(
            std::make_shared<Join_operation>(
                mesh_context()
            )
        );
    }
    if (make_button("Kis", has_selection_mode, button_size)) {
        m_context.operation_stack->push(
            std::make_shared<Kis_operation>(
                mesh_context()
            )
        );
    }
    if (make_button("Meta", has_selection_mode, button_size)) {
        m_context.operation_stack->push(
            std::make_shared<Meta_operation>(
                mesh_context()
            )
        );
    }
    if (make_button("Ortho", has_selection_mode, button_size)) {
        m_context.operation_stack->push(
            std::make_shared<Subdivide_operation>(
                mesh_context()
            )
        );
    }
    if (make_button("Gyro", has_selection_mode, button_size)) {
        m_context.operation_stack->push(
            std::make_shared<Gyro_operation>(
                mesh_context()
            )
        );
    }
    if (make_button("Dual", has_selection_mode, button_size)) {
        m_context.operation_stack->push(
            std::make_shared<Dual_operator>(
                mesh_context()
            )
        );
    }
    if (make_button("Ambo", has_selection_mode, button_size)) {
        m_context.operation_stack->push(
            std::make_shared<Ambo_operator>(
                mesh_context()
            )
        );
    }
    if (make_button("Truncate", has_selection_mode, button_size)) {
        m_context.operation_stack->push(
            std::make_shared<Truncate_operator>(
                mesh_context()
            )
        );
    }
    if (make_button("Normalize", has_selection_mode, button_size)) {
        m_context.operation_stack->push(
            std::make_shared<Normalize_operation>(
                mesh_context()
            )
        );
    }
    if (make_button("Reverse", has_selection_mode, button_size)) {
        m_context.operation_stack->push(
            std::make_shared<Reverse_operation>(
                mesh_context()
            )
        );
    }
    //// if (make_button("GUI Quad", erhe::imgui::Item_mode::normal, button_size)) {
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
