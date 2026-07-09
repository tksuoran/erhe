#include "transform/scale_tool.hpp"

#include "app_context.hpp"
#include "graphics/icon_set.hpp"
#include "scene/scene_view.hpp"
#include "tools/tools.hpp"
#include "transform/handle_enums.hpp"
#include "transform/handle_visualizations.hpp"
#include "transform/transform_tool.hpp"

#include "erhe_math/math_util.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace editor {

using namespace glm;

namespace {

// Lower bound for the multiplicative box-scale factor. Prevents the dragged face from
// crossing the pivot (which would mirror/invert the object); it is NOT relied upon to
// recover from zero - that is handled by the absolute reconstruction path below.
constexpr float c_min_scale_factor = 0.001f;

// When the selection's bounding-box extent along the dragged axis is at or below this
// (world units), the box is treated as degenerate.
constexpr float c_box_degenerate_extent = 1e-4f;

// A box whose dragged-axis extent is thinner than this fraction of its largest extent is
// also treated as degenerate. Dividing by such a small old_size (the multiplicative
// new_size/old_size) is numerically hypersensitive, so these are routed to the per-node
// absolute path instead, which sets the size directly with no division.
constexpr float c_box_thin_aspect = 0.05f;

// A scale component at or below this makes a transform effectively singular: the anchor's
// inverse (used by Transform_tool::adjust) blows up and the multiplicative model amplifies
// the first frame. Such selections are handled per node from the Trs components instead.
// Must be comfortably larger than c_min_scale_factor so the value the multiplicative path
// floors to is itself caught here on the next drag.
constexpr float c_singular_scale = 1e-2f;

// Node-local geometry extent below this is treated as genuinely flat (e.g. a quad), which
// cannot be given thickness by scaling; such nodes are left untouched during recovery.
constexpr float c_min_geometry_extent = 1e-6f;

// Compute a node's intrinsic (scale-independent) bounding box in its own local space.
[[nodiscard]] auto node_local_aabb(erhe::scene::Node* node) -> erhe::math::Aabb
{
    erhe::math::Aabb aabb{};
    const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node);
    if (mesh) {
        for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
            if (!mesh_primitive.primitive) {
                continue;
            }
            const erhe::math::Aabb local = mesh_primitive.primitive->get_bounding_box();
            if (local.is_valid()) {
                aabb.include(local);
            }
        }
    }
    return aabb;
}

}

Scale_tool::Scale_tool(App_context& app_context, Icon_set& icon_set, Tools& tools)
    : Subtool{app_context, tools, Tool_flags::toolbox | Tool_flags::allow_secondary}
{
    set_base_priority  (c_priority);
    set_description    ("Scale");
    set_icon           (icon_set.custom_icons, icon_set.icons.scale);
}

void Scale_tool::handle_priority_update(const int old_priority, const int new_priority)
{
    auto& shared = get_shared();
    shared.settings.show_scale = new_priority > old_priority;
}

auto Scale_tool::begin(const unsigned int axis_mask, Scene_view* scene_view) -> bool
{
    m_axis_mask = axis_mask;
    m_active    = true;
    m_box_mode  = false;
    m_uniform_need_initial_point = true;

    const Handle active_handle = m_context.transform_tool->get_active_handle();
    switch (active_handle) {
        case Handle::e_handle_box_scale_pos_x: m_box_mode = true; m_box_axis = 0; m_box_positive = true;  break;
        case Handle::e_handle_box_scale_neg_x: m_box_mode = true; m_box_axis = 0; m_box_positive = false; break;
        case Handle::e_handle_box_scale_pos_y: m_box_mode = true; m_box_axis = 1; m_box_positive = true;  break;
        case Handle::e_handle_box_scale_neg_y: m_box_mode = true; m_box_axis = 1; m_box_positive = false; break;
        case Handle::e_handle_box_scale_pos_z: m_box_mode = true; m_box_axis = 2; m_box_positive = true;  break;
        case Handle::e_handle_box_scale_neg_z: m_box_mode = true; m_box_axis = 2; m_box_positive = false; break;
        default: break;
    }

    if (m_box_mode) {
        const Handle_visualizations* visualizations = get_shared().get_visualizations();
        if ((visualizations == nullptr) || !visualizations->is_box_valid()) {
            m_box_mode = false;
            return false;
        }
        m_box_frame = visualizations->get_box_frame();
        m_box_aabb  = visualizations->get_box_aabb();

        // Do NOT capture the projection baseline here. The press ray and the first update
        // ray can differ (press-to-first-move delta, or a failed projection at press that
        // would force a divergent fallback), and any difference becomes a transform applied
        // before the user moves. Instead defer the baseline to the first update, where it is
        // taken from the exact same projection subsequent frames use - making the first
        // frame a guaranteed no-op.
        m_box_need_initial_proj = true;
    }

    return (axis_mask != 0) && (scene_view != nullptr);
}

