#include "windows/operations.hpp"

#include "editor_context.hpp"
#include "operations/operation_stack.hpp"
#include "operations/geometry_operations.hpp"
#include "operations/merge_operation.hpp"
#include "operations/item_parent_change_operation.hpp"
#include "renderers/mesh_memory.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_profile/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#endif

#include <taskflow/taskflow.hpp>  // Taskflow is header-only

namespace editor {

Operations::Operations(
    erhe::commands::Commands&    commands,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context
)
    : Imgui_window           {imgui_renderer, imgui_windows, "Operations", "operations"}
    , m_context              {editor_context}
    , m_merge_command        {commands, "Geometry.Merge",                     [this]() -> bool { merge        (); return true; } }
    , m_triangulate_command  {commands, "Geometry.Triangulate",               [this]() -> bool { triangulate  (); return true; } }
    , m_normalize_command    {commands, "Geometry.Normalize",                 [this]() -> bool { normalize    (); return true; } }
    , m_reverse_command      {commands, "Geometry.Reverse",                   [this]() -> bool { reverse      (); return true; } }

    , m_catmull_clark_command{commands, "Geometry.Subdivision.Catmull-Clark", [this]() -> bool { catmull_clark(); return true; } }
    , m_sqrt3_command        {commands, "Geometry.Subdivision.Sqrt3",         [this]() -> bool { sqrt3        (); return true; } }

