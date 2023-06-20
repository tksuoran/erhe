
#include "tools/brushes/brush_tool.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "graphics/icon_set.hpp"
#include "operations/insert_operation.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/render_context.hpp"
#include "scene/collision_generator.hpp"
#include "scene/material_library.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/brushes/brush.hpp"
#include "tools/grid.hpp"
#include "tools/grid_tool.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tool.hpp"
#include "tools/tools.hpp"
#include "windows/content_library_window.hpp"
#include "windows/operations.hpp"

#include "erhe/commands/input_arguments.hpp"
#include "erhe/commands/command.hpp"
#include "erhe/commands/commands.hpp"
#include "erhe/configuration/configuration.hpp"
#include "erhe/imgui/imgui_helpers.hpp"
#include "erhe/imgui/imgui_window.hpp"
#include "erhe/renderer/line_renderer.hpp"
#include "erhe/renderer/text_renderer.hpp"
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

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe/xr/xr_action.hpp"
#   include "erhe/xr/headset.hpp"
#endif

#include <glm/gtx/transform.hpp>

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

#include <glm/glm.hpp>

#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace editor
{

using glm::mat3;
using glm::mat4;
using glm::vec3;
using glm::vec4;
using erhe::geometry::Polygon; // Resolve conflict with wingdi.h BOOL Polygon(HDC,const POINT *,int)

#pragma region Commands
Brush_tool_preview_command::Brush_tool_preview_command(
    erhe::commands::Commands& commands,
    Editor_context&           context
)
    : Command  {commands, "Brush_tool.motion_preview"}
    , m_context{context}
{
}

auto Brush_tool_preview_command::try_call() -> bool
{
    if (
        (get_command_state() != erhe::commands::State::Active) ||
        !m_context.brush_tool->is_enabled()
    ) {
        return false;
    }
    m_context.brush_tool->on_motion();
    return true;
}

Brush_tool_insert_command::Brush_tool_insert_command(
    erhe::commands::Commands& commands,
    Editor_context&           context
)
    : Command  {commands, "Brush_tool.insert"}
    , m_context{context}
{
}

void Brush_tool_insert_command::try_ready()
{
    if (m_context.brush_tool->try_insert_ready()) {
        set_ready();
    }
}

auto Brush_tool_insert_command::try_call() -> bool
{
    if (get_command_state() != erhe::commands::State::Ready) {
        return false;
    }
    const bool consumed = m_context.brush_tool->try_insert();
    set_inactive();
    return consumed;
}
#pragma endregion Commands

Brush_tool::Brush_tool(
    erhe::commands::Commands& commands,
    Editor_context&           editor_context,
    Editor_message_bus&       editor_message_bus,
    Headset_view&             headset_view,
    Icon_set&                 icon_set,
    Tools&                    tools
)
    : Tool             {editor_context}
    , m_preview_command{commands, editor_context}
    , m_insert_command {commands, editor_context}
{
    set_base_priority(Brush_tool::c_priority);
    set_description  ("Brush Tool");
    set_flags        (Tool_flags::toolbox);
    set_icon         (icon_set.icons.brush_big);

    commands.register_command(&m_preview_command);
    commands.register_command(&m_insert_command);
    commands.bind_command_to_update      (&m_preview_command);
    commands.bind_command_to_mouse_button(&m_insert_command, erhe::toolkit::Mouse_button_right, true);

#if defined(ERHE_XR_LIBRARY_OPENXR)
    const auto* headset = headset_view.get_headset();
    if (headset != nullptr) {
        auto& xr_right = headset->get_actions_right();
        commands.bind_command_to_xr_boolean_action(&m_insert_command, xr_right.trigger_click, erhe::commands::Button_trigger::Button_pressed);
        commands.bind_command_to_xr_boolean_action(&m_insert_command, xr_right.a_click,       erhe::commands::Button_trigger::Button_pressed);
    }
#else
    static_cast<void>(headset_view);
#endif

    tools.register_tool(this);

    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            on_message(message);
        }
    );

    m_preview_command.set_host(this);
    m_insert_command .set_host(this);
}

void Brush_tool::on_message(Editor_message& message)
{
    Tool::on_message(message);
    using namespace erhe::toolkit;
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_mesh)) {
        on_motion();
    }
}

void Brush_tool::handle_priority_update(int old_priority, int new_priority)
{
    if (new_priority < old_priority) {
        disable();
        m_preview_command.set_inactive();
        m_hover = Hover_entry{};
        remove_brush_mesh();
    }
    if (new_priority > old_priority) {
        enable();
        m_preview_command.set_active();
        on_motion();
    }
}

