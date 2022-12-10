
#include "brushes/brush_tool.hpp"
#include "brushes/brush.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "operations/insert_operation.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/render_context.hpp"
#include "scene/material_library.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/grid_tool.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "windows/content_library_window.hpp"
#include "windows/operations.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/renderers/text_renderer.hpp"
#include "erhe/application/view.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/operation/clone.hpp"
#include "erhe/physics/icollision_shape.hpp"
#include "erhe/primitive/enums.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/scene_host.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"

#include <glm/gtx/transform.hpp>

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

using glm::mat3;
using glm::mat4;
using glm::vec3;
using glm::vec4;
using erhe::geometry::Polygon; // Resolve conflict with wingdi.h BOOL Polygon(HDC,const POINT *,int)

auto Brush_tool_preview_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    static_cast<void>(context);

    if (
        (get_command_state() != erhe::application::State::Active) ||
        !m_brush_tool.is_enabled()
    )
    {
        return false;
    }
    m_brush_tool.on_motion();
    return true;
}

void Brush_tool_insert_command::try_ready(
    erhe::application::Command_context& context
)
{
    if (m_brush_tool.try_insert_ready())
    {
        set_ready(context);
    }
}

auto Brush_tool_insert_command::try_call(
    erhe::application::Command_context& command_context
) -> bool
{
    if (get_command_state() != erhe::application::State::Ready)
    {
        return false;
    }
    const bool consumed = m_brush_tool.try_insert();
    set_inactive(command_context);
    return consumed;
}

Brush_tool::Brush_tool()
    : erhe::components::Component{c_type_name}
    , m_preview_command          {*this}
    , m_insert_command           {*this}
{
}

Brush_tool::~Brush_tool() noexcept
{
}

void Brush_tool::declare_required_components()
{
    require<erhe::application::Commands>();
    m_configuration = require<erhe::application::Configuration>();
    require<Editor_message_bus>();
    require<Editor_scenes     >();
    require<Operations        >();
    require<Tools             >();
}

void Brush_tool::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    const auto& tools = get<Tools>();
    tools->register_tool(this);

    const auto commands = get<erhe::application::Commands>();
    commands->register_command(&m_preview_command);
    commands->register_command(&m_insert_command);
    commands->bind_command_to_update                  (&m_preview_command);
    commands->bind_command_to_mouse_click             (&m_insert_command, erhe::toolkit::Mouse_button_right);
    commands->bind_command_to_controller_trigger_click(&m_insert_command);

    get<Operations>()->register_active_tool(this);

    get<Editor_message_bus>()->add_receiver(
        [&](Editor_message& message)
        {
            on_message(message);
        }
    );
}

void Brush_tool::post_initialize()
{
    m_line_renderer_set      = get<erhe::application::Line_renderer_set>();
    m_text_renderer          = get<erhe::application::Text_renderer    >();
    m_content_library_window = get<Content_library_window>();
    m_editor_scenes          = get<Editor_scenes         >();
    m_grid_tool              = get<Grid_tool             >();
    m_operation_stack        = get<Operation_stack       >();
    m_selection_tool         = get<Selection_tool        >();
    m_viewport_windows       = get<Viewport_windows      >();
}

void Brush_tool::on_message(Editor_message& message)
{
    using namespace erhe::toolkit;
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_scene_view))
    {
        remove_brush_mesh();
        m_scene_view = message.scene_view;
    }
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover))
    {
        on_motion();
    }
}

void Brush_tool::remove_brush_mesh()
{
    if (m_brush_node)
    {
        // Do not remove this. It is not possible to rely on reset calling destructor,
        // because parent will have shared_ptr to the child.
        m_brush_node->set_parent({});
        m_brush_node.reset();
    }
    if (m_brush_mesh)
    {
        m_brush_mesh.reset();
    }
}

auto Brush_tool::try_insert_ready() -> bool
{
    return is_enabled() && (m_hover.mesh || (m_hover.grid != nullptr));
}

auto Brush_tool::try_insert() -> bool
{
    auto brush = m_content_library_window->selected_brush();
    if (
        !m_brush_node ||
        !m_brush_mesh ||
        !m_hover.position.has_value() ||
        !brush
    )
    {
        return false;
    }

        get<Editor_scenes>()->sanity_check();

    do_insert_operation();

        get<Editor_scenes>()->sanity_check();

    remove_brush_mesh();

        get<Editor_scenes>()->sanity_check();

    return true;
}

