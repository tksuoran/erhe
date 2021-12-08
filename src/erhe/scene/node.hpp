#pragma once

#include "erhe/scene/transform.hpp"
#include "erhe/toolkit/unique_id.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace erhe::scene
{

class Node;

class INode_attachment
{
public:
    virtual ~INode_attachment();

    static constexpr uint64_t c_flag_bit_none        = 0u;
    static constexpr uint64_t c_flag_bit_is_physics  = (1u << 0);
    static constexpr uint64_t c_flag_bit_is_raytrace = (1u << 1);

    virtual [[nodiscard]] auto node_attachment_type() const -> const char* = 0;
    virtual void on_attached_to           (Node* node) { m_node = node; };
    virtual void on_detached_from         (Node* node) { static_cast<void>(node); m_node = nullptr; };
    virtual void on_node_transform_changed() {};

    [[nodiscard]] auto node     () const -> Node*;
    [[nodiscard]] auto flag_bits() const -> uint64_t;
    [[nodiscard]] auto flag_bits() -> uint64_t&;

protected:
    Node*    m_node     {nullptr};
    uint64_t m_flag_bits{c_flag_bit_none};
};

class Node
    : public std::enable_shared_from_this<Node>
{
public:
    explicit Node(const std::string_view name);

    virtual ~Node();

    static constexpr uint64_t c_visibility_none        = 0u;
    static constexpr uint64_t c_visibility_content     = (1u << 0);
    static constexpr uint64_t c_visibility_shadow_cast = (1u << 1);
    static constexpr uint64_t c_visibility_id          = (1u << 2);
    static constexpr uint64_t c_visibility_tool        = (1u << 3);
    static constexpr uint64_t c_visibility_brush       = (1u << 4);
    static constexpr uint64_t c_visibility_selected    = (1u << 5);

    static constexpr uint64_t c_flag_bit_none         = 0u;
    static constexpr uint64_t c_flag_bit_is_transform = (1u << 0);
    static constexpr uint64_t c_flag_bit_is_empty     = (1u << 1);
    static constexpr uint64_t c_flag_bit_is_physics   = (1u << 2);
    static constexpr uint64_t c_flag_bit_is_icamera   = (1u << 4);
    static constexpr uint64_t c_flag_bit_is_camera    = (1u << 5);
    static constexpr uint64_t c_flag_bit_is_light     = (1u << 6);
    static constexpr uint64_t c_flag_bit_is_mesh      = (1u << 7);

    virtual void on_attached_to      (Node& node);
    virtual void on_detached_from    (Node& node);
    virtual void on_transform_changed();

    virtual [[nodiscard]] auto node_type() const -> const char*;

    [[nodiscard]] auto parent                    () const -> Node*;
    [[nodiscard]] auto depth                     () const -> size_t;
    [[nodiscard]] auto children                  () const -> const std::vector<std::shared_ptr<Node>>&;
    [[nodiscard]] auto attachments               () const -> const std::vector<std::shared_ptr<INode_attachment>>&;
    [[nodiscard]] auto visibility_mask           () const -> uint64_t;
    [[nodiscard]] auto visibility_mask           () -> uint64_t&;
    [[nodiscard]] auto flag_bits                 () const -> uint64_t;
    [[nodiscard]] auto flag_bits                 () -> uint64_t&;
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
    [[nodiscard]] auto transform_point_from_world_to_local    (const glm::vec3 p) const -> glm::vec3;
    [[nodiscard]] auto transform_direction_from_world_to_local(const glm::vec3 p) const -> glm::vec3;
    [[nodiscard]] auto is_selected() const -> bool;
    [[nodiscard]] auto root       () -> Node*;
    [[nodiscard]] auto root       () const -> const Node*;
    [[nodiscard]] auto name       () const -> const std::string&;
    [[nodiscard]] auto label      () const -> const std::string&;
    [[nodiscard]] auto child_count() const -> size_t;
    [[nodiscard]] auto get_id     () const -> erhe::toolkit::Unique_id<Node>::id_type;

    void set_depth_recursive       (const size_t depth);
    void update_transform          (const uint64_t serial = 0);
    void update_transform_recursive(const uint64_t serial = 0);
    void sanity_check              () const;
    void sanity_check_root_path    (const Node* node) const;
    void set_parent_from_node      (const glm::mat4 matrix);
    void set_parent_from_node      (const Transform& transform);
    void set_node_from_parent      (const glm::mat4 matrix);
    void set_node_from_parent      (const Transform& transform);
    void set_world_from_node       (const glm::mat4 matrix);
    void set_world_from_node       (const Transform& transform);
    void attach                    (const std::shared_ptr<Node>& node);
    auto detach                    (Node* node) -> bool;
    void attach                    (const std::shared_ptr<INode_attachment>& attachment);
    auto detach                    (INode_attachment* attachment) -> bool;
    void unparent                  ();
    void set_name                  (const std::string_view name);

protected:
    class Transforms
    {
    public:
        Transform parent_from_node; // normative
        Transform world_from_node;  // calculated by update_transform()
    };

    Transforms                                     m_transforms;
    std::uint64_t                                  m_last_transform_update_serial{0};
    Node*                                          m_parent         {nullptr};
    std::vector<std::shared_ptr<Node>>             m_children;
    std::vector<std::shared_ptr<INode_attachment>> m_attachments;
    uint64_t                                       m_visibility_mask{c_visibility_none};
    uint64_t                                       m_flag_bits      {c_flag_bit_none};
    size_t                                         m_depth          {0};
    erhe::toolkit::Unique_id<Node>                 m_id;
    std::string                                    m_name;
    std::string                                    m_label;
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