void Brush_tool::remove_brush_mesh()
{
    // Remove mesh attachment *before* removing node
    if (m_brush_mesh) {
        m_brush_mesh.reset();
    }
    if (m_brush_node) {
        // Do not remove this. It is not possible to rely on reset calling destructor,
        // because parent will have shared_ptr to the child.
        m_brush_node->set_parent({});
        m_brush_node.reset();
    }
}

auto Brush_tool::try_insert_ready() -> bool
{
    return is_enabled() &&
        (m_hover.mesh || (m_hover.grid != nullptr));
}

auto Brush_tool::try_insert() -> bool
{
    auto brush = m_context.content_library_window->selected_brush();
    if (
        !m_brush_node ||
        !m_brush_mesh ||
        !m_hover.position.has_value() ||
        !brush
    ) {
        return false;
    }

        m_context.editor_scenes->sanity_check();

    do_insert_operation();

        m_context.editor_scenes->sanity_check();

    remove_brush_mesh();

        m_context.editor_scenes->sanity_check();

    return true;
}

void Brush_tool::on_motion()
{
    auto* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return;
    }

    m_hover = scene_view->get_nearest_hover(
        Hover_entry::content_bit |
        Hover_entry::grid_bit
    );

    if (
        (m_hover.geometry != nullptr) &&
        (m_hover.local_index > m_hover.geometry->get_polygon_count())
    ) {
        m_hover.local_index = 0;
        m_hover.geometry    = nullptr;
    }

    if (
        m_hover.mesh &&
        (m_hover.mesh->get_node() != nullptr) &&
        m_hover.position.has_value()
    ) {
        m_hover.position = m_hover.mesh->get_node()->transform_direction_from_world_to_local(
            m_hover.position.value()
        );
    }

    update_mesh();
}

