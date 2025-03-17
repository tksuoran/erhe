#include "windows/operations.hpp"

#include "editor_context.hpp"
#include "editor_message_bus.hpp"
#include "scene/scene_root.hpp"
#include "operations/operation_stack.hpp"
#include "operations/geometry_operations.hpp"
#include "operations/merge_operation.hpp"
#include "operations/node_transform_operation.hpp"
#include "operations/item_parent_change_operation.hpp"
#include "renderers/mesh_memory.hpp"
#include "scene/scene_builder.hpp"
#include "tools/selection_tool.hpp"
#include "windows/property_editor.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_defer/defer.hpp"
#include "erhe_file/file.hpp"
#include "erhe_gltf/gltf.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/file_dialog.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_profile/profile.hpp"

#include <imgui/imgui.h>

#if defined(ERHE_WINDOW_LIBRARY_SDL)
# include <SDL3/SDL_dialog.h>
#endif

#include <taskflow/taskflow.hpp>  // Taskflow is header-only

namespace editor {

Operations::Operations(
    erhe::commands::Commands&    commands,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context,
    Editor_message_bus&          editor_message_bus
)
    : Imgui_window            {imgui_renderer, imgui_windows, "Operations", "operations"}
    , m_context               {editor_context}
    , m_merge_command         {commands, "Geometry.Merge",                     [this]() -> bool { merge         (); return true; } }
    , m_triangulate_command   {commands, "Geometry.Triangulate",               [this]() -> bool { triangulate   (); return true; } }
    , m_normalize_command     {commands, "Geometry.Normalize",                 [this]() -> bool { normalize     (); return true; } }
    , m_bake_transform_command{commands, "Geometry.BakeTransform",             [this]() -> bool { bake_transform(); return true; } }
    , m_reverse_command       {commands, "Geometry.Reverse",                   [this]() -> bool { reverse       (); return true; } }
    , m_repair_command        {commands, "Geometry.Repair",                    [this]() -> bool { repair        (); return true; } }
    , m_weld_command          {commands, "Geometry.Weld",                      [this]() -> bool { weld          (); return true; } }

    , m_difference_command    {commands, "Geometry.Difference",                [this]() -> bool { difference    (); return true; } }
    , m_intersection_command  {commands, "Geometry.Intersection",              [this]() -> bool { intersection  (); return true; } }
    , m_union_command         {commands, "Geometry.Union",                     [this]() -> bool { union_        (); return true; } }

    , m_catmull_clark_command {commands, "Geometry.Subdivision.Catmull-Clark", [this]() -> bool { catmull_clark (); return true; } }
    , m_sqrt3_command         {commands, "Geometry.Subdivision.Sqrt3",         [this]() -> bool { sqrt3         (); return true; } }

    , m_dual_command          {commands, "Geometry.Conway.Dual",               [this]() -> bool { dual          (); return true; } }
    , m_join_command          {commands, "Geometry.Conway.Join",               [this]() -> bool { join          (); return true; } }
    , m_kis_command           {commands, "Geometry.Conway.Kis",                [this]() -> bool { kis           (); return true; } }
    , m_meta_command          {commands, "Geometry.Conway.Meta",               [this]() -> bool { meta          (); return true; } }
    , m_ortho_command         {commands, "Geometry.Conway.Ortho",              [this]() -> bool { ortho         (); return true; } }
    , m_ambo_command          {commands, "Geometry.Conway.Ambo",               [this]() -> bool { ambo          (); return true; } }
    , m_truncate_command      {commands, "Geometry.Conway.Truncate",           [this]() -> bool { truncate      (); return true; } }
    , m_gyro_command          {commands, "Geometry.Conway.Gyro",               [this]() -> bool { gyro          (); return true; } }
    , m_chamfer_command       {commands, "Geometry.Conway.Chamfer",            [this]() -> bool { chamfer       (); return true; } }
    , m_export_gltf_command   {commands, "File.Export.glTF",                   [this]() -> bool { export_gltf   (); return true; } }
{
    commands.register_command(&m_merge_command         );
    commands.register_command(&m_triangulate_command   );
    commands.register_command(&m_normalize_command     );
    commands.register_command(&m_bake_transform_command);
    commands.register_command(&m_reverse_command       );
    commands.register_command(&m_repair_command        );
    commands.register_command(&m_weld_command          );

    commands.register_command(&m_difference_command  );
    commands.register_command(&m_intersection_command);
    commands.register_command(&m_union_command       );

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
    commands.register_command(&m_chamfer_command );

    commands.bind_command_to_menu(&m_merge_command,          "Geometry.Merge");
    commands.bind_command_to_menu(&m_triangulate_command,    "Geometry.Triangulate");
    commands.bind_command_to_menu(&m_normalize_command,      "Geometry.Normalize");
    commands.bind_command_to_menu(&m_bake_transform_command, "Geometry.Bake-Transform");
    commands.bind_command_to_menu(&m_reverse_command,        "Geometry.Reverse");
    commands.bind_command_to_menu(&m_repair_command,         "Geometry.Repair");
    commands.bind_command_to_menu(&m_weld_command,           "Geometry.Weld");

    commands.bind_command_to_menu(&m_difference_command,     "Geometry.CSG.Difference");
    commands.bind_command_to_menu(&m_intersection_command,   "Geometry.CSG.Intersection");
    commands.bind_command_to_menu(&m_union_command,          "Geometry.CSG.Union");

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
    commands.bind_command_to_menu(&m_chamfer_command , "Geometry.Conway Operations.Chamfer");

    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            on_message(message);
        }
    );
}

