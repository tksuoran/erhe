#include "scene/node_physics.hpp"
#include "erhe/physics/log.hpp"
#include "erhe/toolkit/tracy_client.hpp"
#include "erhe/toolkit/verify.hpp"

namespace editor
{

using namespace erhe::scene;
using namespace erhe::physics;
using namespace std;

Node_physics::Node_physics(IRigid_body_create_info& create_info)
    : m_node           {{}}
    , m_rigid_body     {IRigid_body::create_shared(create_info, this)}
    , m_collision_shape{create_info.collision_shape}
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
    glm::mat3 basis{};
    glm::vec3 origin{};
    get_world_transform(basis, origin);
    VERIFY(m_rigid_body);
    m_rigid_body->set_world_transform(basis, origin);
}

void Node_physics::on_detach(Node& node)
{
    ZoneScoped;

    m_node.reset();
}

//auto Node_physics::get_node_transform() const -> glm::mat4
void Node_physics::get_world_transform(glm::mat3& basis, glm::vec3& origin)
{
    ZoneScoped;

    if (m_node.get() == nullptr)
    {
        basis  = glm::mat3{1.0f};
        origin = glm::vec3{0.0f};
        return;
    }

    glm::mat4 m = m_node->world_from_node();
    glm::vec3 p = glm::vec3{m[3]};

    basis = glm::mat3{m};
    origin = p;
}

void Node_physics::set_world_transform(const glm::mat3 basis, const glm::vec3 origin) 
{
    ZoneScoped;

    if (m_node.get() == nullptr)
    {
        return;
    }

    // TODO Take center of mass into account

    if (origin.y < -100.0f)
    {
        const glm::vec3 p{0.0f, 8.0f, 0.0f};
        m_rigid_body->set_world_transform(basis, p);
        m_rigid_body->set_linear_velocity(glm::vec3{0.0f, 0.0f, 0.0f});
        m_rigid_body->set_angular_velocity(glm::vec3{0.0f, 0.0f, 0.0f});
    }

    const auto& m = basis;
    glm::mat4 transform{m};
    transform[3] = glm::vec4{origin.x, origin.y, origin.z, 1.0f};
    m_node->parent = nullptr;
    m_node->transforms.parent_from_node.set(transform);
}

void Node_physics::on_node_updated()
{
    ZoneScoped;

    VERIFY(m_node.get() != nullptr);

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