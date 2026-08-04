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

#include <algorithm>
#include <cmath>
#include <functional>
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
// them to world units so the gizmo keeps a constant on-screen size. The base
// sizes come from Transform_tool_config (Settings > Transform Tool, read live
// each frame); derived values are computed here so the relationships hold
// whatever the settings say. Notes that still apply:
// - Translate arrows: a doubled pointer cone on a thin shaft (shaft width
//   below) so the tip reads as a pointer, not a bar.
// - Axis handles: the translate arrows sit OUTSIDE the rotate sphere
//   (arrow_start), so they and the rings never contest the same radius.
//   Scale-axis handles are cube-tipped (translate is cone-tipped - the
//   shape is the tell-apart cue) and their placement composes with the
//   translate visibility TOGGLE, never with transient drag hiding
//   (placement must not jump mid-drag): with translate hidden they take
//   exactly the translate layout; with translate shown each axis line
//   reads inside-out as ring sphere -> translate arrow -> scale cube
//   (shaft resuming past the translate cone tip with a gap).
// - Plane handles: concentric shells inside the rotate sphere, inside-out
//   uniform-scale wedge (the innermost sector of EVERY plane - the three
//   wedges together are the one uniform-scale handle) -> plane-translate
//   annular sector -> plane-scale annular sector, the last one ending at
//   the rotate ring radius. Radial gaps separate the shells
//   (plane_sector_gap) and the ring (ring_sector_gap, larger); a shell
//   hidden by its tool's visibility toggle donates its space to the
//   visible ones (see get_plane_shell_layout). In positive-only translate
//   mode each plane's sectors span the camera-facing quadrant; otherwise
//   they are full annuli.
// - Ring pick tube radius (config ring_pick_radius, default 0.2): ring
//   samples are spaced ~0.049 R apart, so values >= ~0.1 leave no dead spots.
struct Gizmo_sizes
{
    float arrow_shaft_length;
    float translate_cone_length;
    float translate_cone_radius;
    float arrow_shaft_pick_radius;
    float arrow_head_pick_radius;
    float translate_head_pick_end;
    float translate_head_pick_radius;
    float rotate_ring_major_radius;
    float view_ring_radius;
    float arrow_ring_gap;
    float arrow_start;
    float uniform_scale_outer;
    float plane_sector_gap;
    float ring_sector_gap;
    float plane_translate_outer;
    float box_scale_cone_length;
    float box_scale_cone_radius;
    float scale_shaft_length;
    float scale_cube_half_length;
    float scale_handle_gap;
    // Line widths: negative = constant screen-space pixels.
    float line_width;
    float line_width_hot;
    float arrow_shaft_width;
    float arrow_shaft_width_hot;
    float ring_width;
    float ring_width_hot;
    float plane_outline_width;
    float plane_fill_alpha;
};

auto get_gizmo_sizes(const Transform_tool_config& config) -> Gizmo_sizes
{
    Gizmo_sizes gz{};
    gz.arrow_shaft_length         = config.arrow_shaft_length;
    gz.translate_cone_length      = config.translate_cone_length;
    gz.translate_cone_radius      = config.translate_cone_radius;
    gz.arrow_shaft_pick_radius    = config.arrow_shaft_pick_radius;
    gz.arrow_head_pick_radius     = config.arrow_head_pick_radius;
    // Translate-arrow head pick span: one extra cone length past the tip.
    gz.translate_head_pick_end    = gz.arrow_shaft_length + 2.0f * gz.translate_cone_length;
    gz.translate_head_pick_radius = config.translate_head_pick_radius;
    gz.rotate_ring_major_radius   = config.rotate_ring_radius;
    gz.view_ring_radius           = config.view_ring_radius_factor * gz.rotate_ring_major_radius;
    gz.arrow_ring_gap             = config.arrow_ring_gap;
    gz.arrow_start                = gz.rotate_ring_major_radius + gz.arrow_ring_gap;
    gz.uniform_scale_outer        = config.uniform_scale_outer_radius;
    gz.plane_sector_gap           = config.plane_sector_gap;
    gz.ring_sector_gap            = config.ring_sector_gap;
    gz.plane_translate_outer      = config.plane_translate_outer_radius;
    gz.box_scale_cone_length      = config.box_scale_cone_length;
    gz.box_scale_cone_radius      = config.box_scale_cone_radius;
    gz.scale_shaft_length         = config.scale_shaft_length;
    gz.scale_cube_half_length     = config.scale_cube_half_length;
    gz.scale_handle_gap           = config.scale_handle_gap;
    // Line widths (config in positive screen-space pixels; negative here
    // selects the renderer's constant-pixel-width mode):
    // - Translate/scale arrow shafts: half the shared handle width - the
    //   thin stem plus the wide cone/cube base reads as a pointer, not a bar.
    // - Rotate ring arcs: own settings; the hot default is thinner than the
    //   resting one - the hot emphasis comes from the brightened color.
    gz.line_width                 = -config.handle_line_width;
    gz.line_width_hot             = -config.handle_line_width_hot;
    gz.arrow_shaft_width          = 0.5f * gz.line_width;
    gz.arrow_shaft_width_hot      = 0.5f * gz.line_width_hot;
    gz.ring_width                 = -config.ring_line_width;
    gz.ring_width_hot             = -config.ring_line_width_hot;
    gz.plane_outline_width        = -config.plane_outline_width;
    gz.plane_fill_alpha           = config.plane_fill_alpha;
    return gz;
}

// Radial layout of the concentric plane-handle shells, inside-out: uniform-
// scale wedge, plane-translate sector, plane-scale sector, rotate ring. The
// config gives the all-visible layout; a shell hidden by its tool's
// visibility TOGGLE (never transient drag hiding - placement must not jump
// mid-drag) donates its space and the visible shells stretch proportionally,
// always keeping plane_sector_gap between neighbors and ring_sector_gap
// below the ring.
// Radii of absent shells are left zero - callers gate on visibility first.
struct Plane_shell_layout
{
    float uniform_outer  {0.0f};
    float translate_inner{0.0f};
    float translate_outer{0.0f};
    float scale_inner    {0.0f};
    float scale_outer    {0.0f};
};