void Operations::on_message(Editor_message& message)
{
    using namespace erhe::bit;
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_scene_view)) {
        m_hover_scene_view = message.scene_view;
        if (message.scene_view != nullptr) {
            m_last_hover_scene_view = message.scene_view;
        }
    }
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

    Property_editor p;

    bool scenes_open{false};
    p.push_group("Scenes", ImGuiTreeNodeFlags_DefaultOpen, 0.0f, &scenes_open);

    std::shared_ptr<erhe::primitive::Material> material = m_context.selection->get_last_selected<erhe::primitive::Material>();
    if (material) {
        p.add_entry("Material", [material](){ ImGui::TextUnformatted(material->get_name().c_str()); });
    }
    m_make_mesh_config.material = material;
   
    p.add_entry("Count", [this](){ ImGui::SliderInt  ("##", &m_make_mesh_config.instance_count, 1, 32); });
    p.add_entry("Scale", [this](){ ImGui::SliderFloat("##", &m_make_mesh_config.object_scale,   0.2f, 2.0f, "%.2f"); });
    p.add_entry("Gap",   [this](){ ImGui::SliderFloat("##", &m_make_mesh_config.instance_gap,   0.0f, 1.0f, "%.2f"); });
    p.pop_group();
    p.show_entries("operations");

    if (scenes_open) {
        ImGui::Indent(20.0f);
        ERHE_DEFER( ImGui::Unindent(20.0f); );
        const auto button_size = ImVec2{ImGui::GetContentRegionAvail().x, 0.0f};
        if (erhe::imgui::make_button("Cubes", erhe::imgui::Item_mode::normal, button_size)) {
            m_context.scene_builder->add_cubes(
                glm::ivec3{m_make_mesh_config.instance_count, m_make_mesh_config.instance_count, m_make_mesh_config.instance_count},
                m_make_mesh_config.object_scale,
                m_make_mesh_config.instance_gap
            );
        }
        if (erhe::imgui::make_button("Platonic Solids", erhe::imgui::Item_mode::normal, button_size)) {
            m_context.scene_builder->add_platonic_solids(m_make_mesh_config);
        }
        if (erhe::imgui::make_button("Johnson Solids", erhe::imgui::Item_mode::normal, button_size)) {
            m_context.scene_builder->add_johnson_solids(m_make_mesh_config);
        }
        if (erhe::imgui::make_button("Curved Shapes", erhe::imgui::Item_mode::normal, button_size)) {
            m_context.scene_builder->add_curved_shapes(m_make_mesh_config);
        }
        if (erhe::imgui::make_button("Chain", erhe::imgui::Item_mode::normal, button_size)) {
            m_context.scene_builder->add_torus_chain(m_make_mesh_config, true);
        }
        if (erhe::imgui::make_button("Toruses", erhe::imgui::Item_mode::normal, button_size)) {
            m_context.scene_builder->add_torus_chain(m_make_mesh_config, false);
        }
    }

    ImGui::Separator();

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

    const auto button_size = ImVec2{ImGui::GetContentRegionAvail().x, 0.0f};
    const auto undo_mode = operation_stack.can_undo() ? erhe::imgui::Item_mode::normal : erhe::imgui::Item_mode::disabled;
    if (erhe::imgui::make_button("Undo", undo_mode, button_size)) {
        operation_stack.undo();
    }

    const auto redo_mode = operation_stack.can_redo() ? erhe::imgui::Item_mode::normal : erhe::imgui::Item_mode::disabled;
    if (erhe::imgui::make_button("Redo", redo_mode, button_size)) {
        operation_stack.redo();
    }

    const auto selected_mesh_count = count_selected_meshes();
    const auto selected_node_count = selection.count<erhe::scene::Node>();
    const auto multi_select_meshes = (selected_mesh_count >= 2) ? erhe::imgui::Item_mode::normal : erhe::imgui::Item_mode::disabled;
    const auto multi_select_nodes  = (selected_node_count >= 2) ? erhe::imgui::Item_mode::normal : erhe::imgui::Item_mode::disabled;
    const auto delete_mode         = (selected_mesh_count + selected_node_count) > 0 ? erhe::imgui::Item_mode::normal : erhe::imgui::Item_mode::disabled;
    if (erhe::imgui::make_button("Delete", delete_mode, button_size)) {
        m_context.selection->delete_selection();
    }

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

    const auto has_selection_mode = (selected_mesh_count >= 1) ? erhe::imgui::Item_mode::normal : erhe::imgui::Item_mode::disabled;

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

    if (make_button("Export glTF", erhe::imgui::Item_mode::normal, button_size)) {
        export_gltf();
    }

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
    if (make_button("Chamfer", has_selection_mode, button_size)) {
        chamfer();
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
    if (make_button("Repair", has_selection_mode, button_size)) {
        repair();
    }
    if (make_button("Weld", has_selection_mode, button_size)) {
        weld();
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

void Operations::bake_transform()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async(
        [this](){
            Compound_operation::Parameters compound_operation_parameters;
            // First: Transform geometries using node transforms
            compound_operation_parameters.operations.push_back(
                std::make_shared<Bake_transform_operation>(mesh_context())
            );
            // Second: Reset transform in all nodes
            const std::vector<std::shared_ptr<erhe::scene::Node>> nodes = m_context.selection->get_all<erhe::scene::Node>();
            for (const std::shared_ptr<erhe::scene::Node>& node : nodes) {
                compound_operation_parameters.operations.push_back(
                    std::make_shared<Node_transform_operation>(
                        Node_transform_operation::Parameters{
                            .node = node,
                            .parent_from_node_before = node->parent_from_node_transform(),
                            .parent_from_node_after = {}
                        }
                    )
                );
            }
            m_context.operation_stack->queue(
                std::make_shared<Compound_operation>(std::move(compound_operation_parameters))
            );
        }
    );
}

void Operations::reverse()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async([this](){m_context.operation_stack->queue(std::make_shared<Reverse_operation>(mesh_context()));});
}

