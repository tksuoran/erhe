#pragma once

#include "erhe/item/item.hpp"

#include <cstdint>
#include <memory>
#include <string_view>

namespace erhe {
    class Item_host;
}

namespace erhe::scene
{

class Node;

class Node_attachment
    : public erhe::Item
{
public:
    Node_attachment();
    explicit Node_attachment(std::size_t id);
    Node_attachment(const std::string_view name, std::size_t id);
    virtual ~Node_attachment() noexcept;

    // Implements Item
    auto get_item_host() const -> erhe::Item_host* override;

    // Public API
    virtual void handle_node_update          (Node* old_node, Node* new_node);
    virtual void handle_node_transform_update();
    virtual void handle_item_host_update(
        Item_host* const old_item_host,
        Item_host* const new_item_host
    )
    {
        static_cast<void>(old_item_host);
        static_cast<void>(new_item_host);
    }

    void handle_node_flag_bits_update(uint64_t old_node_flag_bits, uint64_t new_node_flag_bits);
    void set_node                    (Node* node, std::size_t position = 0);

    [[nodiscard]] auto get_node() -> Node*;
    [[nodiscard]] auto get_node() const -> const Node*;

protected:
    Node* m_node{nullptr};
};

} // namespace erhe::scene