auto get_plane_shell_layout(
    const Gizmo_sizes& gz,
    const bool         show_uniform,
    const bool         show_translate,
    const bool         show_scale
) -> Plane_shell_layout
{
    Plane_shell_layout out{};
    const int count = (show_uniform ? 1 : 0) + (show_translate ? 1 : 0) + (show_scale ? 1 : 0);
    if (count == 0) {
        return out;
    }
    // ring_sector_gap (larger than the inter-shell gap) keeps the ring
    // visually separated from the outermost sector.
    const float gap         = gz.plane_sector_gap;
    const float outer_bound = gz.rotate_ring_major_radius - gz.ring_sector_gap;
    const float w_uniform   = gz.uniform_scale_outer;
    const float w_translate = gz.plane_translate_outer - (gz.uniform_scale_outer + gap);
    const float w_scale     = outer_bound - (gz.plane_translate_outer + gap);
    const float nominal =
        (show_uniform   ? w_uniform   : 0.0f) +
        (show_translate ? w_translate : 0.0f) +
        (show_scale     ? w_scale     : 0.0f);
    const float available = outer_bound - gap * static_cast<float>(count - 1);
    const float stretch   = (nominal > 0.0f) ? (available / nominal) : 0.0f;
    float radius = 0.0f;
    if (show_uniform) {
        out.uniform_outer = radius + stretch * w_uniform;
        radius = out.uniform_outer + gap;
    }
    if (show_translate) {
        out.translate_inner = radius;
        out.translate_outer = radius + stretch * w_translate;
        radius = out.translate_outer + gap;
    }
    if (show_scale) {
        out.scale_inner = radius;
        out.scale_outer = radius + stretch * w_scale;
    }
    return out;
}

constexpr int  ring_arc_sample_count = 128;

// Camera-aligned view-rotate ring: light gray, outside the rotate sphere
// with a gap. Dragging it rotates around the viewing axis.
constexpr vec4 view_ring_color{0.7f, 0.7f, 0.7f, 1.0f};

constexpr int   cone_side_count          = 16;


// Per-axis / uniform handle colors and the sector outline colors come from
// Transform_tool_config (axis_color_x/y/z, axis_outline_color_x/y/z,
// uniform_scale_color, uniform_scale_outline_color, Settings > Transform
// Tool); render() snapshots them each frame.

// Flat shading for the solid handle tips (translate cone lateral surface,
// scale cube faces): a fixed world-space light with an ambient floor so no
// face ever goes black. The translate cone's base disc instead gets a fixed
// darkening, reading as the cone's underside.
constexpr vec3  handle_light_direction{0.408f, 0.816f, 0.408f}; // normalized
constexpr float handle_shade_ambient = 0.55f;
constexpr float handle_shade_diffuse = 0.45f;
constexpr float cone_base_darken     = 0.5f;
constexpr vec4 box_outline_color{1.00f, 0.70f, 0.1f, 1.0f};

