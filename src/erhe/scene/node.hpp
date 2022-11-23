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

class INode_attachment
{
public:
    virtual ~INode_attachment() noexcept;

    static constexpr uint64_t c_flag_bit_none                = 0u;
    static constexpr uint64_t c_flag_bit_is_physics          = (1u << 0);
    static constexpr uint64_t c_flag_bit_is_raytrace         = (1u << 1);
    static constexpr uint64_t c_flag_bit_is_frame_controller = (1u << 2);

    void set_node(Node* node);

    [[nodiscard]] virtual auto node_attachment_type() const -> const char* = 0;
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

    [[nodiscard]] auto get_node () const -> Node*;
    [[nodiscard]] auto flag_bits() const -> uint64_t;
    [[nodiscard]] auto flag_bits() -> uint64_t&;

protected:
    Node*    m_node     {nullptr};
    uint64_t m_flag_bits{c_flag_bit_none};
};

class Node_visibility
{
public:
    static constexpr uint64_t none         = 0u;
    static constexpr uint64_t visible      = (1u << 0);
    static constexpr uint64_t content      = (1u << 1);
    static constexpr uint64_t shadow_cast  = (1u << 2);
    static constexpr uint64_t id           = (1u << 3);
    static constexpr uint64_t tool         = (1u << 4);
    static constexpr uint64_t brush        = (1u << 5);
    static constexpr uint64_t selected     = (1u << 6);
    static constexpr uint64_t rendertarget = (1u << 7);
    static constexpr uint64_t controller   = (1u << 8);
};

class Node_flag_bit
{
public:
    static constexpr uint64_t none                      = 0u;
    static constexpr uint64_t is_transform              = (1u << 0);
    static constexpr uint64_t is_empty                  = (1u << 1);
    static constexpr uint64_t is_physics                = (1u << 2);
    static constexpr uint64_t is_camera                 = (1u << 3);
    static constexpr uint64_t is_light                  = (1u << 4);
    static constexpr uint64_t is_mesh                   = (1u << 5);
    static constexpr uint64_t is_rendertarget           = (1u << 6);
    static constexpr uint64_t no_message                = (1u << 7); // no message bus
    static constexpr uint64_t show_debug_visualizations = (1u << 8);
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
    static uint64_t       s_global_update_serial;
};

class Scene_host;

class Node_data
{
public:
    Node_transforms                                transforms;
    Scene_host*                                    host           {nullptr};
    std::weak_ptr<Node>                            parent         {};
    std::vector<std::shared_ptr<Node>>             children;
    std::vector<std::shared_ptr<INode_attachment>> attachments;
    uint64_t                                       visibility_mask{Node_visibility::none};
    uint64_t                                       flag_bits      {Node_flag_bit::show_debug_visualizations};
    std::size_t                                    depth          {0};
    glm::vec4                                      wireframe_color{0.6f, 0.7f, 0.8f, 0.7f};
    std::string                                    name;
    std::string                                    label;

    static constexpr unsigned int bit_transform      {1u << 0};
    static constexpr unsigned int bit_host           {1u << 1};
    static constexpr unsigned int bit_parent         {1u << 2};
    static constexpr unsigned int bit_children       {1u << 3};
    static constexpr unsigned int bit_attachments    {1u << 4};
    static constexpr unsigned int bit_visibility_mask{1u << 5};
    static constexpr unsigned int bit_flag_bits      {1u << 6};
    static constexpr unsigned int bit_depth          {1u << 7};
    static constexpr unsigned int bit_wireframe_color{1u << 8};
    static constexpr unsigned int bit_name           {1u << 9};
    static constexpr unsigned int bit_label          {1u << 10};

    static auto diff_mask(const Node_data& lhs, const Node_data& rhs) -> unsigned int;
};

class Node
    : public std::enable_shared_from_this<Node>
{
public:
    explicit Node(const std::string_view name);

    virtual ~Node() noexcept;

    virtual void handle_parent_update         (Node* old_parent, Node* new_parent);
    virtual void handle_scene_host_update     (Scene_host* old_scene_host, Scene_host* new_scene_host);
    virtual void handle_transform_update      (uint64_t serial) const;
    virtual void handle_visibility_mask_update();

    void handle_add_child   (const std::shared_ptr<Node>& child_node, std::size_t position = 0);
    void handle_remove_child(Node* child_node);

    [[nodiscard]] virtual auto node_type() const -> const char*;

    [[nodiscard]] auto parent                    () const -> std::weak_ptr<Node>;
    [[nodiscard]] auto depth                     () const -> size_t;
    [[nodiscard]] auto children                  () const -> const std::vector<std::shared_ptr<Node>>&;
    [[nodiscard]] auto mutable_children          () -> std::vector<std::shared_ptr<Node>>&;
    [[nodiscard]] auto attachments               () const -> const std::vector<std::shared_ptr<INode_attachment>>&;
    [[nodiscard]] auto get_visibility_mask       () const -> uint64_t;
                  void set_visibility_mask       (const uint64_t value);
    [[nodiscard]] auto get_flag_bits             () const -> uint64_t;
                  void set_flag_bits             (const uint64_t value);
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
    [[nodiscard]] auto is_selected        () const -> bool;
    [[nodiscard]] auto root               () -> std::weak_ptr<Node>;
    [[nodiscard]] auto name               () const -> const std::string&;
    [[nodiscard]] auto label              () const -> const std::string&;
    [[nodiscard]] auto child_count        () const -> std::size_t;
    [[nodiscard]] auto get_id             () const -> erhe::toolkit::Unique_id<Node>::id_type;
    [[nodiscard]] auto get_index_in_parent() const -> std::size_t;
    [[nodiscard]] auto get_index_of_child (const Node* child) const -> std::optional<std::size_t>;
    [[nodiscard]] auto is_ancestor        (const Node* ancestor_candidate) const -> bool;
    [[nodiscard]] auto get_scene_host     () const -> Scene_host*;
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
    void attach                (const std::shared_ptr<INode_attachment>& attachment);
    auto detach                (INode_attachment* attachment) -> bool;
    void set_name              (const std::string_view name);

    Node_data node_data;

protected:
    erhe::toolkit::Unique_id<Node> m_id;
};

auto is_empty    (const Node* const node) -> bool;
auto is_empty    (const std::shared_ptr<Node>& node) -> bool;
auto is_transform(const Node* const node) -> bool;
auto is_transform(const std::shared_ptr<Node>& node) -> bool;

class Visibility_filter
{
public:
    [[nodiscard]]
    auto operator()(const uint64_t visibility_mask) const -> bool
    {
        if ((visibility_mask & require_all_bits_set) != require_all_bits_set)
        {
            return false;
        }
        if (require_at_least_one_bit_set != 0u)
        {
            if ((visibility_mask & require_at_least_one_bit_set) == 0u)
            {
                return false;
            }
        }
        if ((visibility_mask & require_all_bits_clear) != 0u)
        {
            return false;
        }
        if (require_at_least_one_bit_clear != 0u)
        {
            if ((visibility_mask & require_at_least_one_bit_clear) == require_at_least_one_bit_clear)
            {
                return false;
            }
        }
        return true;
    }

    uint64_t require_all_bits_set          {0};
    uint64_t require_at_least_one_bit_set  {0};
    uint64_t require_all_bits_clear        {0};
    uint64_t require_at_least_one_bit_clear{0};
};

} // namespace erhe::scene
