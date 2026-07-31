#include "transform/rotate_tool.hpp"
#include "windows/property_editor.hpp"

#include "app_context.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "editor_log.hpp"
#include "input_state.hpp"
#include "graphics/icon_set.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_view.hpp"
#include "tools/tools.hpp"
#include "transform/handle_enums.hpp"
#include "transform/transform_tool.hpp"

#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_renderer/text_renderer.hpp"
#include "erhe_scene/camera.hpp"

#include <imgui/imgui.h>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace editor {

using namespace glm;

Rotate_tool::Rotate_tool(App_context& app_context, Icon_set& icon_set, Tools& tools)
    : Subtool{app_context, tools, Tool_flags::toolbox | Tool_flags::allow_secondary}
{
    set_base_priority  (c_priority);
    set_description    ("Rotate");
    set_icon           (icon_set.custom_icons, icon_set.icons.rotate);
}

Rotate_tool::~Rotate_tool() noexcept = default;

void Rotate_tool::handle_priority_update(const int old_priority, const int new_priority)
{
    auto& shared = get_shared();
    shared.settings.show_rotate = new_priority > old_priority;
}

void Rotate_tool::imgui(Property_editor& property_editor)
{
    Property_editor& p = property_editor;
    p.reset();
    //auto& shared = get_shared();
    p.push_group("Rotate Tool", ImGuiTreeNodeFlags_DefaultOpen);
    p.add_entry("Snap Enable", [this]() { ImGui::Checkbox("##", &get_shared().settings.rotate_snap_enable); });
    p.add_entry("Snap Value", [this]() {
        const float snap_values[] = {  5.0f, 10.0f, 15.0f, 20.0f, 30.0f, 45.0f, 60.0f, 90.0f };
        const char* snap_items [] = { "5",  "10",  "15",  "20",  "30",  "45",  "60",  "90" };
        if (ImGui::BeginCombo("##", snap_items[m_rotate_snap_index])) {
            ImGui::TextUnformatted("Rotation Snap Value:");
            for (int i = 0, end = IM_ARRAYSIZE(snap_items); i < end; ++i) {
                bool selected = (i == m_rotate_snap_index);
                bool clicked = ImGui::Selectable(snap_items[i], &selected, ImGuiSelectableFlags_None);
                if (clicked) {
                    m_rotate_snap_index = i;
                }
            }
            ImGui::EndCombo();
        }
        if (
            (m_rotate_snap_index >= 0) &&
            (m_rotate_snap_index < IM_ARRAYSIZE(snap_values))
        ) {
            get_shared().settings.rotate_snap = snap_values[m_rotate_snap_index];
        }
    });
    p.pop_group();
    p.show_entries();
}

auto Rotate_tool::begin(unsigned int axis_mask, Scene_view* scene_view) -> bool
{
    m_axis_mask     = axis_mask;
    m_active        = true;
    m_current_angle = 0.0f;

    //if (is_rotate_active()) {
    auto& shared = get_shared();
    const bool world        = !shared.settings.use_anchor_orientation();
    const vec3 n            = get_plane_normal(world);
    const vec3 side         = get_plane_side  (world);
    const vec3 center       = shared.world_from_anchor_initial_state.get_translation();
    const auto intersection = project_pointer_to_plane(scene_view, n, center);

    if (!intersection.has_value()) {
        log_trs_tool->trace("drag not possible - no intersection");
        return false;
    }

    m_normal               = n;
    m_reference_direction  = normalize(intersection.value() - center);
    m_center_of_rotation   = center;
    m_start_rotation_angle = erhe::math::angle_of_rotation<float>(m_reference_direction, n, side);

    return true;
}

auto Rotate_tool::snap(const float angle_radians) const -> float
{
    auto& shared = get_shared();
    // Snap when the toggle is enabled or while Control is held. The live key state
    // is read each update, so toggling Control mid-drag takes effect immediately.
    const bool snap_enabled = shared.settings.rotate_snap_enable || m_context.input_state->control;
    if (!snap_enabled) {
        return angle_radians;
    }

    const float snap = glm::radians<float>(shared.settings.rotate_snap);
    return std::floor((angle_radians + snap * 0.5f) / snap) * snap;
}

