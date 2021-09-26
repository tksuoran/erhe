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
    virtual ~INode_attachment() {};
    virtual auto name     () const -> const std::string& = 0;
    virtual void on_attach(Node& node) {};
    virtual void on_detach(Node& node) {};

    static constexpr uint64_t c_visibility_content      = (1ul << 0ul);
    static constexpr uint64_t c_visibility_shadow_cast  = (1ul << 1ul);
    static constexpr uint64_t c_visibility_id           = (1ul << 2ul);
    static constexpr uint64_t c_visibility_tool         = (1ul << 3ul);
    static constexpr uint64_t c_visibility_brush        = (1ul << 4ul);
    static constexpr uint64_t c_visibility_selected     = (1ul << 5ul);
    static constexpr uint64_t c_visibility_none         = uint64_t(0);

    uint64_t visibility_mask{c_visibility_none};
};

class Node
    : public std::enable_shared_from_this<Node>
    , public INode_attachment
{
public:
    explicit Node(std::string_view name);

    virtual ~Node();

    void update(const uint32_t update_serial = 0, const bool cache_enable = false);
    void attach(const std::shared_ptr<INode_attachment>& attachment);
    auto detach(const std::shared_ptr<INode_attachment>& attachment) -> bool;

    template <typename T>
    auto get_attachment() const -> std::shared_ptr<T>
    {
        for (const auto& attachment : attachments)
        {
            auto typed_attachment = std::dynamic_pointer_cast<T>(attachment);
            if (typed_attachment)
            {
                return typed_attachment;
            }
        }
        return {};
    }

    auto parent_from_node  () const -> glm::mat4;
    auto world_from_node   () const -> glm::mat4;
    auto node_from_parent  () const -> glm::mat4;
    auto node_from_world   () const -> glm::mat4;
    auto world_from_parent () const -> glm::mat4;
    auto position_in_world () const -> glm::vec4;
    auto direction_in_world() const -> glm::vec4;
    auto root              () -> Node*;
    auto root              () const -> const Node*;

    // INode_attachment
    auto name() const -> const std::string& override
    {
        return m_name;
    }

    auto label() const -> const std::string&
    {
        return m_label;
    }

    void set_name(std::string_view name);

    Node* parent{nullptr};

    auto attachment_count() -> size_t
    {
        return attachments.size();
    }

    class Transforms
    {
    public:
        Transform parent_from_node; // normative
        Transform world_from_node;  // calculated by update()
    };
    Transforms transforms;

    std::vector<std::shared_ptr<INode_attachment>> attachments;

protected:
    std::string                    m_name;
    erhe::toolkit::Unique_id<Node> m_id;
    std::string                    m_label;
    bool                           m_updated{false};
    std::uint32_t                  m_last_update_serial{0};
};

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
