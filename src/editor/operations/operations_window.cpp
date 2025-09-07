#include "operations/operations_window.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "content_library/content_library.hpp"
#include "items.hpp"
#include "operations/geometry_operations.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/item_parent_change_operation.hpp"
#include "operations/merge_operation.hpp"
#include "operations/mesh_operation.hpp"
#include "operations/node_transform_operation.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/mesh_memory.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"
#include "windows/property_editor.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_configuration/configuration.hpp"
#include "erhe_defer/defer.hpp"
#include "erhe_file/file.hpp"
#include "erhe_gltf/gltf.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"

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
    App_context&                 app_context,
    App_message_bus&             app_message_bus
)
    : Imgui_window              {imgui_renderer, imgui_windows, "Operations", "operations"}
    , m_context                 {app_context}
    , m_merge_command           {commands, "Geometry.Merge",                     [this]() -> bool { merge           (); return true; } }
    , m_triangulate_command     {commands, "Geometry.Triangulate",               [this]() -> bool { triangulate     (); return true; } }
    , m_normalize_command       {commands, "Geometry.Normalize",                 [this]() -> bool { normalize       (); return true; } }
    , m_bake_transform_command  {commands, "Geometry.BakeTransform",             [this]() -> bool { bake_transform  (); return true; } }
    , m_center_transform_command{commands, "Geometry.CenterTransform",           [this]() -> bool { center_transform(); return true; } }
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

    , m_generate_tangents_command{commands, "Geometry.GenerateTangents",      [this]() -> bool { generate_tangents(); return true; } }
    , m_make_geometry_command    {commands, "Mesh.MakeGeometry",              [this]() -> bool { make_geometry(); return true; } }
    , m_make_raytrace_command    {commands, "Mesh.MakeRaytrace",              [this]() -> bool { make_raytrace(); return true; } }

    , m_export_gltf_command   {commands, "File.Export.glTF",                   [this]() -> bool { export_gltf   (); return true; } }
    , m_create_material       {commands, "Create.Material",                    [this]() -> bool { create_material(); return true; } }
{
    commands.register_command(&m_merge_command         );
    commands.register_command(&m_triangulate_command   );
    commands.register_command(&m_normalize_command     );
    commands.register_command(&m_bake_transform_command);
    commands.register_command(&m_center_transform_command);
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
    commands.register_command(&m_ambo_command    );
    commands.register_command(&m_truncate_command);
    commands.register_command(&m_gyro_command    );
    commands.register_command(&m_chamfer_command );

    commands.register_command(&m_generate_tangents_command );
    commands.register_command(&m_make_geometry_command );
    commands.register_command(&m_make_raytrace_command );

    commands.register_command(&m_export_gltf_command);
    commands.register_command(&m_create_material);

    commands.bind_command_to_menu(&m_merge_command,            "Geometry.Merge");
    commands.bind_command_to_menu(&m_triangulate_command,      "Geometry.Triangulate");
    commands.bind_command_to_menu(&m_normalize_command,        "Geometry.Normalize");
    commands.bind_command_to_menu(&m_bake_transform_command,   "Geometry.Bake-Transform");
    commands.bind_command_to_menu(&m_center_transform_command, "Geometry.Canter-Transform");
    commands.bind_command_to_menu(&m_reverse_command,          "Geometry.Reverse");
    commands.bind_command_to_menu(&m_repair_command,           "Geometry.Repair");
    commands.bind_command_to_menu(&m_weld_command,             "Geometry.Weld");

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

    commands.bind_command_to_menu(&m_generate_tangents_command, "Geometry.Generate Tangents");
    commands.bind_command_to_menu(&m_make_geometry_command,     "Mesh.Make Geometry");
    commands.bind_command_to_menu(&m_make_raytrace_command,     "Mesh.Make Raytrace");

    commands.bind_command_to_menu(&m_export_gltf_command, "File.Export glTF");
    commands.bind_command_to_menu(&m_create_material,     "Create.Material");

    const auto& ini = erhe::configuration::get_ini_file_section(erhe::c_erhe_config_file_path, "scene");
    ini.get("instance_count", m_make_mesh_config.instance_count);
    ini.get("instance_gap",   m_make_mesh_config.instance_gap);
    ini.get("object_scale",   m_make_mesh_config.object_scale);
    ini.get("detail",         m_make_mesh_config.detail);

    app_message_bus.add_receiver(
        [&](App_message& message) {
            on_message(message);
        }
    );
}

