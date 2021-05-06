#pragma once

#include "erhe/scene/transform.hpp"

#include <cstdint>
#include <string>

namespace erhe::scene
{

class Node
{
public:
    void update(uint32_t update_serial = 0, bool cache_enable = false);

    auto parent_from_node() const -> glm::mat4
    {
        return transforms.parent_from_node.matrix();
    }

    auto world_from_node() const -> glm::mat4
    {
        return transforms.world_from_node.matrix();
    }

    auto node_from_parent() const -> glm::mat4
    {
        return transforms.parent_from_node.inverse_matrix();
    }

    auto node_from_world() const -> glm::mat4
    {
        return transforms.world_from_node.inverse_matrix();
    }

    auto position_in_world() const -> glm::vec4
    {
        return world_from_node() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    auto direction_in_world() const -> glm::vec4
    {
        return world_from_node() * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
    }

    auto root() -> Node*
    {
        if (parent == nullptr)
        {
            return this;
        }
        return parent->root();
    }

    auto root() const -> const Node*
    {
        if (parent == nullptr)
        {
            return this;
        }
        return parent->root();
    }

    std::string name;
    Node*       parent{nullptr};
    int         reference_count{0};

    struct Transforms
    {
        Transform parent_from_node; // normative
        Transform world_from_node;  // calculated by update()
    };
    Transforms transforms;

protected:
    bool          m_updated{false};
    std::uint32_t m_last_update_serial{0};
};

} // namespace erhe::scene
