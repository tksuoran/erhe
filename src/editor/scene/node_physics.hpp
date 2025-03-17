#pragma once

#include "erhe_scene/node_attachment.hpp"
#include "erhe_physics/irigid_body.hpp"

#include <functional>

namespace erhe {
    class Item_host;
}
namespace erhe::physics {
    class IWorld;
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
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t                    override;
    auto get_type_name() const -> std::string_view            override;

    // Implements / overrides Node_attachment
    auto clone_attachment       () const -> std::shared_ptr<Node_attachment>                     override;
    void handle_item_host_update(erhe::Item_host* old_item_host, erhe::Item_host* new_item_host) override;

    // Public API
    [[nodiscard]] auto get_rigid_body()       ->       erhe::physics::IRigid_body*;
    [[nodiscard]] auto get_rigid_body() const -> const erhe::physics::IRigid_body*;

    void before_physics_simulation();
    void after_physics_simulation();

    void set_physics_world(erhe::physics::IWorld* value);
    [[nodiscard]] auto get_physics_world() const -> erhe::physics::IWorld*;

    std::vector<glm::vec3> markers;

    erhe::physics::Motion_mode physics_motion_mode{erhe::physics::Motion_mode::e_dynamic};

private:
    erhe::physics::IWorld*                      m_physics_world{nullptr};
    erhe::physics::IRigid_body_create_info      m_create_info;
    std::shared_ptr<erhe::physics::IRigid_body> m_rigid_body;
};

auto is_physics(const erhe::Item_base* item) -> bool;
auto is_physics(const std::shared_ptr<erhe::Item_base>& item) -> bool;

auto get_node_physics(const erhe::scene::Node* node) -> std::shared_ptr<Node_physics>;

} // namespace editor
