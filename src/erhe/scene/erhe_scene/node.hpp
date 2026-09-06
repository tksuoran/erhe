#pragma once

#include "erhe_scene/imageable.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_property/dependency_property.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace erhe { class Item_host; }

namespace erhe::scene {

class Xformable;
class Node_attachment;
class Scene;
class Scene_host;

class Node_transforms
{
public:
    mutable std::uint64_t parent_from_node_serial{0}; // update needed if 0
    mutable std::uint64_t world_from_node_serial {0}; // update needed if 0

    // Set when the node is in its Scene's transform-dirty list (cleared by
    // Scene::update_node_transforms), so repeated transform writes within one
    // frame enqueue the node only once.
    mutable bool          scene_transform_dirty  {false};

    // True while scene_transform_dirty is set IF every write that dirtied the
    // node came from the owner of no_transform_update nodes' transforms (the
    // physics writeback; see Scene::set_transform_owner_writes). Propagation
    // from such a node keeps skipping no_transform_update children; any other
    // writer's dirt CARRIES them (ancestor edits move body-driven subtrees).
    mutable bool          scene_transform_dirty_by_owner{false};

    // One of these is normative, and the other is calculated by update_transform()
    Trs_transform         parent_from_node;
    mutable Trs_transform world_from_node;  

    static auto get_current_serial() -> uint64_t;
    static auto get_next_serial   () -> uint64_t;

private:
    static uint64_t s_global_update_serial;
};

class Node_data
{
public:
    Node_data();
    Node_data(const Node_data& src, for_clone);

    Node_transforms                               transforms;
    Scene_host*                                   host     {nullptr};
    std::vector<std::shared_ptr<Node_attachment>> attachments;

    static constexpr unsigned int bit_transform  {1u << 0};
    static constexpr unsigned int bit_attachments{1u << 1};

