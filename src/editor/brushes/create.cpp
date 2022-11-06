#include "brushes/create.hpp"

#include "editor_scenes.hpp"
#include "brushes/brush.hpp"
#include "brushes/brushes.hpp"
#include "operations/insert_operation.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_view.hpp"
#include "tools/tools.hpp"
#include "tools/selection_tool.hpp"
#include "windows/materials_window.hpp"

#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/shapes/sphere.hpp"
#include "erhe/physics/icollision_shape.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

void Create_uv_sphere::render_preview(
    const Render_context&                 render_context,
    erhe::application::Line_renderer_set& line_renderer_set,
    const erhe::scene::Transform&         transform
)
{
    if (!m_preview)
    {
        return;
    }

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

[[nodiscard]] auto Create_uv_sphere::imgui(
    const Brush_create_context& context
) -> std::shared_ptr<Brush>
{
    ImGui::Checkbox   ("Preview", &m_preview);
    ImGui::SliderFloat("Radius", &m_radius,      0.0f, 10.0f);
    ImGui::SliderInt  ("Slices", &m_slice_count, 1, 100);
    ImGui::SliderInt  ("Stacks", &m_stack_count, 1, 100);
    if (ImGui::Button("Create"))
    {
        const auto geometry = std::make_shared<erhe::geometry::Geometry>(
            std::move(
                erhe::geometry::shapes::make_sphere(
                    m_radius,
                    std::max(1, m_slice_count), // slice count
                    std::max(1, m_stack_count)  // stack count
                )
            )
        );

        geometry->build_edges();
        geometry->compute_polygon_normals();
        geometry->compute_tangents();
        geometry->compute_polygon_centroids();
        geometry->compute_point_normals(erhe::geometry::c_point_normals_smooth);

        const auto collision_shape = erhe::physics::ICollision_shape::create_sphere_shape_shared(m_radius);

        std::shared_ptr<Brush> brush = std::make_shared<Brush>(context.build_info);
        brush->initialize(
            Brush::Create_info{
                .geometry        = geometry,
                .build_info      = context.build_info,
                .normal_style    = context.normal_style,
                .density         = context.density,
                .volume          = geometry->get_mass_properties().volume,
                .collision_shape = collision_shape
            }
        );
        return brush;
    }
    return {};
}

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
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
    get<Tools>()->register_background_tool(this);
}

void Create::post_initialize()
{
    m_line_renderer_set = get<erhe::application::Line_renderer_set>();
    m_brushes          = get<Brushes         >();
    m_editor_scenes    = get<Editor_scenes   >();
    m_materials_window = get<Materials_window>();
    m_mesh_memory      = get<Mesh_memory     >();
    m_operation_stack  = get<Operation_stack >();
    m_selection_tool   = get<Selection_tool  >();
}

void Create::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const Brush_create_context context{
        .build_info   = m_mesh_memory->build_info,
        .normal_style = erhe::primitive::Normal_style::corner_normals,
        .density      = 1.0f
    };

    const auto parent = m_selection_tool->selection().empty()
        ? std::shared_ptr<erhe::scene::Node>()
        : m_selection_tool->selection().front();
    const uint64_t visibility_flags =
        erhe::scene::Node_visibility::visible     |
        erhe::scene::Node_visibility::content     |
        erhe::scene::Node_visibility::shadow_cast |
        erhe::scene::Node_visibility::id;

    Scene_root* scene_root = parent
        ? reinterpret_cast<Scene_root*>(parent->node_data.host)
        : m_editor_scenes->get_current_scene_root().get();
    if (scene_root == nullptr)
    {
        return;
    }

    const glm::mat4 world_from_node = parent
        ? parent->world_from_node()
        : glm::mat4{1.0f};

    const Instance_create_info brush_instance_create_info
    {
        .node_visibility_flags = visibility_flags,
        .scene_root            = scene_root,
        .world_from_node       = world_from_node,
        .material              = m_materials_window->selected_material(),
        .scale                 = 1.0
    };
    if (ImGui::Button("Empty Node"))
    {
        auto new_empty_node = scene_root->create_new_empty_node();
        new_empty_node->set_name("new empty node");
        get<Operation_stack>()->push(
            std::make_shared<Node_insert_remove_operation>(
                Node_insert_remove_operation::Parameters{
                    .scene_root = scene_root,
                    .node       = new_empty_node,
                    .parent     = parent,
                    .mode       = Scene_item_operation::Mode::insert
                }
            )
        );
    }
    if (ImGui::TreeNodeEx("UV Sphere", ImGuiTreeNodeFlags_Framed))
    {
        const auto brush = m_create_uv_sphere.imgui(context);
        if (brush)
        {
            const auto instance = brush->make_instance(brush_instance_create_info);

            auto op = std::make_shared<Mesh_insert_remove_operation>(
                Mesh_insert_remove_operation::Parameters{
                    .scene_root     = scene_root,
                    .mesh           = instance.mesh,
                    .node_physics   = instance.node_physics,
                    .node_raytrace  = instance.node_raytrace,
                    .parent         = parent,
                    .mode           = Scene_item_operation::Mode::insert
                }
            );
            m_operation_stack->push(op);
        }
        ImGui::TreePop();
    }
    //// if (ImGui::TreeNodeEx("Cone", ImGuiTreeNodeFlags_Framed))
    //// {
    ////     ImGui::TreePop();
    //// }
    //// if (ImGui::TreeNodeEx("Cylinder", ImGuiTreeNodeFlags_Framed))
    //// {
    ////     ImGui::TreePop();
    //// }
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
    m_create_uv_sphere.render_preview(
        context,
        *m_line_renderer_set.get(),
        transform
    );
}

} // namespace editor