void Operations::on_message(App_message& message)
{
    if (erhe::utility::test_bit_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_scene_view)) {
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
    const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = m_context.selection->get_selected_items();
    std::size_t count = 0;
    for (const auto& item : selected_items) {
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
#if 0
        if (erhe::imgui::make_button("Cubes", erhe::imgui::Item_mode::normal, button_size)) {
            m_context.scene_builder->add_cubes(
                glm::ivec3{m_make_mesh_config.instance_count, m_make_mesh_config.instance_count, m_make_mesh_config.instance_count},
                m_make_mesh_config.object_scale,
                m_make_mesh_config.instance_gap
            );
        }
#endif
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

    Operation_stack& operation_stack = *m_context.operation_stack;
    Selection&       selection       = *m_context.selection;

    const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = selection.get_selected_items();

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
    const auto selected_node_count = count<erhe::scene::Node>(selected_items);
    const auto multi_select_meshes = (selected_mesh_count >= 2) ? erhe::imgui::Item_mode::normal : erhe::imgui::Item_mode::disabled;
    const auto multi_select_nodes  = (selected_node_count >= 2) ? erhe::imgui::Item_mode::normal : erhe::imgui::Item_mode::disabled;
    const auto delete_mode         = (selected_mesh_count + selected_node_count) > 0 ? erhe::imgui::Item_mode::normal : erhe::imgui::Item_mode::disabled;
    if (erhe::imgui::make_button("Delete", delete_mode, button_size)) {
        m_context.selection->delete_selection();
    }

    if (erhe::imgui::make_button("Attach", multi_select_nodes, button_size)) {
        const auto& node0 = get<erhe::scene::Node>(selected_items, 0);
        const auto& node1 = get<erhe::scene::Node>(selected_items, 1);
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
    if (make_button("Gen Tangents", has_selection_mode, button_size)) {
        generate_tangents();
    }
    if (make_button("Make Geometry", has_selection_mode, button_size)) {
        make_geometry();
    }
    if (make_button("Make Raytrace", has_selection_mode, button_size)) {
        make_raytrace();
    }
    if (make_button("Bake Transform", has_selection_mode, button_size)) {
        bake_transform();
    }
    if (make_button("Center Transform", has_selection_mode, button_size)) {
        center_transform();
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
}

void Operations::merge()
{
    /// tf::Executor& executor = m_context.operation_stack->get_executor();
    /// executor.silent_async(
    ///     [this]()
    ///     {
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
    ///     }
    /// );
}

void Operations::triangulate()
{
    async_for_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            m_context.operation_stack->queue(
                std::make_shared<Triangulate_operation>(std::move(mesh_operation_parameters))
            );
        }
    );
}

void Operations::normalize()
{
    async_for_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            m_context.operation_stack->queue(
                std::make_shared<Normalize_operation>(std::move(mesh_operation_parameters))
            );
        }
    );
}

void Operations::bake_transform()
{
    /// tf::Executor& executor = m_context.operation_stack->get_executor();
    /// executor.silent_async(
    ///     [this]()
    ///     {
            Compound_operation::Parameters compound_operation_parameters;
            // First: Transform geometries using node transforms
            compound_operation_parameters.operations.push_back(
                std::make_shared<Bake_transform_operation>(mesh_context())
            );
            // Second: Reset transform in all nodes
            const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = m_context.selection->get_selected_items();
            const std::vector<std::shared_ptr<erhe::scene::Node>> nodes = get_all<erhe::scene::Node>(selected_items);
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
    ///    }
    /// );
}