    static auto diff_mask(const Node_data& lhs, const Node_data& rhs) -> unsigned int;
};

// A transformable prim (doc/usd-compatibility-plan.md C5, USD
// `UsdGeomXformable`): the level of the prim class hierarchy that carries a
// transform. Every class below it transforms its children; a prim outside it
// has no transform of its own, so a transform composes through it.
//
// The level is never instantiated on its own; `Xform` is the transform-only
// prim every node-creation path makes. `Node` is the name most of erhe still
// spells `Xformable` with.
class Xformable
    : public erhe::Item<
        Item_base,
        Imageable,
        Xformable,
        erhe::Item_kind::clone_using_custom_clone_constructor
    >
{
public:
    Xformable();
    explicit Xformable(const Xformable&);
    Xformable& operator=(const Xformable&);

    explicit Xformable(std::string_view name);
    Xformable(const Xformable& src, for_clone);
    ~Xformable() noexcept override;

    [[nodiscard]] auto shared_node_from_this() -> std::shared_ptr<Xformable>;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Xformable"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t
    {
        return Imageable::get_static_type() | erhe::Item_type::xformable;
    }
    auto get_item_host          () const -> erhe::Item_host*                     override;
    void handle_flag_bits_update(uint64_t old_flag_bits, uint64_t new_flag_bits) override;

    // Implements / overrides Hierarchy
    using Hierarchy::set_parent;
    void set_parent          (const std::shared_ptr<erhe::Hierarchy>& parent, std::size_t position) override;
    void handle_parent_update(erhe::Hierarchy* old_parent, erhe::Hierarchy* new_parent)             override;

    // Inherited properties reach child nodes and then attachments (D23).
    void for_each_inheritance_child(const std::function<void(erhe::property::Dependency_object&)>& callback) override;
    // A node holds the properties of every attachment class (Light, Camera,
    // Mesh, ...) for the attachments below it to inherit (D30): its
    // secondary owner type is Node_attachment, whose descendants they are.
    [[nodiscard]] auto get_secondary_property_owner_type() const -> std::optional<erhe::property::Owner_type> override;

    // Public API
    [[nodiscard]] auto get_parent_node() const -> std::shared_ptr<Xformable>;
    void set_node_parent(Xformable* parent);
    void set_node_parent(Xformable* parent, std::size_t position);

    void attach                  (const std::shared_ptr<Node_attachment>& attachment);
    auto detach                  (Node_attachment* attachment) -> bool;
    auto get_attachment_count    (const erhe::Item_filter& filter) const -> std::size_t;
    // Overrides Typed: registers / unregisters the node with the scene host
    // and carries the host to the attachments and to the prim subtree.
    void handle_item_host_update (erhe::Item_host* old_scene_host, erhe::Item_host* new_scene_host) override;
    void handle_transform_update (uint64_t serial) const;
    void handle_add_attachment   (const std::shared_ptr<Node_attachment>& attachment, std::size_t position = std::numeric_limits<std::size_t>::max());
    void handle_remove_attachment(Node_attachment* attachment);

    [[nodiscard]] auto get_attachments                        () const -> const std::vector<std::shared_ptr<Node_attachment>>&;
    [[nodiscard]] auto parent_from_node_transform             () const -> const Trs_transform&;
    [[nodiscard]] auto parent_from_node_transform             () -> Trs_transform&;
    [[nodiscard]] auto parent_from_node                       () const -> glm::mat4;
    [[nodiscard]] auto world_from_node_transform              () const -> const Trs_transform&;
    [[nodiscard]] auto world_from_node                        () const -> glm::mat4;
    [[nodiscard]] auto node_from_parent                       () const -> glm::mat4;
    [[nodiscard]] auto node_from_world                        () const -> glm::mat4;
    [[nodiscard]] auto world_from_parent                      () const -> glm::mat4;
    [[nodiscard]] auto parent_from_world                      () const -> glm::mat4;
    [[nodiscard]] auto position_in_world                      () const -> glm::vec4;
    [[nodiscard]] auto direction_in_world                     () const -> glm::vec4;
    [[nodiscard]] auto look_at                                (glm::vec3 target_position) const -> glm::mat4;
    [[nodiscard]] auto look_at                                (const Xformable& target) const -> glm::mat4;
    [[nodiscard]] auto transform_point_from_world_to_local    (glm::vec3 p) const -> glm::vec3;
    [[nodiscard]] auto transform_direction_from_world_to_local(glm::vec3 p) const -> glm::vec3;
    [[nodiscard]] auto transform_point_from_local_to_world    (glm::vec3 p) const -> glm::vec3;
    [[nodiscard]] auto transform_direction_from_local_to_world(glm::vec3 p) const -> glm::vec3;
    [[nodiscard]] auto get_scene                              () const -> Scene*;

    void node_sanity_check     (bool destruction_in_progress = false) const;
    void update_world_from_node();
    void update_transform      (uint64_t serial);
    void set_parent_from_node  (glm::mat4 parent_from_node);
    void set_parent_from_node  (const Transform& parent_from_node);
    void set_parent_from_node  (const Trs_transform& parent_from_node);
    void set_node_from_parent  (glm::mat4 node_from_parent);
    void set_node_from_parent  (const Transform& node_from_parent);
    void set_world_from_node   (glm::mat4 world_from_node);
    void set_world_from_node   (const Transform& world_from_node);
    void set_world_from_node   (const Trs_transform& world_from_node);
    void set_node_from_world   (glm::mat4 node_from_world);
    void set_node_from_world   (const Transform& node_from_world);

    // Registered properties (doc/property-system.md section 4.2, D18):
    // bridged onto node_data.transforms.parent_from_node, so the transform
    // keeps its deferred matrix decomposition and its per-frame write paths,
    // and the editor / undo / MCP reach it through the property store. Writes
    // through them run the same update as set_parent_from_node.
    static const erhe::property::Property<glm::vec3> translation_property;
    static const erhe::property::Property<glm::quat> rotation_property;
    static const erhe::property::Property<glm::vec3> scale_property;
    // Computed (doc/property-system.md D26): the components of
    // world_from_node_transform(), pushed to expressions from
    // handle_transform_update (which the propagation pass runs on every
    // descendant whose world transform it recomputes).
    static const erhe::property::Property<glm::vec3> world_translation_property;
    static const erhe::property::Property<glm::quat> world_rotation_property;
    static const erhe::property::Property<glm::vec3> world_scale_property;

    // Optional developer sanity check: when enabled, every transform write to
    // a node carrying Item_flags::no_transform_update logs a warning with the
    // node name. Off by default; nodes owned by awake physics bodies carry the
    // flag and are written legitimately every simulation step, so expect those
    // to be reported too while the check is on.
    static bool s_check_no_transform_update_writes;

    Node_data node_data;
};

// The name most of erhe spells `Xformable` with. It is retired when the
// prim class hierarchy (doc/usd-compatibility-plan.md C5) is complete.
using Node = Xformable;

template <typename T>
auto get_attachment(const Xformable* node) -> std::shared_ptr<T>
{
    if (node == nullptr) {
        return {};
    }
    for (const auto& attachment : node->get_attachments()) {
        auto result = std::dynamic_pointer_cast<T>(attachment);
        if (result) {
            return result;
        }
    }
    return {};
}

} // namespace erhe::scene

