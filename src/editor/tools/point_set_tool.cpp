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
#include "transform/handle_visualizations.hpp"
#include "transform/transform_tool.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/node.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

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

auto Point_set_tool::get_hot_point() const -> std::optional<std::size_t>
{
    return m_hot_point;
}

void Point_set_tool::clear_hot_point()
{
    m_hot_point.reset();
}

void Point_set_tool::begin_pointer_drag()
{
    m_press.reset();
    if (!m_hot_point.has_value()) {
        return;
    }
    Press press{.point = m_hot_point.value(), .toggle = false, .was_selected = is_point_selected(m_hot_point.value())};
    // Ctrl / Shift toggle only on the point itself; on an arrow they are the drag's snap modifiers.
    const Input_state* const input_state = m_context.input_state;
    Scene_view* const        scene_view  = get_hover_scene_view();
    Viewport_scene_view* const viewport_scene_view = (scene_view != nullptr) ? scene_view->as_viewport_scene_view() : nullptr;
    if ((input_state != nullptr) && (input_state->control || input_state->shift) && (viewport_scene_view != nullptr)) {
        const std::optional<glm::vec2> pointer = viewport_scene_view->get_position_in_viewport();
        get_point_world_positions(m_scratch_world);
        if (pointer.has_value() && (press.point < m_scratch_world.size()) && m_scratch_world[press.point].has_value()) {
            const std::optional<glm::vec3> projected = viewport_scene_view->project_to_viewport(m_scratch_world[press.point].value());
            press.toggle = projected.has_value() && (glm::distance(glm::vec2{projected.value()}, pointer.value()) <= c_pick_radius);
        }
    }
    if (!press.was_selected) {
        select_point(press.point, press.toggle ? Point_select::toggle : Point_select::replace);
    }
    m_press = press;
}

void Point_set_tool::end_pointer_drag(const Point_drag_result result)
{
    if (!m_press.has_value()) {
        return;
    }
    const Press press = m_press.value();
    m_press.reset();
    // A click on a point that was selected at the press: the click selection rule, applied at the release.
    if ((result == Point_drag_result::unmoved) && press.was_selected) {
        select_point(press.point, press.toggle ? Point_select::toggle : Point_select::replace);
    }
}