auto Scale_tool::update(Scene_view* scene_view) -> bool
{
    using vec3 = glm::vec3;

    if ((m_axis_mask == 0) || (scene_view == nullptr)) {
        return false;
    }

    if (m_box_mode) {
        return update_box(scene_view);
    }

    const auto& shared = get_shared();
    switch (std::popcount(m_axis_mask)) {
        case 1: {
            const vec3 drag_world_direction = get_axis_direction();
            const vec3 P0                   = shared.get_initial_drag_position_in_world() - drag_world_direction;
            const vec3 P1                   = shared.get_initial_drag_position_in_world() + drag_world_direction;
            const auto closest_point        = scene_view->get_closest_point_on_line(P0, P1);
            if (closest_point.has_value()) {
                update(closest_point.value());
                return true;
            }
            return false;
        }

        case 2: {
            const vec3 P             = shared.get_initial_drag_position_in_world();
            const vec3 N             = get_plane_normal(!shared.settings.use_anchor_orientation());
            const auto closest_point = scene_view->get_closest_point_on_plane(N, P);
            if (closest_point.has_value()) {
                update(closest_point.value());
                return true;
            }
            return false;
        }

        case 3: {
            return update_uniform(scene_view);
        }

        default: {
            return false;
        }
    }
}

void Scale_tool::update(const vec3 drag_position_in_world)
{
    const auto& shared           = get_shared();
    const vec3  center_of_scale  = shared.world_from_anchor_initial_state.get_translation();
    const float initial_distance = glm::distance(center_of_scale, shared.get_initial_drag_position_in_world());
    if (initial_distance < 1e-6f) {
        return;
    }
    const float current_distance = glm::distance(center_of_scale, drag_position_in_world);
    const float s                = current_distance / initial_distance;
    const vec3  scale = [&](){
        switch (m_axis_mask) {
            case Axis_mask::x  : return vec3{s, 1.0f, 1.0f};
            case Axis_mask::y  : return vec3{1.0f, s, 1.0f};
            case Axis_mask::z  : return vec3{1.0f, 1.0f, s};
            case Axis_mask::xy : return vec3{s, s, 1.0f};
            case Axis_mask::xz : return vec3{s, 1.0f, s};
            case Axis_mask::yz : return vec3{1.0f, s, s};
            case Axis_mask::xyz: return vec3{s, s, s};
            default:             return vec3{1.0f};
        }
    }();

    m_context.transform_tool->adjust_scale(center_of_scale, scale);
}

