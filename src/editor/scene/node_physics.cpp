#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "editor_log.hpp"

#include "erhe_log/log_glm.hpp"
#include "erhe_physics/iworld.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

using erhe::physics::IRigid_body_create_info;
using erhe::physics::IRigid_body;
using erhe::physics::Motion_mode;
using erhe::scene::Node_attachment;

Node_physics::Node_physics(const Node_physics&) = default;
Node_physics& Node_physics::operator=(const Node_physics&) = default;

Node_physics::Node_physics(const Node_physics& src, erhe::for_clone)
    : Item               {src, erhe::for_clone{}}
    , markers            {}        // clone does not initially have markers
    , physics_motion_mode{src.physics_motion_mode}
    , m_physics_world    {nullptr} // clone is initially detached
    , m_create_info      {src.m_create_info}
    , m_rigid_body       {}        // clone rigid body is not initially created
{
}

Node_physics::Node_physics(const IRigid_body_create_info& create_info)
    : m_create_info{create_info}
{
}

Node_physics::~Node_physics() noexcept
{
    set_node(nullptr);
}

auto Node_physics::get_static_type() -> uint64_t
{
    return erhe::Item_type::node_attachment | erhe::Item_type::physics;
}

auto Node_physics::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Node_physics::get_type_name() const -> std::string_view
{
    return static_type_name;
}

void Node_physics::set_physics_world(erhe::physics::IWorld* value)
{
    if (value != nullptr) {
        ERHE_VERIFY(m_physics_world == nullptr);
    }
    m_physics_world = value;
}

auto Node_physics::get_physics_world() const -> erhe::physics::IWorld*
{
    return m_physics_world;
}

auto Node_physics::clone_attachment() const -> std::shared_ptr<Node_attachment>
{
    return std::make_shared<Node_physics>(*this, erhe::for_clone{});
}

void Node_physics::handle_item_host_update(erhe::Item_host* const old_item_host, erhe::Item_host* const new_item_host)
{
    ERHE_VERIFY(old_item_host != new_item_host);

    // NOTE: This also keeps this alive is old host is only shared_ptr to it
    const auto shared_this = std::static_pointer_cast<Node_physics>(shared_from_this());

    if (old_item_host != nullptr) {
        Scene_root* old_scene_root = static_cast<Scene_root*>(old_item_host);
        old_scene_root->unregister_node_physics(shared_this);
        m_rigid_body.reset();
    }
    if (new_item_host != nullptr) {
        Scene_root* new_scene_root = static_cast<Scene_root*>(new_item_host);
        auto& physics_world = new_scene_root->get_physics_world();

        log_physics->trace("making rigid body for {}", get_node()->get_name());
        m_rigid_body = physics_world.create_rigid_body_shared(m_create_info);
        m_rigid_body->set_owner(this);
        new_scene_root->register_node_physics(shared_this);
    }
}

void Node_physics::before_physics_simulation()
{
    const erhe::scene::Trs_transform& world_from_node_transform = get_node()->world_from_node_transform();
    erhe::physics::Transform transform{
        glm::mat3_cast(world_from_node_transform.get_rotation()),
        world_from_node_transform.get_translation()
    };
    m_rigid_body->set_world_transform(transform);

    const auto get_transform = m_rigid_body->get_world_transform();
    const glm::vec3 get_world_position = glm::vec3{get_transform * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};
}

// This is intended to be called from physics backend
void Node_physics::after_physics_simulation()
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(m_node != nullptr);
    ERHE_VERIFY(m_rigid_body);

    const auto motion_mode = m_rigid_body->get_motion_mode();
    if (motion_mode != erhe::physics::Motion_mode::e_dynamic) {
        return;
    }

    const auto transform = m_rigid_body->get_world_transform();
    const glm::vec3 world_position = glm::vec3{transform * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};

    if (world_position.y < -100.0f) {
        const glm::vec3 respawn_location{0.0f, 8.0f, 0.0f};
        m_rigid_body->set_world_transform (erhe::physics::Transform{glm::mat3{1.0f}, respawn_location});
        m_rigid_body->set_linear_velocity (glm::vec3{0.0f, 0.0f, 0.0f});
        m_rigid_body->set_angular_velocity(glm::vec3{0.0f, 0.0f, 0.0f});
        const glm::mat4 matrix = erhe::math::create_translation<float>(respawn_location);
        get_node()->set_world_from_node(matrix);
    } else {
        get_node()->set_world_from_node(transform);
    }
}

auto Node_physics::get_rigid_body() -> IRigid_body*
{
    return (m_physics_world != nullptr) ? m_rigid_body.get() : nullptr;
}

auto Node_physics::get_rigid_body() const -> const IRigid_body*
{
    return (m_physics_world != nullptr) ? m_rigid_body.get() : nullptr;
}

auto is_physics(const erhe::Item_base* const scene_item) -> bool
{
    if (scene_item == nullptr) {
        return false;
    }
    return erhe::bit::test_all_rhs_bits_set(
        scene_item->get_type(),
        erhe::Item_type::physics
    );
}

auto is_physics(const std::shared_ptr<erhe::Item_base>& item) -> bool
{
    return is_physics(item.get());
}

auto get_node_physics(const erhe::scene::Node* node) -> std::shared_ptr<Node_physics>
{
    for (const auto& attachment : node->get_attachments()) {
        auto node_physics = std::dynamic_pointer_cast<Node_physics>(attachment);
        if (node_physics) {
            return node_physics;
        }
    }
    return {};
}

}
