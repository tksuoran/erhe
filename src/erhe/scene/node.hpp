#pragma once

#include "erhe/scene/transform.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace erhe::scene
{

class INode_attachment;

class Node
    : public std::enable_shared_from_this<Node>
{
public:
    virtual ~Node();

    void update(const uint32_t update_serial = 0, const bool cache_enable = false);
    void attach(const std::shared_ptr<INode_attachment>& attachment);
    auto detach(const std::shared_ptr<INode_attachment>& attachment) -> bool;

    template <typename T>
    auto get_attachment() const -> std::shared_ptr<T>
    {
        for (const auto& attachment : m_attachments)
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

    std::string name;
    Node*       parent{nullptr};

    auto attachment_count() -> size_t 
    {
        return m_attachments.size();
    }

    class Transforms
    {
    public:
        Transform parent_from_node; // normative
        Transform world_from_node;  // calculated by update()
    };
    Transforms transforms;

protected:
    bool                                           m_updated{false};
    std::uint32_t                                  m_last_update_serial{0};
    std::vector<std::shared_ptr<INode_attachment>> m_attachments;
};

class INode_attachment
{
public:
    virtual ~INode_attachment() {};
    virtual auto name     () const -> const std::string& = 0;
    virtual void on_attach(Node& node) {};
    virtual void on_detach(Node& node) {};
};

} // namespace erhe::scene