void Operations::center_transform()
{
    /// tf::Executor& executor = m_context.operation_stack->get_executor();
    /// executor.silent_async(
    ///     [this]()
    ///     {
            Compound_operation::Parameters compound_operation_parameters;

            // First: Transform geometries using node transforms
            Mesh_operation_parameters parameters = mesh_context();
            //std::unordered_map<uint64_t, glm::mat4> mesh_transform;
            parameters.make_entry_node_callback = [](erhe::scene::Node* node, Mesh_operation_parameters& parameters) {
                const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(node);
                if (!mesh) {
                    return;
                }
                const erhe::math::Aabb aabb_world  = mesh->get_aabb_world();
                const glm::vec3        aabb_center = aabb_world.center();
                parameters.transform = erhe::math::create_translation<float>(-aabb_center);
            };
            compound_operation_parameters.operations.push_back(
                std::make_shared<Bake_transform_operation>(std::move(parameters))
            );
            // Second: Reset transform in all nodes
            const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = m_context.selection->get_selected_items();
            const std::vector<std::shared_ptr<erhe::scene::Node>> nodes = get_all<erhe::scene::Node>(selected_items);
            for (const std::shared_ptr<erhe::scene::Node>& node : nodes) {
                const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(node.get());
                if (!mesh) {
                    return;
                }
                const erhe::math::Aabb aabb_world       = mesh->get_aabb_world();
                const glm::vec3        aabb_center      = aabb_world.center();
                const glm::mat4        offset_transform = erhe::math::create_translation<float>(aabb_center);

                const glm::mat4 world_from_node_before = node->world_from_node();
                const glm::mat4 world_from_node_after  = offset_transform * node->world_from_node();
                compound_operation_parameters.operations.push_back(
                    std::make_shared<Node_transform_operation>(
                        Node_transform_operation::Parameters{
                            .node                    = node,
                            .parent_from_node_before = node->parent_from_node_transform(),
                            .parent_from_node_after  = erhe::scene::Transform{node->parent_from_world() * world_from_node_after}
                        }
                    )
                );
            }
            m_context.operation_stack->queue(
                std::make_shared<Compound_operation>(std::move(compound_operation_parameters))
            );
    ///    }
    /// );
}

void Operations::reverse()
{
    async_for_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            m_context.operation_stack->queue(
                std::make_shared<Reverse_operation>(std::move(mesh_operation_parameters))
            );
        }
    );
}

void Operations::repair()
{
    async_for_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            m_context.operation_stack->queue(
                std::make_shared<Repair_operation>(std::move(mesh_operation_parameters))
            );
        }
    );
}

void Operations::weld()
{
    async_for_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            m_context.operation_stack->queue(
                std::make_shared<Weld_operation>(std::move(mesh_operation_parameters))
            );
        }
    );
}