void Operations::repair()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async([this](){m_context.operation_stack->queue(std::make_shared<Repair_operation>(mesh_context()));});
}

void Operations::weld()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async([this](){m_context.operation_stack->queue(std::make_shared<Weld_operation>(mesh_context()));});
}

void Operations::difference()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async(
        [this](){
            m_context.operation_stack->queue(
                std::make_shared<Difference_operation>(
                    mesh_context()
                )
            );
        }
    );
}
void Operations::intersection()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async([this](){m_context.operation_stack->queue(std::make_shared<Intersection_operation>(mesh_context()));});
}
void Operations::union_()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async([this](){m_context.operation_stack->queue(std::make_shared<Union_operation>(mesh_context()));});
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

void Operations::chamfer()
{
    tf::Executor& executor = m_context.operation_stack->get_executor();
    executor.silent_async([this](){m_context.operation_stack->queue(std::make_shared<Chamfer_operation>(mesh_context()));});
}

#if defined(ERHE_WINDOW_LIBRARY_SDL)
static void s_export_callback(void* userdata, const char* const* filelist, int filter)
{
    Operations* operations = static_cast<Operations*>(userdata);
    operations->export_callback(filelist, filter);
}
#endif

void Operations::export_callback(const char* const* filelist, int filter)
{
    static_cast<void>(filter);
    if (filelist == nullptr) {
        // error
        return;
    }
    const char* const file = *filelist;
    if (file == nullptr) {
        // nothing chosen / canceled
        return;
    }
    //std::optional<std::filesystem::path> path = erhe::file::select_file_for_write();
    std::optional<std::filesystem::path> path = std::filesystem::path{file};

    if (m_last_hover_scene_view == nullptr) {
        return;
    }

    std::shared_ptr<Scene_root> scene_root = m_last_hover_scene_view->get_scene_root();
    if (!scene_root) {
        return;
    }

    const erhe::scene::Scene& scene = scene_root->get_scene();
    std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
    if (!root_node) {
        return;
    }

    if (path.has_value()) {
        const bool binary = true;
        std::string gltf = erhe::gltf::export_gltf(*root_node.get(), binary);
        log_operations->info("{}", gltf);
        erhe::file::write_file(path.value(), gltf);
    }
}

void Operations::export_gltf()
{
#if defined(ERHE_WINDOW_LIBRARY_SDL)
    SDL_DialogFileFilter filters[3];
    filters[0].name    = "glTF files";
    filters[0].pattern = "glb;gltf";
    filters[1].name    = "All files";
    filters[1].pattern = "*";
    SDL_Window* window = static_cast<SDL_Window*>(m_context.context_window->get_sdl_window());
    SDL_ShowSaveFileDialog(s_export_callback, this, window, filters, 2, nullptr);
#elif defined(ERHE_OS_WINDOWS)
    try {
        std::optional<std::filesystem::path> path_opt = erhe::file::select_file_for_write();
        if (path_opt.has_value()) {
            std::string path = path_opt.value().string();
            const char* const filelist[2] = {
                path.data(),
                nullptr
            };
            int filter = 0;
            export_callback(filelist, filter);
        }
    } catch (...) {
        log_operations->error("exception: file dialog / glTF export");
    }
#else
    const char* const filelist[2] =
    {
        "erhe.glb",
        nullptr
    };
    int filter = 0;
    export_callback(filelist, filter);
#endif
}

} // namespace editor