    , m_dual_command         {commands, "Geometry.Conway.Dual",               [this]() -> bool { dual         (); return true; } }
    , m_join_command         {commands, "Geometry.Conway.Join",               [this]() -> bool { join         (); return true; } }
    , m_kis_command          {commands, "Geometry.Conway.Kis",                [this]() -> bool { kis          (); return true; } }
    , m_meta_command         {commands, "Geometry.Conway.Meta",               [this]() -> bool { meta         (); return true; } }
    , m_ortho_command        {commands, "Geometry.Conway.Ortho",              [this]() -> bool { ortho        (); return true; } }
    , m_ambo_command         {commands, "Geometry.Conway.Ambo",               [this]() -> bool { ambo         (); return true; } }
    , m_truncate_command     {commands, "Geometry.Conway.Truncate",           [this]() -> bool { truncate     (); return true; } }
    , m_gyro_command         {commands, "Geometry.Conway.Gyro",               [this]() -> bool { gyro         (); return true; } }
{
    commands.register_command(&m_merge_command      );
    commands.register_command(&m_triangulate_command);
    commands.register_command(&m_normalize_command  );
    commands.register_command(&m_reverse_command    );

    commands.register_command(&m_catmull_clark_command);
    commands.register_command(&m_sqrt3_command        );

    commands.register_command(&m_ortho_command   );
    commands.register_command(&m_dual_command    );
    commands.register_command(&m_join_command    );
    commands.register_command(&m_kis_command     );
    commands.register_command(&m_meta_command    );
    commands.register_command(&m_ortho_command   );
    commands.register_command(&m_ambo_command    );
    commands.register_command(&m_truncate_command);
    commands.register_command(&m_gyro_command    );

    commands.bind_command_to_menu(&m_merge_command,       "Geometry.Merge");
    commands.bind_command_to_menu(&m_triangulate_command, "Geometry.Triangulate");
    commands.bind_command_to_menu(&m_normalize_command,   "Geometry.Normalize");
    commands.bind_command_to_menu(&m_reverse_command,     "Geometry.Reverse");

    commands.bind_command_to_menu(&m_catmull_clark_command, "Geometry.Subdivision.Catmull-Clark");
    commands.bind_command_to_menu(&m_sqrt3_command,         "Geometry.Subdivision.Sqrt(3)");

    commands.bind_command_to_menu(&m_dual_command    , "Geometry.Conway Operations.Dual");
    commands.bind_command_to_menu(&m_join_command    , "Geometry.Conway Operations.Join");
    commands.bind_command_to_menu(&m_kis_command     , "Geometry.Conway Operations.Kis");
    commands.bind_command_to_menu(&m_meta_command    , "Geometry.Conway Operations.Meta");
    commands.bind_command_to_menu(&m_ortho_command   , "Geometry.Conway Operations.Ortho");
    commands.bind_command_to_menu(&m_ambo_command    , "Geometry.Conway Operations.Ambo");
    commands.bind_command_to_menu(&m_truncate_command, "Geometry.Conway Operations.Truncate");
    commands.bind_command_to_menu(&m_gyro_command    , "Geometry.Conway Operations.Gyro");
}

auto Operations::mesh_context() -> Mesh_operation_parameters
{
    return Mesh_operation_parameters{
        .context = m_context,
        .build_info{
            .primitive_types = {
                .fill_triangles  = true,
                .edge_lines      = true,
                .corner_points   = true,
                .centroid_points = true
            },
            .buffer_info     = m_context.mesh_memory->buffer_info
        }
    };
}

// Special rule to count meshes as selected even when the node that
// contains the mesh is seletected and mesh itself is not selected.
auto Operations::count_selected_meshes() const -> size_t
{
    const auto& selection = m_context.selection->get_selection();
    std::size_t count = 0;
    for (const auto& item : selection) {
        auto node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        if (node) {
            for (const auto& attachment : node->get_attachments()) {
                if (erhe::scene::is_mesh(attachment)) {
                    ++count;
                }
            }
        } else if (erhe::scene::is_mesh(item)) {
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
            operation_stack.queue(
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

    //if (make_button("Unparent", has_selection_mode, button_size)) {
    //    const auto& node0 = selection.get<erhe::scene::Node>();
    //    if (node0) {
    //        m_context.operation_stack->queue(
    //            std::make_shared<Item_parent_change_operation>(
    //                std::shared_ptr<erhe::scene::Node>{},
    //                node0,
    //                std::shared_ptr<erhe::scene::Node>{},
    //                std::shared_ptr<erhe::scene::Node>{}
    //            )
    //        );
    //    }
    //}

    if (make_button("Merge", multi_select_meshes, button_size)) {
        merge();
    }

    if (make_button("Catmull-Clark", has_selection_mode, button_size)) {
        catmull_clark();
    }
    if (make_button("Sqrt3", has_selection_mode, button_size)) {
        sqrt3();
    }
    if (make_button("Triangulate", has_selection_mode, button_size)) {
        triangulate();
    }
    if (make_button("Join", has_selection_mode, button_size)) {
        join();
    }
    if (make_button("Kis", has_selection_mode, button_size)) {
        kis();
    }
    if (make_button("Meta", has_selection_mode, button_size)) {
        meta();
    }
    if (make_button("Ortho", has_selection_mode, button_size)) {
        ortho();
    }
    if (make_button("Gyro", has_selection_mode, button_size)) {
        gyro();
    }
    if (make_button("Dual", has_selection_mode, button_size)) {
        dual();
    }
    if (make_button("Ambo", has_selection_mode, button_size)) {
        ambo();
    }
    if (make_button("Truncate", has_selection_mode, button_size)) {
        truncate();
    }
    if (make_button("Normalize", has_selection_mode, button_size)) {
        normalize();
    }
    if (make_button("Reverse", has_selection_mode, button_size)) {
        reverse();
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

void Operations::merge()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async(
        [this](){
            m_context.operation_stack->queue(
                std::make_shared<Merge_operation>(
                    Merge_operation::Parameters{
                        .context = m_context,
                        .build_info{
                            .primitive_types{
                                .fill_triangles  = true,
                                .edge_lines      = true,
                                .corner_points   = true,
                                .centroid_points = true
                            },
                            .buffer_info    = m_context.mesh_memory->buffer_info
                        }
                    }
                )
            );
        }
    );
}

void Operations::triangulate()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async([this](){ m_context.operation_stack->queue(std::make_shared<Triangulate_operation>( mesh_context()));});
}

void Operations::normalize()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async([this](){ m_context.operation_stack->queue(std::make_shared<Normalize_operation>(mesh_context()));});
}

void Operations::reverse()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async([this](){m_context.operation_stack->queue(std::make_shared<Reverse_operation>(mesh_context()));});
}

void Operations::catmull_clark()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async([this](){m_context.operation_stack->queue(std::make_shared<Catmull_clark_subdivision_operation>(mesh_context()));});
}

void Operations::sqrt3()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async([this](){m_context.operation_stack->queue(std::make_shared<Sqrt3_subdivision_operation>(mesh_context()));});
}

void Operations::dual()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async([this](){m_context.operation_stack->queue(std::make_shared<Dual_operation>(mesh_context()));});
}

void Operations::join()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async([this](){m_context.operation_stack->queue(std::make_shared<Join_operation>(mesh_context()));});
}

void Operations::kis()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async([this](){m_context.operation_stack->queue(std::make_shared<Kis_operation>(mesh_context()));});
}

void Operations::meta()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async([this](){m_context.operation_stack->queue(std::make_shared<Meta_operation>(mesh_context()));});
}

void Operations::ortho()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async([this](){m_context.operation_stack->queue(std::make_shared<Subdivide_operation>(mesh_context()));});
}

void Operations::ambo()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async([this](){m_context.operation_stack->queue(std::make_shared<Ambo_operation>(mesh_context()));});
}

void Operations::truncate()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async([this](){m_context.operation_stack->queue(std::make_shared<Truncate_operation>(mesh_context()));});
}

void Operations::gyro()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async([this](){m_context.operation_stack->queue(std::make_shared<Gyro_operation>(mesh_context()));});
}

} // namespace editor