void Operations::generate_tangents()
{
    async_for_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            m_context.operation_stack->queue(
                std::make_shared<Generate_tangents_operation>(std::move(mesh_operation_parameters))
            );
        }
    );
}
void Operations::make_geometry()
{
    async_for_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            Compound_operation::Parameters compound_parameters;
            for (const std::shared_ptr<erhe::Item_base>& item : mesh_operation_parameters.items) {
                std::shared_ptr<erhe::scene::Mesh> scene_mesh = erhe::scene::get_mesh(item);
                ERHE_VERIFY(scene_mesh);

                erhe::scene::Node*                              node              = scene_mesh->get_node();
                std::shared_ptr<Node_physics>                   node_physics      = get_node_physics(node);
                const std::vector<erhe::scene::Mesh_primitive>& primitives_before = scene_mesh->get_primitives();
                std::vector<erhe::scene::Mesh_primitive>        primitives_after  = primitives_before;

                for (erhe::scene::Mesh_primitive& mesh_primitive : primitives_after) {
                    erhe::primitive::Primitive& primitive = *mesh_primitive.primitive.get();
                    const bool geometry_ok = primitive.make_geometry();
                    if (!geometry_ok) {
                        continue;
                    }
                }

                Mesh_operation_parameters parameters = mesh_context();
                parameters.items.push_back(item);
                std::shared_ptr<Mesh_operation> mesh_operation = std::make_shared<Mesh_operation>(std::move(parameters));
                mesh_operation->add_entry(
                    Mesh_operation::Entry{
                        .scene_mesh = scene_mesh,
                        .before = {
                            .node_physics = node_physics,
                            .primitives   = primitives_before
                        },
                        .after = {
                            .node_physics = node_physics,
                            .primitives   = primitives_after
                        }
                    }
                );
                compound_parameters.operations.push_back(std::move(mesh_operation));
            }

            if (!compound_parameters.operations.empty()) {
                m_context.operation_stack->queue(
                    std::make_shared<Compound_operation>(std::move(compound_parameters))
                );
            }
        }
    );
}
void Operations::make_raytrace()
{
    async_for_nodes_with_mesh(
        [](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            for (const std::shared_ptr<erhe::Item_base>& item : mesh_operation_parameters.items) {
                std::shared_ptr<erhe::scene::Mesh> scene_mesh = erhe::scene::get_mesh(item);
                ERHE_VERIFY(scene_mesh);
                erhe::Item_host* item_host = item->get_item_host();
                ERHE_VERIFY(item_host != nullptr);
                Scene_root* scene_root = static_cast<Scene_root*>(item_host);
                std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root->item_host_mutex};

                scene_root->begin_mesh_rt_update(scene_mesh);
                std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = scene_mesh->get_mutable_primitives();
                for (erhe::scene::Mesh_primitive& mesh_primitive : mesh_primitives) {
                    erhe::primitive::Primitive& primitive = *mesh_primitive.primitive.get();
                    // Ensure raytrace exists
                    ERHE_VERIFY(primitive.make_raytrace());
                }
                scene_mesh->update_rt_primitives();
                scene_root->end_mesh_rt_update(scene_mesh);
            }
        }
    );
}

void Operations::difference()
{
    async_for_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            m_context.operation_stack->queue(
                std::make_shared<Difference_operation>(std::move(mesh_operation_parameters))
            );
        }
    );
}

void Operations::async_for_nodes_with_mesh(
    std::function<void(Mesh_operation_parameters&& parameters)> op
)
{
    // Locate item host
    erhe::Item_host* item_host = nullptr;
    {
        const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = m_context.selection->get_selected_items();
        for (const auto& item : selected_items) {
            if (item_host == nullptr) {
                item_host = item->get_item_host();
                break;
            }
        }
    }
    if (item_host == nullptr) {
        return;
    }
    Scene_root* scene_root = static_cast<Scene_root*>(item_host);

    // Grab item host lock. This is the master lock that prevents
    // concurrent access to
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root->item_host_mutex};

    // Gather items with mesh and their tasks
    std::vector<std::shared_ptr<erhe::Item_base>> items;
    std::vector<tf::AsyncTask> item_tasks;
    const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = m_context.selection->get_selected_items();
    for (const std::shared_ptr<erhe::Item_base>& item : selected_items) {
        const bool is_content = erhe::utility::test_bit_set(item->get_flag_bits(), erhe::Item_flags::content);
        if (!is_content) {
            continue;
        }

        const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        if (!node) {
            continue;
        }
        const erhe::scene::Node* raw_node = node.get();
        std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(raw_node);
        if (!mesh) {
            continue;
        }
        items.push_back(item);
        const tf::AsyncTask& previous_task = item->get_task();
        if (!previous_task.empty()) {
            item_tasks.push_back(item->get_task());
        }
    }

    if (items.empty()) {
        return;
    }

    tf::Executor& executor = m_context.operation_stack->get_executor();
    tf::AsyncTask task = executor.silent_dependent_async(
        [this, op, items]()
        {
            Mesh_operation_parameters parameters = mesh_context();
            parameters.items = items;
            op(std::move(parameters));
        },
        item_tasks.begin(), item_tasks.end()
    );

    for (const std::shared_ptr<erhe::Item_base>& item : items) {
        item->set_task(task);
    }
}

