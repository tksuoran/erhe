#include "brushes/create.hpp"

#include "editor_scenes.hpp"
#include "brushes/brush.hpp"
#include "brushes/brushes.hpp"
#include "operations/insert_operation.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_commands.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/tools.hpp"
#include "tools/selection_tool.hpp"
#include "windows/materials_window.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/shapes/box.hpp"
#include "erhe/geometry/shapes/cone.hpp"
#include "erhe/geometry/shapes/sphere.hpp"
#include "erhe/geometry/shapes/torus.hpp"
#include "erhe/physics/icollision_shape.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#   include <imgui/misc/cpp/imgui_stdlib.h>
#endif

namespace editor
{

void Create_uv_sphere::render_preview(
    const Render_context&                 render_context,
    erhe::application::Line_renderer_set& line_renderer_set,
    const erhe::scene::Transform&         transform
)
{
    if (render_context.scene_view == nullptr)
    {
        return;
    }

    const auto& view_camera = render_context.scene_view->get_camera();
    if (view_camera)
    {
        //const erhe::scene::Transform& camera_world_from_node_transform = view_camera->world_from_node_transform();

        auto& line_renderer = *line_renderer_set.hidden.at(2).get();
        const glm::vec4 edge_color            {1.0f, 1.0f, 1.0f, 1.0f};
        const glm::vec4 great_circle_color    {0.5f, 0.5f, 0.5f, 0.5f};
        const float     edge_thickness        {6.0f};
        const float     great_circle_thickness{4.0f};
        line_renderer.add_sphere(
            transform,
            edge_color,
            great_circle_color,
            edge_thickness,
            great_circle_thickness,
            glm::vec3{0.0f, 0.0f,0.0f},
            m_radius,
            &view_camera->world_from_node_transform(),
            80
        );
    }
}

void Create_uv_sphere::imgui()
{
    ImGui::Text("Sphere Parameters");

    ImGui::SliderFloat("Radius",  &m_radius,      0.0f, 4.0f);
    ImGui::SliderInt  ("Slices",  &m_slice_count, 1, 100);
    ImGui::SliderInt  ("Stacks",  &m_stack_count, 1, 100);
}

[[nodiscard]] auto Create_uv_sphere::create(
    Brush_data& brush_create_info
) const -> std::shared_ptr<Brush>
{
    brush_create_info.geometry = std::make_shared<erhe::geometry::Geometry>(
        erhe::geometry::shapes::make_sphere(
            m_radius,
            std::max(1, m_slice_count), // slice count
            std::max(1, m_stack_count)  // stack count
        )
    );

    brush_create_info.geometry->build_edges();
    brush_create_info.geometry->compute_polygon_normals();
    brush_create_info.geometry->compute_tangents();
    brush_create_info.geometry->compute_polygon_centroids();
    brush_create_info.geometry->compute_point_normals(erhe::geometry::c_point_normals_smooth);
    brush_create_info.collision_shape = erhe::physics::ICollision_shape::create_sphere_shape_shared(m_radius);

    std::shared_ptr<Brush> brush = std::make_shared<Brush>(brush_create_info);
    return brush;
}

////
////

void Create_cone::render_preview(
    const Render_context&                 render_context,
    erhe::application::Line_renderer_set& line_renderer_set,
    const erhe::scene::Transform&         transform
)
{
    if (render_context.scene_view == nullptr)
    {
        return;
    }

    const auto& view_camera = render_context.scene_view->get_camera();
    if (view_camera)
    {
        //const erhe::scene::Transform& camera_world_from_node_transform = view_camera->world_from_node_transform();

        auto& line_renderer = *line_renderer_set.hidden.at(2).get();
        const glm::vec4 major_color    {1.0f, 1.0f, 1.0f, 1.0f};
        const glm::vec4 minor_color    {1.0f, 1.0f, 1.0f, 0.5f};
        const float     major_thickness{6.0f};
        const float     minor_thickness{3.0f};
        line_renderer.add_cone(
            transform,
            major_color,
            minor_color,
            major_thickness,
            minor_thickness,
            glm::vec3{0.0f, 0.0f,0.0f},
            m_height,
            m_bottom_radius,
            m_top_radius,
            view_camera->position_in_world(),
            80
        );
    }
}

void Create_cone::imgui()
{
    ImGui::Text("Cone Parameters");

    ImGui::SliderFloat("Height",        &m_height ,       0.0f, 3.0f);
    ImGui::SliderFloat("Bottom Radius", &m_bottom_radius, 0.0f, 3.0f);
    ImGui::SliderFloat("Top Radius",    &m_top_radius,    0.0f, 3.0f);
    ImGui::SliderInt  ("Slices",        &m_slice_count,   1, 40);
    ImGui::SliderInt  ("Stacks",        &m_stack_count,   1, 6);
}

[[nodiscard]] auto Create_cone::create(
    Brush_data& brush_create_info
) const -> std::shared_ptr<Brush>
{
    brush_create_info.geometry = std::make_shared<erhe::geometry::Geometry>(
        erhe::geometry::shapes::make_conical_frustum(
            0.0f,
            m_height,
            m_bottom_radius,
            m_top_radius,
            true,
            true,
            std::max(3, m_slice_count), // slice count
            std::max(1, m_stack_count)  // stack count
        )
    );

    brush_create_info.geometry->transform(erhe::toolkit::mat4_swap_xy);
    brush_create_info.geometry->build_edges();
    brush_create_info.geometry->compute_polygon_normals();
    brush_create_info.geometry->compute_tangents();
    brush_create_info.geometry->compute_polygon_centroids();
    brush_create_info.geometry->compute_point_normals(erhe::geometry::c_point_normals_smooth);

    std::shared_ptr<Brush> brush = std::make_shared<Brush>(brush_create_info);
    return brush;
}

////
////

void Create_torus::render_preview(
    const Render_context&                 render_context,
    erhe::application::Line_renderer_set& line_renderer_set,
    const erhe::scene::Transform&         transform
)
{
    if (render_context.scene_view == nullptr)
    {
        return;
    }

    const auto& view_camera = render_context.scene_view->get_camera();
    if (view_camera)
    {
        //const erhe::scene::Transform& camera_world_from_node_transform = view_camera->world_from_node_transform();

        auto& line_renderer = *line_renderer_set.hidden.at(2).get();
        const glm::vec4 major_color    {1.0f, 1.0f, 1.0f, 1.0f};
        const glm::vec4 minor_color    {1.0f, 1.0f, 1.0f, 0.5f};
        const float     major_thickness{6.0f};
        //const float     minor_thickness{3.0f};
        line_renderer.add_torus(
            transform,
            major_color,
            minor_color,
            major_thickness,
            //minor_thickness,
            //glm::vec3{0.0f, 0.0f, 0.0f},
            m_major_radius,
            m_minor_radius,
            m_use_debug_camera
                ? m_debug_camera
                : view_camera->position_in_world(),
            20,
            10,
            m_epsilon,
            m_debug_major,
            m_debug_minor
        );
    }
}

void Create_torus::imgui()
{
    ImGui::Text("Torus Parameters");

    ImGui::SliderFloat("Major Radius", &m_major_radius, 0.0f, 3.0f);
    ImGui::SliderFloat("Minor Radius", &m_minor_radius, 0.0f, m_major_radius);
    ImGui::SliderInt  ("Major Steps",  &m_major_steps,  3, 40);
    ImGui::SliderInt  ("Minor Steps",  &m_minor_steps,  3, 40);

    ImGui::Separator();

    ImGui::Checkbox    ("Use Debug Camera", &m_use_debug_camera);
    ImGui::SliderFloat3("Debug Camera",     &m_debug_camera.x, -4.0f, 4.0f);
    ImGui::SliderInt   ("Debug Major",      &m_debug_major,     0, 100);
    ImGui::SliderInt   ("Debug Minor",      &m_debug_minor,     0, 100);
    ImGui::SliderFloat ("Epsilon",          &m_epsilon,         0.0f, 1.0f);
}

[[nodiscard]] auto Create_torus::create(
    Brush_data& brush_create_info
) const -> std::shared_ptr<Brush>
{
    brush_create_info.geometry = std::make_shared<erhe::geometry::Geometry>(
        erhe::geometry::shapes::make_torus(
            m_major_radius,
            m_minor_radius,
            m_major_steps,
            m_minor_steps
        )
    );

    brush_create_info.geometry->transform(erhe::toolkit::mat4_swap_yz);
    brush_create_info.geometry->build_edges();
    brush_create_info.geometry->compute_polygon_normals();
    brush_create_info.geometry->compute_tangents();
    brush_create_info.geometry->compute_polygon_centroids();
    brush_create_info.geometry->compute_point_normals(erhe::geometry::c_point_normals_smooth);

    std::shared_ptr<Brush> brush = std::make_shared<Brush>(brush_create_info);
    return brush;
}

////
////

void Create_box::render_preview(
    const Render_context&                 render_context,
    erhe::application::Line_renderer_set& line_renderer_set,
    const erhe::scene::Transform&         transform
)
{
    if (render_context.scene_view == nullptr)
    {
        return;
    }

    const auto& view_camera = render_context.scene_view->get_camera();
    if (view_camera)
    {
        //const erhe::scene::Transform& camera_world_from_node_transform = view_camera->world_from_node_transform();

        auto& line_renderer = *line_renderer_set.hidden.at(2).get();
        const glm::vec4 major_color    {1.0f, 1.0f, 1.0f, 1.0f};
        //const glm::vec4 minor_color    {1.0f, 1.0f, 1.0f, 0.5f};
        //const float     major_thickness{6.0f};
        //const float     minor_thickness{3.0f};
        line_renderer.add_cube(
            transform.matrix(),
            major_color,
            -0.5f * m_size,
             0.5f * m_size
        );
    }
}

void Create_box::imgui()
{
    ImGui::Text("Box Parameters");

    ImGui::SliderFloat3("Size",    &m_size.x, 0.0f, 10.0f);
    ImGui::SliderInt3  ("Steps",   &m_steps.x, 1, 10);
    ImGui::SliderFloat ("Power",   &m_power, 0.0f, 10.0f);
}

[[nodiscard]] auto Create_box::create(
    Brush_data& brush_create_info
) const -> std::shared_ptr<Brush>
{
    brush_create_info.geometry = std::make_shared<erhe::geometry::Geometry>(
        erhe::geometry::shapes::make_box(
            m_size,
            m_steps,
            m_power
        )
    );

    //brush_create_info.geometry->transform(erhe::toolkit::mat4_swap_xy);
    brush_create_info.geometry->build_edges();
    brush_create_info.geometry->compute_polygon_normals();
    brush_create_info.geometry->compute_tangents();
    brush_create_info.geometry->compute_polygon_centroids();
    brush_create_info.geometry->compute_point_normals(erhe::geometry::c_point_normals_smooth);
    //// brush_create_info.collision_shape = TODO

    std::shared_ptr<Brush> brush = std::make_shared<Brush>(brush_create_info);
    return brush;
}

////
////

Create::Create()
    : erhe::components::Component    {c_type_name}
    , erhe::application::Imgui_window{c_title}
{
}

Create::~Create() noexcept
{
}

void Create::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
    require<Tools>();
}