void Brush_tool::on_enable_state_changed()
{
    const auto& commands = get<erhe::application::Commands>();
    erhe::application::Command_context command_context{
        *commands.get()
    };

    if (is_enabled())
    {
        m_preview_command.set_active(command_context);
        on_motion();
    }
    else
    {
        m_preview_command.set_inactive(command_context);
        m_hover = Hover_entry{};
        remove_brush_mesh();
    }
}

void Brush_tool::on_motion()
{
    if (m_scene_view == nullptr)
    {
        return;
    }

    m_hover = m_scene_view->get_nearest_hover(
        Hover_entry::content_bit |
        Hover_entry::grid_bit
    );

    if (
        (m_hover.geometry != nullptr) &&
        (m_hover.local_index > m_hover.geometry->get_polygon_count())
    )
    {
        m_hover.local_index = 0;
        m_hover.geometry    = nullptr;
    }

    if (
        m_hover.mesh &&
        (m_hover.mesh->get_node() != nullptr) &&
        m_hover.position.has_value()
    )
    {
        m_hover.position = m_hover.mesh->get_node()->transform_direction_from_world_to_local(m_hover.position.value());
    }

    update_mesh();
}

// Returns transform which places brush in parent (hover) mesh space.
auto Brush_tool::get_hover_mesh_transform() -> mat4
{
    auto brush = m_content_library_window->selected_brush();

    if (
        (m_hover.mesh == nullptr) ||
        (m_hover.geometry == nullptr) ||
        (m_hover.local_index >= m_hover.geometry->get_polygon_count())
    )
    {
        return mat4{1};
    }

    const auto polygon_id = static_cast<erhe::geometry::Polygon_id>(m_hover.local_index);
    const Polygon&  polygon    = m_hover.geometry->polygons[polygon_id];
    Reference_frame hover_frame(*m_hover.geometry, polygon_id, 0, 0);

    Reference_frame brush_frame = brush->get_reference_frame(
        polygon.corner_count,
        static_cast<uint32_t>(m_polygon_offset),
        static_cast<uint32_t>(m_corner_offset)
    );
    hover_frame.N *= -1.0f;
    hover_frame.B *= -1.0f;

    ERHE_VERIFY(brush_frame.scale() != 0.0f);

    const float hover_scale = hover_frame.scale();
    const float brush_scale = brush_frame.scale();
    const float scale       = hover_scale / brush_scale;

    m_transform_scale = m_scale * scale;
    if (m_transform_scale != 1.0f)
    {
        const mat4 scale_transform = erhe::toolkit::create_scale(m_transform_scale);
        brush_frame.transform_by(scale_transform);
    }

    if (
        !m_snap_to_hover_polygon &&
        m_hover.position.has_value()
    )
    {
        hover_frame.centroid = m_hover.position.value();
    }

    const mat4 hover_transform = hover_frame.transform();
    const mat4 brush_transform = brush_frame.transform();
    const mat4 inverse_brush   = inverse(brush_transform);
    const mat4 align           = hover_transform * inverse_brush;
    return align;
}

auto Brush_tool::get_hover_grid_transform() -> mat4
{
    if (m_hover.grid == nullptr)
    {
        return mat4{1};
    }

    m_transform_scale = m_scale;
    auto brush = m_content_library_window->selected_brush();
    Reference_frame brush_frame = brush->get_reference_frame(
        static_cast<uint32_t>(m_polygon_offset),
        static_cast<uint32_t>(m_corner_offset)
    );
    const mat4 scale_transform = erhe::toolkit::create_scale(m_scale);
    brush_frame.transform_by(scale_transform);
    const mat4 brush_transform = brush_frame.transform();
    const mat4 inverse_brush   = inverse(brush_transform);

    const glm::dvec3 position_in_grid0 = m_hover.grid->grid_from_world() * glm::dvec4{m_hover.position.value(), 1.0};
    const glm::dvec3 position_in_grid  = m_snap_to_grid
        ? m_hover.grid->snap_grid_position(position_in_grid0)
        : position_in_grid0;
    const glm::dmat4 offset            = erhe::toolkit::create_translation<double>(position_in_grid);
    const glm::dmat4 world_from_grid   = m_hover.grid->world_from_grid() * offset;

    const mat4 align = mat4{world_from_grid} * inverse_brush;
    return align;
}

