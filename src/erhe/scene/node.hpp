#pragma once

#include "erhe/scene/item.hpp"
#include "erhe/scene/trs_transform.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace erhe::scene
{

class Node;
class Scene;
class Item_host;

class Node_attachment
    : public Item
{
public:
    Node_attachment();
    explicit Node_attachment(const std::string_view name);
    virtual ~Node_attachment() noexcept;

    void set_node(Node* node, std::size_t position = 0);

    virtual void handle_node_update           (Node* old_node, Node* new_node);
    virtual void handle_node_flag_bits_update (uint64_t old_node_flag_bits, uint64_t new_node_flag_bits);
    virtual void handle_node_transform_update () {};
    virtual void handle_item_host_update(
        Item_host* const old_item_host,
        Item_host* const new_item_host
    )
    {
        static_cast<void>(old_item_host);
        static_cast<void>(new_item_host);
    }

    [[nodiscard]] auto get_node     () -> Node*;
    [[nodiscard]] auto get_node     () const -> const Node*;
    [[nodiscard]] auto get_item_host() const -> Item_host* override;

protected:
    Node* m_node{nullptr};
};

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

class Item_host;

class Node_data
{
public:
    Node_transforms                               transforms;
    Item_host*                                    host     {nullptr};
    std::vector<std::shared_ptr<Node_attachment>> attachments;

    static constexpr unsigned int bit_transform  {1u << 0};
    static constexpr unsigned int bit_attachments{1u << 1};

    static auto diff_mask(const Node_data& lhs, const Node_data& rhs) -> unsigned int;
};

class Node
    : public Item
{
public:
    Node();
    explicit Node(const std::string_view name);
    ~Node() noexcept override;

    // Implements Item
    [[nodiscard]] static auto get_static_type     () -> uint64_t;
    [[nodiscard]] static auto get_static_type_name() -> const char*;
    auto get_type               () const -> uint64_t                             override;
    auto get_type_name          () const -> const char*                          override;
    void handle_flag_bits_update(uint64_t old_flag_bits, uint64_t new_flag_bits) override;

    // Overrides Item
    void set_parent          (const std::shared_ptr<Item>& parent, std::size_t position = 0) override;
    void handle_parent_update(Item* old_parent, Item* new_parent)                            override;

    // Public API
    [[nodiscard]] auto get_parent_node() const -> std::shared_ptr<Node>;
    void set_parent(Node* parent, std::size_t position = 0);

    void attach                  (const std::shared_ptr<Node_attachment>& attachment);
    auto detach                  (Node_attachment* attachment) -> bool;
    auto get_attachment_count    (const Item_filter& filter) const -> std::size_t;
    void handle_scene_host_update(Item_host* old_scene_host, Item_host* new_scene_host);
    void handle_transform_update (uint64_t serial) const;
    void handle_add_attachment   (const std::shared_ptr<Node_attachment>& attachment, std::size_t position = 0);
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
    [[nodiscard]] auto look_at                                (const Node& target) const -> glm::mat4;
    [[nodiscard]] auto transform_point_from_world_to_local    (const glm::vec3 p) const -> glm::vec3;
    [[nodiscard]] auto transform_direction_from_world_to_local(const glm::vec3 p) const -> glm::vec3;
    [[nodiscard]] auto transform_point_from_local_to_world    (const glm::vec3 p) const -> glm::vec3;
    [[nodiscard]] auto transform_direction_from_local_to_world(const glm::vec3 p) const -> glm::vec3;

    [[nodiscard]] auto get_item_host                          () const -> Item_host* override;
    [[nodiscard]] auto get_scene                              () const -> Scene*;

    void node_sanity_check     () const;
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

[[nodiscard]] auto is_node(const Item* scene_item) -> bool;
[[nodiscard]] auto is_node(const std::shared_ptr<Item>& scene_item) -> bool;
[[nodiscard]] auto as_node(Item* scene_item) -> Node*;
[[nodiscard]] auto as_node(const std::shared_ptr<Item>& scene_item) -> std::shared_ptr<Node>;

[[nodiscard]] auto as_node_attachment(
    const std::shared_ptr<Item>& scene_item
) -> std::shared_ptr<Node_attachment>;

} // namespace erhe::scene