auto Rotate_tool::update(Scene_view* scene_view) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (scene_view == nullptr) {
        return false;
    }

    bool ready_to_rotate = update_circle_around(scene_view);
    if (!ready_to_rotate) {
        ready_to_rotate = update_parallel(scene_view);
    }

    if (ready_to_rotate) {
        update_final();
    }

    return ready_to_rotate;
}

auto Rotate_tool::update_circle_around(Scene_view* scene_view) -> bool
{
    m_intersection = project_pointer_to_plane(
        scene_view,
        m_normal,
        m_center_of_rotation
    );
    return m_intersection.has_value();
}

auto Rotate_tool::update_parallel(Scene_view* scene_view) -> bool
{
    const auto p_origin_opt    = scene_view->get_control_ray_origin_in_world();
    const auto p_direction_opt = scene_view->get_control_ray_direction_in_world();
    if (!p_origin_opt.has_value() || !p_direction_opt.has_value()) {
        return false;
    }

    const auto& shared = get_shared();
    const auto p0        = p_origin_opt.value();
    const auto direction = p_direction_opt.value();
    const auto q0        = p0 + shared.initial_drag_position_distance_to_camera * direction;

    m_intersection = project_to_offset_plane(m_center_of_rotation, q0);
    return true;
}

void Rotate_tool::update_final()
{
    ERHE_VERIFY(m_intersection.has_value());

    const vec3  q_                     = normalize                           (m_intersection.value() - m_center_of_rotation);
    const float angle                  = erhe::math::angle_of_rotation<float>(q_, m_normal, m_reference_direction);
    const float snapped_angle          = snap                                (angle);
    const vec3  rotation_axis_in_world = get_axis_direction                  ();
    const mat4  rotation               = erhe::math::create_rotation<float>  (snapped_angle, rotation_axis_in_world);

    m_current_angle = angle;

    m_context.transform_tool->adjust_rotation(m_center_of_rotation, glm::quat_cast(rotation));
}

