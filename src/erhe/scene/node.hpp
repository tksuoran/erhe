#pragma once

#include "erhe/scene/transform.hpp"
#include "erhe/toolkit/unique_id.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace erhe::scene
{

class Node;

class Node_physics;
class Light;
class Camera;
class Mesh;

class Node
    : public std::enable_shared_from_this<Node>
{
public:
    explicit Node(const std::string_view name);

    virtual ~Node();

    static constexpr uint64_t c_visibility_none        = 0;
    static constexpr uint64_t c_visibility_content     = (1 << 0);
    static constexpr uint64_t c_visibility_shadow_cast = (1 << 1);
    static constexpr uint64_t c_visibility_id          = (1 << 2);
    static constexpr uint64_t c_visibility_tool        = (1 << 3);
    static constexpr uint64_t c_visibility_brush       = (1 << 4);
    static constexpr uint64_t c_visibility_selected    = (1 << 5);

    static constexpr uint64_t c_flag_bit_none         = 0;
    static constexpr uint64_t c_flag_bit_is_transform = (1 << 0);
    static constexpr uint64_t c_flag_bit_is_empty     = (1 << 1);
    static constexpr uint64_t c_flag_bit_is_physics   = (1 << 2);
    static constexpr uint64_t c_flag_bit_is_light     = (1 << 3);
    static constexpr uint64_t c_flag_bit_is_camera    = (1 << 4);
    static constexpr uint64_t c_flag_bit_is_mesh      = (1 << 5);

    virtual void on_attached_to      (Node& node);
    virtual void on_detached_from    (Node& node);
    virtual void on_transform_changed() {}
    virtual auto node_type           () const -> const char*;

    void set_depth_recursive       (size_t depth);
    void update_transform          (const uint64_t serial = 0);
    void update_transform_recursive(const uint64_t serial = 0);

    void sanity_check              () const;
    void sanity_check_root_path    (const Node* node) const;
    auto parent                    () const -> Node*;
    auto depth                     () const -> size_t;
    auto children                  () const -> const std::vector<std::shared_ptr<Node>>&;
    auto visibility_mask           () const -> uint64_t;
    auto visibility_mask           () -> uint64_t&;
    auto flag_bits                 () const -> uint64_t;
    auto flag_bits                 () -> uint64_t&;
    auto parent_from_node_transform() const -> const Transform&;
    auto node_from_parent_transform() const -> const Transform;
    auto parent_from_node          () const -> glm::mat4;
    auto world_from_node_transform () const -> const Transform&;
    auto node_from_world_transform () const -> const Transform;
    auto world_from_node           () const -> glm::mat4;
    auto node_from_parent          () const -> glm::mat4;
    auto node_from_world           () const -> glm::mat4;
    auto world_from_parent         () const -> glm::mat4;
    auto position_in_world         () const -> glm::vec4;
    auto direction_in_world        () const -> glm::vec4;

    void set_parent_from_node      (const glm::mat4 matrix);
    void set_parent_from_node      (const Transform& transform);
    
    void set_node_from_parent      (const glm::mat4 matrix);
    void set_node_from_parent      (const Transform& transform);

    void set_world_from_node       (const glm::mat4 matrix);
    void set_world_from_node       (const Transform& transform);

    auto is_selected() const -> bool;
    void attach     (const std::shared_ptr<Node>& node);
    auto detach     (Node* node) -> bool;
    void unparent   ();
    auto root       () -> Node*;
    auto root       () const -> const Node*;
    auto name       () const -> const std::string&;
    auto label      () const -> const std::string&;
    void set_name   (const std::string_view name);
    auto child_count() const -> size_t;

protected:
    class Transforms
    {
    public:
        Transform parent_from_node; // normative
        Transform world_from_node;  // calculated by update_transform()
    };

    Transforms                         m_transforms;
    std::uint64_t                      m_last_transform_update_serial{0};
    Node*                              m_parent         {nullptr};
    std::vector<std::shared_ptr<Node>> m_children;
    uint64_t                           m_visibility_mask{c_visibility_none};
    uint64_t                           m_flag_bits      {c_flag_bit_none};
    size_t                             m_depth          {0};
    erhe::toolkit::Unique_id<Node>     m_id;
    std::string                        m_name;
    std::string                        m_label;
};

auto is_empty    (const Node* const node) -> bool;
auto is_empty    (const std::shared_ptr<Node>& node) -> bool;
auto is_transform(const Node* const node) -> bool;
auto is_transform(const std::shared_ptr<Node>& node) -> bool;

class Visibility_filter
{
public:
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