void Create::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
    get<Tools>()->register_background_tool(this);
}

void Create::post_initialize()
{
    m_line_renderer_set = get<erhe::application::Line_renderer_set>();
    m_brushes           = get<Brushes         >();
    m_editor_scenes     = get<Editor_scenes   >();
    m_materials_window  = get<Materials_window>();
    m_mesh_memory       = get<Mesh_memory     >();
    m_operation_stack   = get<Operation_stack >();
    m_scene_commands    = get<Scene_commands  >();
    m_selection_tool    = get<Selection_tool  >();
}

namespace
{

constexpr ImVec2 button_size{110.0f, 0.0f};

}

void Create::brush_create_button(const char* label, Brush_create* brush_create)
{
    if (ImGui::Button(label, button_size))
    {
        if (m_brush_create == brush_create)
        {
            m_brush_create = nullptr;
        } else {
            m_brush_create = brush_create;
            m_brush_name = label;
        }
    }
}

void Create::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const auto viewport_window = get<Viewport_windows>()->last_window();
    if (!viewport_window)
    {
        return;
    }

    const auto selection_front = m_selection_tool->selection().empty()
        ? std::shared_ptr<erhe::scene::Node>()
        : m_selection_tool->selection().front();

    erhe::scene::Scene_host* scene_host = selection_front
        ? reinterpret_cast<Scene_root*>(selection_front->node_data.host)
        : viewport_window->get_scene_root().get();
    if (scene_host == nullptr)
    {
        return;
    }
    Scene_root* scene_root = reinterpret_cast<Scene_root*>(scene_host);
    ERHE_VERIFY(scene_root != nullptr);

    const auto parent = selection_front
        ? selection_front
        : scene_root->get_scene()->root_node;

    ERHE_VERIFY(parent);

    const glm::mat4 world_from_node = parent->world_from_node();

    ImGui::Text("Nodes");
    if (ImGui::Button("Empty Node", button_size))
    {
        m_scene_commands->create_new_empty_node();
    }
    if (ImGui::Button("Camera", button_size))
    {
        m_scene_commands->create_new_camera();
    }
    if (ImGui::Button("Light", button_size))
    {
        m_scene_commands->create_new_light();
    }

    ImGui::Separator();
    ImGui::Text("Meshes");
    brush_create_button("UV Sphere", &m_create_uv_sphere);
    brush_create_button("Cone",      &m_create_cone);
    brush_create_button("Torus",     &m_create_torus);
    brush_create_button("Box",       &m_create_box);

    if (m_brush_create != nullptr)
    {
        m_brush_create->imgui();
        erhe::application::make_combo(
            "Normal Style",
            m_normal_style,
            erhe::primitive::c_normal_style_strings,
            IM_ARRAYSIZE(erhe::primitive::c_normal_style_strings)
        );
        const bool create_instance = ImGui::Button("Create Instance", button_size);
        ImGui::InputText("Brush Name", &m_brush_name);
        const bool create_brush    = ImGui::Button("Create Brush", button_size);
        if (create_instance || create_brush)
        {
            Brush_data brush_create_info{
                .name            = m_brush_name,
                .build_info      = m_mesh_memory->build_info,
                .normal_style    = m_normal_style,
                .density         = m_density,
                .physics_enabled = get<erhe::application::Configuration>()->physics.static_enable
            };

            m_brush = m_brush_create->create(brush_create_info);
            if (m_brush && create_instance)
            {
                const uint64_t visibility_flags =
                    erhe::scene::Node_visibility::visible     |
                    erhe::scene::Node_visibility::content     |
                    erhe::scene::Node_visibility::shadow_cast |
                    erhe::scene::Node_visibility::id;

                const Instance_create_info brush_instance_create_info
                {
                    .node_visibility_flags = visibility_flags,
                    .scene_root            = scene_root,
                    .world_from_node       = world_from_node,
                    .material              = m_materials_window->selected_material(),
                    .scale                 = 1.0
                };
                const auto instance = m_brush->make_instance(brush_instance_create_info);
                instance.mesh->mesh_data.layer_id = scene_root->layers().content()->id.get_id();

                auto op = std::make_shared<Node_insert_remove_operation>(
                    Node_insert_remove_operation::Parameters{
                        .selection_tool = m_selection_tool.get(),
                        .node           = instance.mesh,
                        .parent         = parent,
                        .mode           = Scene_item_operation::Mode::insert
                    }
                );
                m_operation_stack->push(op);
            }
        }
        if (m_brush && create_brush)
        {
            m_brushes->register_brush(m_brush);
            m_brush.reset();
        }
    }

    {
        const auto& selection = m_selection_tool->selection();
        if (!selection.empty())
        {
            std::shared_ptr<erhe::geometry::Geometry> source_geometry;
            for (const auto& node : selection)
            {
                const auto& mesh = as_mesh(node);
                if (mesh)
                {
                    for (const auto& primitive : mesh->mesh_data.primitives)
                    {
                        if (primitive.source_geometry)
                        {
                            source_geometry = primitive.source_geometry;
                            break;
                        }
                    }
                    if (source_geometry)
                    {
                        break;
                    }
                }
            }
            if (source_geometry)
            {
                if (m_brush_create == nullptr)
                {
                    erhe::application::make_combo(
                        "Normal Style",
                        m_normal_style,
                        erhe::primitive::c_normal_style_strings,
                        IM_ARRAYSIZE(erhe::primitive::c_normal_style_strings)
                    );
                    ImGui::InputText("Brush Name", &m_brush_name);
                }

                ImGui::Text("Selected Primitive: %s", source_geometry->name.c_str());
                if (ImGui::Button("Selected Mesh to Brush"))
                {
                    Brush_data brush_create_info{
                        .name            = m_brush_name,
                        .build_info      = m_mesh_memory->build_info,
                        .normal_style    = m_normal_style,
                        .geometry        = source_geometry,
                        .density         = m_density,
                        .physics_enabled = get<erhe::application::Configuration>()->physics.static_enable
                    };
                    static_cast<void>(m_brushes->make_brush(brush_create_info));
                }
            }
        }
    }

#endif
}

void Create::tool_render(const Render_context& context)
{
    const auto parent = m_selection_tool->selection().empty()
        ? std::shared_ptr<erhe::scene::Node>()
        : m_selection_tool->selection().front();
    const erhe::scene::Transform transform = parent
        ? parent->world_from_node_transform()
        : erhe::scene::Transform{};
    if (m_brush_create != nullptr)
    {
        m_brush_create->render_preview(context, *m_line_renderer_set.get(), transform);
    }
}

} // namespace editor
