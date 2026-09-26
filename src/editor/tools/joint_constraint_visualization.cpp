#include "tools/joint_constraint_visualization.hpp"

#include "app_context.hpp"
#include "renderers/render_context.hpp"
#include "scene/ik_properties.hpp"
#include "scene/joint.hpp"
#include "scene/joint_system.hpp"
#include "scene/node_physics_system.hpp"
#include "scene/rig_properties.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "transform/ik_constraint.hpp"
#include "transform/ik_drag.hpp"
#include "transform/transform_tool.hpp"

#include "erhe_graphics/enums.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_physics/physics_joint_settings.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_renderer/debug_renderer_bucket.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"

#include <algorithm>
#include <cmath>

namespace editor {

namespace {

// Rotation + translation of a node world transform as a physics transform;
// scale is ignored, the convention Joint_system builds constraints with.
[[nodiscard]] auto world_transform_of(const erhe::scene::Node& node) -> erhe::physics::Transform
{
    const erhe::scene::Trs_transform& world_from_node = node.world_from_node_transform();
    return erhe::physics::Transform{
        glm::mat3_cast(world_from_node.get_rotation()),
        world_from_node.get_translation()
    };
}

[[nodiscard]] auto is_hovered(const erhe::scene::Node* node) -> bool
{
    return (node != nullptr) && node->is_hovered();
}

[[nodiscard]] auto is_hovered_mesh(const erhe::scene::Node* node) -> bool
{
    return is_hovered(node) && erhe::is<erhe::scene::Mesh>(static_cast<const erhe::Item_base*>(node));
}

[[nodiscard]] auto is_hovered_bone(const erhe::scene::Node* node) -> bool
{
    return is_hovered(node) && erhe::scene::is_bone(static_cast<const erhe::Item_base*>(node));
}

// Nearest self-or-ancestor mesh; the body a pending joint's frame belongs to
// is not known without a physics entry, and the mesh is what the user sees.
[[nodiscard]] auto nearest_mesh(const erhe::scene::Node* node) -> const erhe::scene::Mesh*
{
    while (node != nullptr) {
        const erhe::scene::Mesh* const mesh = dynamic_cast<const erhe::scene::Mesh*>(node);
        if (mesh != nullptr) {
            return mesh;
        }
        node = node->get_parent_node().get();
    }
    return nullptr;
}

// Derived joint size (Debug_visualizations_style::joint_size 0): half of the
// larger of the body's bounding radius and the frame-to-body distance, so a
// pendulum's pivot visual reads against the swing it limits.
[[nodiscard]] auto derive_joint_size(const Physics_joint_state& state) -> float
{
    const erhe::scene::Node* const body_node = (state.body_node_a != nullptr) ? state.body_node_a : state.frame_node_0;
    const erhe::scene::Mesh* const mesh      = nearest_mesh(body_node);
    float extent = 0.0f;
    if (mesh != nullptr) {
        const erhe::math::Aabb aabb = mesh->get_aabb_world();
        if (aabb.is_valid()) {
            extent = std::max(extent, 0.5f * glm::length(aabb.diagonal()));
        }
        extent = std::max(extent, glm::distance(state.frame_a.origin, glm::vec3{mesh->position_in_world()}));
    }
    if (extent <= 0.0f) {
        extent = 0.5f;
    }
    return std::clamp(0.5f * extent, 0.02f, 10.0f);
}

[[nodiscard]] auto make_line_style(const Debug_visualizations_style& style) -> Joint_line_style
{
    return Joint_line_style{
        .limit_color        = style.joint_limit_color,
        .free_color         = style.joint_free_color,
        .value_color        = style.joint_value_color,
        .violation_color    = style.joint_violation_color,
        .pending_color      = style.joint_pending_color,
        .approximated_color = style.joint_approximated_color,
        .body_link_color    = style.joint_body_link_color,
        .swing_color        = style.ik_limit_swing_color,
        .twist_color        = style.ik_limit_twist_color,
        .line_width         = style.joint_line_width,
        .thin_line_width    = style.joint_thin_line_width,
        .arc_segments       = 24
    };
}

} // anonymous namespace

auto get_physics_joint_state(const Joint_entry& entry, Physics_joint_state& state) -> bool
{
    state = Physics_joint_state{};
    const Joint* const joint = entry.joint;
    if (joint == nullptr) {
        return false;
    }
    const std::shared_ptr<erhe::scene::Node> node_0 = joint->get_body_0();
    if (!node_0) {
        return false;
    }
    const std::shared_ptr<erhe::scene::Node> node_1 = joint->get_body_1();
    state.joint        = joint;
    state.frame_node_0 = node_0.get();
    state.frame_node_1 = node_1.get();

    std::array<erhe::physics::Constraint_axis_limit, 6> limits{};
    if (entry.constraint) {
        // Live: the frames the constraint was built with, carried by the
        // bodies as they move.
        const Joint_constraint_state& constraint_state = entry.state;
        state.live = true;
        limits     = constraint_state.limits;
        state.body_node_a = (constraint_state.node_physics_a != nullptr) ? constraint_state.node_physics_a->node : nullptr;
        state.body_node_b = (constraint_state.node_physics_b != nullptr) ? constraint_state.node_physics_b->node : nullptr;
        state.frame_a = (state.body_node_a != nullptr)
            ? world_transform_of(*state.body_node_a) * constraint_state.frame_in_a
            : world_transform_of(*node_0);
        state.frame_b = (state.body_node_b != nullptr)
            ? world_transform_of(*state.body_node_b) * constraint_state.frame_in_b
            : constraint_state.frame_in_b;
    } else {
        // Pending: the frame nodes, as Joint_system will capture them.
        const std::shared_ptr<erhe::physics::Physics_joint_settings> settings = joint->get_settings();
        if (settings) {
            limits = settings->get_axis_limits();
        }
        state.frame_a = world_transform_of(*node_0);
        state.frame_b = node_1 ? world_transform_of(*node_1) : state.frame_a;
    }
    state.shape       = erhe::physics::get_enforced_joint_limits(limits);
    state.coordinates = erhe::physics::measure_joint_coordinates(state.frame_a, state.frame_b, state.shape);
    state.range_check = erhe::physics::check_joint_range(state.shape, state.coordinates, c_joint_linear_tolerance, c_joint_angular_tolerance);
    return true;
}

auto get_ik_limit_state(const erhe::scene::Node& node, const Ik_drag* const ik_drag, Ik_limit_state& state) -> bool
{
    state = Ik_limit_state{};
    const glm::quat local_rotation = node.parent_from_node_transform().get_rotation();
    const glm::vec3 tail           = node.get_value(Rig::tail_property());

    Ik_joint_constraint constraint{};
    glm::quat           pinned_local = local_rotation;
    const bool from_drag =
        (ik_drag != nullptr) &&
        ik_drag->is_active() &&
        ik_drag->find_joint_constraint(node, constraint, pinned_local);
    if (!from_drag) {
        constraint = resolve_ik_constraint(node, local_rotation, derive_ik_twist_axis(tail));
    }
    if (!constraint.enabled || (constraint.twist_axis < 0)) {
        return false;
    }

    const glm::quat world_rotation        = node.world_from_node_transform().get_rotation();
    const glm::quat parent_world_rotation = world_rotation * glm::inverse(local_rotation);
    const float     tail_component        = tail[constraint.twist_axis];

    state.node                  = &node;
    state.constraint            = constraint;
    state.local_rotation        = local_rotation;
    state.pinned_local_rotation = pinned_local;
    state.world_from_rest       = glm::normalize(parent_world_rotation * constraint.rest_rotation);
    state.head                  = glm::vec3{node.position_in_world()};
    state.length                = glm::length(glm::vec3{node.world_from_node() * glm::vec4{tail, 0.0f}});
    state.twist_sign            = (tail_component < 0.0f) ? -1.0f : 1.0f;
    state.current               = decompose_ik_rotation(constraint, local_rotation);
    state.within_limits         = is_ik_rotation_within_limits(constraint, local_rotation, 1.0e-3f);
    state.from_drag             = from_drag;
    return true;
}

void Joint_constraint_visualization::render(
    const Render_context&                context,
    const Debug_visualizations_settings& settings,
    const Debug_visualizations_style&    style
)
{
    ERHE_PROFILE_FUNCTION();

    m_drawn_physics_joints.clear();
    m_drawn_ik_bones.clear();
    const Joint_constraint_filter filter = settings.joint_constraints;
    if (filter == Joint_constraint_filter::off) {
        return;
    }
    const std::shared_ptr<Scene_root> scene_root = context.scene_view.get_scene_root();
    if (!scene_root) {
        return;
    }
    const Joint_line_style line_style = make_line_style(style);
    m_lines.clear();
    if (settings.joint_constraints_physics) {
        physics_joints(context, *scene_root, filter, line_style, style.joint_size);
    }
    if (settings.joint_constraints_ik) {
        ik_limits(context, *scene_root, filter, line_style, style.joint_size);
    }
    flush(context);
}

void Joint_constraint_visualization::physics_joints(
    const Render_context&         context,
    Scene_root&                   scene_root,
    const Joint_constraint_filter filter,
    const Joint_line_style&       line_style,
    const float                   joint_size
)
{
    static_cast<void>(context);
    Physics_joint_state state{};
    for (const std::unique_ptr<Joint_entry>& entry : scene_root.get_joint_system().get_entries()) {
        if (!get_physics_joint_state(*entry, state)) {
            continue;
        }
        const erhe::scene::Node* const nodes[4] = {state.frame_node_0, state.frame_node_1, state.body_node_a, state.body_node_b};
        bool shown = (filter == Joint_constraint_filter::all);
        if (!shown && (filter != Joint_constraint_filter::off)) {
            // A Joint prim hovered in the item tree shows in both hovered
            // modes.
            shown = state.joint->is_hovered();
            for (const erhe::scene::Node* const node : nodes) {
                if (filter == Joint_constraint_filter::hovered_mesh) {
                    shown = shown || is_hovered_mesh(node);
                } else if (filter == Joint_constraint_filter::hovered_bone) {
                    shown = shown || is_hovered_bone(node);
                }
            }
        }
        if (!shown) {
            continue;
        }
        Physics_joint_line_input input{
            .frame_a       = state.frame_a,
            .frame_b       = state.frame_b,
            .body_a_origin = (state.body_node_a != nullptr) ? std::optional<glm::vec3>{glm::vec3{state.body_node_a->position_in_world()}} : std::optional<glm::vec3>{},
            .body_b_origin = (state.body_node_b != nullptr) ? std::optional<glm::vec3>{glm::vec3{state.body_node_b->position_in_world()}} : std::optional<glm::vec3>{},
            .shape         = state.shape,
            .coordinates   = state.coordinates,
            .range_check   = state.range_check,
            .live          = state.live,
            .size          = (joint_size > 0.0f) ? joint_size : derive_joint_size(state),
            .style         = line_style
        };
        build_physics_joint_lines(input, m_lines);
        m_drawn_physics_joints.push_back(state.joint->get_id());
    }
}

void Joint_constraint_visualization::add_ik_bone(
    const erhe::scene::Node& node,
    const Ik_drag* const     ik_drag,
    const Joint_line_style&  line_style,
    const float              joint_size
)
{
    Ik_limit_state state{};
    if (!get_ik_limit_state(node, ik_drag, state)) {
        return;
    }
    sample_ik_swing_boundary(state.constraint, state.pinned_local_rotation, 8, m_swing_boundary);
    const Ik_limit_line_input input{
        .head            = state.head,
        .world_from_rest = state.world_from_rest,
        .constraint      = &state.constraint,
        .boundary        = &m_swing_boundary,
        .current         = state.current,
        .within_limits   = state.within_limits,
        .twist_sign      = state.twist_sign,
        .length          = (joint_size > 0.0f) ? joint_size : std::max(state.length, 0.01f),
        .style           = line_style
    };
    build_ik_limit_lines(input, m_lines);
    m_drawn_ik_bones.push_back(node.get_id());
}

void Joint_constraint_visualization::ik_limits(
    const Render_context&         context,
    Scene_root&                   scene_root,
    const Joint_constraint_filter filter,
    const Joint_line_style&       line_style,
    const float                   joint_size
)
{
    const Transform_tool* const transform_tool = context.app_context.transform_tool;
    const Ik_drag* const        ik_drag        = (transform_tool != nullptr) ? &transform_tool->get_ik_drag() : nullptr;
    erhe::scene::Scene* const   scene          = scene_root.get_hosted_scene();
    if (scene == nullptr) {
        return;
    }

    if (filter == Joint_constraint_filter::hovered_mesh) {
        // The bones that move the hovered mesh: its skin's joints, and the
        // bone ancestors of a rigidly bound mesh.
        m_hovered_mesh_bones.clear();
        for (erhe::scene::Mesh_layer* layer : scene_root.layers().mesh_layers()) {
            for (const std::shared_ptr<erhe::scene::Mesh>& mesh : layer->meshes) {
                if (!mesh || !mesh->is_hovered()) {
                    continue;
                }
                if (mesh->skin) {
                    for (const std::shared_ptr<erhe::scene::Node>& joint : mesh->skin->skin_data.joints) {
                        if (joint) {
                            m_hovered_mesh_bones.push_back(joint.get());
                        }
                    }
                }
                for (std::shared_ptr<erhe::scene::Node> ancestor = mesh->get_parent_node(); ancestor; ancestor = ancestor->get_parent_node()) {
                    if (erhe::scene::is_bone(static_cast<const erhe::Item_base*>(ancestor.get()))) {
                        m_hovered_mesh_bones.push_back(ancestor.get());
                    }
                }
            }
        }
        std::sort(m_hovered_mesh_bones.begin(), m_hovered_mesh_bones.end());
        m_hovered_mesh_bones.erase(std::unique(m_hovered_mesh_bones.begin(), m_hovered_mesh_bones.end()), m_hovered_mesh_bones.end());
        for (const erhe::scene::Node* const bone : m_hovered_mesh_bones) {
            add_ik_bone(*bone, ik_drag, line_style, joint_size);
        }
        return;
    }

    scene->for_each_node([&](const std::shared_ptr<erhe::scene::Node>& node) {
        if (!node) {
            return true;
        }
        const erhe::Item_base* const item    = static_cast<const erhe::Item_base*>(node.get());
        const bool                   is_bone = erhe::scene::is_bone(item);
        if (filter == Joint_constraint_filter::hovered_bone) {
            if (is_bone && node->is_hovered()) {
                add_ik_bone(*node, ik_drag, line_style, joint_size);
            }
            return true;
        }
        // all: every bone, and any node carrying IK values of its own.
        if (is_bone || has_local_ik_value(*node)) {
            add_ik_bone(*node, ik_drag, line_style, joint_size);
        }
        return true;
    });
}

void Joint_constraint_visualization::flush(const Render_context& context)
{
    if (m_lines.lines.empty()) {
        return;
    }
    // X-ray, so a joint inside its body and a bone inside its skin show.
    erhe::renderer::Primitive_renderer line_renderer = context.get(
        erhe::renderer::Debug_renderer_config{
            .primitive_type    = erhe::graphics::Primitive_type::line,
            .stencil_reference = 2,
            .draw_visible      = true,
            .draw_hidden       = true,
            .xray              = true
        }
    );
    for (const Joint_line& line : m_lines.lines) {
        line_renderer.add_line(line.color, line.width, line.p0, line.color, line.width, line.p1);
    }
    m_lines.clear();
}

} // namespace editor
