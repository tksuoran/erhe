#pragma once

#include "erhe_item/item.hpp"

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
    : public erhe::Item<
        Item_base,
        Item_base,
        Node_attachment,
        erhe::Item_kind::clone_using_custom_clone_constructor
    >
{
public:
    Node_attachment();
    explicit Node_attachment(const Node_attachment&);

    Node_attachment& operator=(const Node_attachment&);
    Node_attachment(const Node_attachment& src, for_clone);
    Node_attachment(const std::string_view name);
    virtual ~Node_attachment() noexcept;

    // Implements Item_base
    auto get_item_host() const -> erhe::Item_host* override;

    // Public API
    virtual auto clone_attachment() const -> std::shared_ptr<Node_attachment>;

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
    void set_node                    (Node* node, std::size_t position = std::numeric_limits<std::size_t>::max());

    [[nodiscard]] auto get_node() -> Node*;
    [[nodiscard]] auto get_node() const -> const Node*;

protected:
    Node* m_node{nullptr};
};

} // namespace erhe::scene
