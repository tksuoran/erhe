#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "editor_log.hpp"

#include "erhe/log/log_glm.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <glm/gtx/matrix_decompose.hpp>

namespace editor
{

using erhe::physics::IRigid_body_create_info;
using erhe::physics::IRigid_body;
using erhe::physics::Motion_mode;
using erhe::scene::Node_attachment;

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
    , m_rigid_body     {IRigid_body::create_rigid_body_shared(create_info, this)}
    , m_collision_shape{create_info.collision_shape}
{
    log_physics->trace("Created Node_physics {}", create_info.debug_label);
}

Node_physics::~Node_physics() noexcept
{
    set_node(nullptr);
}

auto Node_physics::static_type() -> uint64_t
{
    return erhe::scene::Item_type::node_attachment | erhe::scene::Item_type::physics;
}

auto Node_physics::static_type_name() -> const char*
{
    return "Node_physics";
}

auto Node_physics::get_type() const -> uint64_t
{
    return static_type();
}

auto Node_physics::type_name() const -> const char*
{
    return static_type_name();
}

void Node_physics::handle_node_scene_host_update(
    erhe::scene::Scene_host* old_scene_host,
    erhe::scene::Scene_host* new_scene_host
)
{
    ERHE_VERIFY(old_scene_host != new_scene_host);

    if (old_scene_host != nullptr) {
        Scene_root* old_scene_root = reinterpret_cast<Scene_root*>(old_scene_host);
        auto& physics_world = old_scene_root->physics_world();
        physics_world.remove_rigid_body(rigid_body());
        m_physics_world = nullptr;
    }
    if (new_scene_host != nullptr) {
        log_physics->trace("attaching {} to physics world", m_rigid_body->get_debug_label());
        Scene_root* new_scene_root = reinterpret_cast<Scene_root*>(new_scene_host);
        auto& physics_world = new_scene_root->physics_world();
        physics_world.add_rigid_body(rigid_body());
        m_physics_world = &physics_world;
    }
}

// This is called from scene graph (Node) when mode is moved
void Node_physics::handle_node_transform_update()
{
    // TODO ERHE_VERIFY(m_node != nullptr);

    if (m_transform_change_from_physics) {
        return;
    }

    // TODO This is a hack, remove when Node_physics handles shape scale
    const glm::mat4 m = get_node()->world_from_node();
    glm::vec3 scale;
    glm::quat orientation;
    glm::vec3 translation;
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::decompose(m, scale, orientation, translation, skew, perspective);
    m_scale = scale;

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
    ERHE_PROFILE_FUNCTION();

    if (get_node() == nullptr) {
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
    const glm::mat4& world_from_rigidbody
)
{
    const auto& m = m_rigidbody_from_node.basis;
    glm::mat4 rigidbody_from_node{m};
    rigidbody_from_node[3] = glm::vec4{
        m_rigidbody_from_node.origin.x,
        m_rigidbody_from_node.origin.y,
        m_rigidbody_from_node.origin.z,
        1.0f
    };

    set_world_from_node(world_from_rigidbody * rigidbody_from_node);
}

void Node_physics::set_world_from_rigidbody(
    const erhe::physics::Transform world_from_rigidbody
)
{
    set_world_from_node(world_from_rigidbody * m_rigidbody_from_node);
}

// This is intended to be called from physics backend
void Node_physics::set_world_from_node(
    const glm::mat4& world_from_node_no_scale
)
{
    ERHE_PROFILE_FUNCTION();

    // TODO ERHE_VERIFY(m_node != nullptr);
    if (m_node == nullptr) {
        return; // TODO FIX
    }

    // TODO This is a hack, remove when Node_physics handles shape scale
    const glm::mat4 scale = erhe::toolkit::create_scale<float>(m_scale);
    const glm::mat4& world_from_node = world_from_node_no_scale * scale;

    // TODO Take center of mass into account

    const glm::vec3 world_position = glm::vec3{world_from_node * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};
    if (world_position.y < -100.0f) {
        const glm::vec3 respawn_location{0.0f, 8.0f, 0.0f};
        m_rigid_body->set_world_transform (erhe::physics::Transform{glm::mat3{world_from_node}, respawn_location});
        m_rigid_body->set_linear_velocity (glm::vec3{0.0f, 0.0f, 0.0f});
        m_rigid_body->set_angular_velocity(glm::vec3{0.0f, 0.0f, 0.0f});
    }

    m_transform_change_from_physics = true;

    const glm::mat4& matrix{world_from_node};
    // TODO don't unparent, call set_world_from_node() instead?
    get_node()->set_parent(get_node()->get_scene()->get_root_node());
    get_node()->set_parent_from_node(matrix);

    m_transform_change_from_physics = false;
}
void Node_physics::set_world_from_node(
    const erhe::physics::Transform world_from_node
)
{
    ERHE_PROFILE_FUNCTION();

    // TDOO ERHE_VERIFY(m_node != nullptr);
    if (m_node == nullptr) {
        return; // TODO FIX
    }

    // TODO Take center of mass into account

    if (world_from_node.origin.y < -100.0f) {
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
    get_node()->set_parent(get_node()->get_scene()->get_root_node());
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

auto is_physics(const erhe::scene::Item* const scene_item) -> bool
{
    if (scene_item == nullptr) {
        return false;
    }
    using namespace erhe::toolkit;
    return test_all_rhs_bits_set(
        scene_item->get_type(),
        erhe::scene::Item_type::physics
    );
}

auto is_physics(const std::shared_ptr<erhe::scene::Item>& scene_item) -> bool
{
    return is_physics(scene_item.get());
}

auto as_physics(erhe::scene::Item* scene_item) -> Node_physics*
{
    if (scene_item == nullptr) {
        return nullptr;
    }
    using namespace erhe::toolkit;
    if (
        !test_all_rhs_bits_set(
            scene_item->get_type(),
            erhe::scene::Item_type::physics
        )
    ) {
        return nullptr;
    }
    return static_cast<Node_physics*>(scene_item);
}

auto as_physics(
    const std::shared_ptr<erhe::scene::Item>& scene_item
) -> std::shared_ptr<Node_physics>
{
    if (!scene_item) {
        return {};
    }
    using namespace erhe::toolkit;
    if (
        !test_all_rhs_bits_set(
            scene_item->get_type(),
            erhe::scene::Item_type::physics
        )
    ) {
        return {};
    }
    return std::static_pointer_cast<Node_physics>(scene_item);
}

auto get_node_physics(
    const erhe::scene::Node* node
) -> std::shared_ptr<Node_physics>
{
    for (const auto& attachment : node->attachments()) {
        auto node_physics = as_physics(attachment);
        if (node_physics) {
            return node_physics;
        }
    }
    return {};
}

}
