#include "tools/point_set_tool.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "input_state.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_scene_view.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "transform/transform_tool.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene/node.hpp"

namespace editor {

IPoint_set::~IPoint_set() noexcept = default;

Point_set_select_command::Point_set_select_command(erhe::commands::Commands& commands, const char* name, Point_set_tool& tool)
    : Command{commands, name}
    , m_tool {tool}
{
}

void Point_set_select_command::try_ready()
{
    if (m_tool.try_ready_select()) {
        set_ready();
    }
}

auto Point_set_select_command::try_call() -> bool
{
    if (get_command_state() != erhe::commands::State::Ready) {
        return false;
    }
    const bool consumed = m_tool.on_select();
    set_inactive();
    return consumed;
}

Point_set_cancel_command::Point_set_cancel_command(erhe::commands::Commands& commands, const char* name, Point_set_tool& tool)
    : Command{commands, name}
    , m_tool {tool}
{
}

void Point_set_cancel_command::try_ready()
{
    if (m_tool.is_own_edit_active()) {
        set_ready();
    }
}

auto Point_set_cancel_command::try_call() -> bool
{
    return m_tool.on_cancel_edit();
}

Point_set_tool::Point_set_tool(
    erhe::commands::Commands& commands,
    App_context&              context,
    App_message_bus&          app_message_bus,
    Tools&                    tools,
    const char* const         select_command_name,
    const char* const         cancel_command_name
)
    : Tool            {context, tools, Tool_flags::background}
    , m_select_command{commands, select_command_name, *this}
    , m_cancel_command{commands, cancel_command_name, *this}
{
    set_base_priority(c_priority);

    m_select_command.set_host(this);
    commands.register_command            (&m_select_command);
    commands.bind_command_to_mouse_button(&m_select_command, erhe::window::Mouse_button_left, erhe::commands::Button_trigger::Button_released);
    // The exact mask 0 dispatches before the mask-less Escape bindings, which it falls through to outside a gesture.
    m_cancel_command.set_host(this);
    commands.register_command  (&m_cancel_command);
    commands.bind_command_to_key(&m_cancel_command, erhe::window::Key_escape, erhe::commands::Button_trigger::Button_pressed, 0u);

    m_hover_scene_view_subscription = app_message_bus.hover_scene_view.subscribe(
        [this](Hover_scene_view_message& message) {
            Tool::on_message(message);
        }
    );
}

auto Point_set_tool::is_view_of_target_scene(const Scene_view& scene_view) const -> bool
{
    const std::shared_ptr<Scene_root> view_scene_root   = scene_view.get_scene_root();
    const std::shared_ptr<Scene_root> target_scene_root = get_point_set_scene_root();
    return view_scene_root && (view_scene_root == target_scene_root);
}

auto Point_set_tool::pick_point(
    Viewport_scene_view&                            viewport_scene_view,
    const glm::vec2&                                pointer,
    const std::span<const std::optional<glm::vec3>> positions
) -> std::optional<std::size_t>
{
    std::optional<std::size_t> best_point;
    float                      best_depth{0.0f};
    for (std::size_t i = 0, end = positions.size(); i < end; ++i) {
        if (!positions[i].has_value()) {
            continue;
        }
        const std::optional<glm::vec3> projected = viewport_scene_view.project_to_viewport(positions[i].value());
        if (!projected.has_value()) {
            continue;
        }
        const float distance = glm::distance(glm::vec2{projected.value()}, pointer);
        if (distance > c_pick_radius) {
            continue;
        }
        // Nearest in depth among the points under the pointer.
        if (!best_point.has_value() || (projected.value().z > best_depth)) {
            best_point = i;
            best_depth = projected.value().z;
        }
    }
    return best_point;
}

auto Point_set_tool::try_ready_select() -> bool
{
    m_pick_point.reset();
    m_pick_miss = false;
    if (!has_point_set_target()) {
        return false;
    }
    Scene_view* scene_view = get_hover_scene_view();
    if ((scene_view == nullptr) || !is_view_of_target_scene(*scene_view)) {
        return false;
    }
    Viewport_scene_view* viewport_scene_view = scene_view->as_viewport_scene_view();
    if (viewport_scene_view == nullptr) {
        return false;
    }
    // The gizmo handles win over point picking.
    Transform_tool* transform_tool = m_context.transform_tool;
    if ((transform_tool != nullptr) && (transform_tool->get_hover_handle() != Handle::e_handle_none)) {
        return false;
    }
    const std::optional<glm::vec2> pointer = viewport_scene_view->get_position_in_viewport();
    if (!pointer.has_value()) {
        return false;
    }
    get_point_world_positions(m_scratch_world);
    m_pick_point = pick_point(*viewport_scene_view, pointer.value(), m_scratch_world);
    if (m_pick_point.has_value()) {
        return true;
    }
    m_pick_miss = try_ready_miss(*viewport_scene_view, pointer.value());
    return m_pick_miss;
}

auto Point_set_tool::on_select() -> bool
{
    if (m_pick_miss) {
        m_pick_miss = false;
        return on_select_miss();
    }
    if (!m_pick_point.has_value() || !has_point_set_target()) {
        return false;
    }
    // Ctrl / Shift toggle, as the object and mesh component selections do.
    const Input_state* const input_state = m_context.input_state;
    const bool toggle = (input_state != nullptr) && (input_state->control || input_state->shift);
    select_point(m_pick_point.value(), toggle ? Point_select::toggle : Point_select::replace);
    m_pick_point.reset();
    return true;
}

auto Point_set_tool::is_own_edit_active() const -> bool
{
    const Transform_tool* const transform_tool = m_context.transform_tool;
    return (transform_tool != nullptr) && (transform_tool->get_point_set_edit() == static_cast<const IPoint_set*>(this));
}

auto Point_set_tool::on_cancel_edit() -> bool
{
    if (!is_own_edit_active()) {
        return false;
    }
    return m_context.transform_tool->cancel_component_edit();
}

auto Point_set_tool::try_ready_miss(Viewport_scene_view&, const glm::vec2&) -> bool
{
    return false;
}

auto Point_set_tool::on_select_miss() -> bool
{
    return false;
}

void Point_set_tool::render_extras(const Render_context&, std::optional<std::size_t>)
{
}

void Point_set_tool::append_point_quad(
    const glm::vec3& position_in_world,
    const glm::vec3& camera_right,
    const glm::vec3& camera_up,
    const float      half_size
)
{
    const glm::vec3 r    = half_size * camera_right;
    const glm::vec3 u    = half_size * camera_up;
    const uint32_t  base = static_cast<uint32_t>(m_scratch_positions.size());
    m_scratch_positions.push_back(position_in_world - r - u);
    m_scratch_positions.push_back(position_in_world + r - u);
    m_scratch_positions.push_back(position_in_world + r + u);
    m_scratch_positions.push_back(position_in_world - r + u);
    m_scratch_indices.push_back(base + 0);
    m_scratch_indices.push_back(base + 1);
    m_scratch_indices.push_back(base + 2);
    m_scratch_indices.push_back(base + 0);
    m_scratch_indices.push_back(base + 2);
    m_scratch_indices.push_back(base + 3);
}

void Point_set_tool::tool_render(const Render_context& context)
{
    // Desktop viewport only: the billboard quads go through Debug_renderer's
    // direct triangle path, which is single-view only (see the equivalent
    // gate in Mesh_component_selection_tool::tool_render).
    if (context.viewport_scene_view == nullptr) {
        return;
    }
    if (!has_point_set_target() || !is_view_of_target_scene(context.scene_view)) {
        return;
    }
    glm::vec3 camera_position{0.0f};
    glm::vec3 camera_right   {1.0f, 0.0f, 0.0f};
    glm::vec3 camera_up      {0.0f, 1.0f, 0.0f};
    const erhe::scene::Node* camera_node = context.get_camera_node();
    if (camera_node != nullptr) {
        const glm::mat4 world_from_camera = camera_node->world_from_node();
        camera_position = glm::vec3{world_from_camera[3]};
        camera_right    = glm::normalize(glm::vec3{world_from_camera[0]});
        camera_up       = glm::normalize(glm::vec3{world_from_camera[1]});
    }
    get_point_world_positions(m_scratch_world);
    std::optional<std::size_t> hovered;
    if (context.viewport_scene_view->is_scene_view_hovered()) {
        const std::optional<glm::vec2> pointer = context.viewport_scene_view->get_position_in_viewport();
        if (pointer.has_value()) {
            hovered = pick_point(*context.viewport_scene_view, pointer.value(), m_scratch_world);
        }
    }
    render_extras(context, hovered);

    // stencil_reference must be non-zero (function=greater against a zero-cleared stencil buffer); 2 matches the other debug tools.
    erhe::renderer::Primitive_renderer triangle_renderer = context.get({erhe::graphics::Primitive_type::triangle, 2, true, false});
    const auto draw_points = [&](const glm::vec4& color, const float size_factor, const auto& predicate) {
        m_scratch_positions.clear();
        m_scratch_indices.clear();
        for (std::size_t i = 0, end = m_scratch_world.size(); i < end; ++i) {
            if (!m_scratch_world[i].has_value() || !predicate(i)) {
                continue;
            }
            const glm::vec3 world     = m_scratch_world[i].value();
            const float     half_size = size_factor * glm::distance(camera_position, world);
            append_point_quad(world, camera_right, camera_up, half_size);
        }
        if (!m_scratch_indices.empty()) {
            triangle_renderer.add_triangles(glm::mat4{1.0f}, color, m_scratch_positions, m_scratch_indices);
        }
    };
    const Debug_visualizations_style& style = context.app_context.editor_settings->debug_visualizations_style;
    const bool is_hovered_unselected = hovered.has_value() && !is_point_selected(hovered.value());
    if (get_point_drawing() == Point_drawing::all) {
        draw_points(style.point_handle_color, style.point_handle_size, [&](const std::size_t i) {
            return !is_point_selected(i) && (!hovered.has_value() || (i != hovered.value()));
        });
    }
    if (is_hovered_unselected) {
        draw_points(style.point_hover_color, style.point_hover_size, [&](const std::size_t i) { return i == hovered.value(); });
    }
    draw_points(style.point_selected_color, style.point_selected_size, [&](const std::size_t i) { return is_point_selected(i); });
}

}
