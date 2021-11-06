#include "scene/node_physics.hpp"
#include "erhe/physics/log.hpp"
#include "erhe/toolkit/tracy_client.hpp"
#include "erhe/toolkit/verify.hpp"

namespace editor
{

using namespace erhe::scene;
using namespace erhe::physics;
using namespace std;

auto is_physics(const Node* const node) -> bool
{
    if (node == nullptr)
    {
        return false;
    }
    return (node->flag_bits() & Node::c_flag_bit_is_physics) == Node::c_flag_bit_is_physics;
}

auto is_physics(const std::shared_ptr<Node>& node) -> bool
{
    return is_physics(node.get());
}

auto as_physics(Node* node) -> Node_physics*
{
    if (node == nullptr)
    {
        return nullptr;
    }
    if ((node->flag_bits() & Node::c_flag_bit_is_physics) == 0)
    {
        return nullptr;
    }
    return reinterpret_cast<Node_physics*>(node);
}

auto as_physics(const std::shared_ptr<Node>& node) -> std::shared_ptr<Node_physics>
{
    if (node)
    {
        return {};
    }
    if ((node->flag_bits() & Node::c_flag_bit_is_physics) == 0)
    {
        return {};
    }
    return std::dynamic_pointer_cast<Node_physics>(node);
}

auto get_physics_node(Node* node) -> std::shared_ptr<Node_physics>
{
    if (node == nullptr)
    {
        return {};
    }
    if ((node->flag_bits() & Node::c_flag_bit_is_physics) == Node::c_flag_bit_is_physics)
    {
        return std::dynamic_pointer_cast<Node_physics>(node->shared_from_this());
    }
    return get_physics_node(node->parent());
}

auto Node_physics::node_type() const -> const char*
{
    return "Node_physics";
}

Node_physics::Node_physics(IRigid_body_create_info& create_info)
    : Node{{}}
    , m_rigid_body     {IRigid_body::create_shared(create_info, this)}
    , m_collision_shape{create_info.collision_shape}
{
    flag_bits() |= Node::c_flag_bit_is_physics;
}

Node_physics::~Node_physics() = default;

void Node_physics::on_attached_to(Node& node)
{
    ZoneScoped;

    erhe::scene::Node::on_attached_to(node);

    glm::mat3 basis{};
    glm::vec3 origin{};
    get_world_transform(basis, origin);
    VERIFY(m_rigid_body);
    m_rigid_body->set_world_transform(basis, origin);
}

void Node_physics::on_transform_changed()
{
    if (m_transform_change_from_physics)
    {
        return;
    }

    update_transform();
    glm::mat3 basis{};
    glm::vec3 origin{};
    get_world_transform(basis, origin);
    VERIFY(m_rigid_body);
    m_rigid_body->set_world_transform(basis, origin);
}

//auto Node_physics::get_node_transform() const -> glm::mat4
void Node_physics::get_world_transform(glm::mat3& basis, glm::vec3& origin)
{
    ZoneScoped;

    glm::mat4 m = world_from_node();
    glm::vec3 p = glm::vec3{m[3]};

    basis = glm::mat3{m};
    origin = p;
}

void Node_physics::set_world_transform(const glm::mat3 basis, const glm::vec3 origin) 
{
    ZoneScoped;

    // TODO Take center of mass into account

    if (origin.y < -100.0f)
    {
        const glm::vec3 p{0.0f, 8.0f, 0.0f};
        m_rigid_body->set_world_transform(basis, p);
        m_rigid_body->set_linear_velocity(glm::vec3{0.0f, 0.0f, 0.0f});
        m_rigid_body->set_angular_velocity(glm::vec3{0.0f, 0.0f, 0.0f});
    }

    m_transform_change_from_physics = true;

    const auto& m = basis;
    glm::mat4 transform{m};
    transform[3] = glm::vec4{origin.x, origin.y, origin.z, 1.0f};
    unparent();
    set_parent_from_node(transform);

    m_transform_change_from_physics = false;
}

void Node_physics::on_node_updated()
{
    ZoneScoped;

    if (m_rigid_body->get_motion_mode() == Motion_mode::e_static)
    {
        log_physics.warn("Attempt to move static rigid body - promoting to kinematic.\n");
        m_rigid_body->set_motion_mode(Motion_mode::e_kinematic);
    }
    glm::mat3 basis{};
    glm::vec3 origin{};
    get_world_transform(basis, origin);
    m_rigid_body->set_world_transform(basis, origin);
}

auto Node_physics::rigid_body() -> IRigid_body*
{
    return m_rigid_body.get();
}

auto Node_physics::rigid_body() const -> const IRigid_body*
{
    return m_rigid_body.get();
}


}