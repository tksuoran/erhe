#include "transform/handle_visualizations.hpp"

#include "app_context.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "editor_log.hpp"
#include "graphics/icon_set.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_view.hpp"
#include "transform/handle_enums.hpp"
#include "transform/move_tool.hpp"
#include "transform/rotate_tool.hpp"
#include "transform/scale_tool.hpp"
#include "transform/transform_tool.hpp"

#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_hash/xxhash.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <optional>
#include <vector>

namespace editor {

using glm::vec3;
using glm::vec4;
using glm::mat3;
using glm::mat4;
using glm::dot;
using glm::cross;
using glm::normalize;

namespace {

// All dimensions are in gizmo units; the view-dependent m_view_scale converts
// them to world units so the gizmo keeps a constant on-screen size.
constexpr float arrow_shaft_length     = 2.75f;
constexpr float arrow_cone_length      = 0.6f;
constexpr float arrow_cone_radius      = 0.15f;                     // scale-axis tip cones
constexpr float translate_cone_radius  = 2.0f * arrow_cone_radius;  // translate arrows: wider base, thinner shaft (below)
constexpr float arrow_tip              = arrow_shaft_length + arrow_cone_length;
constexpr float arrow_shaft_pick_radius= 0.12f;
constexpr float arrow_head_pick_end    = arrow_shaft_length + 2.0f * arrow_cone_length;
constexpr float arrow_head_pick_radius = 0.45f;

constexpr float plane_half_extent      = 0.6f;
constexpr float plane_pick_half_extent = 0.78f;

constexpr float rotate_ring_major_radius = 4.0f;
constexpr float ring_pick_radius         = 0.2f;
constexpr int   ring_arc_sample_count    = 128;

// Camera-aligned view-rotate ring: light gray, outside the rotate sphere
// with a gap. Dragging it rotates around the viewing axis.
constexpr float view_ring_radius = 1.3f * rotate_ring_major_radius;
constexpr vec4  view_ring_color{0.7f, 0.7f, 0.7f, 1.0f};

// Positive-only translate mode: the plane quads sit at the gizmo center
// with their min corner ON the center, so the three quads share edges
// along the positive axes. Being deep inside the rotate sphere, they read
// as a distinct inner cluster instead of fighting the rings for space;
// the axis arrows move OUTSIDE the rotate sphere instead (arrow_start
// below), so translate handles and rings never contest the same radius.
constexpr float plane_positive_offset = plane_half_extent;

// Axis arrows start outside the rotate sphere (3D radius; the arrows are
// radial, so only a view nearly along the axis foreshortens one into the
// projected sphere - and that arrow is degenerate for translation anyway).
constexpr float arrow_ring_gap = 0.25f;
constexpr float arrow_start    = rotate_ring_major_radius + arrow_ring_gap;

// With the rotate rings hidden (show_rotate off) there is nothing to avoid:
// the quads always extend into the positive octant (no camera-facing flip)
// and each positive arrow starts right where the quads end along its axis.
constexpr float plane_positive_end = 2.0f * plane_half_extent;

constexpr float center_cube_half_length  = 0.25f;
constexpr float center_cube_pick_radius  = 0.5f;

constexpr float box_scale_cone_length    = 0.6f;
constexpr float box_scale_cone_radius    = 0.2f;

constexpr int   cone_side_count          = 16;

constexpr float line_width_normal   = -3.0f; // negative = constant screen-space pixels
constexpr float line_width_hot      = -4.5f;
// Translate arrow shafts: half the shared handle width - the thin stem plus
// the wide cone base (translate_cone_radius) reads as a pointer, not a bar.
constexpr float arrow_shaft_width_normal = 0.5f * line_width_normal;
constexpr float arrow_shaft_width_hot    = 0.5f * line_width_hot;
// Rotate ring arcs: slightly thinner than the shared handle width. The
// hot (hover/active) arc is thinner still - narrower than the resting
// arc; the hot emphasis comes from the brightened color, not width.
constexpr float ring_width_normal   = 0.75f * line_width_normal;
constexpr float ring_width_hot      = 0.5f * 0.75f * line_width_hot;
constexpr float plane_outline_width = -2.0f;
constexpr float plane_fill_alpha    = 0.5f;

// Base colors matching the former handle mesh materials (X / Y / Z / uniform).
constexpr vec4 axis_colors[3] = {
    vec4{1.00f, 0.00f, 0.0f, 1.0f},
    vec4{0.23f, 1.00f, 0.0f, 1.0f},
    vec4{0.00f, 0.23f, 1.0f, 1.0f}
};
constexpr vec4 xyz_color        {0.70f, 0.70f, 0.7f, 1.0f};
constexpr vec4 box_outline_color{1.00f, 0.70f, 0.1f, 1.0f};

// All handle rendering uses the x-ray buckets: the occluded pass blends at
// full strength instead of the dim hidden-pass constant, so the gizmo stays
// readable inside content meshes.
constexpr erhe::renderer::Debug_renderer_config handle_line_config{
    .primitive_type    = erhe::graphics::Primitive_type::line,
    .stencil_reference = 2,
    .draw_visible      = true,
    .draw_hidden       = true,
    .xray              = true
};
constexpr erhe::renderer::Debug_renderer_config handle_fill_config{
    .primitive_type    = erhe::graphics::Primitive_type::triangle,
    .stencil_reference = 2,
    .draw_visible      = true,
    .draw_hidden       = true,
    .xray              = true
};
// The debug buckets layer first-wins per pixel (stencil greater/replace at a
// shared reference), and line vs triangle buckets have no defined mutual
// order - so "arrows in front of rings" cannot come from submission order.
// The translate arrows use a higher stencil reference instead: their pixels
// beat every reference-2 gizmo pixel no matter which bucket drew first.
constexpr erhe::renderer::Debug_renderer_config arrow_line_config{
    .primitive_type    = erhe::graphics::Primitive_type::line,
    .stencil_reference = 3,
    .draw_visible      = true,
    .draw_hidden       = true,
    .xray              = true
};
constexpr erhe::renderer::Debug_renderer_config arrow_fill_config{
    .primitive_type    = erhe::graphics::Primitive_type::triangle,
    .stencil_reference = 3,
    .draw_visible      = true,
    .draw_hidden       = true,
    .xray              = true
};

constexpr Handle translate_pos_handles[3] = {
    Handle::e_handle_translate_pos_x, Handle::e_handle_translate_pos_y, Handle::e_handle_translate_pos_z
};
constexpr Handle translate_neg_handles[3] = {
    Handle::e_handle_translate_neg_x, Handle::e_handle_translate_neg_y, Handle::e_handle_translate_neg_z
};
// Plane handles indexed by their perpendicular axis, matching the axis whose
// color they use (X -> YZ, Y -> XZ, Z -> XY).
constexpr Handle translate_plane_handles[3] = {
    Handle::e_handle_translate_yz, Handle::e_handle_translate_xz, Handle::e_handle_translate_xy
};
constexpr Handle scale_plane_handles[3] = {
    Handle::e_handle_scale_yz, Handle::e_handle_scale_xz, Handle::e_handle_scale_xy
};
constexpr Handle ring_handles[3] = {
    Handle::e_handle_rotate_x, Handle::e_handle_rotate_y, Handle::e_handle_rotate_z
};
constexpr Handle scale_axis_handles[3] = {
    Handle::e_handle_scale_x, Handle::e_handle_scale_y, Handle::e_handle_scale_z
};
constexpr Handle box_scale_pos_handles[3] = {
    Handle::e_handle_box_scale_pos_x, Handle::e_handle_box_scale_pos_y, Handle::e_handle_box_scale_pos_z
};
constexpr Handle box_scale_neg_handles[3] = {
    Handle::e_handle_box_scale_neg_x, Handle::e_handle_box_scale_neg_y, Handle::e_handle_box_scale_neg_z
};

// Rotation-ring arc visibility (rotate_visible_arcs_only): the shown rings
// are treated as a ball of mutually perpendicular discs. A point on one
// ring is occluded - not drawn and not pickable - when the sight line from
// the eye to it passes through the disc of another SHOWN ring first; a
// hidden ring's disc covers nothing, so with a single ring shown the whole
// ring is visible.
auto is_ring_point_occluded(
    const vec3&  eye,
    const vec3&  point,
    const vec3&  center,
    const mat3&  basis,
    const float  radius,
    const int    axis,
    const bool   ring_shown[3]
) -> bool
{
    const vec3  eye_to_point = point - eye;
    const float sight_length = glm::length(eye_to_point);
    for (int other = 0; other < 3; ++other) {
        if ((other == axis) || !ring_shown[other]) {
            continue;
        }
        const vec3  n     = basis[other];
        const float denom = dot(eye_to_point, n);
        if (std::abs(denom) < 1.0e-6f * sight_length) {
            continue; // sight line grazes the disc plane
        }
        // t == 1 is the ring point itself; the rings cross the other discs'
        // planes exactly on the disc boundary, so exclude the endpoint.
        const float t = dot(center - eye, n) / denom;
        if ((t <= 0.0f) || (t >= 1.0f - 1.0e-3f)) {
            continue;
        }
        const vec3 q = eye + t * eye_to_point;
        if (glm::length(q - center) <= radius) {
            return true;
        }
    }
    return false;
}

void draw_cone_fill(
    erhe::renderer::Primitive_renderer& triangle_renderer,
    const vec4&  color,
    const vec3&  base_center,
    const vec3&  direction,
    const vec3&  side1,
    const vec3&  side2,
    const float  length,
    const float  radius
)
{
    std::vector<vec3>     positions;
    std::vector<uint32_t> indices;
    positions.reserve(cone_side_count + 2);
    indices  .reserve(cone_side_count * 6);
    for (int k = 0; k < cone_side_count; ++k) {
        const float theta = glm::two_pi<float>() * static_cast<float>(k) / static_cast<float>(cone_side_count);
        positions.push_back(base_center + radius * (std::cos(theta) * side1 + std::sin(theta) * side2));
    }
    const uint32_t apex_index = cone_side_count;
    const uint32_t base_index = cone_side_count + 1;
    positions.push_back(base_center + length * direction);
    positions.push_back(base_center);
    for (uint32_t k = 0; k < static_cast<uint32_t>(cone_side_count); ++k) {
        const uint32_t k2 = (k + 1) % cone_side_count;
        indices.push_back(k);
        indices.push_back(k2);
        indices.push_back(apex_index);
        indices.push_back(k2);
        indices.push_back(k);
        indices.push_back(base_index);
    }
    triangle_renderer.add_triangles(mat4{1.0f}, color, positions, indices);
}

void draw_quad(
    erhe::renderer::Primitive_renderer& line_renderer,
    erhe::renderer::Primitive_renderer& triangle_renderer,
    const vec4&  color,
    const vec3&  center,
    const vec3&  u_half, // center-to-edge vector along u
    const vec3&  v_half
)
{
    const vec3 corners[4] = {
        center - u_half - v_half,
        center + u_half - v_half,
        center + u_half + v_half,
        center - u_half + v_half
    };
    const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    triangle_renderer.add_triangles(mat4{1.0f}, vec4{vec3{color}, plane_fill_alpha}, corners, indices);
    line_renderer.set_thickness(plane_outline_width);
    line_renderer.add_lines(
        color,
        {
            {corners[0], corners[1]},
            {corners[1], corners[2]},
            {corners[2], corners[3]},
            {corners[3], corners[0]}
        }
    );
}

void draw_cube_fill(
    erhe::renderer::Primitive_renderer& triangle_renderer,
    const vec4&  color,
    const vec3&  center,
    const mat3&  basis,
    const float  half_length
)
{
    std::vector<vec3> positions;
    positions.reserve(8);
    for (int i = 0; i < 8; ++i) {
        const vec3 offset =
            ((i & 1) != 0 ? half_length : -half_length) * basis[0] +
            ((i & 2) != 0 ? half_length : -half_length) * basis[1] +
            ((i & 4) != 0 ? half_length : -half_length) * basis[2];
        positions.push_back(center + offset);
    }
    static constexpr uint32_t indices[36] = {
        0, 2, 3,  0, 3, 1,  // -z .. face windings are irrelevant (no culling in the debug pass)
        4, 5, 7,  4, 7, 6,
        0, 1, 5,  0, 5, 4,
        2, 6, 7,  2, 7, 3,
        0, 4, 6,  0, 6, 2,
        1, 3, 7,  1, 7, 5
    };
    triangle_renderer.add_triangles(mat4{1.0f}, color, positions, indices);
}

// Closest approach between a ray and a segment [a, b]; returns false when the
// closest ray point lies behind the ray origin.
auto ray_segment_distance(
    const vec3& ray_origin,
    const vec3& ray_direction, // unit
    const vec3& a,
    const vec3& b,
    float&      t_out,
    vec3&       closest_on_segment_out,
    float&      distance_out
) -> bool
{
    const vec3  u  = b - a;
    const vec3  r  = a - ray_origin;
    const float bb = dot(ray_direction, u);
    const float cc = dot(u, u);
    const float dr = dot(ray_direction, r);
    const float ur = dot(u, r);
    const float denom = cc - bb * bb;
    float s = (std::abs(denom) > 1.0e-9f)
        ? (dr * bb - ur) / denom
        : 0.0f;
    s = glm::clamp(s, 0.0f, 1.0f);
    const vec3  q = a + s * u;
    const float t = dot(q - ray_origin, ray_direction);
    if (t <= 0.0f) {
        return false;
    }
    t_out                  = t;
    closest_on_segment_out = q;
    distance_out           = glm::length(q - (ray_origin + t * ray_direction));
    return true;
}

}

Handle_visualizations::Handle_visualizations(App_context& app_context)
    : m_context{app_context}
{
}

void Handle_visualizations::update_for_view(Scene_view* scene_view)
{
    // TODO also consider fov
    m_scene_view = scene_view;
    if (scene_view == nullptr) {
        return;
    }

    const auto camera = scene_view->get_camera();
    if (!camera) {
        return;
    }
    const auto* camera_node = camera->get_node();
    if (camera_node == nullptr) {
        return;
    }

    const glm::vec3 view_position_in_world   = glm::vec3{camera_node->position_in_world()};
    const glm::vec3 anchor_position_in_world = glm::vec3{m_world_from_anchor.get_translation()};
    m_view_distance = glm::length(anchor_position_in_world - glm::vec3{view_position_in_world});
    if (!std::isfinite(m_view_distance)) {
        log_trs_tool->error("!isfinite()");
    }
}

auto Handle_visualizations::get_gizmo_radius() const -> float
{
    return rotate_ring_major_radius * m_view_scale;
}

auto Handle_visualizations::get_view_scale() const -> float
{
    return m_view_scale;
}

auto Handle_visualizations::get_view_ring_radius() const -> float
{
    return view_ring_radius * m_view_scale;
}

void Handle_visualizations::update_transforms()
{
    ERHE_PROFILE_FUNCTION();

    if (m_scene_view == nullptr) {
        return;
    }

    const float distance_scale    = m_context.editor_settings->gizmo_scale * m_view_distance / 100.0f;
    const float perspective_scale = m_scene_view->get_perspective_scale();
    const float scalar_scale      = distance_scale * perspective_scale;
    if (!std::isfinite(scalar_scale)) {
        log_trs_tool->error("!isfinite()");
    }
    m_view_scale = scalar_scale;

    compute_selection_box();
}

void Handle_visualizations::set_anchor(const erhe::scene::Trs_transform& world_from_anchor)
{
    m_world_from_anchor = world_from_anchor;
}

auto Handle_visualizations::get_basis() const -> mat3
{
    // The gizmo follows only the anchor's position and orientation, never its
    // scale or skew; Global reference mode strips the orientation back to
    // world axes.
    return m_context.transform_tool->shared.settings.use_anchor_orientation()
        ? glm::mat3_cast(m_world_from_anchor.get_rotation())
        : mat3{1.0f};
}

auto Handle_visualizations::has_target() const -> bool
{
    const Transform_tool_shared& shared = m_context.transform_tool->shared;
    return !shared.entries.empty() || shared.component_mode;
}

auto Handle_visualizations::is_handle_shown(const Handle handle) const -> bool
{
    Transform_tool*                transform_tool = m_context.transform_tool;
    const Transform_tool_settings& settings       = transform_tool->shared.settings;

    switch (get_handle_tool(handle)) {
        case Handle_tool::e_handle_tool_translate: if (!settings.show_translate) { return false; } break;
        case Handle_tool::e_handle_tool_rotate:    if (!settings.show_rotate)    { return false; } break;
        case Handle_tool::e_handle_tool_scale:     if (!settings.show_scale)     { return false; } break;
        default: return false;
    }

    const Handle_type type     = get_handle_type(handle);
    const bool        box_mode = settings.scale_gizmo_mode == Scale_gizmo_mode::bounding_box;
    if ((handle == Handle::e_handle_rotate_view) && !m_context.editor_settings->transform_tool.rotate_view_ring) {
        return false;
    }
    if ((handle == Handle::e_handle_rotate_free) && !m_context.editor_settings->transform_tool.rotate_arcball) {
        return false;
    }
    if (box_mode && ((type == Handle_type::e_handle_type_scale_axis) || (type == Handle_type::e_handle_type_scale_plane))) {
        return false;
    }
    // The uniform-scale cube is shown in both scale gizmo modes: the
    // bounding-box cones only offer per-axis scaling.
    if (!box_mode && (type == Handle_type::e_handle_type_box_scale)) {
        return false;
    }

    // Single-arrow translate mode (translate_negative_handles off) shows one
    // direction arrow per axis - the camera-facing one. That choice is
    // view-dependent, so it is made in render() and pick() (which know the
    // eye), not here; both direction handles count as shown at this level.

    // While a drag is active, show only the handles whose constraint exactly
    // matches the dragged one: an axis drag keeps both direction arrows of
    // that axis, a plane drag keeps only the plane handle.
    const Handle active_handle = transform_tool->get_active_handle();
    if (settings.hide_inactive && (active_handle != Handle::e_handle_none) && (handle != active_handle)) {
        const unsigned int axis_mask       = get_axis_mask(handle);
        const bool         translate_match = m_context.move_tool ->is_active() && (m_context.move_tool ->get_axis_mask() == axis_mask);
        const bool         scale_match     = m_context.scale_tool->is_active() && ((m_context.scale_tool->get_axis_mask() & axis_mask) == axis_mask);
        // A rotate drag keeps some translate arrows, repositioned by
        // render() and pick() to start at the protractor ring radius:
        // - LOCAL space only: the arrows lying in the rotation plane (axes
        //   orthogonal to the rotation axis) - they rotate with the anchor,
        //   showing the orientation change. In world space they would sit
        //   static, so they are hidden like the other inactive handles.
        // - All spaces: the arrow along the rotation axis itself, starting
        //   at the tip of the rotation axis line the protractor draws.
        const bool         rotate_drag_active = m_context.rotate_tool->is_active();
        const unsigned int rotate_axis_mask   = rotate_drag_active ? m_context.rotate_tool->get_axis_mask() : 0u;
        const bool rotate_in_plane =
            rotate_drag_active &&
            settings.use_anchor_orientation() &&
            (type == Handle_type::e_handle_type_translate_axis) &&
            ((rotate_axis_mask & axis_mask) == 0);
        const bool rotate_axis_arrow =
            rotate_drag_active &&
            (type == Handle_type::e_handle_type_translate_axis) &&
            (rotate_axis_mask == axis_mask);
        if (!translate_match && !scale_match && !rotate_in_plane && !rotate_axis_arrow) {
            return false;
        }
    }

    // Every rotate ring is hidden during ANY active drag (and with them the
    // single-shown-ring tangent/bitangent guides): a rotate drag shows the
    // Rotate_tool protractor instead, and translate/scale drags draw their
    // own travel guides. Without this, a same-axis ring would pass the
    // axis-mask match above and ride along with axis drags.
    if ((active_handle != Handle::e_handle_none) && (type == Handle_type::e_handle_type_rotate)) {
        return false;
    }

    return true;
}

void Handle_visualizations::render(const Render_context& context, const Handle hover_handle, const Handle active_handle)
{
    ERHE_PROFILE_FUNCTION();

    if (!has_target()) {
        return;
    }
    const auto* camera_node = context.get_camera_node();
    if (camera_node == nullptr) {
        return;
    }
    const float s = m_view_scale;
    if (!(s > 0.0f) || !std::isfinite(s)) {
        return;
    }

    const vec3 c     = m_world_from_anchor.get_translation();
    const mat3 basis = get_basis();
    const vec3 eye   = vec3{camera_node->position_in_world()};

    erhe::renderer::Primitive_renderer line_renderer           = context.get(handle_line_config);
    erhe::renderer::Primitive_renderer triangle_renderer       = context.get(handle_fill_config);
    erhe::renderer::Primitive_renderer arrow_line_renderer     = context.get(arrow_line_config);
    erhe::renderer::Primitive_renderer arrow_triangle_renderer = context.get(arrow_fill_config);

    const auto is_hot = [&](const Handle handle) {
        return (hover_handle == handle) || (active_handle == handle);
    };
    const auto handle_color = [&](const Handle handle, const vec4& base) {
        return is_hot(handle) ? vec4{2.0f * vec3{base}, base.a} : base;
    };

    const bool positive_only = !m_context.editor_settings->transform_tool.translate_negative_handles;
    // With no rotate rings shown (the show_rotate toggle, not transient drag
    // hiding - placement must not jump mid-drag) the camera-facing choices
    // exist to dodge nothing: the quads sit in the positive octant and the
    // positive arrow of each axis starts where the quads end.
    const bool fixed_octant = positive_only && !m_context.transform_tool->shared.settings.show_rotate;

    // Translate planes
    for (int perp = 0; perp < 3; ++perp) {
        const Handle handle = translate_plane_handles[perp];
        if (!is_handle_shown(handle)) {
            continue;
        }
        // Camera-facing quadrant: like the single-arrow choice above, each
        // in-plane axis flips toward the eye, so the quad extends on the
        // near side of the gizmo and stays clear of the visible rotation
        // arcs (which hug the far reaches of the projected sphere).
        const float offset = positive_only ? plane_positive_offset : 0.0f;
        const vec3  u      = basis[(perp + 1) % 3];
        const vec3  v      = basis[(perp + 2) % 3];
        const vec3  su     = (fixed_octant || (dot(u, eye - c) >= 0.0f)) ? u : -u;
        const vec3  sv     = (fixed_octant || (dot(v, eye - c) >= 0.0f)) ? v : -v;
        const vec3  center = c + (s * offset) * (su + sv);
        draw_quad(line_renderer, triangle_renderer, handle_color(handle, axis_colors[perp]), center, (s * plane_half_extent) * su, (s * plane_half_extent) * sv);
    }

    // Rotate rings. Occluded arc segments (arcs_only mode) are not drawn at
    // all - matching pick(), where they are not pickable. Only SHOWN rings
    // act as occluders, so a single shown ring renders as the full circle.
    {
        const bool  arcs_only = m_context.editor_settings->transform_tool.rotate_visible_arcs_only;
        const float radius    = s * rotate_ring_major_radius;
        const bool  ring_shown[3] = {
            is_handle_shown(ring_handles[0]),
            is_handle_shown(ring_handles[1]),
            is_handle_shown(ring_handles[2])
        };
        const int shown_count =
            (ring_shown[0] ? 1 : 0) + (ring_shown[1] ? 1 : 0) + (ring_shown[2] ? 1 : 0);
        std::vector<erhe::renderer::Line> visible_lines;
        visible_lines.reserve(ring_arc_sample_count);
        for (int axis = 0; axis < 3; ++axis) {
            if (!ring_shown[axis]) {
                continue;
            }
            const Handle handle = ring_handles[axis];
            const vec3 side1 = basis[(axis + 1) % 3];
            const vec3 side2 = basis[(axis + 2) % 3];
            visible_lines.clear();
            vec3 previous        {0.0f};
            bool previous_visible{false};
            for (int i = 0; i <= ring_arc_sample_count; ++i) {
                const float theta   = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(ring_arc_sample_count);
                const vec3  normal  = std::cos(theta) * side1 + std::sin(theta) * side2;
                const vec3  point   = c + radius * normal;
                const bool  visible = !arcs_only || !is_ring_point_occluded(eye, point, c, basis, radius, axis, ring_shown);
                if ((i > 0) && visible && previous_visible) {
                    visible_lines.push_back({previous, point});
                }
                previous         = point;
                previous_visible = visible;
            }
            const vec4  color = handle_color(handle, axis_colors[axis]);
            const float width = is_hot(handle) ? ring_width_hot : ring_width_normal;
            if (!visible_lines.empty()) {
                line_renderer.set_thickness(width);
                line_renderer.set_line_color(color);
                line_renderer.add_lines(visible_lines);
            }
            // Single shown ring (e.g. during a rotate drag): tangent and
            // bitangent guides in the ring plane, from the axes' intersection
            // point (the gizmo center) out to the arc, fading in toward it.
            if (shown_count == 1) {
                const vec4 transparent = vec4{vec3{color}, 0.0f};
                const vec4 opaque      = vec4{vec3{color}, 1.0f};
                const vec3 directions[4] = { side1, -side1, side2, -side2 };
                for (const vec3& direction : directions) {
                    line_renderer.add_line(transparent, width, c, opaque, width, c + radius * direction);
                }
            }
        }
    }

    // Camera-aligned view-rotate ring (light gray, outside the rotate
    // sphere): dragging it rotates around the viewing axis.
    if (is_handle_shown(Handle::e_handle_rotate_view)) {
        const Handle handle   = Handle::e_handle_rotate_view;
        const vec3   view_dir = normalize(eye - c);
        const vec3   ref      = (std::abs(view_dir.y) < 0.9f) ? vec3{0.0f, 1.0f, 0.0f} : vec3{1.0f, 0.0f, 0.0f};
        const vec3   vs1      = normalize(cross(view_dir, ref));
        const vec3   vs2      = normalize(cross(view_dir, vs1));
        const float  radius   = s * view_ring_radius;
        std::vector<erhe::renderer::Line> lines;
        lines.reserve(ring_arc_sample_count);
        vec3 previous{0.0f};
        for (int i = 0; i <= ring_arc_sample_count; ++i) {
            const float theta = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(ring_arc_sample_count);
            const vec3  point = c + radius * (std::cos(theta) * vs1 + std::sin(theta) * vs2);
            if (i > 0) {
                lines.push_back({previous, point});
            }
            previous = point;
        }
        line_renderer.set_thickness(is_hot(handle) ? ring_width_hot : ring_width_normal);
        line_renderer.set_line_color(handle_color(handle, view_ring_color));
        line_renderer.add_lines(lines);
    }

    // Translate arrows. In single-arrow mode (translate_negative_handles
    // off) each axis shows only the direction facing the camera, so no
    // arrow is drawn receding behind the rotate sphere.
    for (int axis = 0; axis < 3; ++axis) {
        const vec3 d     = basis[axis];
        const vec3 side1 = basis[(axis + 1) % 3];
        const vec3 side2 = basis[(axis + 2) % 3];
        const bool positive_towards_eye = dot(d, eye - c) >= 0.0f;
        const Handle directional_handles[2] = {translate_pos_handles[axis], translate_neg_handles[axis]};
        // During a rotate drag the surviving in-plane arrows start at the
        // protractor ring radius, expanding outward from the ring.
        const float start = m_context.rotate_tool->is_active()
            ? m_context.editor_settings->transform_tool.rotate_ring_size
            : fixed_octant ? plane_positive_end : arrow_start;
        for (int sign = 0; sign < 2; ++sign) {
            if (positive_only && ((sign == 0) != (fixed_octant || positive_towards_eye))) {
                continue;
            }
            const Handle handle = directional_handles[sign];
            if (!is_handle_shown(handle)) {
                continue;
            }
            const vec3 dir   = (sign == 0) ? d : -d;
            const vec4 color = handle_color(handle, axis_colors[axis]);
            arrow_line_renderer.set_thickness(is_hot(handle) ? arrow_shaft_width_hot : arrow_shaft_width_normal);
            arrow_line_renderer.add_lines(color, {{c + (s * start) * dir, c + (s * (start + arrow_shaft_length)) * dir}});
            draw_cone_fill(arrow_triangle_renderer, color, c + (s * (start + arrow_shaft_length)) * dir, dir, side1, side2, s * arrow_cone_length, s * translate_cone_radius);
        }
    }

    // Basic scale: per-axis tip cones (both directions map to one handle),
    // plane quads (always centered) and the uniform-scale center cube.
    for (int axis = 0; axis < 3; ++axis) {
        const Handle handle = scale_axis_handles[axis];
        if (!is_handle_shown(handle)) {
            continue;
        }
        const vec3 d     = basis[axis];
        const vec3 side1 = basis[(axis + 1) % 3];
        const vec3 side2 = basis[(axis + 2) % 3];
        const vec4 color = handle_color(handle, axis_colors[axis]);
        draw_cone_fill(triangle_renderer, color, c + (s * arrow_shaft_length) * d, d, side1, side2, s * arrow_cone_length, s * arrow_cone_radius);
        draw_cone_fill(triangle_renderer, color, c - (s * arrow_shaft_length) * d, -d, side1, side2, s * arrow_cone_length, s * arrow_cone_radius);
    }
    for (int perp = 0; perp < 3; ++perp) {
        const Handle handle = scale_plane_handles[perp];
        if (!is_handle_shown(handle)) {
            continue;
        }
        const vec3 u = basis[(perp + 1) % 3];
        const vec3 v = basis[(perp + 2) % 3];
        draw_quad(line_renderer, triangle_renderer, handle_color(handle, axis_colors[perp]), c, (s * plane_half_extent) * u, (s * plane_half_extent) * v);
    }
    if (is_handle_shown(Handle::e_handle_scale_xyz)) {
        const vec4 color = handle_color(Handle::e_handle_scale_xyz, xyz_color);
        draw_cube_fill(triangle_renderer, color, c, basis, s * center_cube_half_length);
    }

    // Bounding-box scale: box outline plus a cone on each face center. The
    // outline is not tied to a single handle so it stays up during a drag.
    const Transform_tool_settings& settings = m_context.transform_tool->shared.settings;
    if (
        settings.show_scale &&
        (settings.scale_gizmo_mode == Scale_gizmo_mode::bounding_box) &&
        m_box_valid
    ) {
        line_renderer.set_thickness(plane_outline_width);
        line_renderer.add_cube(m_box_frame, box_outline_color, m_box_aabb.min, m_box_aabb.max);

        const mat3 box_basis{m_box_frame};
        for (int axis = 0; axis < 3; ++axis) {
            const vec3 side1  = normalize(box_basis[(axis + 1) % 3]);
            const vec3 side2  = normalize(box_basis[(axis + 2) % 3]);
            const vec3 center = m_box_aabb.center();
            for (int sign = 0; sign < 2; ++sign) {
                const Handle handle = (sign == 0) ? box_scale_pos_handles[axis] : box_scale_neg_handles[axis];
                if (!is_handle_shown(handle)) {
                    continue;
                }
                vec3 face_center_box = center;
                face_center_box[axis] = (sign == 0) ? m_box_aabb.max[axis] : m_box_aabb.min[axis];
                const vec3 base = vec3{m_box_frame * vec4{face_center_box, 1.0f}};
                const vec3 dir  = ((sign == 0) ? 1.0f : -1.0f) * normalize(box_basis[axis]);
                draw_cone_fill(triangle_renderer, handle_color(handle, axis_colors[axis]), base, dir, side1, side2, s * box_scale_cone_length, s * box_scale_cone_radius);
            }
        }
    }
}

auto Handle_visualizations::pick(const glm::vec3& ray_origin, const glm::vec3& ray_direction) const -> std::optional<Handle_pick>
{
    if (!has_target()) {
        return std::nullopt;
    }
    const float s = m_view_scale;
    if (!(s > 0.0f) || !std::isfinite(s)) {
        return std::nullopt;
    }

    const vec3 c     = m_world_from_anchor.get_translation();
    const mat3 basis = get_basis();
    const vec3 d     = normalize(ray_direction);

    std::optional<Handle_pick> best;
    // Strictly-nearer replacement keeps the earlier candidate on ties, so the
    // consider() call order below breaks overlapping-handle ties (translate
    // before scale, matching how the handles visually stack).
    const auto consider = [&best](const Handle handle, const float t, const vec3& position) {
        if (!best.has_value() || (t < best->t)) {
            best = Handle_pick{handle, t, position};
        }
    };

    const bool positive_only = !m_context.editor_settings->transform_tool.translate_negative_handles;
    // Mirrors render()'s fixed-octant presentation with rotate rings hidden.
    const bool fixed_octant  = positive_only && !m_context.transform_tool->shared.settings.show_rotate;

    // Translate arrows: a thin capsule along the shaft plus a fatter capsule
    // around the cone head (matching the old collision geometry). Mirrors
    // render()'s single-arrow choice: only the camera-facing direction of
    // each axis is pickable when translate_negative_handles is off.
    for (int axis = 0; axis < 3; ++axis) {
        const bool positive_towards_eye = dot(basis[axis], ray_origin - c) >= 0.0f;
        const Handle directional_handles[2] = {translate_pos_handles[axis], translate_neg_handles[axis]};
        // Mirrors render(): in-plane arrows sit at the protractor ring
        // during a rotate drag.
        const float start = m_context.rotate_tool->is_active()
            ? m_context.editor_settings->transform_tool.rotate_ring_size
            : fixed_octant ? plane_positive_end : arrow_start;
        for (int sign = 0; sign < 2; ++sign) {
            if (positive_only && ((sign == 0) != (fixed_octant || positive_towards_eye))) {
                continue;
            }
            const Handle handle = directional_handles[sign];
            if (!is_handle_shown(handle)) {
                continue;
            }
            const vec3 dir = ((sign == 0) ? 1.0f : -1.0f) * basis[axis];
            float t{0.0f};
            vec3  q{0.0f};
            float dist{0.0f};
            if (
                ray_segment_distance(ray_origin, d, c + (s * start) * dir, c + (s * (start + arrow_shaft_length)) * dir, t, q, dist) &&
                (dist <= s * arrow_shaft_pick_radius)
            ) {
                consider(handle, t, q);
            }
            if (
                ray_segment_distance(ray_origin, d, c + (s * (start + arrow_shaft_length)) * dir, c + (s * (start + arrow_head_pick_end)) * dir, t, q, dist) &&
                (dist <= s * arrow_head_pick_radius)
            ) {
                consider(handle, t, q);
            }
        }
    }

    // Plane quads (translate, then basic-scale which is always centered).
    const auto pick_plane = [&](const Handle handle, const int perp, const float offset) {
        if (!is_handle_shown(handle)) {
            return;
        }
        const vec3  n     = basis[perp];
        const float denom = dot(d, n);
        if (std::abs(denom) < 1.0e-6f) {
            return;
        }
        // Camera-facing quadrant, mirroring render(): the offset moves the
        // quad center toward the eye's side of each in-plane axis. The
        // bounds check is symmetric around the center, so unsigned u/v axes
        // suffice for it.
        const vec3  u      = basis[(perp + 1) % 3];
        const vec3  v      = basis[(perp + 2) % 3];
        const vec3  su     = (fixed_octant || (dot(u, ray_origin - c) >= 0.0f)) ? u : -u;
        const vec3  sv     = (fixed_octant || (dot(v, ray_origin - c) >= 0.0f)) ? v : -v;
        const vec3  center = c + (s * offset) * (su + sv);
        const float t      = dot(center - ray_origin, n) / denom;
        if (t <= 0.0f) {
            return;
        }
        const vec3  q  = ray_origin + t * d;
        const float uu = dot(q - center, u);
        const float vv = dot(q - center, v);
        if ((std::abs(uu) > s * plane_pick_half_extent) || (std::abs(vv) > s * plane_pick_half_extent)) {
            return;
        }
        consider(handle, t, q);
    };
    for (int perp = 0; perp < 3; ++perp) {
        pick_plane(translate_plane_handles[perp], perp, positive_only ? plane_positive_offset : 0.0f);
    }

    // Uniform-scale center cube, approximated by a sphere.
    if (is_handle_shown(Handle::e_handle_scale_xyz)) {
        const float radius = s * center_cube_pick_radius;
        const float tca    = dot(c - ray_origin, d);
        if (tca > 0.0f) {
            const float d2 = dot(c - ray_origin, c - ray_origin) - tca * tca;
            if (d2 <= radius * radius) {
                const float thc = std::sqrt(radius * radius - d2);
                const float t   = (tca - thc > 0.0f) ? (tca - thc) : tca;
                consider(Handle::e_handle_scale_xyz, t, ray_origin + t * d);
            }
        }
    }

    // Basic-scale tip cones: capsules around both cone heads of each axis.
    for (int axis = 0; axis < 3; ++axis) {
        const Handle handle = scale_axis_handles[axis];
        if (!is_handle_shown(handle)) {
            continue;
        }
        for (int sign = 0; sign < 2; ++sign) {
            const vec3 dir = ((sign == 0) ? 1.0f : -1.0f) * basis[axis];
            float t{0.0f};
            vec3  q{0.0f};
            float dist{0.0f};
            if (
                ray_segment_distance(ray_origin, d, c + (s * arrow_shaft_length) * dir, c + (s * arrow_head_pick_end) * dir, t, q, dist) &&
                (dist <= s * arrow_head_pick_radius)
            ) {
                consider(handle, t, q);
            }
        }
    }
    for (int perp = 0; perp < 3; ++perp) {
        pick_plane(scale_plane_handles[perp], perp, 0.0f);
    }

    // Rotate rings: test the sampled ring points; in visible-arcs mode the
    // samples occluded by other SHOWN rings' discs are not pickable
    // (matching render). The samples are spaced ~0.049 R apart while the
    // pick tube radius is 0.05 R, so testing sample points leaves no dead
    // zones. A ray that already hit a translate arrow never picks a ring:
    // the arrows render on a higher stencil reference, so at any shared
    // pixel the arrow is what the user sees, even when the ring is nearer.
    if (!best.has_value() || (get_handle_type(best->handle) != Handle_type::e_handle_type_translate_axis)) {
        const bool  arcs_only = m_context.editor_settings->transform_tool.rotate_visible_arcs_only;
        const float radius    = s * rotate_ring_major_radius;
        const bool  ring_shown[3] = {
            is_handle_shown(ring_handles[0]),
            is_handle_shown(ring_handles[1]),
            is_handle_shown(ring_handles[2])
        };
        for (int axis = 0; axis < 3; ++axis) {
            if (!ring_shown[axis]) {
                continue;
            }
            const Handle handle = ring_handles[axis];
            const vec3 side1 = basis[(axis + 1) % 3];
            const vec3 side2 = basis[(axis + 2) % 3];
            for (int i = 0; i < ring_arc_sample_count; ++i) {
                const float theta  = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(ring_arc_sample_count);
                const vec3  normal = std::cos(theta) * side1 + std::sin(theta) * side2;
                const vec3  point  = c + radius * normal;
                if (arcs_only && is_ring_point_occluded(ray_origin, point, c, basis, radius, axis, ring_shown)) {
                    continue;
                }
                const float t = dot(point - ray_origin, d);
                if (t <= 0.0f) {
                    continue;
                }
                if (best.has_value() && (t >= best->t)) {
                    continue;
                }
                if (glm::length(point - (ray_origin + t * d)) > s * ring_pick_radius) {
                    continue;
                }
                consider(handle, t, point);
            }
        }
    }

    // View-rotate ring: sampled circle points like the axis rings, in the
    // camera-aligned plane used by render() (eye = the ray origin here).
    if (is_handle_shown(Handle::e_handle_rotate_view)) {
        const Handle handle   = Handle::e_handle_rotate_view;
        const vec3   view_dir = normalize(ray_origin - c);
        const vec3   ref      = (std::abs(view_dir.y) < 0.9f) ? vec3{0.0f, 1.0f, 0.0f} : vec3{1.0f, 0.0f, 0.0f};
        const vec3   vs1      = normalize(cross(view_dir, ref));
        const vec3   vs2      = normalize(cross(view_dir, vs1));
        const float  radius   = s * view_ring_radius;
        for (int i = 0; i < ring_arc_sample_count; ++i) {
            const float theta = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(ring_arc_sample_count);
            const vec3  point = c + radius * (std::cos(theta) * vs1 + std::sin(theta) * vs2);
            const float t     = dot(point - ray_origin, d);
            if (t <= 0.0f) {
                continue;
            }
            if (best.has_value() && (t >= best->t)) {
                continue;
            }
            if (glm::length(point - (ray_origin + t * d)) > s * ring_pick_radius) {
                continue;
            }
            consider(handle, t, point);
        }
    }

    // Free (arcball) rotation: the whole rotate sphere, but only when the ray
    // hit NO other handle - every explicit handle wins over it, regardless of
    // depth, so it must not go through consider().
    if (!best.has_value() && is_handle_shown(Handle::e_handle_rotate_free)) {
        const float radius = s * rotate_ring_major_radius;
        const float tca    = dot(c - ray_origin, d);
        if (tca > 0.0f) {
            const float d2 = dot(c - ray_origin, c - ray_origin) - tca * tca;
            if (d2 <= radius * radius) {
                const float thc = std::sqrt(radius * radius - d2);
                const float t   = (tca - thc > 0.0f) ? (tca - thc) : tca;
                best = Handle_pick{Handle::e_handle_rotate_free, t, ray_origin + t * d};
            }
        }
    }

    return best;
}

void Handle_visualizations::compute_selection_box()
{
    m_box_valid = false;

    // Use a rigid box frame (translation + rotation, no scale). This keeps the frame
    // invertible even when the selection has a zero scale component, and makes the box
    // AABB measure true world-space extents (so the drag math and the degenerate-recovery
    // path both work in world units).
    const Transform_tool_shared&   shared    = m_context.transform_tool->shared;
    const Transform_tool_settings& settings  = shared.settings;
    const glm::mat4                box_frame = settings.use_anchor_orientation()
        ? erhe::math::create_translation<float>(shared.world_from_anchor.get_translation()) * glm::mat4_cast(shared.world_from_anchor.get_rotation())
        : erhe::math::create_translation<float>(shared.world_from_anchor.get_translation());
    const glm::mat4 box_inv = glm::inverse(box_frame);
    m_box_frame = box_frame;

    erhe::math::Aabb aabb{};
    bool             any{false};
    for (const Transform_entry& entry : shared.entries) {
        const std::shared_ptr<erhe::scene::Node>& node = entry.node;
        if (!node) {
            continue;
        }
        const glm::mat4 box_from_node = box_inv * node->world_from_node();
        std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
        if (mesh) {
            for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
                if (!mesh_primitive.primitive) {
                    continue;
                }
                const erhe::math::Aabb local = mesh_primitive.primitive->get_bounding_box();
                if (!local.is_valid()) {
                    continue;
                }
                aabb.include(local.transformed_by(box_from_node));
                any = true;
            }
        } else {
            aabb.include(glm::vec3{box_from_node * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}});
            any = true;
        }
    }

    m_box_aabb  = aabb;
    m_box_valid = any && aabb.is_valid();
}