void Operations::intersection()
{
    async_for_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            m_context.operation_stack->queue(
                std::make_shared<Intersection_operation>(std::move(mesh_operation_parameters))
            );
        }
    );
}
void Operations::union_()
{
    async_for_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            m_context.operation_stack->queue(
                std::make_shared<Union_operation>(std::move(mesh_operation_parameters))
            );
        }
    );
}

void Operations::catmull_clark()
{
    async_for_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            m_context.operation_stack->queue(
                std::make_shared<Catmull_clark_subdivision_operation>(std::move(mesh_operation_parameters))
            );
        }
    );
}

void Operations::sqrt3()
{
    async_for_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            m_context.operation_stack->queue(
                std::make_shared<Sqrt3_subdivision_operation>(std::move(mesh_operation_parameters))
            );
        }
    );
}

void Operations::dual()
{
    async_for_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            m_context.operation_stack->queue(
                std::make_shared<Dual_operation>(std::move(mesh_operation_parameters))
            );
        }
    );
}

void Operations::join()
{
    async_for_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            m_context.operation_stack->queue(
                std::make_shared<Join_operation>(std::move(mesh_operation_parameters))
            );
        }
    );
}

void Operations::kis()
{
    async_for_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            m_context.operation_stack->queue(
                std::make_shared<Kis_operation>(std::move(mesh_operation_parameters))
            );
        }
    );
}

void Operations::meta()
{
    async_for_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            m_context.operation_stack->queue(
                std::make_shared<Meta_operation>(std::move(mesh_operation_parameters))
            );
        }
    );
}

void Operations::ortho()
{
    async_for_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            m_context.operation_stack->queue(
                std::make_shared<Subdivide_operation>(std::move(mesh_operation_parameters))
            );
        }
    );
}

void Operations::ambo()
{
    async_for_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            m_context.operation_stack->queue(
                std::make_shared<Ambo_operation>(std::move(mesh_operation_parameters))
            );
        }
    );
}

void Operations::truncate()
{
    async_for_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            m_context.operation_stack->queue(
                std::make_shared<Truncate_operation>(std::move(mesh_operation_parameters))
            );
        }
    );
}

void Operations::gyro()
{
    async_for_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            m_context.operation_stack->queue(
                std::make_shared<Gyro_operation>(std::move(mesh_operation_parameters))
            );
        }
    );
}

void Operations::chamfer()
{
    async_for_nodes_with_mesh(
        [this](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            m_context.operation_stack->queue(
                std::make_shared<Chamfer_operation>(std::move(mesh_operation_parameters))
            );
        }
    );
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

void Operations::create_material()
{
    std::shared_ptr<Scene_root> scene_root = m_last_hover_scene_view->get_scene_root();
    if (!scene_root) {
        return;
    }

    std::shared_ptr<Content_library>      content_library = scene_root->get_content_library();
    std::shared_ptr<Content_library_node> materials       = content_library->materials;

    std::shared_ptr<erhe::primitive::Material> new_material = std::make_shared<erhe::primitive::Material>(
        erhe::primitive::Material_create_info{
            .name = "New Material",
            .data = {
                .base_color = glm::vec3{0.5f, 0.5f, 0.5f},
                .roughness  = glm::vec2{0.5f, 0.5f},
                .metallic   = 1.0f
            }
        }
    );
    std::shared_ptr<Content_library_node> new_content_library_node = std::make_shared<Content_library_node>(new_material);

    std::shared_ptr<Item_insert_remove_operation> make_material_operation = std::make_shared<Item_insert_remove_operation>(
        Item_insert_remove_operation::Parameters{
            .context = m_context,
            .item    = new_content_library_node,
            .parent  = materials,
            .mode    = Item_insert_remove_operation::Mode::insert
        }
    );

    m_context.operation_stack->queue(make_material_operation);
}

}
