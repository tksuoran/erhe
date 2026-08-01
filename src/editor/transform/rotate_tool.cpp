#include "transform/rotate_tool.hpp"
#include "windows/property_editor.hpp"

#include "app_context.hpp"
#include "app_settings.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "editor_log.hpp"
#include "input_state.hpp"
#include "graphics/icon_set.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_view.hpp"
#include "tools/tools.hpp"
#include "transform/handle_enums.hpp"
#include "transform/handle_visualizations.hpp"
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
    // Persistent preference (Transform_tool_config); touch() schedules the autosave.
    p.add_entry("Snap Absolute", [this]() {
        if (ImGui::Checkbox("##", &m_context.editor_settings->transform_tool.rotate_snap_absolute)) {
            m_context.app_settings->settings_store().touch();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Snap the resulting absolute angle around the rotation axis to snap multiples; when off, snap the drag delta instead");
        }
    });
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
    m_view_mode     = (axis_mask == Axis_mask::view);
    m_free_mode     = (axis_mask == Axis_mask::free);

    auto& shared = get_shared();
    const vec3 center = shared.world_from_anchor_initial_state.get_translation();
    m_center_of_rotation = center;

    if (m_free_mode) {
        // Incremental screen-plane trackball: state primed by the first
        // update_arcball() call.
        const Handle_visualizations* visualizations = shared.get_visualizations();
        if (visualizations == nullptr) {
            return false;
        }
        m_arcball_radius     = visualizations->get_gizmo_radius();
        m_arcball_rotation   = glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
        m_arcball_prev_valid = false;
        return true;
    }

    vec3 n;
    vec3 side;
    if (m_view_mode) {
        // Rotation around the viewing axis (the camera-aligned outer ring):
        // plane normal = eye-to-anchor direction, side = any perpendicular.
        // The eye is the camera node (in XR the head, not the controller
        // ray origin) so the drag plane is the same plane render() and
        // pick() place the ring in.
        std::optional<vec3> eye_opt = scene_view->get_control_ray_origin_in_world();
        const std::shared_ptr<erhe::scene::Camera> camera = scene_view->get_camera();
        const erhe::scene::Node* camera_node = camera ? camera->get_node() : nullptr;
        if (camera_node != nullptr) {
            eye_opt = vec3{camera_node->position_in_world()};
        }
        if (!eye_opt.has_value()) {
            return false;
        }
        n = normalize(center - eye_opt.value());
        const vec3 ref = (std::abs(n.y) < 0.9f) ? vec3{0.0f, 1.0f, 0.0f} : vec3{1.0f, 0.0f, 0.0f};
        side = normalize(cross(n, ref));
    } else {
        const bool world = !shared.settings.use_anchor_orientation();
        n    = get_plane_normal(world);
        side = get_plane_side  (world);
    }
    const auto intersection = project_pointer_to_plane(scene_view, n, center);

    if (!intersection.has_value()) {
        log_trs_tool->trace("drag not possible - no intersection");
        return false;
    }

    m_normal               = n;
    m_reference_direction  = normalize(intersection.value() - center);
    m_axis_side            = side;
    m_start_rotation_angle = erhe::math::angle_of_rotation<float>(m_reference_direction, n, side);

    return true;
}

