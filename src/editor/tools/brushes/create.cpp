#include "tools/brushes/create.hpp"

#include "editor_scenes.hpp"
#include "operations/insert_operation.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_commands.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/brushes/brush.hpp"
#include "tools/brushes/brush_tool.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "windows/content_library_window.hpp"

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
    const Render_context&         render_context,
    const erhe::scene::Transform& transform
)
{
    const auto* camera_node = render_context.get_camera_node();
    if (camera_node == nullptr)
    {
        return;
    }

    auto& line_renderer = *erhe::application::g_line_renderer_set->hidden.at(2).get();
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
        &camera_node->world_from_node_transform(),
        80
    );
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
    const Render_context&         render_context,
    const erhe::scene::Transform& transform
)
{
    const auto* camera_node = render_context.get_camera_node();
    if (camera_node == nullptr)
    {
        return;
    }

    auto& line_renderer = *erhe::application::g_line_renderer_set->hidden.at(2).get();
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
        camera_node->position_in_world(),
        80
    );
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
    const Render_context&         render_context,
    const erhe::scene::Transform& transform
)
{
    const auto* camera_node = render_context.get_camera_node();
    if (camera_node == nullptr)
    {
        return;
    }

    auto& line_renderer = *erhe::application::g_line_renderer_set->hidden.at(2).get();
    const glm::vec4 major_color    {1.0f, 1.0f, 1.0f, 1.0f};
    const glm::vec4 minor_color    {1.0f, 1.0f, 1.0f, 0.5f};
    const float     major_thickness{6.0f};
    line_renderer.add_torus(
        transform,
        major_color,
        minor_color,
        major_thickness,
        m_major_radius,
        m_minor_radius,
        m_use_debug_camera
            ? m_debug_camera
            : camera_node->position_in_world(),
        20,
        10,
        m_epsilon,
        m_debug_major,
        m_debug_minor
    );
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
    const Render_context&         render_context,
    const erhe::scene::Transform& transform
)
{
    if (render_context.scene_view == nullptr)
    {
        return;
    }

    const auto& view_camera = render_context.scene_view->get_camera();
    if (view_camera)
    {
        auto& line_renderer = *erhe::application::g_line_renderer_set->hidden.at(2).get();
        const glm::vec4 major_color    {1.0f, 1.0f, 1.0f, 1.0f};
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

Create* g_create{nullptr};

Create::Create()
    : erhe::components::Component    {c_type_name}
    , erhe::application::Imgui_window{c_title}
{
}

Create::~Create() noexcept
{
    ERHE_VERIFY(g_create == nullptr);
}

void Create::deinitialize_component()
{
    ERHE_VERIFY(g_create == this);
    m_brush_create = nullptr;
    m_brush.reset();
    g_create = nullptr;
}

void Create::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
    require<Tools>();
}

void Create::initialize_component()
{
    ERHE_PROFILE_FUNCTION
    ERHE_VERIFY(g_create == nullptr);

    erhe::application::g_imgui_windows->register_imgui_window(this);

    set_base_priority(c_priority);
    set_description  (c_title);
    set_flags        (Tool_flags::background);
    g_tools->register_tool(this);

    g_create = this;
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
        }
        else
        {
            m_brush_create = brush_create;
            m_brush_name = label;
        }
    }
}

auto Create::find_parent() -> std::shared_ptr<erhe::scene::Node>
{
    const auto selected_node   = g_selection_tool->get_first_selected_node();
    const auto selected_scene  = g_selection_tool->get_first_selected_scene();
    const auto viewport_window = g_viewport_windows->last_window();

    erhe::scene::Scene_host* scene_host = selected_node
        ? reinterpret_cast<Scene_root*>(selected_node->get_item_host())
        : selected_scene
            ? reinterpret_cast<Scene_root*>(selected_scene->get_root_node()->get_item_host())
            : viewport_window
                ? viewport_window->get_scene_root().get()
                : nullptr;
    if (scene_host == nullptr)
    {
        return {};
    }
    auto* scene_root = reinterpret_cast<Scene_root*>(scene_host);

    const auto parent = selected_node
        ? selected_node
        : scene_root->get_hosted_scene()->get_root_node();

    return parent;
}

void Create::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const auto parent = find_parent();
    if (!parent)
    {
        return;
    }

    Scene_root* scene_root = reinterpret_cast<Scene_root*>(parent->get_item_host());
    auto content_library = scene_root->content_library();

    const glm::mat4 world_from_node = parent->world_from_node();

    ImGui::Text("Nodes");
    if (ImGui::Button("Empty Node", button_size))
    {
        g_scene_commands->create_new_empty_node();
    }
    if (ImGui::Button("Camera", button_size))
    {
        g_scene_commands->create_new_camera();
    }
    if (ImGui::Button("Light", button_size))
    {
        g_scene_commands->create_new_light();
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
                .build_info      = g_mesh_memory->build_info,
                .normal_style    = m_normal_style,
                .density         = m_density,
                .physics_enabled = erhe::application::g_configuration->physics.static_enable
            };

            m_brush = m_brush_create->create(brush_create_info);
            if (m_brush && create_instance)
            {
                using Item_flags = erhe::scene::Item_flags;
                const uint64_t node_flags =
                    Item_flags::visible     |
                    Item_flags::content     |
                    Item_flags::show_in_ui;
                const uint64_t mesh_flags =
                    Item_flags::visible     |
                    Item_flags::content     |
                    Item_flags::shadow_cast |
                    Item_flags::id          |
                    Item_flags::show_in_ui;

                const Instance_create_info brush_instance_create_info
                {
                    .node_flags       = node_flags,
                    .mesh_flags       = mesh_flags,
                    .scene_root       = scene_root,
                    .world_from_node  = world_from_node,
                    .material         = g_content_library_window->selected_material(),
                    .scale            = 1.0
                };
                const auto instance_node = m_brush->make_instance(brush_instance_create_info);

                auto op = std::make_shared<Node_insert_remove_operation>(
                    Node_insert_remove_operation::Parameters{
                        .node   = instance_node,
                        .parent = parent,
                        .mode   = Scene_item_operation::Mode::insert
                    }
                );
                g_operation_stack->push(op);
            }
            m_brush_create = nullptr;
        }
        if (m_brush && create_brush)
        {
            content_library->brushes.add(m_brush);
            m_brush.reset();
        }
    }

    {
        const auto& selection = g_selection_tool->selection();
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
                        .build_info      = g_mesh_memory->build_info,
                        .normal_style    = m_normal_style,
                        .geometry        = source_geometry,
                        .density         = m_density,
                        .physics_enabled = erhe::application::g_configuration->physics.static_enable
                    };
                    //// source_geometry->build_edges();
                    //// source_geometry->compute_polygon_normals();
                    //// source_geometry->compute_tangents();
                    //// source_geometry->compute_polygon_centroids();
                    //// source_geometry->compute_point_normals(erhe::geometry::c_point_normals_smooth);
                    content_library->brushes.make(brush_create_info);
                }
            }
        }
    }

#endif
}

void Create::tool_render(const Render_context& context)
{
    const auto parent = find_parent();
    if (!parent)
    {
        return;
    }

    Scene_root* scene_root = reinterpret_cast<Scene_root*>(parent->get_item_host());
    if (context.get_scene() != scene_root->get_hosted_scene())
    {
        return;
    }

    const erhe::scene::Transform transform = parent
        ? parent->world_from_node_transform()
        : erhe::scene::Transform{};
    if (m_brush_create != nullptr)
    {
        m_brush_create->render_preview(context, transform);
    }
}

} // namespace editor
