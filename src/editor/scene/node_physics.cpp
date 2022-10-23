#include "scene/node_physics.hpp"
#include "scene/helpers.hpp"
#include "editor_log.hpp"

#include "erhe/log/log_glm.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/physics/physics_log.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

namespace editor
{

using erhe::physics::IRigid_body_create_info;
using erhe::physics::IRigid_body;
using erhe::physics::Motion_mode;
using erhe::scene::INode_attachment;

Node_physics::Node_physics(
    const IRigid_body_create_info& create_info
)
    : m_rigidbody_from_node{}
    , m_node_from_rigidbody{}
    , m_motion_mode        {
        (create_info.mass == 0.0f)
            ? erhe::physics::Motion_mode::e_static
            : erhe::physics::Motion_mode::e_dynamic
    }
    , m_rigid_body         {IRigid_body::create_rigid_body_shared(create_info, this)}
    , m_collision_shape    {create_info.collision_shape}
{
    log_physics->trace("Created Node_physics {}", create_info.debug_label);
    m_flag_bits |= INode_attachment::c_flag_bit_is_physics;
}

Node_physics::~Node_physics() noexcept
{
    if (m_physics_world != nullptr)
    {
        remove_from_physics_world(*m_physics_world, *this);
    }
}

auto Node_physics::node_attachment_type() const -> const char*
{
    return "Node_physics";
}

void Node_physics::on_attached_to(erhe::physics::IWorld* world)
{
    log_physics->trace("attached {} to world", m_rigid_body->get_debug_label());
    m_physics_world = world;
}

void Node_physics::on_detached_from(erhe::physics::IWorld* world)
{
    static_cast<void>(world);
    m_physics_world = nullptr;
}

// This is called from scene graph (Node) when mode is moved
void Node_physics::on_node_transform_changed()
{
    if (m_transform_change_from_physics)
    //if (m_motion_mode != Motion_mode::e_dynamic)
    {
        return;
    }

    const erhe::physics::Transform world_from_node = get_world_from_node();

    log_physics->trace(
        "on_node_transform_changed {} pos = {}",
        m_rigid_body->get_debug_label(),
        world_from_node.origin
    );

    ERHE_VERIFY(m_rigid_body);
    m_rigid_body->set_world_transform(world_from_node * m_node_from_rigidbody);
}

void Node_physics::set_rigidbody_from_node(const erhe::physics::Transform rigidbody_from_node)
{
    m_rigidbody_from_node = rigidbody_from_node;
    m_node_from_rigidbody = inverse(rigidbody_from_node);
}

// This gets called by Motion_state_adapter
auto Node_physics::get_world_from_rigidbody() const -> erhe::physics::Transform
{
    return get_world_from_node() * m_node_from_rigidbody;
}

auto Node_physics::get_motion_mode() const -> erhe::physics::Motion_mode
{
    return m_motion_mode;
}

void Node_physics::set_motion_mode(const erhe::physics::Motion_mode motion_mode)
{
    m_motion_mode = motion_mode;
}

auto Node_physics::get_world_from_node() const -> erhe::physics::Transform
{
    ERHE_PROFILE_FUNCTION

    if (get_node() == nullptr)
    {
        return erhe::physics::Transform{};
    }

    const glm::mat4 m = get_node()->world_from_node();
    const glm::vec3 p = glm::vec3{m[3]};

    return erhe::physics::Transform{
        glm::mat3{m},
        p
    };
}

void Node_physics::set_world_from_rigidbody(
    const erhe::physics::Transform world_from_rigidbody
)
{
    //log_physics->trace("{} pos = {}", m_rigid_body->get_debug_label(), world_from_rigidbody.origin);
    set_world_from_node(world_from_rigidbody * m_rigidbody_from_node);
}

// This is intended to be called from physics backend
void Node_physics::set_world_from_node(
    const erhe::physics::Transform world_from_node
)
{
    ERHE_PROFILE_FUNCTION

    ERHE_VERIFY(m_node != nullptr);

    // TODO Take center of mass into account

    if (world_from_node.origin.y < -100.0f)
    {
        const glm::vec3 respawn_location{0.0f, 8.0f, 0.0f};
        m_rigid_body->set_world_transform (erhe::physics::Transform{world_from_node.basis, respawn_location});
        m_rigid_body->set_linear_velocity (glm::vec3{0.0f, 0.0f, 0.0f});
        m_rigid_body->set_angular_velocity(glm::vec3{0.0f, 0.0f, 0.0f});
    }

    m_transform_change_from_physics = true;

    const auto& m = world_from_node.basis;
    glm::mat4 matrix{m};
    matrix[3] = glm::vec4{
        world_from_node.origin.x,
        world_from_node.origin.y,
        world_from_node.origin.z,
        1.0f
    };
    // TODO don't unparent, call set_world_from_node() instead?
    get_node()->unparent();
    get_node()->set_parent_from_node(matrix);

    m_transform_change_from_physics = false;
}

auto Node_physics::rigid_body() -> IRigid_body*
{
    return m_rigid_body.get();
}

auto Node_physics::rigid_body() const -> const IRigid_body*
{
    return m_rigid_body.get();
}

auto is_physics(const INode_attachment* const attachment) -> bool
{
    if (attachment == nullptr)
    {
        return false;
    }
    return (attachment->flag_bits() & INode_attachment::c_flag_bit_is_physics) == INode_attachment::c_flag_bit_is_physics;
}

auto is_physics(const std::shared_ptr<INode_attachment>& attachment) -> bool
{
    return is_physics(attachment.get());
}

auto as_physics(INode_attachment* attachment) -> Node_physics*
{
    if (attachment == nullptr)
    {
        return nullptr;
    }
    if ((attachment->flag_bits() & INode_attachment::c_flag_bit_is_physics) == 0)
    {
        return nullptr;
    }
    return reinterpret_cast<Node_physics*>(attachment);
}

auto as_physics(
    const std::shared_ptr<INode_attachment>& attachment
) -> std::shared_ptr<Node_physics>
{
    if (!attachment)
    {
        return {};
    }
    if ((attachment->flag_bits() & INode_attachment::c_flag_bit_is_physics) == 0)
    {
        return {};
    }
    return std::dynamic_pointer_cast<Node_physics>(attachment);
}

auto get_physics_node(
    erhe::scene::Node* node
) -> std::shared_ptr<Node_physics>
{
    for (const auto& attachment : node->attachments())
    {
        auto node_physics = as_physics(attachment);
        if (node_physics)
        {
            return node_physics;
        }
    }
    return {};
}

}
