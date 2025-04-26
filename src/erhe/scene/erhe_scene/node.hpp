#pragma once

#include "erhe_item/hierarchy.hpp"
#include "erhe_scene/trs_transform.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace erhe {
    class Item_host;
}

namespace erhe::scene {

class Node;
class Node_attachment;
class Scene;
class Scene_host;

class Node_transforms
{
public:
    mutable std::uint64_t parent_from_node_serial{0}; // update needed if 0
    mutable std::uint64_t world_from_node_serial {0}; // update needed if 0

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

class Node
    : public erhe::Item<
        Item_base,
        Hierarchy,
        Node,
        erhe::Item_kind::clone_using_custom_clone_constructor
    >
{
public:
    Node();
    explicit Node(const Node&);
    Node& operator=(const Node&);

    explicit Node(const std::string_view name);
    Node(const Node& src, for_clone);
    ~Node() noexcept override;

    [[nodiscard]] auto shared_node_from_this() -> std::shared_ptr<Node>;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Node"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type               () const -> uint64_t                             override;
    auto get_type_name          () const -> std::string_view                     override;
    auto get_item_host          () const -> erhe::Item_host*                     override;
    void handle_flag_bits_update(uint64_t old_flag_bits, uint64_t new_flag_bits) override;

    // Implements / overrides Hierarchy
    using Hierarchy::set_parent;
    void set_parent          (const std::shared_ptr<erhe::Hierarchy>& parent, std::size_t position) override;
    void handle_parent_update(erhe::Hierarchy* old_parent, erhe::Hierarchy* new_parent)             override;

    // Public API
    [[nodiscard]] auto get_parent_node() const -> std::shared_ptr<Node>;
    void set_node_parent(Node* parent);
    void set_node_parent(Node* parent, std::size_t position);

    void attach                  (const std::shared_ptr<Node_attachment>& attachment);
    auto detach                  (Node_attachment* attachment) -> bool;
    auto get_attachment_count    (const erhe::Item_filter& filter) const -> std::size_t;
    void handle_item_host_update (erhe::Item_host* old_scene_host, erhe::Item_host* new_scene_host);
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
    [[nodiscard]] auto look_at                                (const glm::vec3 target_position) const -> glm::mat4;
    [[nodiscard]] auto look_at                                (const Node& target) const -> glm::mat4;
    [[nodiscard]] auto transform_point_from_world_to_local    (const glm::vec3 p) const -> glm::vec3;
    [[nodiscard]] auto transform_direction_from_world_to_local(const glm::vec3 p) const -> glm::vec3;
    [[nodiscard]] auto transform_point_from_local_to_world    (const glm::vec3 p) const -> glm::vec3;
    [[nodiscard]] auto transform_direction_from_local_to_world(const glm::vec3 p) const -> glm::vec3;
    [[nodiscard]] auto get_scene                              () const -> Scene*;

    void node_sanity_check     (bool destruction_in_progress = false) const;
    void update_world_from_node();
    void update_transform      (uint64_t serial);
    void set_parent_from_node  (const glm::mat4 parent_from_node);
    void set_parent_from_node  (const Transform& parent_from_node);
    void set_node_from_parent  (const glm::mat4 node_from_parent);
    void set_node_from_parent  (const Transform& node_from_parent);
    void set_world_from_node   (const glm::mat4 world_from_node);
    void set_world_from_node   (const Transform& world_from_node);
    void set_node_from_world   (const glm::mat4 node_from_world);
    void set_node_from_world   (const Transform& node_from_world);

    Node_data node_data;
};

[[nodiscard]] auto is_node(const erhe::Item_base* item) -> bool;
[[nodiscard]] auto is_node(const std::shared_ptr<erhe::Item_base>& item) -> bool;

} // namespace erhe::scene