// All handle rendering uses the x-ray buckets: the occluded pass blends at
// full strength instead of the dim hidden-pass constant, so the gizmo stays
// readable inside content meshes.
//
// The debug buckets layer first-wins per pixel (stencil greater/replace at a
// shared reference), and line vs triangle buckets have no defined mutual
// order - so layering cannot come from submission order. The gizmo layers
// front-to-back purely by stencil reference instead:
//   5 = controller ray (headset_view.cpp)
//   4 = single-axis translate arrows, scale-axis shafts and cubes
//   3 = plane translate quads, plane-scale outline squares
//   2 = rotate rings, center cube, everything else
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
constexpr erhe::renderer::Debug_renderer_config plane_line_config{
    .primitive_type    = erhe::graphics::Primitive_type::line,
    .stencil_reference = 3,
    .draw_visible      = true,
    .draw_hidden       = true,
    .xray              = true
};
constexpr erhe::renderer::Debug_renderer_config plane_fill_config{
    .primitive_type    = erhe::graphics::Primitive_type::triangle,
    .stencil_reference = 3,
    .draw_visible      = true,
    .draw_hidden       = true,
    .xray              = true
};
constexpr erhe::renderer::Debug_renderer_config arrow_line_config{
    .primitive_type    = erhe::graphics::Primitive_type::line,
    .stencil_reference = 4,
    .draw_visible      = true,
    .draw_hidden       = true,
    .xray              = true
};
constexpr erhe::renderer::Debug_renderer_config arrow_fill_config{
    .primitive_type    = erhe::graphics::Primitive_type::triangle,
    .stencil_reference = 4,
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

// shaded: flat-shade each lateral triangle with the fixed handle light.
// base_rgb_scale: rgb multiplier for the base disc (the cone's underside);
// 1.0 keeps the old uniform look.
// The debug triangle bucket is first-wins per pixel (stencil, no depth
// writes), so within one solid whichever triangle is submitted first shows.
// The cone is convex and opaque, so submitting only the eye-facing triangles
// IS the correct order: cull back faces against the eye here.
void draw_cone_fill(
    erhe::renderer::Primitive_renderer& triangle_renderer,
    const vec4&  color,
    const vec3&  eye,
    const vec3&  base_center,
    const vec3&  direction,
    const vec3&  side1,
    const vec3&  side2,
    const float  length,
    const float  radius,
    const bool   shaded,
    const float  base_rgb_scale
)
{
    std::vector<vec3> positions;
    positions.reserve(cone_side_count + 2);
    for (int k = 0; k < cone_side_count; ++k) {
        const float theta = glm::two_pi<float>() * static_cast<float>(k) / static_cast<float>(cone_side_count);
        positions.push_back(base_center + radius * (std::cos(theta) * side1 + std::sin(theta) * side2));
    }
    const uint32_t apex_index = cone_side_count;
    const uint32_t base_index = cone_side_count + 1;
    positions.push_back(base_center + length * direction);
    positions.push_back(base_center);

    // Lateral surface (eye-facing triangles only, see above).
    std::vector<uint32_t> indices;
    indices.reserve(cone_side_count * 3);
    for (uint32_t k = 0; k < static_cast<uint32_t>(cone_side_count); ++k) {
        const uint32_t k2 = (k + 1) % cone_side_count;
        const vec3& p0 = positions[k];
        const vec3& p1 = positions[k2];
        const vec3& pa = positions[apex_index];
        vec3 normal = cross(p1 - p0, pa - p0);
        // Orient outward: away from the cone axis at the edge midpoint.
        if (dot(normal, 0.5f * (p0 + p1) - base_center) < 0.0f) {
            normal = -normal;
        }
        if (dot(normal, eye - p0) <= 0.0f) {
            continue;
        }
        if (shaded) {
            const float len = glm::length(normal);
            const float lambert   = (len > 0.0f) ? std::max(0.0f, dot(normal / len, handle_light_direction)) : 0.0f;
            const float intensity = handle_shade_ambient + handle_shade_diffuse * lambert;
            triangle_renderer.add_triangle(mat4{1.0f}, vec4{intensity * vec3{color}, color.a}, p0, p1, pa);
        } else {
            indices.insert(indices.end(), {k, k2, apex_index});
        }
    }
    if (!indices.empty()) {
        triangle_renderer.add_triangles(mat4{1.0f}, color, positions, indices);
    }

    // Base disc: visible only from the underside (normal = -direction).
    if (dot(direction, eye - base_center) < 0.0f) {
        std::vector<uint32_t> base_indices;
        base_indices.reserve(cone_side_count * 3);
        for (uint32_t k = 0; k < static_cast<uint32_t>(cone_side_count); ++k) {
            const uint32_t k2 = (k + 1) % cone_side_count;
            base_indices.insert(base_indices.end(), {k2, k, base_index});
        }
        triangle_renderer.add_triangles(mat4{1.0f}, vec4{base_rgb_scale * vec3{color}, color.a}, positions, base_indices);
    }
}

// Annular sector in the plane spanned by (su, sv): the plane-translate and
// plane-scale handles' shape. Spans the (su, sv) quadrant, or the full
// annulus when full_annulus is set (negative-handles translate mode). Fill
// at the given alpha plus an outline; the outline adds the radial edges
// only in quadrant mode (a full annulus has none).
void draw_annular_sector(
    erhe::renderer::Primitive_renderer& line_renderer,
    erhe::renderer::Primitive_renderer& triangle_renderer,
    const vec4&  color,
    const vec4&  outline_color,
    const float  fill_alpha,
    const float  outline_width,
    const bool   draw_outline,
    const vec3&  center,
    const vec3&  su,
    const vec3&  sv,
    const float  inner_radius,
    const float  outer_radius,
    const bool   full_annulus
)
{
    const int   segment_count = full_annulus ? 96 : 24;
    const float span          = full_annulus ? glm::two_pi<float>() : glm::half_pi<float>();
    std::vector<vec3> positions;
    positions.reserve(2 * (segment_count + 1));
    std::vector<uint32_t> indices;
    indices.reserve(6 * static_cast<size_t>(segment_count));
    std::vector<erhe::renderer::Line> outline;
    outline.reserve(2 * static_cast<size_t>(segment_count) + 2);
    for (int i = 0; i <= segment_count; ++i) {
        const float theta = span * static_cast<float>(i) / static_cast<float>(segment_count);
        const vec3  dir   = std::cos(theta) * su + std::sin(theta) * sv;
        positions.push_back(center + inner_radius * dir);
        positions.push_back(center + outer_radius * dir);
        if (i > 0) {
            const uint32_t k = static_cast<uint32_t>(2 * i);
            indices.insert(indices.end(), {k - 2, k, k + 1, k - 2, k + 1, k - 1});
            outline.push_back({positions[k - 2], positions[k]});
            outline.push_back({positions[k - 1], positions[k + 1]});
        }
    }
    if (!full_annulus) {
        outline.push_back({positions[0], positions[1]});
        outline.push_back({positions[positions.size() - 2], positions[positions.size() - 1]});
    }
    triangle_renderer.add_triangles(mat4{1.0f}, vec4{vec3{color}, fill_alpha}, positions, indices);
    if (draw_outline) {
        line_renderer.set_thickness(outline_width);
        line_renderer.set_line_color(outline_color);
        line_renderer.add_lines(outline);
    }
}

// Flat-shades each face with the fixed handle light (used by the scale-axis
// tip cubes). Like draw_cone_fill, only the eye-facing faces are submitted:
// the first-wins debug bucket has no intra-bucket depth ordering, and the
// cube is convex, so back faces must simply not be drawn.
void draw_cube_fill(
    erhe::renderer::Primitive_renderer& triangle_renderer,
    const vec4&  color,
    const vec3&  eye,
    const vec3&  center,
    const mat3&  basis,
    const float  half_length
)
{
    vec3 corners[8];
    for (int i = 0; i < 8; ++i) {
        const vec3 offset =
            ((i & 1) != 0 ? half_length : -half_length) * basis[0] +
            ((i & 2) != 0 ? half_length : -half_length) * basis[1] +
            ((i & 4) != 0 ? half_length : -half_length) * basis[2];
        corners[i] = center + offset;
    }
    // Faces keyed by axis and sign; corner index bits: 1 = +x, 2 = +y, 4 = +z.
    static constexpr uint32_t face_corners[6][4] = {
        {0, 2, 6, 4}, // -x
        {1, 3, 7, 5}, // +x
        {0, 1, 5, 4}, // -y
        {2, 3, 7, 6}, // +y
        {0, 1, 3, 2}, // -z
        {4, 5, 7, 6}  // +z
    };
    for (int face = 0; face < 6; ++face) {
        const int   axis      = face / 2;
        const float sign      = ((face % 2) == 0) ? -1.0f : 1.0f;
        const vec3  normal    = sign * normalize(basis[axis]);
        if (dot(normal, eye - (center + sign * half_length * basis[axis])) <= 0.0f) {
            continue;
        }
        const float lambert   = std::max(0.0f, dot(normal, handle_light_direction));
        const float intensity = handle_shade_ambient + handle_shade_diffuse * lambert;
        const vec4  face_color{intensity * vec3{color}, color.a};
        const uint32_t* fc = face_corners[face];
        triangle_renderer.add_triangle(mat4{1.0f}, face_color, corners[fc[0]], corners[fc[1]], corners[fc[2]]);
        triangle_renderer.add_triangle(mat4{1.0f}, face_color, corners[fc[0]], corners[fc[2]], corners[fc[3]]);
    }
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
    const Gizmo_sizes gz = get_gizmo_sizes(m_context.editor_settings->transform_tool);
    return gz.rotate_ring_major_radius * m_view_scale;
}

auto Handle_visualizations::get_view_scale() const -> float
{
    return m_view_scale;
}

auto Handle_visualizations::get_view_ring_radius() const -> float
{
    const Gizmo_sizes gz = get_gizmo_sizes(m_context.editor_settings->transform_tool);
    return gz.view_ring_radius * m_view_scale;
}

void Handle_visualizations::update_transforms()
{
    ERHE_PROFILE_FUNCTION();

    if (m_scene_view == nullptr) {
        return;
    }

    const float distance_scale    = m_context.editor_settings->transform_tool.gizmo_scale * m_view_distance / 100.0f;
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

    const Handle_tool tool = get_handle_tool(handle);
    switch (tool) {
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
    // that axis, a plane drag keeps only the plane handle, a uniform-scale
    // drag keeps only the uniform wedges. Everything else on the same plane
    // (or anywhere) hides - only the active handle stays up.
    const Handle active_handle = transform_tool->get_active_handle();
    if (settings.hide_inactive && (active_handle != Handle::e_handle_none) && (handle != active_handle)) {
        // The mask match alone cannot tell translate_xy from scale_xy (same
        // axis mask), so each match also requires the handle's own tool
        // family - a plane-translate drag must not keep the plane-scale
        // sector up, nor the other way around.
        const unsigned int axis_mask       = get_axis_mask(handle);
        const bool         translate_match = (tool == Handle_tool::e_handle_tool_translate) && m_context.move_tool ->is_active() && (m_context.move_tool ->get_axis_mask() == axis_mask);
        const bool         scale_match     = (tool == Handle_tool::e_handle_tool_scale)     && m_context.scale_tool->is_active() && (m_context.scale_tool->get_axis_mask() == axis_mask);
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

auto Handle_visualizations::get_octant_signs(const vec3& eye, const vec3& center, const mat3& basis) const -> std::array<bool, 3>
{
    const bool drag_active =
        (m_context.transform_tool != nullptr) &&
        (m_context.transform_tool->get_active_handle() != Handle::e_handle_none);
    if (!drag_active) {
        for (int axis = 0; axis < 3; ++axis) {
            m_octant_signs[axis] = dot(basis[axis], eye - center) >= 0.0f;
        }
    }
    return m_octant_signs;
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

    const Transform_tool_config& tt_config = m_context.editor_settings->transform_tool;
    const Gizmo_sizes gz = get_gizmo_sizes(tt_config);
    const vec4 axis_colors[3]         = {tt_config.axis_color_x,         tt_config.axis_color_y,         tt_config.axis_color_z};
    const vec4 axis_outline_colors[3] = {tt_config.axis_outline_color_x, tt_config.axis_outline_color_y, tt_config.axis_outline_color_z};
    const vec4 hover_axis_colors[3]   = {tt_config.hover_color_x,        tt_config.hover_color_y,        tt_config.hover_color_z};

    erhe::renderer::Primitive_renderer line_renderer           = context.get(handle_line_config);
    erhe::renderer::Primitive_renderer triangle_renderer       = context.get(handle_fill_config);
    erhe::renderer::Primitive_renderer plane_line_renderer     = context.get(plane_line_config);
    erhe::renderer::Primitive_renderer plane_triangle_renderer = context.get(plane_fill_config);
    erhe::renderer::Primitive_renderer arrow_line_renderer     = context.get(arrow_line_config);
    erhe::renderer::Primitive_renderer arrow_triangle_renderer = context.get(arrow_fill_config);

    const auto is_hot = [&](const Handle handle) {
        return (hover_handle == handle) || (active_handle == handle);
    };
    // While a handle is hovered (pre-drag only - active drags hide the other
    // handles via hide_inactive instead), the rest of the gizmo dims to
    // hover_dim_factor alpha. Handles RELATED to the hovered one keep full
    // strength: a plane-translate hover keeps the axis arrows of the plane's
    // two axes, a rotate-ring hover keeps the translate arrow along the
    // rotation axis, a uniform-scale hover keeps every scale handle. The
    // arcball region has no visible handle, so its hover dims nothing.
    const bool hover_dimming =
        (active_handle == Handle::e_handle_none) &&
        (hover_handle != Handle::e_handle_none) &&
        (hover_handle != Handle::e_handle_rotate_free);
    const auto handle_alpha = [&](const Handle handle) -> float {
        if (!hover_dimming || (handle == hover_handle)) {
            return 1.0f;
        }
        const Handle_type  type       = get_handle_type(handle);
        const unsigned int mask       = get_axis_mask(handle);
        const unsigned int hover_mask = get_axis_mask(hover_handle);
        bool related = false;
        switch (get_handle_type(hover_handle)) {
            case Handle_type::e_handle_type_translate_plane:
                // The plane's two axis arrows show fully; every OTHER
                // single-axis handle - the perpendicular translate arrow and
                // all scale-axis handles - hides fully rather than dimming
                // (regardless of hover_dim_factor): axis lines not lying in
                // the plane just distract. The rest dims normally.
                if (type == Handle_type::e_handle_type_translate_axis) {
                    if ((mask & hover_mask) == mask) {
                        return 1.0f;
                    }
                    return 0.0f;
                }
                if (type == Handle_type::e_handle_type_scale_axis) {
                    return 0.0f;
                }
                break;
            case Handle_type::e_handle_type_scale_plane:
                // Mirror of the plane-translate rule: the plane's two
                // scale-axis handles show fully; every other single-axis
                // handle - the perpendicular scale handle and all translate
                // arrows - hides fully. The rest dims normally.
                if (type == Handle_type::e_handle_type_scale_axis) {
                    if ((mask & hover_mask) == mask) {
                        return 1.0f;
                    }
                    return 0.0f;
                }
                if (type == Handle_type::e_handle_type_translate_axis) {
                    return 0.0f;
                }
                break;
            case Handle_type::e_handle_type_rotate:
                related = (type == Handle_type::e_handle_type_translate_axis) && (mask == hover_mask);
                break;
            case Handle_type::e_handle_type_scale_uniform:
                // Every scale handle shows fully; all translate arrows hide
                // fully - a uniform scale involves no translation direction.
                if (type == Handle_type::e_handle_type_translate_axis) {
                    return 0.0f;
                }
                related = get_handle_tool(handle) == Handle_tool::e_handle_tool_scale;
                break;
            default:
                break;
        }
        return related ? 1.0f : m_context.editor_settings->transform_tool.hover_dim_factor;
    };
    // hot: the explicit hover color for this handle (hover_color_x/y/z,
    // hover_uniform_scale_color, hover_view_ring_color - defaults are the
    // old 2x-brightened base colors).
    const auto handle_color = [&](const Handle handle, const vec4& base, const vec4& hot) {
        return is_hot(handle)
            ? hot
            : vec4{vec3{base}, base.a * handle_alpha(handle)};
    };
    // With hover_dim_factor 0 a dimmed handle is fully invisible - skip its
    // draws entirely: alpha-0 pixels would still claim their first-wins
    // stencil bucket (blocking overlapping gizmo pixels) and waste fill.
    const auto dimmed_away = [&](const Handle handle) {
        return !is_hot(handle) && (handle_alpha(handle) <= 0.0f);
    };

    const Transform_tool_settings& settings = m_context.transform_tool->shared.settings;
    const bool positive_only = !m_context.editor_settings->transform_tool.translate_negative_handles;
    // Frozen at drag-start values while a drag is active.
    const std::array<bool, 3> octant_signs = get_octant_signs(eye, c, basis);

    // Radial shell layout for the plane handles, reflowed from the tools'
    // visibility toggles (hidden shells donate their space).
    const bool box_mode = settings.scale_gizmo_mode == Scale_gizmo_mode::bounding_box;
    const Plane_shell_layout shells = get_plane_shell_layout(gz, settings.show_scale, settings.show_translate, settings.show_scale && !box_mode);

    // Sector outlines draw ONLY on the hovered / active sector handle - at
    // rest the sector stack is fill-only, keeping it visually quiet. The
    // uniform-scale handle counts as one: hovering it outlines all three of
    // its wedges (they are the same Handle).

    // Translate plane sectors. In positive-only mode each sector spans the
    // camera-facing quadrant (like the single-arrow choice, each in-plane
    // axis flips toward the eye); in negative-handles mode it is a full
    // annulus. The sectors draw on their own stencil layer (3) so they beat
    // the rotate rings (2) but stay under the translate arrows (4).
    for (int perp = 0; perp < 3; ++perp) {
        const Handle handle = translate_plane_handles[perp];
        if (!is_handle_shown(handle) || dimmed_away(handle)) {
            continue;
        }
        const vec3 u  = basis[(perp + 1) % 3];
        const vec3 v  = basis[(perp + 2) % 3];
        const vec3 su = octant_signs[(perp + 1) % 3] ? u : -u;
        const vec3 sv = octant_signs[(perp + 2) % 3] ? v : -v;
        draw_annular_sector(
            plane_line_renderer, plane_triangle_renderer,
            handle_color(handle, axis_colors[perp], hover_axis_colors[perp]), handle_color(handle, axis_outline_colors[perp], hover_axis_colors[perp]),
            gz.plane_fill_alpha * handle_alpha(handle), gz.plane_outline_width,
            is_hot(handle),
            c, su, sv, s * shells.translate_inner, s * shells.translate_outer, !positive_only
        );
    }

    // Rotate rings. Occluded arc segments (arcs_only mode) are not drawn at
    // all - matching pick(), where they are not pickable. Only SHOWN rings
    // act as occluders, so a single shown ring renders as the full circle.
    {
        const bool  arcs_only = m_context.editor_settings->transform_tool.rotate_visible_arcs_only;
        const float radius    = s * gz.rotate_ring_major_radius;
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
            if (dimmed_away(handle)) {
                continue;
            }
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
            const vec4  color = handle_color(handle, axis_colors[axis], hover_axis_colors[axis]);
            const float width = is_hot(handle) ? gz.ring_width_hot : gz.ring_width;
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
    if (is_handle_shown(Handle::e_handle_rotate_view) && !dimmed_away(Handle::e_handle_rotate_view)) {
        const Handle handle   = Handle::e_handle_rotate_view;
        const vec3   view_dir = normalize(eye - c);
        const vec3   ref      = (std::abs(view_dir.y) < 0.9f) ? vec3{0.0f, 1.0f, 0.0f} : vec3{1.0f, 0.0f, 0.0f};
        const vec3   vs1      = normalize(cross(view_dir, ref));
        const vec3   vs2      = normalize(cross(view_dir, vs1));
        const float  radius   = s * gz.view_ring_radius;
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
        line_renderer.set_thickness(is_hot(handle) ? gz.ring_width_hot : gz.ring_width);
        line_renderer.set_line_color(handle_color(handle, view_ring_color, tt_config.hover_view_ring_color));
        line_renderer.add_lines(lines);
    }

    // The solid handle tips (translate cones, scale cubes) all land in the
    // same first-wins triangle bucket (stencil 4), so where they overlap on
    // screen - axis pointing near the camera stacks cone and cube, diagonal
    // views cross pieces of different axes - the winner would be whichever
    // loop ran first, not the nearer piece. Collect the tip draws with their
    // eye distance and flush them sorted nearest-first below.
    struct Solid_tip
    {
        float                 depth; // squared eye distance to the piece center
        std::function<void()> draw;
    };
    std::vector<Solid_tip> solid_tips;

    // Translate arrows. In single-arrow mode (translate_negative_handles
    // off) each axis shows only the direction facing the camera, so no
    // arrow is drawn receding behind the rotate sphere.
    for (int axis = 0; axis < 3; ++axis) {
        const vec3 d     = basis[axis];
        const vec3 side1 = basis[(axis + 1) % 3];
        const vec3 side2 = basis[(axis + 2) % 3];
        const bool positive_towards_eye = octant_signs[axis];
        const Handle directional_handles[2] = {translate_pos_handles[axis], translate_neg_handles[axis]};
        const unsigned int axis_mask = get_axis_mask(directional_handles[0]);
        // During an active single-axis translate drag both directions of the
        // dragged axis are shown even in single-arrow mode: the drag can move
        // either way, and the pair reads as the drag axis.
        const bool axis_drag_here =
            m_context.move_tool->is_active() &&
            (m_context.move_tool->get_axis_mask() == axis_mask);
        // While a plane-translate sector is hovered (pre-drag) both direction
        // arrows of the plane's two axes are shown - the pairs read as the
        // plane's travel directions (this replaced the old hover plane grid).
        // The hovered handle itself is also always exempt from the single-
        // arrow filter, so hover can rest on an opposite arrow it brought up.
        const bool plane_hover_here =
            (active_handle == Handle::e_handle_none) &&
            (hover_handle != Handle::e_handle_none) &&
            (get_handle_type(hover_handle) == Handle_type::e_handle_type_translate_plane) &&
            ((axis_mask & get_axis_mask(hover_handle)) == axis_mask);
        // During a rotate drag the surviving in-plane arrows start at the
        // protractor ring radius, expanding outward from the ring.
        const float start = m_context.rotate_tool->is_active()
            ? m_context.editor_settings->transform_tool.rotate_ring_size
            : gz.arrow_start;
        for (int sign = 0; sign < 2; ++sign) {
            if (
                positive_only && !axis_drag_here && !plane_hover_here &&
                (directional_handles[sign] != hover_handle) &&
                ((sign == 0) != positive_towards_eye)
            ) {
                continue;
            }
            const Handle handle = directional_handles[sign];
            if (!is_handle_shown(handle) || dimmed_away(handle)) {
                continue;
            }
            const vec3 dir   = (sign == 0) ? d : -d;
            const vec4 color = handle_color(handle, axis_colors[axis], hover_axis_colors[axis]);
            arrow_line_renderer.set_thickness(is_hot(handle) ? gz.arrow_shaft_width_hot : gz.arrow_shaft_width);
            arrow_line_renderer.add_lines(color, {{c + (s * start) * dir, c + (s * (start + gz.arrow_shaft_length)) * dir}});
            const vec3 base       = c + (s * (start + gz.arrow_shaft_length)) * dir;
            const vec3 tip_center = base + (0.5f * s * gz.translate_cone_length) * dir;
            solid_tips.push_back({
                dot(tip_center - eye, tip_center - eye),
                [&arrow_triangle_renderer, color, eye, base, dir, side1, side2, s, gz]() {
                    draw_cone_fill(arrow_triangle_renderer, color, eye, base, dir, side1, side2, s * gz.translate_cone_length, s * gz.translate_cone_radius, true, cone_base_darken);
                }
            });
        }
    }

    // Basic scale: cube-tipped axis handles (both directions map to one
    // handle), outline-only plane squares and the uniform-scale center cube.
    // Placement composes with the translate/rotate visibility TOGGLES, not
    // drag state: with translate hidden the scale handles take exactly the
    // translate layout; with translate shown they continue the same lines
    // outward past the translate handles (see the Gizmo_sizes notes).
    for (int axis = 0; axis < 3; ++axis) {
        const Handle handle = scale_axis_handles[axis];
        if (!is_handle_shown(handle) || dimmed_away(handle)) {
            continue;
        }
        const vec3 d     = basis[axis];
        const vec4 color = handle_color(handle, axis_colors[axis], hover_axis_colors[axis]);
        const bool positive_towards_eye = octant_signs[axis];
        // Both sides during an active scale drag of this axis (the pair
        // reads as the scale axis), mirroring the translate arrows' rule.
        const bool axis_drag_here =
            m_context.scale_tool->is_active() &&
            (m_context.scale_tool->get_axis_mask() == get_axis_mask(handle));
        // Both sides while this axis' plane-scale sector is hovered (the
        // pairs read as the plane's scale axes), and while this handle is
        // hovered itself - one scale handle covers both directions, so the
        // side hover rests on must not vanish under it.
        const bool plane_hover_here =
            (active_handle == Handle::e_handle_none) &&
            (hover_handle != Handle::e_handle_none) &&
            (get_handle_type(hover_handle) == Handle_type::e_handle_type_scale_plane) &&
            ((get_axis_mask(handle) & get_axis_mask(hover_handle)) == get_axis_mask(handle));
        // Same base start as the translate arrows so the two layouts agree.
        const float translate_start = m_context.rotate_tool->is_active()
            ? m_context.editor_settings->transform_tool.rotate_ring_size
            : gz.arrow_start;
        for (int sign = 0; sign < 2; ++sign) {
            if (
                positive_only && !axis_drag_here && !plane_hover_here &&
                (handle != hover_handle) &&
                ((sign == 0) != positive_towards_eye)
            ) {
                continue;
            }
            const float start = settings.show_translate
                ? translate_start + gz.arrow_shaft_length + gz.translate_cone_length + gz.scale_handle_gap
                : translate_start;
            const float shaft = settings.show_translate ? gz.scale_shaft_length : gz.arrow_shaft_length;
            const vec3  dir   = (sign == 0) ? d : -d;
            arrow_line_renderer.set_thickness(is_hot(handle) ? gz.arrow_shaft_width_hot : gz.arrow_shaft_width);
            arrow_line_renderer.add_lines(color, {{c + (s * start) * dir, c + (s * (start + shaft)) * dir}});
            const vec3 cube_center = c + (s * (start + shaft + gz.scale_cube_half_length)) * dir;
            solid_tips.push_back({
                dot(cube_center - eye, cube_center - eye),
                [&arrow_triangle_renderer, color, eye, cube_center, basis, s, gz]() {
                    draw_cube_fill(arrow_triangle_renderer, color, eye, cube_center, basis, s * gz.scale_cube_half_length);
                }
            });
        }
    }
    // Flush the solid tips nearest-first: in the first-wins bucket that is
    // exactly back-to-front occlusion between the pieces.
    std::sort(
        solid_tips.begin(), solid_tips.end(),
        [](const Solid_tip& a, const Solid_tip& b) { return a.depth < b.depth; }
    );
    for (const Solid_tip& tip : solid_tips) {
        tip.draw();
    }
    // Plane-scale sectors: the outer shell of the concentric plane layout,
    // ending at the rotate ring radius (minus the sector gap). Lower fill
    // alpha than the translate sectors so the two shells read differently
    // even where they meet across the gap.
    for (int perp = 0; perp < 3; ++perp) {
        const Handle handle = scale_plane_handles[perp];
        if (!is_handle_shown(handle) || dimmed_away(handle)) {
            continue;
        }
        const vec3 u  = basis[(perp + 1) % 3];
        const vec3 v  = basis[(perp + 2) % 3];
        const vec3 su = octant_signs[(perp + 1) % 3] ? u : -u;
        const vec3 sv = octant_signs[(perp + 2) % 3] ? v : -v;
        draw_annular_sector(
            plane_line_renderer, plane_triangle_renderer,
            handle_color(handle, axis_colors[perp], hover_axis_colors[perp]), handle_color(handle, axis_outline_colors[perp], hover_axis_colors[perp]),
            0.5f * gz.plane_fill_alpha * handle_alpha(handle), gz.plane_outline_width,
            is_hot(handle),
            c, su, sv, s * shells.scale_inner, s * shells.scale_outer, !positive_only
        );
    }
    // Uniform scale: the innermost wedge of every plane - the three wedges
    // (one per plane, from the gizmo center outward) are together the ONE
    // uniform-scale handle, so they hover and drag as one.
    if (is_handle_shown(Handle::e_handle_scale_xyz) && !dimmed_away(Handle::e_handle_scale_xyz)) {
        const vec4 color         = handle_color(Handle::e_handle_scale_xyz, tt_config.uniform_scale_color,         tt_config.hover_uniform_scale_color);
        const vec4 outline_color = handle_color(Handle::e_handle_scale_xyz, tt_config.uniform_scale_outline_color, tt_config.hover_uniform_scale_color);
        for (int perp = 0; perp < 3; ++perp) {
            const vec3 u  = basis[(perp + 1) % 3];
            const vec3 v  = basis[(perp + 2) % 3];
            const vec3 su = octant_signs[(perp + 1) % 3] ? u : -u;
            const vec3 sv = octant_signs[(perp + 2) % 3] ? v : -v;
            draw_annular_sector(
                plane_line_renderer, plane_triangle_renderer,
                color, outline_color, gz.plane_fill_alpha * handle_alpha(Handle::e_handle_scale_xyz), gz.plane_outline_width,
                is_hot(Handle::e_handle_scale_xyz),
                c, su, sv, 0.0f, s * shells.uniform_outer, !positive_only
            );
        }
    }

    // Bounding-box scale: box outline plus a cone on each face center. The
    // outline is not tied to a single handle so it stays up during a drag.
    if (
        settings.show_scale &&
        (settings.scale_gizmo_mode == Scale_gizmo_mode::bounding_box) &&
        m_box_valid
    ) {
        line_renderer.set_thickness(gz.plane_outline_width);
        line_renderer.add_cube(m_box_frame, box_outline_color, m_box_aabb.min, m_box_aabb.max);

        const mat3 box_basis{m_box_frame};
        for (int axis = 0; axis < 3; ++axis) {
            const vec3 side1  = normalize(box_basis[(axis + 1) % 3]);
            const vec3 side2  = normalize(box_basis[(axis + 2) % 3]);
            const vec3 center = m_box_aabb.center();
            for (int sign = 0; sign < 2; ++sign) {
                const Handle handle = (sign == 0) ? box_scale_pos_handles[axis] : box_scale_neg_handles[axis];
                if (!is_handle_shown(handle) || dimmed_away(handle)) {
                    continue;
                }
                vec3 face_center_box = center;
                face_center_box[axis] = (sign == 0) ? m_box_aabb.max[axis] : m_box_aabb.min[axis];
                const vec3 base = vec3{m_box_frame * vec4{face_center_box, 1.0f}};
                const vec3 dir  = ((sign == 0) ? 1.0f : -1.0f) * normalize(box_basis[axis]);
                draw_cone_fill(triangle_renderer, handle_color(handle, axis_colors[axis], hover_axis_colors[axis]), eye, base, dir, side1, side2, s * gz.box_scale_cone_length, s * gz.box_scale_cone_radius, false, 1.0f);
            }
        }
    }
}

auto Handle_visualizations::pick(const glm::vec3& eye_position, const glm::vec3& ray_origin, const glm::vec3& ray_direction) const -> std::optional<Handle_pick>
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

    const Gizmo_sizes gz = get_gizmo_sizes(m_context.editor_settings->transform_tool);

    std::optional<Handle_pick> best;
    // Strictly-nearer replacement keeps the earlier candidate on ties, so the
    // consider() call order below breaks overlapping-handle ties (translate
    // before scale, matching how the handles visually stack).
    const auto consider = [&best](const Handle handle, const float t, const vec3& position) {
        if (!best.has_value() || (t < best->t)) {
            best = Handle_pick{handle, t, position};
        }
    };

    const Transform_tool_settings& settings = m_context.transform_tool->shared.settings;
    const bool positive_only = !m_context.editor_settings->transform_tool.translate_negative_handles;
    // Mirrors render(): frozen at drag-start values while a drag is active.
    const std::array<bool, 3> octant_signs = get_octant_signs(eye_position, c, basis);

    // Translate arrows: a thin capsule along the shaft plus a fatter capsule
    // around the cone head (matching the old collision geometry). Mirrors
    // render()'s single-arrow choice: only the camera-facing direction of
    // each axis is pickable when translate_negative_handles is off.
    // Mirrors render()'s hover-driven arrow exemptions. pick() runs while
    // the transform tool still holds the PREVIOUS frame's hover - that lag
    // is what makes the switch stable: the opposite arrows a plane hover
    // brought up stay pickable on the very frame hover moves onto them.
    const Handle hover_handle  = m_context.transform_tool->get_hover_handle();
    const Handle active_handle = m_context.transform_tool->get_active_handle();

    for (int axis = 0; axis < 3; ++axis) {
        const bool positive_towards_eye = octant_signs[axis];
        const Handle directional_handles[2] = {translate_pos_handles[axis], translate_neg_handles[axis]};
        const unsigned int axis_mask = get_axis_mask(directional_handles[0]);
        // Mirrors render(): both directions of the dragged axis are shown
        // (and pickable) during an active single-axis translate drag.
        const bool axis_drag_here =
            m_context.move_tool->is_active() &&
            (m_context.move_tool->get_axis_mask() == axis_mask);
        // Mirrors render(): both plane-axis arrows while the plane-translate
        // sector is hovered; the hovered handle itself is always exempt.
        const bool plane_hover_here =
            (active_handle == Handle::e_handle_none) &&
            (hover_handle != Handle::e_handle_none) &&
            (get_handle_type(hover_handle) == Handle_type::e_handle_type_translate_plane) &&
            ((axis_mask & get_axis_mask(hover_handle)) == axis_mask);
        // Mirrors render(): in-plane arrows sit at the protractor ring
        // during a rotate drag.
        const float start = m_context.rotate_tool->is_active()
            ? m_context.editor_settings->transform_tool.rotate_ring_size
            : gz.arrow_start;
        for (int sign = 0; sign < 2; ++sign) {
            if (
                positive_only && !axis_drag_here && !plane_hover_here &&
                (directional_handles[sign] != hover_handle) &&
                ((sign == 0) != positive_towards_eye)
            ) {
                continue;
            }
            const Handle handle = directional_handles[sign];
            if (!is_handle_shown(handle)) {
                continue;
            }
            // The head capsule normally extends one extra cone length past
            // the tip as grab margin; with the scale handle continuing this
            // axis line, cap it halfway into the gap between the two - the
            // fat translate capsule (hit at a nearer ray-t) would otherwise
            // engulf the thin scale shaft and cube behind it and win every
            // pick. Each handle owns exactly its own span of the axis line,
            // so hover flips between them with no overlap, no dead delay.
            const float head_pick_end = is_handle_shown(scale_axis_handles[axis])
                ? gz.arrow_shaft_length + gz.translate_cone_length + 0.5f * gz.scale_handle_gap
                : gz.translate_head_pick_end;
            const vec3 dir = ((sign == 0) ? 1.0f : -1.0f) * basis[axis];
            float t{0.0f};
            vec3  q{0.0f};
            float dist{0.0f};
            if (
                ray_segment_distance(ray_origin, d, c + (s * start) * dir, c + (s * (start + gz.arrow_shaft_length)) * dir, t, q, dist) &&
                (dist <= s * gz.arrow_shaft_pick_radius)
            ) {
                consider(handle, t, q);
            }
            if (
                ray_segment_distance(ray_origin, d, c + (s * (start + gz.arrow_shaft_length)) * dir, c + (s * (start + head_pick_end)) * dir, t, q, dist) &&
                (dist <= s * gz.translate_head_pick_radius)
            ) {
                consider(handle, t, q);
            }
        }
    }

    // Plane annular sectors (translate, then plane-scale below). The pick
    // zone grows radially by half the sector gap, so neighboring shells'
    // pick zones never overlap. Quadrant bounds are exact (mirroring the
    // drawn sector); in negative-handles mode the sector is a full annulus
    // and the quadrant test is skipped.
    const float sector_pick_margin = 0.5f * gz.plane_sector_gap;
    const auto pick_sector = [&](const Handle handle, const int perp, const float inner_radius, const float outer_radius) {
        if (!is_handle_shown(handle)) {
            return;
        }
        const vec3  n     = basis[perp];
        const float denom = dot(d, n);
        if (std::abs(denom) < 1.0e-6f) {
            return;
        }
        const vec3  u  = basis[(perp + 1) % 3];
        const vec3  v  = basis[(perp + 2) % 3];
        const vec3  su = octant_signs[(perp + 1) % 3] ? u : -u;
        const vec3  sv = octant_signs[(perp + 2) % 3] ? v : -v;
        const float t  = dot(c - ray_origin, n) / denom;
        if (t <= 0.0f) {
            return;
        }
        const vec3  q  = ray_origin + t * d;
        const float uu = dot(q - c, su);
        const float vv = dot(q - c, sv);
        if (positive_only && ((uu < 0.0f) || (vv < 0.0f))) {
            return;
        }
        const float r = std::sqrt(uu * uu + vv * vv);
        if ((r < s * (inner_radius - sector_pick_margin)) || (r > s * (outer_radius + sector_pick_margin))) {
            return;
        }
        consider(handle, t, q);
    };
    // Mirrors render()'s shell layout, reflowed from the visibility toggles.
    const bool box_mode = settings.scale_gizmo_mode == Scale_gizmo_mode::bounding_box;
    const Plane_shell_layout shells = get_plane_shell_layout(gz, settings.show_scale, settings.show_translate, settings.show_scale && !box_mode);
    for (int perp = 0; perp < 3; ++perp) {
        pick_sector(translate_plane_handles[perp], perp, shells.translate_inner, shells.translate_outer);
    }

    // Uniform scale: the innermost wedge of every plane. Near the center
    // the three wedges' pick zones overlap, but they are all the same
    // handle, so any of them winning is correct.
    for (int perp = 0; perp < 3; ++perp) {
        pick_sector(Handle::e_handle_scale_xyz, perp, 0.0f, shells.uniform_outer);
    }

    // Basic-scale axis handles: a thin capsule along the shaft plus a fatter
    // capsule across the tip cube. Mirrors render()'s placement (translate-
    // relative when the translate arrows are shown) and side choice.
    for (int axis = 0; axis < 3; ++axis) {
        const Handle handle = scale_axis_handles[axis];
        if (!is_handle_shown(handle)) {
            continue;
        }
        const bool positive_towards_eye = octant_signs[axis];
        // Mirrors render(): both sides during an active scale drag of this
        // axis, while this axis' plane-scale sector is hovered, and while
        // this handle is hovered itself.
        const bool axis_drag_here =
            m_context.scale_tool->is_active() &&
            (m_context.scale_tool->get_axis_mask() == get_axis_mask(handle));
        const bool plane_hover_here =
            (active_handle == Handle::e_handle_none) &&
            (hover_handle != Handle::e_handle_none) &&
            (get_handle_type(hover_handle) == Handle_type::e_handle_type_scale_plane) &&
            ((get_axis_mask(handle) & get_axis_mask(hover_handle)) == get_axis_mask(handle));
        const float translate_start = m_context.rotate_tool->is_active()
            ? m_context.editor_settings->transform_tool.rotate_ring_size
            : gz.arrow_start;
        for (int sign = 0; sign < 2; ++sign) {
            if (
                positive_only && !axis_drag_here && !plane_hover_here &&
                (handle != hover_handle) &&
                ((sign == 0) != positive_towards_eye)
            ) {
                continue;
            }
            const float start = settings.show_translate
                ? translate_start + gz.arrow_shaft_length + gz.translate_cone_length + gz.scale_handle_gap
                : translate_start;
            const float shaft = settings.show_translate ? gz.scale_shaft_length : gz.arrow_shaft_length;
            const vec3 dir = ((sign == 0) ? 1.0f : -1.0f) * basis[axis];
            float t{0.0f};
            vec3  q{0.0f};
            float dist{0.0f};
            if (
                ray_segment_distance(ray_origin, d, c + (s * start) * dir, c + (s * (start + shaft)) * dir, t, q, dist) &&
                (dist <= s * gz.arrow_shaft_pick_radius)
            ) {
                consider(handle, t, q);
            }
            // Head capsule with a half-cube grab margin past the cube. In
            // combined mode the whole (short) handle is covered at head
            // radius - the shaft span there is no longer shadowed by the
            // translate head capsule, which is capped at the gap.
            const float head_pick_start = settings.show_translate ? start : (start + shaft);
            if (
                ray_segment_distance(ray_origin, d, c + (s * head_pick_start) * dir, c + (s * (start + shaft + 3.0f * gz.scale_cube_half_length)) * dir, t, q, dist) &&
                (dist <= s * gz.arrow_head_pick_radius)
            ) {
                consider(handle, t, q);
            }
        }
    }
    // Plane-scale sectors, mirroring render()'s shell radii.
    for (int perp = 0; perp < 3; ++perp) {
        pick_sector(scale_plane_handles[perp], perp, shells.scale_inner, shells.scale_outer);
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
        const float radius    = s * gz.rotate_ring_major_radius;
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
                if (arcs_only && is_ring_point_occluded(eye_position, point, c, basis, radius, axis, ring_shown)) {
                    continue;
                }
                const float t = dot(point - ray_origin, d);
                if (t <= 0.0f) {
                    continue;
                }
                if (best.has_value() && (t >= best->t)) {
                    continue;
                }
                if (glm::length(point - (ray_origin + t * d)) > s * m_context.editor_settings->transform_tool.ring_pick_radius) {
                    continue;
                }
                consider(handle, t, point);
            }
        }
    }

    // View-rotate ring: sampled circle points like the axis rings, in the
    // camera-aligned plane used by render().
    if (is_handle_shown(Handle::e_handle_rotate_view)) {
        const Handle handle   = Handle::e_handle_rotate_view;
        const vec3   view_dir = normalize(eye_position - c);
        const vec3   ref      = (std::abs(view_dir.y) < 0.9f) ? vec3{0.0f, 1.0f, 0.0f} : vec3{1.0f, 0.0f, 0.0f};
        const vec3   vs1      = normalize(cross(view_dir, ref));
        const vec3   vs2      = normalize(cross(view_dir, vs1));
        const float  radius   = s * gz.view_ring_radius;
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
            if (glm::length(point - (ray_origin + t * d)) > s * m_context.editor_settings->transform_tool.ring_pick_radius) {
                continue;
            }
            consider(handle, t, point);
        }
    }

    // Free (arcball) rotation: the whole rotate sphere, but only when the ray
    // hit NO other handle - every explicit handle wins over it, regardless of
    // depth, so it must not go through consider().
    if (!best.has_value() && is_handle_shown(Handle::e_handle_rotate_free)) {
        const float radius = s * gz.rotate_ring_major_radius;
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

auto Handle_visualizations::intersect_rotate_sphere(const glm::vec3& ray_origin, const glm::vec3& ray_direction) const -> std::optional<Rotate_sphere_intersection>
{
    if (!has_target()) {
        return std::nullopt;
    }
    const float s = m_view_scale;
    if (!(s > 0.0f) || !std::isfinite(s)) {
        return std::nullopt;
    }
    // The rotation sphere exists as a ray-stop region only while the rotate
    // gizmo is up: any ring shown, or the arcball region active.
    const bool sphere_present =
        is_handle_shown(Handle::e_handle_rotate_x)    ||
        is_handle_shown(Handle::e_handle_rotate_y)    ||
        is_handle_shown(Handle::e_handle_rotate_z)    ||
        is_handle_shown(Handle::e_handle_rotate_free);
    if (!sphere_present) {
        return std::nullopt;
    }
    const Gizmo_sizes gz = get_gizmo_sizes(m_context.editor_settings->transform_tool);
    const vec3  c      = m_world_from_anchor.get_translation();
    const vec3  d      = normalize(ray_direction);
    const float radius = s * gz.rotate_ring_major_radius;
    const float tca    = dot(c - ray_origin, d);
    const float d2     = dot(c - ray_origin, c - ray_origin) - tca * tca;
    if (d2 > radius * radius) {
        return std::nullopt;
    }
    const float thc    = std::sqrt(radius * radius - d2);
    const float t_far  = tca + thc;
    if (t_far <= 0.0f) {
        return std::nullopt;
    }
    const float t_near = std::max(tca - thc, 0.0f);

    // First crossing of any gizmo axis plane between entry and exit.
    const mat3 basis = get_basis();
    float      t_plane_best{std::numeric_limits<float>::max()};
    for (int axis = 0; axis < 3; ++axis) {
        const vec3  n     = basis[axis];
        const float denom = dot(d, n);
        if (std::abs(denom) < 1.0e-6f) {
            continue;
        }
        const float t = dot(c - ray_origin, n) / denom;
        if ((t > t_near) && (t < t_far) && (t < t_plane_best)) {
            t_plane_best = t;
        }
    }

    return Rotate_sphere_intersection{
        .entry = ray_origin + t_near * d,
        .exit  = ray_origin + t_far  * d,
        .first_plane_crossing = (t_plane_best < std::numeric_limits<float>::max())
            ? std::optional<glm::vec3>{ray_origin + t_plane_best * d}
            : std::nullopt
    };
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