auto Scale_tool::update_uniform(Scene_view* scene_view) -> bool
{
    // Uniform scale is driven by the pointer displacement along the screen's up-right
    // diagonal (drag up/right to grow, down/left to shrink), mapped exponentially so the
    // factor is always positive and the gesture is symmetric: one gizmo radius of
    // displacement doubles (or halves) the scale. The distance-from-center ratio used by
    // the axis and plane handles is not usable here: the grab point is on the center
    // handle, so its distance from the center is ~0 - the ratio would be hypersensitive
    // and shrinking would have almost no input range.
    const Transform_tool_shared& shared = get_shared();

    const Handle_visualizations* visualizations = shared.get_visualizations();
    if (visualizations == nullptr) {
        return false;
    }
    const float gizmo_radius = visualizations->get_gizmo_radius();
    if (!std::isfinite(gizmo_radius) || (gizmo_radius <= 0.0f)) {
        return false;
    }

    const std::shared_ptr<erhe::scene::Camera> camera = scene_view->get_camera();
    if (!camera) {
        return false;
    }
    const erhe::scene::Node* camera_node = camera->get_node();
    if (camera_node == nullptr) {
        return false;
    }
    const glm::mat4 world_from_camera = camera_node->world_from_node();
    const vec3      camera_right      = glm::normalize(vec3{world_from_camera[0]});
    const vec3      camera_up         = glm::normalize(vec3{world_from_camera[1]});
    const vec3      camera_back       = glm::normalize(vec3{world_from_camera[2]});
    const vec3      drag_direction    = glm::normalize(camera_right + camera_up);

    // Track the pointer on the view-facing plane through the center of scale.
    const vec3                center_of_scale = shared.world_from_anchor_initial_state.get_translation();
    const std::optional<vec3> closest_point   = scene_view->get_closest_point_on_plane(camera_back, center_of_scale);
    if (!closest_point.has_value()) {
        return false;
    }

    if (m_uniform_need_initial_point) {
        // First live drag frame: establish the baseline from the exact same projection
        // subsequent frames use, making the first frame a guaranteed no-op (same deferred
        // baseline rationale as box mode above).
        m_uniform_initial_point      = closest_point.value();
        m_uniform_need_initial_point = false;
    }

    const float drive = glm::dot(closest_point.value() - m_uniform_initial_point, drag_direction);
    const float s     = std::exp2(drive / gizmo_radius);
    m_context.transform_tool->adjust_scale(center_of_scale, vec3{s, s, s});
    return true;
}

auto Scale_tool::box_axis_projection(Scene_view* scene_view) const -> std::optional<float>
{
    const int       axis    = m_box_axis;
    const glm::mat4 box_inv = glm::inverse(m_box_frame);

    // Center of the dragged face, in box coordinates.
    const glm::vec3 center          = m_box_aabb.center();
    const float     moving_face     = m_box_positive ? m_box_aabb.max[axis] : m_box_aabb.min[axis];
    glm::vec3       face_center_box = center;
    face_center_box[axis]           = moving_face;

    const glm::vec3 center_world = glm::vec3{m_box_frame * glm::vec4{face_center_box, 1.0f}};
    const glm::vec3 axis_dir     = glm::normalize(glm::vec3{m_box_frame[axis]});

    const std::optional<glm::vec3> closest = scene_view->get_closest_point_on_line(
        center_world - axis_dir,
        center_world + axis_dir
    );
    if (!closest.has_value()) {
        return {};
    }
    return (box_inv * glm::vec4{closest.value(), 1.0f})[axis];
}

