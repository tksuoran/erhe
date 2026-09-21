#include "transform/physics_driven_drag.hpp"

#include "app_context.hpp"
#include "app_message.hpp"
#include "editor_log.hpp"
#include "physics/physics_drag_constraint.hpp"
#include "scene/node_physics_system.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_settings_resolve.hpp"
#include "transform/transform_tool.hpp"

#include "config/generated/editor_settings_config.hpp"
#include "config/generated/physics_config.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_utility/bit_helpers.hpp"

#include <algorithm>

namespace editor {

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
        erhe::physics::IRigid_body* const rigid_body = get_node_rigid_body(*node.get());
        if ((rigid_body == nullptr) || (rigid_body->get_motion_mode() != erhe::physics::Motion_mode::e_dynamic)) {
            continue; // kinematic (a selected unjointed body), static: the node write moves it
        }
        Scene_root* const scene_root = dynamic_cast<Scene_root*>(node->get_item_host());
        if ((scene_root == nullptr) || !scene_root->has_physics_world()) {
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
        driven_node.node      = node;
        driven_node.item_host = scene_root;
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
                *scene_root,
                *rigid_body,
                glm::vec3{0.0f, 0.0f, 0.0f},
                drag_point,
                make_jointed_body_drag_settings(rigid_body->get_mass())
            );
            log_physics->trace("Transform drag pulls jointed body through physics: {}", node->describe());
        } else {
            scene_root->get_node_physics_system().begin_interaction(*node.get());
            log_physics->trace("Transform drag holds dynamic body kinematic: {}", node->describe());
        }
        m_driven_nodes.push_back(std::move(driven_node));
    }
}

auto Physics_driven_drag::drive(const erhe::scene::Node* const node, const glm::mat4& world_from_node) -> bool
{
    for (Driven_node& driven_node : m_driven_nodes) {
        if ((driven_node.node.get() != node) || !driven_node.spring) {
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
        if ((driven_node.node.get() == node) && driven_node.spring) {
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
    } else if (driven_node.node) {
        Node_physics_system* const system = find_node_physics_system(*driven_node.node.get());
        if (system != nullptr) {
            system->end_interaction(*driven_node.node.get());
        }
    }
    driven_node.node.reset();
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
            [](const Driven_node& driven_node) { return !driven_node.node; }
        ),
        m_driven_nodes.end()
    );
}

void Physics_driven_drag::on_items_removed(const Removed_items& removed)
{
    for (Driven_node& driven_node : m_driven_nodes) {
        if (driven_node.node && removed.lookup.contains(driven_node.node.get())) {
            release(driven_node);
        }
    }
    m_driven_nodes.erase(
        std::remove_if(
            m_driven_nodes.begin(), m_driven_nodes.end(),
            [](const Driven_node& driven_node) { return !driven_node.node; }
        ),
        m_driven_nodes.end()
    );
}

}