// Incremental screen-plane trackball: each frame, the pointer's displacement
// in the view plane through the rotation center adds a rotation about the
// in-plane axis perpendicular to that motion, with angle = distance / gizmo
// radius, accumulated into a quaternion. Mapping increments instead of a
// fixed start-to-current pair keeps the rotation unbounded - any ball-surface
// mapping saturates once the pointer leaves the sphere, because the angle
// between two mapped vectors can never exceed 180 degrees.
auto Rotate_tool::update_arcball(Scene_view* scene_view) -> bool
{
    const auto origin_opt    = scene_view->get_control_ray_origin_in_world();
    const auto direction_opt = scene_view->get_control_ray_direction_in_world();
    if (!origin_opt.has_value() || !direction_opt.has_value()) {
        return false;
    }
    const vec3  o       = origin_opt.value();
    const vec3  d       = normalize(direction_opt.value());
    const vec3  to_eye  = o - m_center_of_rotation;
    const float eye_len = glm::length(to_eye);
    if (!(eye_len > 1e-6f)) {
        return false;
    }
    const vec3  n     = to_eye / eye_len;                       // view plane normal (toward the eye)
    const float denom = glm::dot(d, n);
    if (std::abs(denom) < 1e-6f) {
        return false;
    }
    const float t = glm::dot(m_center_of_rotation - o, n) / denom;
    if (t <= 0.0f) {
        return false;
    }
    const vec3 planar = o + t * d - m_center_of_rotation;       // pointer in the view plane, from the center

    if (!m_arcball_prev_valid) {
        m_arcball_prev       = planar;
        m_arcball_prev_valid = true;
        return true;
    }

    const vec3  dp  = planar - m_arcball_prev;
    const float len = glm::length(dp);
    if (len > 1e-9f) {
        // cross(n, dp) has length |dp| (dp lies in the plane normal to n),
        // so dividing by len normalizes it. The sign makes the sphere point
        // under the pointer follow the pointer's motion.
        const vec3  axis  = cross(n, dp) / len;
        const float angle = len / m_arcball_radius;
        m_arcball_rotation = glm::angleAxis(angle, axis) * m_arcball_rotation;
        m_arcball_prev     = planar;
        m_current_angle += angle;
    }
    m_context.transform_tool->adjust_rotation(m_center_of_rotation, m_arcball_rotation);
    return true;
}

// The anchor's twist about the rotation axis at drag start (swing-twist
// decomposition), wrapped to (-pi, pi]. Used for the protractor angle labels
// and the axis-absolute frame, and as the absolute-snap bias.
auto Rotate_tool::initial_twist() const -> float
{
    const float n_length = glm::length(m_normal);
    const vec3  axis     = (n_length > 1e-6f) ? m_normal / n_length : vec3{0.0f, 0.0f, 1.0f};
    const glm::quat q0   = get_shared().world_from_anchor_initial_state.get_rotation();
    float twist = 2.0f * std::atan2(glm::dot(vec3{q0.x, q0.y, q0.z}, axis), q0.w);
    if (twist >  glm::pi<float>()) { twist -= glm::two_pi<float>(); }
    if (twist < -glm::pi<float>()) { twist += glm::two_pi<float>(); }
    return twist;
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
    // Absolute snapping lands the resulting absolute angle around the axis
    // (initial twist + delta) on snap multiples; relative snapping snaps the
    // drag delta itself, preserving any initial off-grid angle.
    const float bias = m_context.editor_settings->transform_tool.rotate_snap_absolute ? initial_twist() : 0.0f;
    return std::floor((bias + angle_radians + snap * 0.5f) / snap) * snap - bias;
}

auto Rotate_tool::update(Scene_view* scene_view) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (scene_view == nullptr) {
        return false;
    }

    if (m_free_mode) {
        return update_arcball(scene_view);
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
    // View-mode rotation happens about the viewing axis, which is not a
    // basis axis (get_axis_direction() has no mapping for it).
    const vec3  rotation_axis_in_world = m_view_mode ? m_normal : get_axis_direction();
    const mat4  rotation               = erhe::math::create_rotation<float>  (snapped_angle, rotation_axis_in_world);

    m_current_angle = angle;

    m_context.transform_tool->adjust_rotation(m_center_of_rotation, glm::quat_cast(rotation));
}

