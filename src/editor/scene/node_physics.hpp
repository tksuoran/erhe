#pragma once

#include "erhe_scene/node_attachment.hpp"
#include "erhe_physics/irigid_body.hpp"

#include <vector>

namespace erhe          { class Item_host; }
namespace erhe::physics { class IWorld; }

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

    std::vector<glm::vec3> markers;

private:
    erhe::physics::IWorld*                      m_physics_world{nullptr};
    erhe::physics::IRigid_body_create_info      m_create_info;
    std::shared_ptr<erhe::physics::IRigid_body> m_rigid_body;
    erhe::physics::Motion_mode                  m_motion_mode{erhe::physics::Motion_mode::e_dynamic};
};

}
