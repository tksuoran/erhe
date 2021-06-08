#include "scene/node_physics.hpp"
#include "erhe/physics/log.hpp"
#include "erhe/toolkit/tracy_client.hpp"
#include "erhe/toolkit/verify.hpp"

namespace editor
{

using namespace erhe::scene;
using namespace erhe::physics;
using namespace std;

Node_physics::Node_physics(Rigid_body::Create_info& create_info)
    : m_node         {{}}
    , rigid_body     {create_info, this}
    , collision_shape{create_info.collision_shape}
{
    m_node.reset();
}

Node_physics::~Node_physics() = default;

const string c_node_physics("Node_physics");

auto Node_physics::name() const -> const string&
{
    return c_node_physics;
}

void Node_physics::on_attach(Node& node)
{
    ZoneScoped;

    m_node = node.shared_from_this();
    btTransform world_transform = get_node_transform();
    rigid_body.bullet_rigid_body.setWorldTransform(world_transform);
}

void Node_physics::on_detach(Node& node)
{
    ZoneScoped;

    m_node.reset();
}

btTransform Node_physics::get_node_transform() const
{
    ZoneScoped;

    if (m_node.get() == nullptr)
    {
        return btTransform::getIdentity();
    }

    glm::mat4 m = m_node->world_from_node();
    glm::vec3 p = glm::vec3(m[3]);
    return btTransform(btMatrix3x3(m[0][0], m[1][0], m[2][0],
                                   m[0][1], m[1][1], m[2][1],
                                   m[0][2], m[1][2], m[2][2]),
                       btVector3(p[0], p[1], p[2]));
}

void Node_physics::getWorldTransform(btTransform& worldTrans) const
{
    ZoneScoped;

    if (m_node.get() == nullptr)
    {
        worldTrans = btTransform::getIdentity();
        return;
    }

    // TODO Take center of mass into account
    glm::mat4 m = m_node->world_from_node();
    worldTrans.setBasis(btMatrix3x3(m[0][0], m[1][0], m[2][0],
                                    m[0][1], m[1][1], m[2][1],
                                    m[0][2], m[1][2], m[2][2]));
    worldTrans.setOrigin(btVector3(m[3][0], m[3][1], m[3][2]));
}

void Node_physics::setWorldTransform(const btTransform& worldTrans)
{
    ZoneScoped;

    if (m_node.get() == nullptr)
    {
        return;
    }

    // TODO Take center of mass into account
    btMatrix3x3 m = worldTrans.getBasis();
    btVector3   p = worldTrans.getOrigin();

    if (p.y() < -100.0f)
    {
        p = btVector3(0.0f, 8.0f, 0.0f);
        rigid_body.bullet_rigid_body.setWorldTransform(btTransform(m, p));
        rigid_body.bullet_rigid_body.setLinearVelocity(btVector3(0.0f, 0.0f, 0.0f));
        rigid_body.bullet_rigid_body.setAngularVelocity(btVector3(0.0f, 0.0f, 0.0f));
    }

    glm::mat4 transform(m.getColumn(0).x(), m.getColumn(0).y(), m.getColumn(0).z(), 0.0f,
                        m.getColumn(1).x(), m.getColumn(1).y(), m.getColumn(1).z(), 0.0f,
                        m.getColumn(2).x(), m.getColumn(2).y(), m.getColumn(2).z(), 0.0f,
                        p.x(),              p.y(),              p.z(),              1.0f);
    m_node->parent = nullptr;
    m_node->transforms.parent_from_node.set(transform);
}

void Node_physics::on_node_updated()
{
    ZoneScoped;

    VERIFY(m_node.get() != nullptr);

    if (rigid_body.get_collision_mode() == Rigid_body::Collision_mode::e_static)
    {
        log_physics.warn("Attempt to move static rigid body - promoting to kinematic.\n");
        rigid_body.set_kinematic();
    }
    btTransform world_transform = get_node_transform();
    rigid_body.bullet_rigid_body.setWorldTransform(world_transform);
}


}