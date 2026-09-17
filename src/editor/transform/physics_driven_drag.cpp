#include "transform/physics_driven_drag.hpp"

#include "app_context.hpp"
#include "app_message.hpp"
#include "editor_log.hpp"
#include "physics/physics_drag_constraint.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_settings_resolve.hpp"
#include "transform/transform_tool.hpp"

#include "config/generated/editor_settings_config.hpp"
#include "config/generated/physics_config.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_utility/bit_helpers.hpp"

#include <algorithm>

namespace editor {

namespace {

// Spring of the drag pull. The static sag of a spring-hung body under gravity
// is g / (2 pi f)^2 independent of mass: 2.5 mm at 10 Hz, so a jointed body
// follows the gizmo closely. Critically damped, so it settles on the target
// without ringing.
constexpr float c_spring_frequency = 10.0f;
constexpr float c_spring_damping   = 1.0f;

// The pull is bounded to this many times the body's weight: enough to lift
// the body and push what it leans on, while the joints never carry more.
// Unbounded, a 10 Hz pull dragged 0.25 m out of a cradle ball's swing plane
// tore its hinge 48 mm (Jolt) / 112 mm (Box3D) apart. Pulling against a joint
// deflects the joint by the pull over the joint's stiffness, so a bound pull
// is what keeps every joint of the dragged body holding.
constexpr float c_max_force_in_body_weights = 3.0f;
constexpr float c_standard_gravity          = 9.81f;

// Jolt's default solver iterations (10 velocity / 2 position) leave a jointed
// body's joints visibly strained under a steady pull; the drag raises the
// dragged body's island to these for the lifetime of the drag constraint.
constexpr unsigned int c_solver_velocity_iterations = 40;
constexpr unsigned int c_solver_position_iterations = 20;

}

Physics_driven_drag::Driven_node::Driven_node() = default;
Physics_driven_drag::Driven_node::Driven_node(Driven_node&&) noexcept = default;
auto Physics_driven_drag::Driven_node::operator=(Driven_node&&) noexcept -> Driven_node& = default;
Physics_driven_drag::Driven_node::~Driven_node() noexcept = default;

Physics_driven_drag::Physics_driven_drag() = default;

Physics_driven_drag::~Physics_driven_drag() noexcept
{
    end();
}

void Physics_driven_drag::begin(App_context& context, std::vector<Transform_entry>& entries, const Transform_drag_kind kind)
{
    end();

    for (Transform_entry& entry : entries) {
        const std::shared_ptr<erhe::scene::Node>& node = entry.node;
        if (!node) {
            continue;
        }
        if (erhe::utility::test_bit_set(node->get_flag_bits(), erhe::Item_flags::lock_viewport_transform)) {
            continue; // the drag does not move it
        }
        std::shared_ptr<Node_physics> node_physics = erhe::scene::get_attachment<Node_physics>(node.get());
        if (!node_physics) {
            continue;
        }
        erhe::physics::IRigid_body* const rigid_body = node_physics->get_rigid_body();
        if ((rigid_body == nullptr) || (rigid_body->get_motion_mode() != erhe::physics::Motion_mode::e_dynamic)) {
            continue; // kinematic (a selected unjointed body), static: the node write moves it
        }
        Scene_root* const scene_root = dynamic_cast<Scene_root*>(node->get_item_host());
        if ((scene_root == nullptr) || !scene_root->has_physics_world() || (node_physics->get_physics_world() == nullptr)) {
            continue;
        }
        const Physics_config& physics = get_effective_physics(*context.editor_settings, *scene_root);
        if (!physics.static_enable || !physics.dynamic_enable) {
            continue; // the simulation is not stepping: the node write carries the body on resume
        }

        // The body may have moved since the entries were captured (it stays
        // dynamic while selected); the drag starts from where it is now.
        entry.parent_from_node_before = node->parent_from_node_transform();
        entry.xform_op_stack_before   = node->copy_xform_op_stack();
        entry.world_from_node_before  = node->world_from_node_transform();

        Driven_node driven_node{};
        driven_node.node_physics = node_physics;
        driven_node.node         = node.get();
        driven_node.item_host    = scene_root;
        const std::shared_ptr<erhe::physics::ICollision_shape> collision_shape = rigid_body->get_collision_shape();
        if (collision_shape) {
            driven_node.center_of_mass_in_node = collision_shape->get_center_of_mass();
        }

        const bool jointed = scene_root->is_jointed_rigid_body(rigid_body);
        if ((kind != Transform_drag_kind::scale) && jointed) {
            const glm::vec3 drag_point = glm::vec3{
                entry.world_from_node_before.get_matrix() * glm::vec4{driven_node.center_of_mass_in_node, 1.0f}
            };
            driven_node.spring = std::make_unique<Physics_drag_constraint>();
            driven_node.spring->attach(
                scene_root->get_physics_world(),
                *rigid_body,
                glm::vec3{0.0f, 0.0f, 0.0f},
                drag_point,
                Physics_drag_constraint_settings{
                    .frequency                  = c_spring_frequency,
                    .damping                    = c_spring_damping,
                    .max_force                  = c_max_force_in_body_weights * rigid_body->get_mass() * c_standard_gravity,
                    .solver_velocity_iterations = c_solver_velocity_iterations,
                    .solver_position_iterations = c_solver_position_iterations
                },
                Physics_drag_monitor_info{
                    .monitor            = &scene_root->get_physics_drag_monitor(),
                    .tool_name          = "transform tool",
                    .node               = node.get(),
                    .values_before_tool = Physics_drag_body_values{
                        .linear_damping  = rigid_body->get_linear_damping(),
                        .angular_damping = rigid_body->get_angular_damping(),
                        .friction        = rigid_body->get_friction(),
                        .gravity_factor  = rigid_body->get_gravity_factor()
                    },
                    .tool_details = fmt::format(
                        "drag kind {}, spring pivot at center of mass {}, drag point teleported to the gizmo pose each drive",
                        (kind == Transform_drag_kind::translate) ? "translate" : "rotate",
                        driven_node.center_of_mass_in_node
                    )
                }
            );
            log_physics->trace("Transform drag pulls jointed body through physics: {}", node->describe());
        } else {
            node_physics->begin_interaction();
            log_physics->trace("Transform drag holds dynamic body kinematic: {}", node->describe());
        }
        m_driven_nodes.push_back(std::move(driven_node));
    }
}

auto Physics_driven_drag::drive(const erhe::scene::Node* const node, const glm::mat4& world_from_node) -> bool
{
    for (Driven_node& driven_node : m_driven_nodes) {
        if ((driven_node.node != node) || !driven_node.spring) {
            continue;
        }
        const glm::vec3 drag_point = glm::vec3{world_from_node * glm::vec4{driven_node.center_of_mass_in_node, 1.0f}};
        driven_node.spring->move_drag_point(drag_point, Drag_point_motion::teleport);
        return true;
    }
    return false;
}

auto Physics_driven_drag::is_spring_driven(const erhe::scene::Node* const node) const -> bool
{
    for (const Driven_node& driven_node : m_driven_nodes) {
        if ((driven_node.node == node) && driven_node.spring) {
            return true;
        }
    }
    return false;
}

auto Physics_driven_drag::is_active() const -> bool
{
    return !m_driven_nodes.empty();
}

void Physics_driven_drag::release(Driven_node& driven_node)
{
    if (driven_node.spring) {
        driven_node.spring->detach();
        driven_node.spring.reset();
    } else if (driven_node.node_physics) {
        driven_node.node_physics->end_interaction();
    }
    driven_node.node_physics.reset();
}

void Physics_driven_drag::end()
{
    for (Driven_node& driven_node : m_driven_nodes) {
        release(driven_node);
    }
    m_driven_nodes.clear();
}

void Physics_driven_drag::on_close_scene(const erhe::Item_host* const closing_host)
{
    for (Driven_node& driven_node : m_driven_nodes) {
        if (driven_node.item_host == closing_host) {
            release(driven_node);
        }
    }
    m_driven_nodes.erase(
        std::remove_if(
            m_driven_nodes.begin(), m_driven_nodes.end(),
            [](const Driven_node& driven_node) { return !driven_node.node_physics; }
        ),
        m_driven_nodes.end()
    );
}

void Physics_driven_drag::on_items_removed(const Removed_items& removed)
{
    for (Driven_node& driven_node : m_driven_nodes) {
        if (removed.lookup.contains(driven_node.node) || removed.lookup.contains(driven_node.node_physics.get())) {
            release(driven_node);
        }
    }
    m_driven_nodes.erase(
        std::remove_if(
            m_driven_nodes.begin(), m_driven_nodes.end(),
            [](const Driven_node& driven_node) { return !driven_node.node_physics; }
        ),
        m_driven_nodes.end()
    );
}

}
