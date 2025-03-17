#include "erhe_scene/node_attachment.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene {

auto Node_attachment::get_static_type() -> uint64_t
{
    return erhe::Item_type::node_attachment;
}

auto Node_attachment::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Node_attachment::get_type_name() const -> std::string_view
{
    return static_type_name;
}

Node_attachment::Node_attachment()                       = default;
Node_attachment::Node_attachment(const Node_attachment&) = default;

Node_attachment& Node_attachment::operator=(const Node_attachment&)
{
    if (m_node != nullptr) {
        m_node->detach(this);
    }
    ERHE_FATAL("This probably won't work correctly.");
}

Node_attachment::Node_attachment(const Node_attachment& src, for_clone)
    : Item  {src}
    , m_node{nullptr} // clone is created as not attached
{
}

auto Node_attachment::clone_attachment() const -> std::shared_ptr<Node_attachment>
{
    return std::make_shared<Node_attachment>(static_cast<const Node_attachment&>(*this), erhe::for_clone{});
}

Node_attachment::Node_attachment(const std::string_view name)
    : Item{name}
{
}

Node_attachment::~Node_attachment() noexcept
{
    if (m_node != nullptr) {
        m_node->detach(this);
    }
}

auto Node_attachment::get_item_host() const -> erhe::Item_host*
{
    if (m_node != nullptr) {
        return m_node->get_item_host();
    }
    return nullptr;
}

void Node_attachment::handle_node_transform_update()
{
}

void Node_attachment::handle_node_update(Node* old_node, Node* new_node)
{
    const uint64_t old_flag_bits = old_node ? old_node->get_flag_bits() : 0;
    const uint64_t new_flag_bits = new_node ? new_node->get_flag_bits() : 0;
    const bool     visible       = (new_flag_bits & Item_flags::visible) == Item_flags::visible;
    if (old_flag_bits != new_flag_bits) {
        handle_node_flag_bits_update(old_flag_bits, new_flag_bits);
    }
    set_visible(visible);
}

void Node_attachment::handle_node_flag_bits_update(const uint64_t old_node_flag_bits, const uint64_t new_node_flag_bits)
{
    static_cast<void>(old_node_flag_bits);
    const bool visible = (new_node_flag_bits & Item_flags::visible) == Item_flags::visible;
    set_visible(visible);
    const bool selected = (new_node_flag_bits & Item_flags::selected) == Item_flags::selected;
    set_selected(selected);
};

void Node_attachment::set_node(Node* const node, const std::size_t position)
{
    if (m_node == node) {
        return;
    }
    Node* const old_node = m_node;
    erhe::Item_host* const old_host = (m_node != nullptr) ? m_node->get_item_host() : nullptr;
    m_node = node;
    erhe::Item_host* const new_host = (m_node != nullptr) ? m_node->get_item_host() : nullptr;

    if (old_node != nullptr) {
        old_node->handle_remove_attachment(this);
    }
    if (node != nullptr) {
        auto weak_this = weak_from_this();
        ERHE_VERIFY(!weak_this.expired());
        auto shared_this = std::static_pointer_cast<Node_attachment>(weak_this.lock());
        node->handle_add_attachment(shared_this, position);

        handle_node_update(old_node, node);
    }
    if (new_host != old_host) {
        handle_item_host_update(old_host, new_host);
        if (m_node != nullptr) {
            handle_node_transform_update();
        }
    }
}

auto Node_attachment::get_node() -> Node*
{
    return m_node;
}

auto Node_attachment::get_node() const -> const Node*
{
    return m_node;
}

} // namespace erhe::scene