// Returns transform which places brush in parent (hover) mesh space.
auto Brush_tool::get_hover_mesh_transform() -> mat4
{
    auto brush = m_context.content_library_window->selected_brush();

    if (
        (m_hover.mesh == nullptr) ||
        (m_hover.geometry == nullptr) ||
        (m_hover.local_index >= m_hover.geometry->get_polygon_count())
    ) {
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
    if (m_transform_scale != 1.0f) {
        const mat4 scale_transform = erhe::toolkit::create_scale(m_transform_scale);
        brush_frame.transform_by(scale_transform);
    }

    if (
        !m_snap_to_hover_polygon &&
        m_hover.position.has_value()
    ) {
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
    if (m_hover.grid == nullptr) {
        return mat4{1};
    }

    m_transform_scale = m_scale;
    auto brush = m_context.content_library_window->selected_brush();
    Reference_frame brush_frame = brush->get_reference_frame(
        static_cast<uint32_t>(m_polygon_offset),
        static_cast<uint32_t>(m_corner_offset)
    );
    const mat4 scale_transform = erhe::toolkit::create_scale(m_scale);
    brush_frame.transform_by(scale_transform);
    const mat4 brush_transform = brush_frame.transform();
    const mat4 inverse_brush   = inverse(brush_transform);

    const glm::vec3 position_in_grid0 = m_hover.grid->grid_from_world() * glm::vec4{m_hover.position.value(), 1.0f};
    const glm::vec3 position_in_grid  = m_snap_to_grid
        ? m_hover.grid->snap_grid_position(position_in_grid0)
        : position_in_grid0;
    const glm::mat4 offset            = erhe::toolkit::create_translation<float>(position_in_grid);
    const glm::mat4 world_from_grid   = m_hover.grid->world_from_grid() * offset;

    const mat4 align = mat4{world_from_grid} * inverse_brush;
    return align;
}

void Brush_tool::update_mesh_node_transform()
{
    auto brush = m_context.content_library_window->selected_brush();
    if (
        !brush ||
        !m_hover.position.has_value() ||
        !m_brush_mesh ||
        !m_brush_node ||
        (!m_hover.mesh && (m_hover.grid == nullptr))
    ) {
        return;
    }

    const auto  transform    = m_hover.mesh
        ? get_hover_mesh_transform()
        : get_hover_grid_transform();
    const auto& brush_scaled = brush->get_scaled(m_transform_scale);
    if (m_hover.mesh) {
        m_brush_node->set_parent(m_hover.mesh->get_node());
        m_brush_node->set_parent_from_node(transform);
    } else if (m_hover.grid) {
        auto* scene_view = get_hover_scene_view();
        ERHE_VERIFY(scene_view != nullptr);
        const auto& scene_root = scene_view->get_scene_root();
        ERHE_VERIFY(scene_root);
        m_brush_node->set_parent(scene_root->get_hosted_scene()->get_root_node());
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
    auto brush = m_context.content_library_window->selected_brush();
    if (
        !m_hover.position.has_value() ||
        !brush
    ) {
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
        erhe::scene::Item_flags::visible     |
        erhe::scene::Item_flags::content     |
        erhe::scene::Item_flags::opaque      |
        erhe::scene::Item_flags::shadow_cast |
        erhe::scene::Item_flags::id          |
        erhe::scene::Item_flags::show_in_ui;
    const uint64_t node_flags =
        erhe::scene::Item_flags::visible     |
        erhe::scene::Item_flags::content     |
        erhe::scene::Item_flags::show_in_ui;

    auto* scene_view = get_hover_scene_view();
    ERHE_VERIFY(scene_view != nullptr);
    const auto& scene_root = scene_view->get_scene_root();
    ERHE_VERIFY(scene_root);

    auto* const hover_node = m_hover.mesh ? m_hover.mesh->get_node() : nullptr;
    const Instance_create_info brush_instance_create_info {
        .node_flags       = node_flags,
        .mesh_flags       = mesh_flags,
        .scene_root       = scene_root.get(),
        .world_from_node  = (hover_node != nullptr)
            ? hover_node->world_from_node() * hover_from_brush
            : hover_from_brush,
        .material         = m_context.content_library_window->selected_material(),
        .scale            = m_transform_scale
    };
    const auto instance_node = brush->make_instance(brush_instance_create_info);

    std::shared_ptr<erhe::scene::Node> parent = (hover_node != nullptr)
        ? std::static_pointer_cast<erhe::scene::Node>(hover_node->shared_from_this())
        : scene_root->get_hosted_scene()->get_root_node();
    const auto& first_selected_node = m_context.selection->get_first_selected_node();
    if (first_selected_node) {
        parent = first_selected_node;
    }

    auto op = std::make_shared<Node_insert_remove_operation>(
        Node_insert_remove_operation::Parameters{
            .context = m_context,
            .node    = instance_node,
            .parent  = parent,
            .mode    = Scene_item_operation::Mode::insert
        }
    );
    m_context.operation_stack->push(op);
}

void Brush_tool::add_brush_mesh()
{
    auto brush = m_context.content_library_window->selected_brush();
    auto* scene_view = get_hover_scene_view();
    if (
        !brush ||
        !m_hover.position.has_value() ||
        (!m_hover.mesh && (m_hover.grid == nullptr)) ||
        (scene_view == nullptr)
    ) {
        return;
    }

    const auto& material = m_context.content_library_window->selected_material();
    if (!material) {
        log_brush->warn("No material selected");
        return;
    }

    const auto& scene_root = scene_view->get_scene_root();
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
        erhe::scene::Item_flags::brush       |
        erhe::scene::Item_flags::visible     |
        erhe::scene::Item_flags::no_message
    );
    m_brush_mesh->enable_flag_bits(
        erhe::scene::Item_flags::brush       |
        erhe::scene::Item_flags::visible     |
        erhe::scene::Item_flags::translucent | // redundant
        erhe::scene::Item_flags::no_message
    );

    m_brush_mesh->mesh_data.layer_id = scene_root->layers().brush()->id;

    m_brush_node->attach(m_brush_mesh);
    m_brush_node->set_parent(scene_root->scene().get_root_node());

    update_mesh_node_transform();
}

void Brush_tool::update_mesh()
{
    auto brush = m_context.content_library_window->selected_brush();
    if (!m_brush_mesh && is_enabled()) {
        if (
            !brush ||
            !m_hover.position.has_value() ||
            (!m_hover.mesh && (m_hover.grid == nullptr))
        ) {
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
    ) {
        remove_brush_mesh();
    }

    update_mesh_node_transform();
}

void Brush_tool::tool_properties()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION();

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

void Brush_tool::tool_render(const Render_context&)
{
    ERHE_PROFILE_FUNCTION();

    if (
        !m_debug_visualization ||
        !m_brush_mesh ||
        !m_brush_node
    ) {
        return;
    }

    auto& line_renderer = *m_context.line_renderer_set->hidden.at(2).get();

    const auto& transform = m_brush_node->parent_from_node_transform();
    glm::mat4 m = transform.get_matrix();

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