auto Scale_tool::update_box(Scene_view* scene_view) -> bool
{
    const int   axis        = m_box_axis;
    const float moving_face = m_box_positive ? m_box_aabb.max[axis] : m_box_aabb.min[axis];

    const std::optional<float> cur_proj_opt = box_axis_projection(scene_view);
    if (!cur_proj_opt.has_value()) {
        return false;
    }
    const float cur_proj = cur_proj_opt.value();
    if (m_box_need_initial_proj) {
        // First live drag frame: establish the baseline so this frame produces zero delta.
        m_box_initial_proj      = cur_proj;
        m_box_need_initial_proj = false;
    }
    const float new_face = moving_face + (cur_proj - m_box_initial_proj);

    const float old_size = m_box_aabb.max[axis] - m_box_aabb.min[axis];

    float pivot   {0.0f};
    float new_size{0.0f};
    if (m_box_positive) {
        pivot    = m_box_aabb.min[axis];
        new_size = new_face - m_box_aabb.min[axis];
    } else {
        pivot    = m_box_aabb.max[axis];
        new_size = m_box_aabb.max[axis] - new_face;
    }

    // The World-space multiplicative path (Transform_tool::adjust) is unsafe whenever a
    // transform it multiplies is singular or near-singular, because it both inverts the
    // anchor and divides by old_size - either of which amplifies the first frame into a
    // visible jump - and it decomposes the resulting per-node matrices. Route to the
    // per-node Trs path (rotation and scale as separate, always-defined components, sized
    // absolutely with no division) in these cases:
    //  - the dragged axis is degenerate: extent ~ 0, or thin relative to the box; or
    //  - the selection anchor has a near-zero scale component (singular inverse); or
    //  - any single selected node has a near-zero scale component: in a multi-selection the
    //    anchor (an average) can look non-singular while an individual node's matrix is
    //    singular and would be corrupted by the decompose in adjust().
    // "Degenerate" is decided by the DRAGGED axis only: absolute sizing (no division) is
    // used when that axis has no usable extent to scale a ratio from. A zero scale on some
    // OTHER axis makes the selection singular (so adjust's anchor inverse is unusable) but
    // the dragged axis is still healthy - that case takes the proportional per-node path,
    // which preserves each node's individual size/position in a multi-selection.
    const glm::vec3 extent        = m_box_aabb.diagonal();
    const float     largest_other = glm::max(extent[(axis + 1) % 3], extent[(axis + 2) % 3]);
    const bool      degenerate    =
        (old_size < c_box_degenerate_extent) ||
        ((largest_other > 0.0f) && (old_size < c_box_thin_aspect * largest_other));

    if (degenerate || is_selection_singular()) {
        return apply_box_per_node(pivot, old_size, new_size);
    }

    const float s = glm::max(new_size / old_size, c_min_scale_factor);

    glm::vec3 scale_vec{1.0f};
    scale_vec[axis] = s;
    glm::vec3 pivot_vec{0.0f};
    pivot_vec[axis] = pivot;

    const glm::mat4 delta_box =
        erhe::math::create_translation<float>(pivot_vec) *
        glm::scale(glm::mat4{1.0f}, scale_vec) *
        erhe::math::create_translation<float>(-pivot_vec);
    const glm::mat4 box_inv     = glm::inverse(m_box_frame);
    const glm::mat4 delta_world = m_box_frame * delta_box * box_inv;
    const glm::mat4 updated_world_from_anchor = delta_world * get_shared().world_from_anchor_initial_state.get_matrix();

    m_context.transform_tool->adjust(updated_world_from_anchor);
    m_context.transform_tool->update_transforms();
    return true;
}

auto Scale_tool::is_selection_singular() const -> bool
{
    const auto scale_has_zero = [](const glm::vec3 scale) -> bool {
        return (std::abs(scale.x) < c_singular_scale) ||
               (std::abs(scale.y) < c_singular_scale) ||
               (std::abs(scale.z) < c_singular_scale);
    };

    const Transform_tool_shared& shared = get_shared();
    if (scale_has_zero(shared.world_from_anchor_initial_state.get_scale())) {
        return true;
    }
    for (const Transform_entry& entry : shared.entries) {
        if (entry.node && scale_has_zero(entry.world_from_node_before.get_scale())) {
            return true;
        }
    }
    return false;
}