void Brush_tool::update_mesh_node_transform()
{
    auto brush = m_content_library_window->selected_brush();
    if (
        !brush ||
        !m_hover.position.has_value() ||
        !m_brush_mesh ||
        !m_brush_node ||
        (!m_hover.mesh && (m_hover.grid == nullptr))
    )
    {
        return;
    }

    const auto  transform    = m_hover.mesh
        ? get_hover_mesh_transform()
        : get_hover_grid_transform();
    const auto& brush_scaled = brush->get_scaled(m_transform_scale);
    if (m_hover.mesh)
    {
        m_brush_node->set_parent(m_hover.mesh->get_node());
        m_brush_node->set_parent_from_node(transform);
    }
    else if (m_hover.grid)
    {
        ERHE_VERIFY(m_scene_view != nullptr);
        const auto& scene_root = m_scene_view->get_scene_root();
        ERHE_VERIFY(scene_root);
        m_brush_node->set_parent(scene_root->get_scene()->get_root_node());
        m_brush_node->set_parent_from_node(transform);
    }

    auto& primitive = m_brush_mesh->mesh_data.primitives.front();
    primitive.gl_primitive_geometry = brush_scaled.gl_primitive_geometry;
    primitive.rt_primitive_geometry = brush_scaled.rt_primitive->primitive_geometry;
    primitive.rt_vertex_buffer      = brush_scaled.rt_primitive->vertex_buffer;
    primitive.rt_index_buffer       = brush_scaled.rt_primitive->index_buffer;
}

void Brush_tool::do_insert_operation()
{
    auto brush = m_content_library_window->selected_brush();
    if (
        !m_hover.position.has_value() ||
        !brush
    )
    {
        return;
    }

    log_brush->trace(
        "{} scale = {}",
        __func__,
        m_transform_scale
    );

    const auto hover_from_brush = m_hover.mesh
        ? get_hover_mesh_transform()
        : get_hover_grid_transform();
    const uint64_t mesh_flags =
        erhe::scene::Scene_item_flags::visible     |
        erhe::scene::Scene_item_flags::content     |
        erhe::scene::Scene_item_flags::shadow_cast |
        erhe::scene::Scene_item_flags::id          |
        erhe::scene::Scene_item_flags::show_in_ui;

    ERHE_VERIFY(m_scene_view != nullptr);
    const auto& scene_root = m_scene_view->get_scene_root();
    ERHE_VERIFY(scene_root);

    auto* const hover_node = m_hover.mesh ? m_hover.mesh->get_node() : nullptr;
    const Instance_create_info brush_instance_create_info
    {
        .mesh_flags       = mesh_flags,
        .scene_root       = scene_root.get(),
        .world_from_node  = (hover_node != nullptr)
            ? hover_node->world_from_node() * hover_from_brush
            : hover_from_brush,
        .material         = m_content_library_window->selected_material(),
        .scale            = m_transform_scale,
        .physics_enabled  = m_configuration->physics.static_enable
    };
    const auto instance_node = brush->make_instance(brush_instance_create_info);

    std::shared_ptr<erhe::scene::Node> parent = (hover_node != nullptr)
        ? std::static_pointer_cast<erhe::scene::Node>(hover_node->shared_from_this())
        : scene_root->get_scene()->get_root_node();
    const auto& first_selected_node = m_selection_tool->get_first_selected_node();
    if (first_selected_node)
    {
        parent = first_selected_node;
    }

    auto op = std::make_shared<Node_insert_remove_operation>(
        Node_insert_remove_operation::Parameters{
            .selection_tool = m_selection_tool.get(),
            .node           = instance_node,
            .parent         = parent,
            .mode           = Scene_item_operation::Mode::insert
        }
    );
    m_operation_stack->push(op);
}

