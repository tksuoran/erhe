#pragma once

#include "erhe/scene/item.hpp"
#include "erhe/scene/transform.hpp"
#include "erhe/toolkit/unique_id.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace erhe::scene
{

class Node;
class Scene;
class Scene_host;

class Node_attachment
    : public Item
{
public:
    Node_attachment();
    explicit Node_attachment(const std::string_view name);
    virtual ~Node_attachment() noexcept;

    void set_node(Node* node, std::size_t position = 0);

    virtual void handle_node_update           (Node* old_node, Node* new_node);
    virtual void handle_node_flag_bits_update (const uint64_t old_node_flag_bits, const uint64_t new_node_flag_bits);
    virtual void handle_node_transform_update () {};
    virtual void handle_node_scene_host_update(
        Scene_host* old_scene_host,
        Scene_host* new_scene_host
    )
    {
        static_cast<void>(old_scene_host);
        static_cast<void>(new_scene_host);
    }

    [[nodiscard]] auto get_node      () -> Node*;
    [[nodiscard]] auto get_node      () const -> const Node*;
    [[nodiscard]] auto get_item_host() const -> Scene_host* override;

protected:
    Node* m_node{nullptr};
};

class Node_transforms
{
public:
    mutable std::uint64_t update_serial{0}; // update needed if 0
    Transform             parent_from_node; // normative
    mutable Transform     world_from_node;  // calculated by update_transform()

    static auto get_current_serial() -> uint64_t;
    static auto get_next_serial   () -> uint64_t;

private:
    static uint64_t s_global_update_serial;
};

class Scene_host;

class Node_data
{
public:
    Node_transforms                               transforms;
    Scene_host*                                   host     {nullptr};
    std::weak_ptr<Node>                           parent   {};
    std::vector<std::shared_ptr<Node>>            children;
    std::vector<std::shared_ptr<Node_attachment>> attachments;
    std::size_t                                   depth {0};

    static constexpr unsigned int bit_transform   {1u << 0};
    static constexpr unsigned int bit_host        {1u << 1};
    static constexpr unsigned int bit_parent      {1u << 2};
    static constexpr unsigned int bit_children    {1u << 3};
    static constexpr unsigned int bit_attachments {1u << 4};
    static constexpr unsigned int bit_flag_bits   {1u << 5};
    static constexpr unsigned int bit_depth       {1u << 6};
    static constexpr unsigned int bit_name        {1u << 7};
    static constexpr unsigned int bit_label       {1u << 8};

    static auto diff_mask(
        const Node_data& lhs,
        const Node_data& rhs
    ) -> unsigned int;
};

class Node
    : public Item
{
public:
    Node();
    explicit Node(const std::string_view name);
    ~Node() noexcept override;

    void remove();

    // Implements Item
    auto get_type () const -> uint64_t    override;
    auto type_name() const -> const char* override;
    void handle_flag_bits_update(uint64_t old_flag_bits, uint64_t new_flag_bits) override;

    // Public API
    void handle_parent_update    (Node* old_parent, Node* new_parent);
    void handle_scene_host_update(Scene_host* old_scene_host, Scene_host* new_scene_host);
    void handle_transform_update (uint64_t serial) const;
    void handle_add_child        (const std::shared_ptr<Node>& child_node, std::size_t position = 0);
    void handle_add_attachment   (const std::shared_ptr<Node_attachment>& attachment, std::size_t position = 0);
    void handle_remove_child     (Node* child_node);
    void handle_remove_attachment(Node_attachment* attachment);

    [[nodiscard]] auto parent                                 () const -> std::weak_ptr<Node>;
    [[nodiscard]] auto get_depth                              () const -> size_t;
    [[nodiscard]] auto children                               () const -> const std::vector<std::shared_ptr<Node>>&;
    [[nodiscard]] auto mutable_children                       () -> std::vector<std::shared_ptr<Node>>&;
    [[nodiscard]] auto attachments                            () const -> const std::vector<std::shared_ptr<Node_attachment>>&;
    [[nodiscard]] auto parent_from_node_transform             () const -> const Transform&;
    [[nodiscard]] auto node_from_parent_transform             () const -> const Transform;
    [[nodiscard]] auto parent_from_node                       () const -> glm::mat4;
    [[nodiscard]] auto world_from_node_transform              () const -> const Transform&;
    [[nodiscard]] auto node_from_world_transform              () const -> const Transform;
    [[nodiscard]] auto world_from_node                        () const -> glm::mat4;
    [[nodiscard]] auto node_from_parent                       () const -> glm::mat4;
    [[nodiscard]] auto node_from_world                        () const -> glm::mat4;
    [[nodiscard]] auto world_from_parent                      () const -> glm::mat4;
    [[nodiscard]] auto position_in_world                      () const -> glm::vec4;
    [[nodiscard]] auto direction_in_world                     () const -> glm::vec4;
    [[nodiscard]] auto look_at                                (const Node& target) const -> glm::mat4;
    [[nodiscard]] auto transform_point_from_world_to_local    (const glm::vec3 p) const -> glm::vec3;
    [[nodiscard]] auto transform_direction_from_world_to_local(const glm::vec3 p) const -> glm::vec3;
    [[nodiscard]] auto root                                   () -> std::weak_ptr<Node>;
    [[nodiscard]] auto child_count                            () const -> std::size_t;
    [[nodiscard]] auto child_count                            (const Item_filter& filter) const -> std::size_t;
    [[nodiscard]] auto attachment_count                       (const Item_filter& filter) const -> std::size_t;
    [[nodiscard]] auto get_index_in_parent                    () const -> std::size_t;
    [[nodiscard]] auto get_index_of_child                     (const Node* child) const -> std::optional<std::size_t>;
    [[nodiscard]] auto is_ancestor                            (const Node* ancestor_candidate) const -> bool;
    [[nodiscard]] auto get_item_host                          () const -> Scene_host* override;
    [[nodiscard]] auto get_scene                              () const -> Scene*;

    void set_parent            (Node* parent, std::size_t position = 0);
    void set_parent            (const std::shared_ptr<Node>& parent, std::size_t position = 0);
    void set_depth_recursive   (std::size_t depth);
    void update_world_from_node();
    void update_transform      (uint64_t serial) const;
    void sanity_check          () const;
    void sanity_check_root_path(const Node* node) const;
    void set_parent_from_node  (const glm::mat4 parent_from_node);
    void set_parent_from_node  (const Transform& parent_from_node);
    void set_node_from_parent  (const glm::mat4 node_from_parent);
    void set_node_from_parent  (const Transform& node_from_parent);
    void set_world_from_node   (const glm::mat4 world_from_node);
    void set_world_from_node   (const Transform& world_from_node);
    void set_node_from_world   (const glm::mat4 node_from_world);
    void set_node_from_world   (const Transform& node_from_world);
    void attach                (const std::shared_ptr<Node_attachment>& attachment);
    auto detach                (Node_attachment* attachment) -> bool;

    void recursive_remove();

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