// The pointer reports no change; runs from Transform_tool::update_for_view() while no gesture is active.
void Point_set_tool::update_hot_point()
{
    const std::optional<std::size_t> previous = m_hot_point;
    m_hot_point.reset();
    if ((get_point_drawing() != Point_drawing::gizmos) || !has_point_set_target()) {
        return;
    }
    Scene_view* const scene_view = get_hover_scene_view();
    if ((scene_view == nullptr) || !is_view_of_target_scene(*scene_view)) {
        return;
    }
    Viewport_scene_view* const viewport_scene_view = scene_view->as_viewport_scene_view();
    if ((viewport_scene_view == nullptr) || !viewport_scene_view->is_scene_view_hovered()) {
        return;
    }
    // The pointer as the control ray has it: the handle test below uses the ray, and both must be of one sample.
    const std::optional<glm::vec3> ray_origin    = scene_view->get_control_ray_origin_in_world();
    const std::optional<glm::vec3> ray_direction = scene_view->get_control_ray_direction_in_world();
    if (!ray_origin.has_value() || !ray_direction.has_value()) {
        return;
    }
    const std::optional<glm::vec3> pointer_projected = viewport_scene_view->project_to_viewport(ray_origin.value() + ray_direction.value());
    if (!pointer_projected.has_value()) {
        return;
    }
    const std::optional<glm::vec2> pointer = glm::vec2{pointer_projected.value()};
    get_point_world_positions(m_scratch_world);
    const auto screen_distance = [&](const glm::vec3& world) -> std::optional<float> {
        const std::optional<glm::vec3> projected = viewport_scene_view->project_to_viewport(world);
        return projected.has_value() ? std::optional<float>{glm::distance(glm::vec2{projected.value()}, pointer.value())} : std::nullopt;
    };
    const bool previous_valid = previous.has_value() && (previous.value() < m_scratch_world.size()) && m_scratch_world[previous.value()].has_value();
    Transform_tool* const transform_tool = m_context.transform_tool;
    // A gizmo handle under the pointer keeps the gizmo where it is.
    if ((transform_tool != nullptr) && transform_tool->is_pointer_on_handle(*scene_view)) {
        if (previous_valid) {
            m_hot_point = previous;
        }
        return;
    }
    const Handle_visualizations* const visualizations = (transform_tool != nullptr) ? transform_tool->shared.get_visualizations() : nullptr;
    const glm::mat3 basis = ((transform_tool != nullptr) && transform_tool->shared.settings.use_anchor_orientation())
        ? glm::mat3_cast(get_point_frame_rotation())
        : glm::mat3{1.0f};
    // The pointer is on the point or inside the screen-space bounding box of the handles its full-size gizmo shows.
    const auto is_within_gizmo = [&](const std::size_t index, const float distance) -> bool {
        if (distance <= c_pick_radius) {
            return true;
        }
        if (visualizations == nullptr) {
            return false;
        }
        const Handle_visualizations::Point_gizmo_extent extent = visualizations->get_point_gizmo_extent(*scene_view, m_scratch_world[index].value(), basis);
        glm::vec2 box_min{std::numeric_limits<float>::max()};
        glm::vec2 box_max{std::numeric_limits<float>::lowest()};
        for (std::size_t i = 0; i < extent.count; ++i) {
            const std::optional<glm::vec3> projected = viewport_scene_view->project_to_viewport(extent.points[i]);
            if (!projected.has_value()) {
                return false;
            }
            box_min = glm::min(box_min, glm::vec2{projected.value()});
            box_max = glm::max(box_max, glm::vec2{projected.value()});
        }
        const glm::vec2 margin{c_gizmo_bounds_margin};
        return
            (extent.count > 0) &&
            glm::all(glm::greaterThanEqual(pointer.value(), box_min - margin)) &&
            glm::all(glm::lessThanEqual   (pointer.value(), box_max + margin));
    };
    // The hovered point keeps the gizmo while the pointer is within it; another point then takes it only from within the pick radius.
    const std::optional<float> previous_distance = previous_valid ? screen_distance(m_scratch_world[previous.value()].value()) : std::nullopt;
    const bool keep_previous = previous_distance.has_value() && is_within_gizmo(previous.value(), previous_distance.value());
    std::optional<std::size_t> nearest;
    float                      nearest_distance{keep_previous ? c_pick_radius : c_hover_radius};
    for (std::size_t i = 0, end = m_scratch_world.size(); i < end; ++i) {
        if (!m_scratch_world[i].has_value()) {
            continue;
        }
        const std::optional<float> distance = screen_distance(m_scratch_world[i].value());
        // Strictly nearer: of coincident points the first wins (a control point over its zero-length handle).
        // Only where its gizmo would keep it, so the gizmo does not come and go as the pointer rests.
        if (distance.has_value() && (distance.value() < nearest_distance) && is_within_gizmo(i, distance.value())) {
            nearest          = i;
            nearest_distance = distance.value();
        }
    }
    m_hot_point = (nearest.has_value() || !keep_previous) ? nearest : previous;
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

void Point_set_tool::tool_render(const Render_context& context)
{
    // Desktop viewport only: the hovered point comes from the viewport's pointer.
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

    // Stencil reference 3: over the curve lines and the gizmo rings (2), under the gizmo arrows (4).
    erhe::renderer::Primitive_renderer visible_renderer = context.get(
        {
            .primitive_type    = erhe::graphics::Primitive_type::line,
            .stencil_reference = 3,
            .draw_visible      = true,
            .draw_hidden       = false
        }
    );
    // The x-ray hidden pass draws at full strength, so the alpha carries the hidden strength.
    erhe::renderer::Primitive_renderer hidden_renderer = context.get(
        {
            .primitive_type    = erhe::graphics::Primitive_type::line,
            .stencil_reference = 3,
            .draw_visible      = false,
            .draw_hidden       = true,
            .xray              = true
        }
    );
    constexpr std::size_t segment_count = 24;
    static const std::array<glm::vec2, segment_count> rim = []() {
        std::array<glm::vec2, segment_count> result;
        for (std::size_t i = 0; i < segment_count; ++i) {
            const float angle = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(segment_count);
            result[i] = glm::vec2{std::cos(angle), std::sin(angle)};
        }
        return result;
    }();
    const Debug_visualizations_style& style = context.app_context.editor_settings->debug_visualizations_style;
    const float hidden_strength = std::clamp(style.point_hidden_strength, 0.0f, 1.0f);
    const float width           = style.point_circle_width;
    const auto draw_points = [&](const glm::vec4& color, const float size_factor, const auto& predicate) {
        const glm::vec4 hidden_color{color.r, color.g, color.b, hidden_strength * color.a};
        for (std::size_t i = 0, end = m_scratch_world.size(); i < end; ++i) {
            if (!m_scratch_world[i].has_value() || !predicate(i)) {
                continue;
            }
            const glm::vec3 world  = m_scratch_world[i].value();
            const float     radius = size_factor * glm::distance(camera_position, world);
            const glm::vec3 r      = radius * camera_right;
            const glm::vec3 u      = radius * camera_up;
            glm::vec3 a = world + (rim[segment_count - 1].x * r) + (rim[segment_count - 1].y * u);
            for (std::size_t k = 0; k < segment_count; ++k) {
                const glm::vec3 b = world + (rim[k].x * r) + (rim[k].y * u);
                visible_renderer.add_line(color, width, a, color, width, b);
                if (hidden_strength > 0.0f) {
                    hidden_renderer.add_line(hidden_color, width, a, hidden_color, width, b);
                }
                a = b;
            }
        }
    };
    // The transform gizmo stands in for the circle of the point it sits on.
    std::optional<std::size_t> gizmo_point;
    if (get_point_drawing() == Point_drawing::gizmos) {
        const Transform_tool* const transform_tool = context.app_context.transform_tool;
        if ((transform_tool != nullptr) && (transform_tool->get_gizmo_point_set() == static_cast<const IPoint_set*>(this))) {
            gizmo_point = m_hot_point;
        }
    }
    const auto has_gizmo = [&](const std::size_t i) { return gizmo_point.has_value() && (i == gizmo_point.value()); };
    const bool is_hovered_unselected = hovered.has_value() && !is_point_selected(hovered.value()) && !has_gizmo(hovered.value());
    if (get_point_drawing() != Point_drawing::highlighted_only) {
        draw_points(style.point_handle_color, style.point_handle_size, [&](const std::size_t i) {
            return !is_point_selected(i) && !has_gizmo(i) && (!hovered.has_value() || (i != hovered.value()));
        });
    }
    if (is_hovered_unselected) {
        draw_points(style.point_hover_color, style.point_hover_size, [&](const std::size_t i) { return i == hovered.value(); });
    }
    draw_points(style.point_selected_color, style.point_selected_size, [&](const std::size_t i) { return is_point_selected(i) && !has_gizmo(i); });
}

}