void Brush_tool::add_brush_mesh()
{
    auto brush = m_content_library_window->selected_brush();
    if (
        !brush ||
        !m_hover.position.has_value() ||
        (!m_hover.mesh && (m_hover.grid == nullptr)) ||
        (m_scene_view == nullptr)
    )
    {
        return;
    }

    const auto& material = m_content_library_window->selected_material();
    if (!material)
    {
        log_brush->warn("No material selected");
        return;
    }

    const auto& scene_root = m_scene_view->get_scene_root();
    ERHE_VERIFY(scene_root);

    brush->late_initialize();
    const auto& brush_scaled = brush->get_scaled(m_transform_scale);
    const std::string name = fmt::format("brush-{}", brush->get_name());
    m_brush_node = std::make_shared<erhe::scene::Node>(name);
    m_brush_mesh = std::make_shared<erhe::scene::Mesh>(
        name,
        erhe::primitive::Primitive{
            .material              = material,
            .gl_primitive_geometry = brush_scaled.gl_primitive_geometry,
            .rt_primitive_geometry = brush_scaled.rt_primitive->primitive_geometry,
            .rt_vertex_buffer      = brush_scaled.rt_primitive->vertex_buffer,
            .rt_index_buffer       = brush_scaled.rt_primitive->index_buffer,
            .source_geometry       = brush_scaled.geometry,
            .normal_style          = brush->data.normal_style
        }
    );
    m_brush_node->enable_flag_bits(
        erhe::scene::Scene_item_flags::visible |
        erhe::scene::Scene_item_flags::brush   |
        erhe::scene::Scene_item_flags::no_message
    );
    m_brush_mesh->enable_flag_bits(
        erhe::scene::Scene_item_flags::visible |
        erhe::scene::Scene_item_flags::brush   |
        erhe::scene::Scene_item_flags::no_message
    );

    m_brush_mesh->mesh_data.layer_id = scene_root->layers().brush()->id.get_id();

    m_brush_node->attach(m_brush_mesh);
    m_brush_node->set_parent(scene_root->scene().get_root_node());

    update_mesh_node_transform();
}

void Brush_tool::update_mesh()
{
    auto brush = m_content_library_window->selected_brush();
    if (!m_brush_mesh && is_enabled())
    {
        if (
            !brush ||
            !m_hover.position.has_value() ||
            (!m_hover.mesh && (m_hover.grid == nullptr))
        )
        {
            return;
        }

        add_brush_mesh();
        return;
    }

    if (
        !is_enabled() ||
        !brush ||
        !m_hover.position.has_value() ||
        (!m_hover.mesh && (m_hover.grid == nullptr))
    )
    {
        remove_brush_mesh();
    }

    update_mesh_node_transform();
}

void Brush_tool::tool_properties()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    ImGui::Checkbox   ("Snap to Polygon", &m_snap_to_hover_polygon);
    ImGui::Checkbox   ("Snap To Grid",    &m_snap_to_grid);
    ImGui::SliderFloat("Scale",           &m_scale, 0.0001f, 32.0f, "%.3f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_Logarithmic);
    ImGui::Checkbox   ("Physics",         &m_with_physics);
    ImGui::DragInt    ("Face Offset",     &m_polygon_offset, 0.1f, 0, INT_MAX);
    ImGui::DragInt    ("Corner Offset",   &m_corner_offset,  0.1f, 0, INT_MAX);

    ImGui::NewLine    ();
    ImGui::Checkbox   ("Debug visualization", &m_debug_visualization);
#endif
}

void Brush_tool::tool_render(
    const Render_context& context
)
{
    ERHE_PROFILE_FUNCTION

    if (
        !m_debug_visualization ||
        (context.scene_view == nullptr) ||
        !m_brush_mesh ||
        !m_brush_node
    )
    {
        return;
    }

    auto& line_renderer = *m_line_renderer_set->hidden.at(2).get();

    const auto& transform = m_brush_node->parent_from_node_transform();
    glm::mat4 m = transform.matrix();

    constexpr vec3 O     { 0.0f };
    constexpr vec3 axis_x{ 1.0f, 0.0f, 0.0f};
    constexpr vec3 axis_y{ 0.0f, 1.0f, 0.0f};
    constexpr vec3 axis_z{ 0.0f, 0.0f, 1.0f};
    constexpr vec4 red   { 1.0f, 0.0f, 0.0f, 1.0f};
    constexpr vec4 green { 0.0f, 1.0f, 0.0f, 1.0f};
    constexpr vec4 blue  { 0.0f, 0.0f, 1.0f, 1.0f};

    line_renderer.set_thickness(10.0f);
    line_renderer.add_lines( m, red,   { { O, axis_x }});
    line_renderer.add_lines( m, green, { { O, axis_y }});
    line_renderer.add_lines( m, blue,  { { O, axis_z }});
}

} // namespace editor