void Rotate_tool::render(const Render_context& context)
{
    if (!is_active()) {
        return;
    }
    // Arcball rotation has no single rotation plane - no protractor.
    if (m_free_mode) {
        return;
    }

    const auto* camera_node = context.get_camera_node();
    if (camera_node == nullptr) {
        return;
    }

    const auto& shared = get_shared();
    const vec3  p = m_center_of_rotation;
    const vec3  n = m_normal;

    // The anchor's twist about the rotation axis at drag start; used for the
    // angle labels in both anchoring modes and for the protractor frame in
    // axis-absolute mode.
    const float n_length = glm::length(n);
    const vec3  axis     = (n_length > 1e-6f) ? n / n_length : vec3{0.0f, 0.0f, 1.0f};
    const float initial  = initial_twist();

    // Protractor frame: drag-relative anchors theta = 0 (the initial spoke)
    // to the pointer position at drag start; axis-absolute anchors theta = 0
    // to the active coordinate space's plane side, placing the initial spoke
    // at the anchor's absolute twist angle around the rotation axis. The
    // drag delta amount is computed identically in both modes.
    const bool axis_absolute =
        context.app_context.editor_settings->transform_tool.rotate_sector_anchoring == Rotate_sector_anchoring::axis_absolute;
    vec3 absolute_side = m_axis_side;
    if (axis_absolute && shared.settings.use_anchor_orientation()) {
        // In local space the plane side is the anchor's own rotated basis
        // and already contains the twist; placing the initial spoke at the
        // twist angle on top of that would show the rotation twice. Untwist
        // the frame so theta 0 is the anchor's zero-rotation direction and
        // the spoke at `initial` lands on the anchor's actual side (this
        // also keeps the tick grid fixed across successive drags).
        absolute_side = glm::angleAxis(-initial, axis) * m_axis_side;
    }
    const vec3  side1 = axis_absolute ? absolute_side : m_reference_direction;
    const vec3  side2 = normalize(cross(n, side1));
    const float a0    = axis_absolute ? initial : 0.0f;


    // Same world-per-gizmo-unit scale as the handle rendering, so the
    // protractor ring radius matches the handles placed at it (the in-plane
    // translate arrows start at the ring during the drag).
    const Handle_visualizations* visualizations = shared.get_visualizations();
    const float scale = (visualizations != nullptr)
        ? visualizations->get_view_scale()
        : context.app_context.editor_settings->transform_tool.gizmo_scale * length(p - vec3{camera_node->position_in_world()}) / 100.0f;
    const float r1 = scale * context.app_context.editor_settings->transform_tool.rotate_ring_size;
    const float snapped_angle     = snap(m_current_angle);
    const float a1                = a0 + snapped_angle;

    // Sector membership in the protractor frame: the swept sector runs from
    // the initial spoke (a0) by the snapped drag angle.
    const auto wrap_pi = [](float a) -> float {
        while (a >  glm::pi<float>()) { a -= glm::two_pi<float>(); }
        while (a < -glm::pi<float>()) { a += glm::two_pi<float>(); }
        return a;
    };
    const auto in_sector = [&wrap_pi, a0, snapped_angle](const float theta) -> bool {
        const float d = wrap_pi(theta - a0);
        return (snapped_angle >= 0.0f)
            ? (d >= 0.0f) && (d <= snapped_angle)
            : (d >= snapped_angle) && (d <= 0.0f);
    };

    const vec4     axis_color = get_axis_color(m_axis_mask, context.app_context.editor_settings->transform_tool);
    constexpr vec4 yellow{1.0f, 1.0f, 0.0f, 1.0f};

    // Axis-colored protractor lines are thinner than the yellow swept-sector
    // lines so the sector reads as the emphasized element.
    constexpr float axis_line_width   = -1.0f;  // negative = constant screen-space pixels
    constexpr float sector_line_width = -1.41f;

    // Background disc behind the protractor: black at a configurable alpha,
    // to lift the ring / step markers off busy content. Depth-tested,
    // visible pass only, NO x-ray - it must never cover geometry in front of
    // the rotation plane (usually the subject being rotated). Stencil
    // reference 1 keeps it under every transform-tool bucket (all >= 2), so
    // the protractor lines always draw over it regardless of order.
    const float background_alpha = context.app_context.editor_settings->transform_tool.rotate_background_alpha;
    if (background_alpha > 0.0f) {
        erhe::renderer::Primitive_renderer background_renderer = context.get(
            erhe::renderer::Debug_renderer_config{
                .primitive_type    = erhe::graphics::Primitive_type::triangle,
                .stencil_reference = 1,
                .draw_visible      = true,
                .draw_hidden       = false,
                .xray              = false
            }
        );
        constexpr int background_segment_count = 96;
        // Slightly past the ring so the ring line itself sits on the disc.
        const float background_radius = 1.05f * r1;
        std::vector<vec3> disc_positions;
        disc_positions.reserve(background_segment_count + 1);
        disc_positions.push_back(p);
        std::vector<uint32_t> disc_indices;
        disc_indices.reserve(3 * static_cast<size_t>(background_segment_count));
        for (int i = 0; i < background_segment_count; ++i) {
            const float theta = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(background_segment_count);
            disc_positions.push_back(p + background_radius * (std::cos(theta) * side1 + std::sin(theta) * side2));
            disc_indices.insert(
                disc_indices.end(),
                {0u, static_cast<uint32_t>(1 + i), static_cast<uint32_t>(1 + ((i + 1) % background_segment_count))}
            );
        }
        background_renderer.add_triangles(glm::mat4{1.0f}, vec4{0.0f, 0.0f, 0.0f, background_alpha}, disc_positions, disc_indices);
    }

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
        // Step markers on a fixed grid of the protractor frame: small ticks
        // every 5 deg, medium every 10 deg, major every 45 deg (inner radii
        // as fractions of the ring radius so the marks scale with the
        // configurable ring size). The small / medium ticks are thinner than
        // the spokes and the ring so they stay background reference marks;
        // the majors keep the full spoke / sector widths. Yellow inside the
        // swept sector, axis color outside.
        constexpr float tick_line_width        = -0.71f;
        constexpr float sector_tick_line_width = -1.0f;
        std::vector<erhe::renderer::Line> inside_major_ticks;
        std::vector<erhe::renderer::Line> inside_minor_ticks;
        std::vector<erhe::renderer::Line> outside_major_ticks;
        std::vector<erhe::renderer::Line> outside_minor_ticks;
        constexpr int tick_count = 72;  // one tick per 5 deg
        for (int i = 0; i < tick_count; ++i) {
            const float theta  = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(tick_count);
            const bool  major  = (i % 9 == 0);  // every 45 deg
            const bool  medium = (i % 2 == 0);  // every 10 deg
            const float r0     = major ? r1 * (5.0f / 6.0f) : medium ? r1 * 0.89f : r1 * 0.94f;
            const vec3  p0     = p + r0 * std::cos(theta) * side1 + r0 * std::sin(theta) * side2;
            const vec3  p1     = p + r1 * std::cos(theta) * side1 + r1 * std::sin(theta) * side2;
            const bool  inside = in_sector(theta);
            auto& bucket = inside
                ? (major ? inside_major_ticks  : inside_minor_ticks)
                : (major ? outside_major_ticks : outside_minor_ticks);
            bucket.push_back({p0, p1});
        }
        line_renderer.set_line_color(axis_color);
        line_renderer.set_thickness(tick_line_width);
        line_renderer.add_lines(outside_minor_ticks);
        line_renderer.set_thickness(axis_line_width);
        line_renderer.add_lines(outside_major_ticks);
        line_renderer.set_line_color(yellow);
        line_renderer.set_thickness(sector_tick_line_width);
        line_renderer.add_lines(inside_minor_ticks);
        line_renderer.set_thickness(sector_line_width);
        line_renderer.add_lines(inside_major_ticks);

        // Initial-angle indicator: full spoke from the center in yellow -
        // the sector boundary / delta color, like the current-angle
        // indicator drawn after the ring.
        const vec3 initial_dir = std::cos(a0) * side1 + std::sin(a0) * side2;
        line_renderer.set_thickness(axis_line_width);
        line_renderer.set_line_color(yellow);
        line_renderer.add_lines({{ p, p + r1 * initial_dir }});
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
            const float mid = 0.5f * (theta0 + theta1);
            (in_sector(mid) ? inside_segments : outside_segments).push_back({p0, p1});
        }
        line_renderer.set_thickness(axis_line_width);
        line_renderer.set_line_color(axis_color);
        line_renderer.add_lines(outside_segments);
        line_renderer.set_thickness(sector_line_width);
        line_renderer.set_line_color(yellow);
        line_renderer.add_lines(inside_segments);
    }

    const auto snapped = p + r1 * std::cos(a1) * side1 + r1 * std::sin(a1) * side2;

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
            const float theta = a0 + snapped_angle * static_cast<float>(i) / static_cast<float>(sector_count);
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

    // Current-angle indicator spoke in yellow (sector boundary / delta
    // color), and the rotation axis itself through the center in the
    // rotation-axis color - two segments meeting at the rotation center
    // with alpha fading to zero there, so the axis reads at its ends
    // without covering the protractor center.
    line_renderer.set_thickness(axis_line_width);
    line_renderer.add_lines(yellow, { { p, snapped } } );
    const vec4 axis_clear{vec3{axis_color}, 0.0f};
    line_renderer.add_line(axis_color, axis_line_width, p - r1 * axis, axis_clear, axis_line_width, p);
    line_renderer.add_line(axis_clear, axis_line_width, p,             axis_color, axis_line_width, p + r1 * axis);

    // Angle readout at the ring: the anchor's twist about the rotation axis
    // at drag start (swing-twist decomposition), printed just outside the
    // ring at the initial (reference) direction; the current value - initial
    // plus the snapped drag angle, so it stays continuous past +/-180 deg -
    // printed the same way at the current direction.
    erhe::renderer::Text_renderer* text_renderer = m_context.text_renderer;
    // viewport_scene_view null = headset render context: text readouts are
    // window-space, so no text in XR (for now).
    if ((text_renderer == nullptr) || !text_renderer->config.enabled || (context.camera == nullptr) || (context.viewport_scene_view == nullptr)) {
        return;
    }
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
    const float bisector_angle = a0 + 0.5f * snapped_angle;

    const auto project = [&](const vec3 position_in_world) -> vec3 {
        return context.viewport.project_to_screen_space(
            clip_from_world, position_in_world, 0.0f, 1.0f, context.scene_view.get_conventions()
        );
    };
    const vec3 initial_anchor = project(p + label_radius * (std::cos(a0)             * side1 + std::sin(a0)             * side2));
    const vec3 current_anchor = project(p + label_radius * (std::cos(a1)             * side1 + std::sin(a1)             * side2));
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
    // vertical_align: -1 = text above the anchor, +1 = below, 0 = ink box
    // centered on the anchor (not baseline-on-anchor: a baseline placement
    // hangs most of the text above the anchor, so it crowds whatever is
    // above it while leaving unused room below).
    const auto place = [&](const vec3& anchor, const erhe::ui::Rectangle& bounds, const int vertical_align) -> Placed_label {
        const float x_print  = anchor.x - 0.5f * bounds.size().x;
        const float y_center = 0.5f * (bounds.min().y + bounds.max().y);
        if (top_left) {
            float y_print = anchor.y + y_center;
            if (vertical_align < 0) { y_print = anchor.y + bounds.min().y; }
            if (vertical_align > 0) { y_print = anchor.y + bounds.max().y; }
            return Placed_label{
                {x_print, y_print},
                {x_print + bounds.min().x, y_print - bounds.max().y},
                {x_print + bounds.max().x, y_print - bounds.min().y}
            };
        }
        float y_print = anchor.y - y_center;
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
    // into the initial or current label. The test is a rect intersection, so
    // horizontal separation counts as fitting just like vertical separation.
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
