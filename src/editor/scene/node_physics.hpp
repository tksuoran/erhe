#pragma once

#include "erhe_scene/node_attachment.hpp"
#include "erhe_physics/irigid_body.hpp"

#include <vector>

namespace erhe          { class Item_host; }
namespace erhe::physics {
    class Collision_filter;
    class IWorld;
    class Physics_material;
}

namespace editor {

class Node_physics
    : public erhe::Item<
        erhe::Item_base,
        erhe::scene::Node_attachment,
        Node_physics,
        erhe::Item_kind::clone_using_custom_clone_constructor
    >
{
public:
    explicit Node_physics(const erhe::physics::IRigid_body_create_info& create_info);
    Node_physics(const Node_physics& src, erhe::for_clone);

    explicit Node_physics(const Node_physics&);
    Node_physics& operator=(const Node_physics&);
    ~Node_physics() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Node_physics"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::node_attachment | erhe::Item_type::physics; }

    // Implements / overrides Node_attachment
    auto clone_attachment       () const -> std::shared_ptr<Node_attachment>                     override;
    void handle_item_host_update(erhe::Item_host* old_item_host, erhe::Item_host* new_item_host) override;

    // Public API
    [[nodiscard]] auto get_rigid_body()       ->       erhe::physics::IRigid_body*;
    [[nodiscard]] auto get_rigid_body() const -> const erhe::physics::IRigid_body*;
    [[nodiscard]] auto get_collision_shape() const -> const std::shared_ptr<erhe::physics::ICollision_shape>&;

    void before_physics_simulation();
    void after_physics_simulation();

    void set_physics_world(erhe::physics::IWorld* value);
    [[nodiscard]] auto get_physics_world() const -> erhe::physics::IWorld*;

    // Returns the intended/persistent motion mode.
    // This is always the "real" mode, even when the rigid body
    // is temporarily overridden to kinematic during interaction.
    [[nodiscard]] auto get_motion_mode() const -> erhe::physics::Motion_mode;
    void               set_motion_mode(erhe::physics::Motion_mode mode);

    // Temporarily override rigid body to kinematic for user interaction.
    // get_motion_mode() continues to return the intended mode.
    void begin_interaction();
    void end_interaction();

    // Shared physics material; updates both create info and the live rigid
    // body. Re-assign after editing a live Physics_material item so the
    // backend re-snapshots the values.
    [[nodiscard]] auto get_physics_material() const -> const std::shared_ptr<erhe::physics::Physics_material>&;
    void               set_physics_material(const std::shared_ptr<erhe::physics::Physics_material>& physics_material);

    // Shared collision filter; updates both create info and the live rigid
    // body. Re-assign after editing a live Collision_filter item so the
    // backend re-snapshots the compiled filter.
    [[nodiscard]] auto get_collision_filter() const -> const std::shared_ptr<erhe::physics::Collision_filter>&;
    void               set_collision_filter(const std::shared_ptr<erhe::physics::Collision_filter>& collision_filter);

    // Trigger (sensor) flag. Changing it recreates the rigid body. A static
    // trigger body is created kinematic non-physical (Jolt sensors must be
    // non-static to detect static bodies); get_motion_mode() continues to
    // return the user-facing motion mode.
    [[nodiscard]] auto is_trigger () const -> bool;
    void               set_trigger(bool trigger);

    [[nodiscard]] auto get_gravity_factor() const -> float;
    void               set_gravity_factor(float gravity_factor);

    // Initial velocities apply when the rigid body is (re)created - at scene
    // attach or after set_trigger() / set_center_of_mass_offset(); they do
    // not change a live body (use the rigid body API for that).
    [[nodiscard]] auto get_initial_linear_velocity () const -> glm::vec3;
    void               set_initial_linear_velocity (const glm::vec3& velocity);
    [[nodiscard]] auto get_initial_angular_velocity() const -> glm::vec3;
    void               set_initial_angular_velocity(const glm::vec3& velocity);

    // Center of mass offset, implemented by (re)wrapping the collision shape
    // in an offset-center-of-mass wrapper. Changing it recreates the rigid body.
    [[nodiscard]] auto get_center_of_mass_offset() const -> glm::vec3;
    void               set_center_of_mass_offset(const glm::vec3& offset);

    std::vector<glm::vec3> markers;

private:
    // The motion mode the rigid body is actually created with: same as
    // m_motion_mode, except that static trigger bodies are created kinematic
    // non-physical (Jolt sensors must be non-static to detect static bodies).
    [[nodiscard]] auto get_effective_motion_mode() const -> erhe::physics::Motion_mode;

    // Creates m_rigid_body from m_create_info, applying the effective motion
    // mode and the current node world transform. Does not add the body to the
    // world; Scene_root::register_node_physics() does that.
    void create_rigid_body(erhe::physics::IWorld& physics_world);

    // Recreates the rigid body from (updated) create info while attached to a
    // scene, going through Scene_root unregister/register so that dependent
    // bookkeeping (joint constraints) stays consistent. No-op when detached.
    void recreate_rigid_body();

    erhe::physics::IWorld*                      m_physics_world{nullptr};
    erhe::physics::IRigid_body_create_info      m_create_info;
    std::shared_ptr<erhe::physics::IRigid_body> m_rigid_body;
    erhe::physics::Motion_mode                  m_motion_mode{erhe::physics::Motion_mode::e_dynamic};
};

}
