#pragma once

#include "erhe/scene/transform.hpp"
#include "erhe/toolkit/unique_id.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace erhe::scene
{

class Node;
class Scene;
class Scene_host;

class Scene_item_flags
{
public:
    static constexpr uint64_t none                      = 0u;
    static constexpr uint64_t no_message                = (1u <<  1);
    static constexpr uint64_t show_debug_visualizations = (1u <<  2);
    static constexpr uint64_t shadow_cast               = (1u <<  3);
    static constexpr uint64_t selected                  = (1u <<  4);
    static constexpr uint64_t visible                   = (1u <<  5);
    static constexpr uint64_t render_wireframe          = (1u <<  6); // TODO
    static constexpr uint64_t render_bounding_box       = (1u <<  7); // TODO

    static constexpr uint64_t node                      = (1u <<  8);
    static constexpr uint64_t attachment                = (1u <<  9);
    static constexpr uint64_t physics                   = (1u << 10);

    static constexpr uint64_t raytrace                  = (1u << 11);
    static constexpr uint64_t frame_controller          = (1u << 12);
    static constexpr uint64_t grid                      = (1u << 13);
    static constexpr uint64_t light                     = (1u << 14);
    static constexpr uint64_t camera                    = (1u << 15);
    static constexpr uint64_t mesh                      = (1u << 16);
    static constexpr uint64_t rendertarget              = (1u << 17);
    static constexpr uint64_t controller                = (1u << 18);

    static constexpr uint64_t content                   = (1u << 19);
    static constexpr uint64_t id                        = (1u << 20);
    static constexpr uint64_t tool                      = (1u << 21);
    static constexpr uint64_t brush                     = (1u << 22);

    [[nodiscard]] static auto to_string(uint64_t mask) -> std::string;
};

class Scene_item_filter
{
public:
    [[nodiscard]] auto operator()(uint64_t filter_bits) const -> bool;

    uint64_t require_all_bits_set          {0};
    uint64_t require_at_least_one_bit_set  {0};
    uint64_t require_all_bits_clear        {0};
    uint64_t require_at_least_one_bit_clear{0};
};

class Scene_item
    : public std::enable_shared_from_this<Scene_item>
{
public:
    Scene_item();
    explicit Scene_item(const std::string_view name);
    virtual ~Scene_item() noexcept;

    [[nodiscard]] auto name                   () const -> const std::string&;
                  void set_name               (const std::string_view name);
    [[nodiscard]] auto label                  () const -> const std::string&;
    [[nodiscard]] auto get_flag_bits          () const -> uint64_t;
    [[nodiscard]] auto flag_bits              () -> uint64_t&;
                  void set_flag_bits          (const uint64_t mask, bool value);
                  void enable_flag_bits       (const uint64_t mask);
                  void disable_flag_bits      (const uint64_t mask);
    [[nodiscard]] auto get_id                 () const -> erhe::toolkit::Unique_id<Scene_item>::id_type;
    [[nodiscard]] auto is_selected            () const -> bool;
                  void set_selected           (bool value);
                  void set_visible            (bool value);
                  void show                   ();
                  void hide                   ();
    [[nodiscard]] auto is_visible             () const -> bool;
    [[nodiscard]] auto is_hidden              () const -> bool;
    [[nodiscard]] auto describe               () -> std::string;
    [[nodiscard]] virtual auto get_scene_host() const -> Scene_host*;
    [[nodiscard]] virtual auto type_name     () const -> const char*;

                  void set_wireframe_color(const glm::vec4& color);
    [[nodiscard]] auto get_wireframe_color() const -> glm::vec4;

protected:
    uint64_t                             m_flag_bits      {Scene_item_flags::none};
    glm::vec4                            m_wireframe_color{0.6f, 0.7f, 0.8f, 0.7f};
    erhe::toolkit::Unique_id<Scene_item> m_id;
    std::string                          m_name;
    std::string                          m_label;
};

class Node_attachment
    : public Scene_item
{
public:
    Node_attachment();
    explicit Node_attachment(const std::string_view name);
    virtual ~Node_attachment() noexcept;

    void set_node(Node* node);

    virtual void handle_node_transform_update      () {};
    virtual void handle_node_visibility_mask_update(const uint64_t mask) { static_cast<void>(mask); };
    virtual void handle_node_scene_host_update(
        Scene_host* old_scene_host,
        Scene_host* new_scene_host
    )
    {
        static_cast<void>(old_scene_host);
        static_cast<void>(new_scene_host);
    }

    [[nodiscard]] auto get_node           () -> Node*;
    [[nodiscard]] auto get_node           () const -> const Node*;
    [[nodiscard]] auto get_scene_host     () const -> Scene_host* override;

protected:
    Node*     m_node           {nullptr};
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
    std::string                                   name;

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
    : public Scene_item
{
public:
    Node();
    explicit Node(const std::string_view name);
    ~Node() noexcept override;

    // Implements Scene_item
    auto type_name() const -> const char* override;

    // Public API
    void handle_parent_update    (Node* old_parent, Node* new_parent);
    void handle_scene_host_update(Scene_host* old_scene_host, Scene_host* new_scene_host);
    void handle_transform_update (uint64_t serial) const;
    void handle_add_child        (const std::shared_ptr<Node>& child_node, std::size_t position = 0);
    void handle_remove_child     (Node* child_node);

    [[nodiscard]] auto parent                    () const -> std::weak_ptr<Node>;
    [[nodiscard]] auto depth                     () const -> size_t;
    [[nodiscard]] auto children                  () const -> const std::vector<std::shared_ptr<Node>>&;
    [[nodiscard]] auto mutable_children          () -> std::vector<std::shared_ptr<Node>>&;
    [[nodiscard]] auto attachments               () const -> const std::vector<std::shared_ptr<Node_attachment>>&;
    [[nodiscard]] auto parent_from_node_transform() const -> const Transform&;
    [[nodiscard]] auto node_from_parent_transform() const -> const Transform;
    [[nodiscard]] auto parent_from_node          () const -> glm::mat4;
    [[nodiscard]] auto world_from_node_transform () const -> const Transform&;
    [[nodiscard]] auto node_from_world_transform () const -> const Transform;
    [[nodiscard]] auto world_from_node           () const -> glm::mat4;
    [[nodiscard]] auto node_from_parent          () const -> glm::mat4;
    [[nodiscard]] auto node_from_world           () const -> glm::mat4;
    [[nodiscard]] auto world_from_parent         () const -> glm::mat4;
    [[nodiscard]] auto position_in_world         () const -> glm::vec4;
    [[nodiscard]] auto direction_in_world        () const -> glm::vec4;
    [[nodiscard]] auto look_at                   (const Node& target) const -> glm::mat4;
    [[nodiscard]] auto transform_point_from_world_to_local    (const glm::vec3 p) const -> glm::vec3;
    [[nodiscard]] auto transform_direction_from_world_to_local(const glm::vec3 p) const -> glm::vec3;
    [[nodiscard]] auto root               () -> std::weak_ptr<Node>;
    [[nodiscard]] auto child_count        () const -> std::size_t;
    [[nodiscard]] auto child_count        (const Scene_item_filter& filter) const -> std::size_t;
    [[nodiscard]] auto attachment_count   (const Scene_item_filter& filter) const -> std::size_t;
    [[nodiscard]] auto get_index_in_parent() const -> std::size_t;
    [[nodiscard]] auto get_index_of_child (const Node* child) const -> std::optional<std::size_t>;
    [[nodiscard]] auto is_ancestor        (const Node* ancestor_candidate) const -> bool;
    [[nodiscard]] auto get_scene_host     () const -> Scene_host* override;
    [[nodiscard]] auto get_scene          () const -> Scene*;

    void set_parent            (Node* parent, std::size_t position = 0);
    void set_parent            (const std::shared_ptr<Node>& parent, std::size_t position = 0);
    void set_depth_recursive   (std::size_t depth);
    void update_world_from_node();
    void update_transform      (uint64_t serial) const;
    void sanity_check          () const;
    void sanity_check_root_path(const Node* node) const;
    void set_parent_from_node  (const glm::mat4 matrix);
    void set_parent_from_node  (const Transform& transform);
    void set_node_from_parent  (const glm::mat4 matrix);
    void set_node_from_parent  (const Transform& transform);
    void set_world_from_node   (const glm::mat4 matrix);
    void set_world_from_node   (const Transform& transform);
    void attach                (const std::shared_ptr<Node_attachment>& attachment);
    auto detach                (Node_attachment* attachment) -> bool;

    Node_data node_data;
};

[[nodiscard]] auto is_node(const Scene_item* const scene_item) -> bool;
[[nodiscard]] auto is_node(const std::shared_ptr<Scene_item>& scene_item) -> bool;
[[nodiscard]] auto as_node(Scene_item* const scene_item) -> Node*;
[[nodiscard]] auto as_node(const std::shared_ptr<Scene_item>& scene_item) -> std::shared_ptr<Node>;

[[nodiscard]] auto as_node_attachment(
    const std::shared_ptr<Scene_item>& scene_item
) -> std::shared_ptr<Node_attachment>;

} // namespace erhe::scene