auto Scale_tool::apply_box_per_node(const float pivot, const float old_size, const float new_size) -> bool
{
    // Per node, both reconstructions are built from the node's separate Trs components (so a
    // zero scale never corrupts rotation, and the anchor inverse is never used). The choice
    // is made per node from that node's own dragged-axis scale:
    //  - collapsed (scale ~ 0): the node cannot be grown multiplicatively (0 * s == 0).
    //    Instead it is sized to FILL the box on the dragged axis and CENTERED in it. So a
    //    fully-collapsed multi-selection has every object fill and stack into the box, and
    //    at the first frame (new_size == box extent, which is ~0 for a point box) it is a
    //    no-op - the box stays a point until the pointer actually moves.
    //  - healthy: scale the node-local axis by s and move its fixed face the way the global
    //    scale-about-pivot would, preserving each node's individual size/position.
    const int       axis    = m_box_axis;
    const glm::mat4 box_inv = glm::inverse(m_box_frame);
    const glm::vec3 u       = glm::normalize(glm::vec3{m_box_frame[axis]});
    const float     s       = (old_size > c_box_degenerate_extent) ? glm::max(new_size / old_size, c_min_scale_factor) : 1.0f;
    const float     dir     = m_box_positive ? 1.0f : -1.0f;
    const float     box_center = pivot + dir * (glm::max(new_size, 0.0f) * 0.5f);

    Transform_tool_shared& shared = get_shared();
    bool                   any    = false;
    for (Transform_entry& entry : shared.entries) {
        const std::shared_ptr<erhe::scene::Node>& node = entry.node;
        if (!node) {
            continue;
        }

        const erhe::scene::Trs_transform& before      = entry.world_from_node_before;
        const glm::vec3                   translation = before.get_translation();
        const glm::quat                   rotation    = before.get_rotation();
        const glm::vec3                   scale       = before.get_scale();
        const glm::vec3                   skew        = before.get_skew();
        const glm::mat3                   rotation_m  = glm::mat3_cast(rotation);

        // Node-local axis best matching the dragged world/box axis.
        int   local_axis = 0;
        float best_align = -1.0f;
        for (int i = 0; i < 3; ++i) {
            const float align = std::abs(glm::dot(glm::vec3{rotation_m[i]}, u));
            if (align > best_align) {
                best_align = align;
                local_axis = i;
            }
        }
        const bool node_collapsed = std::abs(scale[local_axis]) < c_singular_scale;

        const erhe::math::Aabb geometry_aabb = node_local_aabb(node.get());
        if (!geometry_aabb.is_valid()) {
            // No geometry (light/camera/empty): nothing to size. A healthy node still rides
            // along (its position scales about the pivot); a collapsed one is left in place.
            if (!node_collapsed) {
                const float origin_box = (box_inv * glm::vec4{translation, 1.0f})[axis];
                const float shift      = (pivot + (origin_box - pivot) * s) - origin_box;
                if (!any) {
                    m_context.transform_tool->touch();
                }
                node->set_world_from_node(erhe::scene::Trs_transform{translation + u * shift, rotation, scale, skew});
                any = true;
            }
            continue;
        }
        const float local_extent = geometry_aabb.diagonal()[local_axis];
        if (local_extent < c_min_geometry_extent) {
            continue; // genuinely flat geometry - cannot be given thickness
        }

        glm::vec3 new_scale = scale;
        float     target    {0.0f}; // box-axis coordinate the chosen reference face/center lands on
        float     reference {0.0f}; // that reference, measured on the scaled node
        if (node_collapsed) {
            // Fill the box on this axis and center the node in it.
            const float sign = (scale[local_axis] < 0.0f) ? -1.0f : 1.0f;
            new_scale[local_axis] = sign * (glm::max(new_size, 0.0f) / local_extent);
        } else {
            new_scale[local_axis] = scale[local_axis] * s;
        }

        const erhe::scene::Trs_transform scaled{translation, rotation, new_scale, skew};
        const glm::mat4                  scaled_matrix   = scaled.get_matrix();
        const erhe::math::Aabb           box_space_after = geometry_aabb.transformed_by(box_inv * scaled_matrix);

        if (node_collapsed) {
            // Center of the scaled node -> center of the box on this axis.
            reference = (box_space_after.min[axis] + box_space_after.max[axis]) * 0.5f;
            target    = box_center;
        } else {
            // Fixed (pivot-side) face -> where the global scale-about-pivot would put it.
            const erhe::math::Aabb box_space_before = geometry_aabb.transformed_by(box_inv * before.get_matrix());
            const float            fixed_before     = m_box_positive ? box_space_before.min[axis] : box_space_before.max[axis];
            reference = m_box_positive ? box_space_after.min[axis] : box_space_after.max[axis];
            target    = pivot + (fixed_before - pivot) * s;
        }

        // Build the result from Trs components (translation carries the shift), never from a
        // matrix: decomposing a zero/low-rank matrix would lose the rotation.
        const glm::vec3 final_translation = translation + u * (target - reference);

        if (!any) {
            m_context.transform_tool->touch();
        }
        node->set_world_from_node(erhe::scene::Trs_transform{final_translation, rotation, new_scale, skew});
        any = true;
    }

    if (any) {
        m_context.transform_tool->update_transforms();
    }
    return any;
}

}