void Handle_visualizations::viewport_toolbar()
{
    ImGui::PushID("Handle_visualizations::viewport_toolbar");
    const auto& icon_set = m_context.icon_set;

    auto& settings = m_context.transform_tool->shared.settings;
    const auto reference_mode_button = [&](const char* label, const Transform_reference_mode mode, const char* tooltip) {
        const bool pressed = erhe::imgui::make_button(label, (settings.reference_mode == mode) ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip);
        }
        if (pressed && (settings.reference_mode != mode)) {
            settings.reference_mode = mode;
            m_context.transform_tool->on_reference_settings_changed();
        }
        ImGui::SameLine();
    };
    reference_mode_button("W", Transform_reference_mode::global,    "Transform in World space");
    reference_mode_button("L", Transform_reference_mode::local,     "Transform in Local space");
    reference_mode_button("R", Transform_reference_mode::reference, "Transform in Reference node space");
    reference_mode_button("S", Transform_reference_mode::selection, "Transform in mesh component Selection space");

    {
        const auto mode = settings.show_translate ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal;

        erhe::imgui::begin_button_style(mode);
        const bool translate_pressed = icon_set->icon_button(ERHE_HASH("move"), m_context.icon_set->icons.move);
        erhe::imgui::end_button_style(mode);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(settings.show_translate ? "Hide Translate Tool" : "Show Translate Tool");
        }
        if (translate_pressed) {
            settings.show_translate = !settings.show_translate;
        }
    }

    ImGui::SameLine();
    {
        const auto mode = settings.show_rotate ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal;
        erhe::imgui::begin_button_style(mode);
        const bool rotate_pressed = icon_set->icon_button(ERHE_HASH("rotate"), m_context.icon_set->icons.rotate);
        erhe::imgui::end_button_style(mode);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(settings.show_rotate ? "Hide Rotate Tool" : "Show Rotate Tool");
        }
        if (rotate_pressed) {
            settings.show_rotate = !settings.show_rotate;
        }
    }

    ImGui::SameLine();
    {
        const auto mode = settings.show_scale ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal;
        erhe::imgui::begin_button_style(mode);
        const bool scale_pressed = icon_set->icon_button(ERHE_HASH("scale"), m_context.icon_set->icons.scale);
        erhe::imgui::end_button_style(mode);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(settings.show_scale ? "Hide Scale Tool" : "Show Scale Tool");
        }
        if (scale_pressed) {
            settings.show_scale = !settings.show_scale;
        }
    }

    ImGui::PopID();
}

}