void Rotate_tool::render(const Render_context& context)
{
    if (!is_active()) {
        return;
    }

    const auto* camera_node = context.get_camera_node();
    if (camera_node == nullptr) {
        return;
    }

    const auto& shared = get_shared();
    const vec3  p                 = m_center_of_rotation;
    const vec3  n                 = m_normal;
    const vec3  side1             = m_reference_direction;
    const vec3  side2             = normalize(cross(n, side1));
    const vec3  position_in_world = p;//node.position_in_world();
    const float distance          = length(position_in_world - vec3{camera_node->position_in_world()});
    const float scale             = context.app_context.editor_settings->gizmo_scale * distance / 100.0f;
    const float r1                = scale * context.app_context.editor_settings->transform_tool.rotate_ring_size;
    const float snapped_angle     = snap(m_current_angle);

    const vec4     axis_color = get_axis_color(m_axis_mask);
    constexpr vec4 yellow{1.0f, 1.0f, 0.0f, 1.0f};

    // Axis-colored protractor lines are thinner than the yellow swept-sector
    // lines so the sector reads as the emphasized element.
    constexpr float axis_line_width   = -1.0f;  // negative = constant screen-space pixels
    constexpr float sector_line_width = -1.41f;

    // X-ray bucket, like all transform-handle line rendering: the occluded
    // pass blends at full strength so the protractor stays readable inside
    // content meshes.
    erhe::renderer::Primitive_renderer line_renderer = context.get(
        erhe::renderer::Debug_renderer_config{
            .primitive_type    = erhe::graphics::Primitive_type::line,
            .stencil_reference = 2,
            .draw_visible      = true,
            .draw_hidden       = true,
            .xray              = true
        }
    );

    {
        const bool snap_enabled = shared.settings.rotate_snap_enable || m_context.input_state->control;
        const int sector_count = snap_enabled
            ? static_cast<int>(glm::two_pi<float>() / glm::radians(shared.settings.rotate_snap))
            : 80;

        // Step markers: yellow inside the swept sector, axis color outside.
        // The i == 0 marker is the full center spoke - the initial angle
        // indicator - in the axis color like the current-angle indicator.
        std::vector<erhe::renderer::Line> inside_ticks;
        std::vector<erhe::renderer::Line> outside_ticks;
        for (int i = 0; i < sector_count; ++i) {
            const float rel   = static_cast<float>(i) / static_cast<float>(sector_count);
            const float theta = rel * glm::two_pi<float>();
            const bool  first = (i == 0);
            const bool  major = (i % 10 == 0);
            // Tick inner radii as fractions of the ring radius so the marks
            // scale with the configurable ring size (fractions chosen to
            // match the original look at ring size 6).
            const float r0    =
                first
                    ? 0.0f
                    : major
                        ? r1 * (5.0f / 6.0f)
                        : r1 * (5.5f / 6.0f);

            const vec3 p0 = p + r0 * std::cos(theta) * side1 + r0 * std::sin(theta) * side2;
            const vec3 p1 = p + r1 * std::cos(theta) * side1 + r1 * std::sin(theta) * side2;

            if (first) {
                line_renderer.set_thickness(axis_line_width);
                line_renderer.set_line_color(axis_color);
                line_renderer.add_lines( {{ p0, p1 }} );
                continue;
            }
            const float theta_signed = (theta > glm::pi<float>()) ? theta - glm::two_pi<float>() : theta;
            const bool  inside       = (snapped_angle >= 0.0f)
                ? (theta_signed >= 0.0f) && (theta_signed <= snapped_angle)
                : (theta_signed >= snapped_angle) && (theta_signed <= 0.0f);
            (inside ? inside_ticks : outside_ticks).push_back({p0, p1});
        }
        line_renderer.set_thickness(axis_line_width);
        line_renderer.set_line_color(axis_color);
        line_renderer.add_lines(outside_ticks);
        line_renderer.set_thickness(sector_line_width);
        line_renderer.set_line_color(yellow);
        line_renderer.add_lines(inside_ticks);
    }

    // Circle (ring): the part inside the swept sector - the sector's outer
    // edge - is yellow, the rest axis color. One polyline with per-segment
    // classification (by midpoint angle) instead of overdrawing a separate
    // yellow arc: two differently-tessellated polylines never coincide
    // exactly, so the color underneath would peek through. The exact sector
    // boundaries are marked by the indicator spokes.
    {
        constexpr int segment_count = 200;
        std::vector<erhe::renderer::Line> inside_segments;
        std::vector<erhe::renderer::Line> outside_segments;
        for (int i = 0; i < segment_count; ++i) {
            const float theta0 = glm::two_pi<float>() * static_cast<float>(i    ) / static_cast<float>(segment_count);
            const float theta1 = glm::two_pi<float>() * static_cast<float>(i + 1) / static_cast<float>(segment_count);
            const vec3  p0     = p + r1 * std::cos(theta0) * side1 + r1 * std::sin(theta0) * side2;
            const vec3  p1     = p + r1 * std::cos(theta1) * side1 + r1 * std::sin(theta1) * side2;
            const float mid        = 0.5f * (theta0 + theta1);
            const float mid_signed = (mid > glm::pi<float>()) ? mid - glm::two_pi<float>() : mid;
            const bool  inside     = (snapped_angle >= 0.0f)
                ? (mid_signed >= 0.0f) && (mid_signed <= snapped_angle)
                : (mid_signed >= snapped_angle) && (mid_signed <= 0.0f);
            (inside ? inside_segments : outside_segments).push_back({p0, p1});
        }
        line_renderer.set_thickness(axis_line_width);
        line_renderer.set_line_color(axis_color);
        line_renderer.add_lines(outside_segments);
        line_renderer.set_thickness(sector_line_width);
        line_renderer.set_line_color(yellow);
        line_renderer.add_lines(inside_segments);
    }

    const auto snapped = p + r1 * std::cos(snapped_angle) * side1 + r1 * std::sin(snapped_angle) * side2;

    // Swept-rotation sector fill from the initial (reference) direction to
    // the current snapped angle, rooted at the rotation center. Low alpha so
    // the delta text placed inside stays readable; the sector's outer edge is
    // the yellow part of the ring above.
    if (std::abs(snapped_angle) > 1e-4f) {
        constexpr float sector_step  = glm::two_pi<float>() / 200.0f;
        const int       sector_count = std::max(1, static_cast<int>(std::ceil(std::abs(snapped_angle) / sector_step)));
        std::vector<vec3>     sector_positions;
        std::vector<uint32_t> sector_indices;
        sector_positions.reserve(sector_count + 2);
        sector_indices.reserve(3 * sector_count);
        sector_positions.push_back(p);
        for (int i = 0; i <= sector_count; ++i) {
            const float theta = snapped_angle * static_cast<float>(i) / static_cast<float>(sector_count);
            sector_positions.push_back(p + r1 * std::cos(theta) * side1 + r1 * std::sin(theta) * side2);
        }
        for (int i = 0; i < sector_count; ++i) {
            sector_indices.push_back(0);
            sector_indices.push_back(static_cast<uint32_t>(i + 1));
            sector_indices.push_back(static_cast<uint32_t>(i + 2));
        }
        erhe::renderer::Primitive_renderer triangle_renderer = context.get(
            {erhe::graphics::Primitive_type::triangle, 2, true, false}
        );
        triangle_renderer.add_triangles(mat4{1.0f}, vec4{1.0f, 1.0f, 0.0f, 0.14f}, sector_positions, sector_indices);
    }

    line_renderer.set_thickness(axis_line_width);
    line_renderer.add_lines(axis_color, { { p, snapped } } );

    // Angle readout at the ring: the anchor's twist about the rotation axis
    // at drag start (swing-twist decomposition), printed just outside the
    // ring at the initial (reference) direction; the current value - initial
    // plus the snapped drag angle, so it stays continuous past +/-180 deg -
    // printed the same way at the current direction.
    erhe::renderer::Text_renderer* text_renderer = m_context.text_renderer;
    if ((text_renderer == nullptr) || !text_renderer->config.enabled || (context.camera == nullptr)) {
        return;
    }
    const float n_length = glm::length(n);
    if (!(n_length > 1e-6f)) {
        return;
    }
    const vec3      axis    = n / n_length;
    const glm::quat q0      = shared.world_from_anchor_initial_state.get_rotation();
    float           initial = 2.0f * std::atan2(glm::dot(vec3{q0.x, q0.y, q0.z}, axis), q0.w);
    if (initial >  glm::pi<float>()) { initial -= glm::two_pi<float>(); }
    if (initial < -glm::pi<float>()) { initial += glm::two_pi<float>(); }
    const float current = initial + snapped_angle;

    const auto projection_transforms = context.camera->projection_transforms(
        context.viewport,
        context.scene_view.get_reverse_depth(),
        context.scene_view.get_depth_range(),
        context.scene_view.get_conventions()
    );
    const mat4 clip_from_world = projection_transforms.clip_from_world.get_matrix();

    constexpr uint32_t white_abgr  = 0xffffffffu;
    constexpr uint32_t yellow_abgr = 0xff00ffffu;
    const float label_radius   = 1.15f * r1;
    const float bisector_angle = 0.5f * snapped_angle;

    const auto project = [&](const vec3 position_in_world) -> vec3 {
        return context.viewport.project_to_screen_space(
            clip_from_world, position_in_world, 0.0f, 1.0f, context.scene_view.get_conventions()
        );
    };
    const vec3 initial_anchor = project(p + label_radius * side1);
    const vec3 current_anchor = project(p + label_radius * (std::cos(snapped_angle)  * side1 + std::sin(snapped_angle)  * side2));
    const vec3 delta_anchor   = project(p + label_radius * (std::cos(bisector_angle) * side1 + std::sin(bisector_angle) * side2));

    const std::string initial_text = fmt::format("{:.1f} deg", glm::degrees(initial));
    const std::string current_text = fmt::format("{:.1f} deg", glm::degrees(current));
    const std::string delta_text   = fmt::format("{:.1f} deg", glm::degrees(snapped_angle));

    // The initial and current labels stack away from each other vertically:
    // the on-screen upper one hangs its bottom edge on its anchor and the
    // lower one its top edge, so the two can touch but never overlap however
    // close the two ring directions get. Measure bounds are in font space
    // (pen origin at the baseline, y up); with a top-left framebuffer origin
    // the text renderer flips glyph y, so a font-space point y lands at
    // print_y - y on screen (print_y + y on bottom-left).
    const bool top_left = context.scene_view.get_framebuffer_origin() == erhe::math::Framebuffer_origin::top_left;
    struct Placed_label {
        glm::vec2 print_position;
        glm::vec2 rect_min;
        glm::vec2 rect_max;
    };
    // vertical_align: -1 = text above the anchor, +1 = below, 0 = baseline on the anchor
    const auto place = [&](const vec3& anchor, const erhe::ui::Rectangle& bounds, const int vertical_align) -> Placed_label {
        const float x_print = anchor.x - 0.5f * bounds.size().x;
        float       y_print = anchor.y;
        if (top_left) {
            if (vertical_align < 0) { y_print = anchor.y + bounds.min().y; }
            if (vertical_align > 0) { y_print = anchor.y + bounds.max().y; }
            return Placed_label{
                {x_print, y_print},
                {x_print + bounds.min().x, y_print - bounds.max().y},
                {x_print + bounds.max().x, y_print - bounds.min().y}
            };
        }
        if (vertical_align < 0) { y_print = anchor.y - bounds.min().y; }
        if (vertical_align > 0) { y_print = anchor.y - bounds.max().y; }
        return Placed_label{
            {x_print, y_print},
            {x_print + bounds.min().x, y_print + bounds.min().y},
            {x_print + bounds.max().x, y_print + bounds.max().y}
        };
    };

    const bool initial_is_upper = top_left
        ? (initial_anchor.y <= current_anchor.y)
        : (initial_anchor.y >= current_anchor.y);
    const Placed_label placed_initial = place(initial_anchor, text_renderer->measure(initial_text), initial_is_upper ? -1 : +1);
    const Placed_label placed_current = place(current_anchor, text_renderer->measure(current_text), initial_is_upper ? +1 : -1);
    const Placed_label placed_delta   = place(delta_anchor,   text_renderer->measure(delta_text),   0);

    // The delta label is skipped when it cannot be shown without running
    // into the initial or current label.
    const auto overlaps = [](const Placed_label& a, const Placed_label& b) -> bool {
        constexpr float padding = 2.0f;
        return
            (a.rect_min.x - padding < b.rect_max.x) && (a.rect_max.x + padding > b.rect_min.x) &&
            (a.rect_min.y - padding < b.rect_max.y) && (a.rect_max.y + padding > b.rect_min.y);
    };
    const bool delta_fits = !overlaps(placed_delta, placed_initial) && !overlaps(placed_delta, placed_current);

    text_renderer->print(vec3{placed_initial.print_position, -initial_anchor.z}, white_abgr, initial_text);
    text_renderer->print(vec3{placed_current.print_position, -current_anchor.z}, white_abgr, current_text);
    if (delta_fits) {
        text_renderer->print(vec3{placed_delta.print_position, -delta_anchor.z}, yellow_abgr, delta_text);
    }
}

}
