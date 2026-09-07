#include "erhe_scene/node_attachment.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene {

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
    // Defer to the polymorphic clone(), which already encodes each concrete
    // attachment type's clonability (via Item_kind) and any custom clone().
    // This is the single source of truth for attachment duplication:
    //  - clone_using_copy_constructor / clone_using_custom_clone_constructor
    //    produce a proper typed clone (Light, Camera, Mesh, ...).
    //  - not_clonable types (Brush_placement, Frame_controller) return nullptr,
    //    so Node's clone constructor skips them instead of attaching a sliced,
    //    meaningless base Node_attachment.
    return std::dynamic_pointer_cast<Node_attachment>(clone());
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

// Visibility is not mirrored here: the attachment inherits the visible /
// shadow_cast / lightmapped properties from its node (D23,
// get_inheritance_parent), and set_node brackets the move with the
// inheritance snapshot.
void Node_attachment::handle_node_update(Node* old_node, Node* new_node)
{
    const uint64_t old_flag_bits = old_node ? old_node->get_flag_bits() : 0;
    const uint64_t new_flag_bits = new_node ? new_node->get_flag_bits() : 0;
    if (old_flag_bits != new_flag_bits) {
        handle_node_flag_bits_update(old_flag_bits, new_flag_bits);
    }
}

void Node_attachment::handle_node_flag_bits_update(const uint64_t old_node_flag_bits, const uint64_t new_node_flag_bits)
{
    static_cast<void>(old_node_flag_bits);
    using namespace erhe::utility;
    const bool selected             = test_bit_set(new_node_flag_bits, Item_flags::selected);
    const bool hovered_in_viewport  = test_bit_set(new_node_flag_bits, Item_flags::hovered_in_viewport);
    const bool hovered_in_item_tree = test_bit_set(new_node_flag_bits, Item_flags::hovered_in_item_tree);
    set_flag_bits(Item_flags::selected,             selected);
    set_flag_bits(Item_flags::hovered_in_viewport,  hovered_in_viewport);
    set_flag_bits(Item_flags::hovered_in_item_tree, hovered_in_item_tree);
};

auto Node_attachment::get_inheritance_parent() const -> const erhe::property::Dependency_object*
{
    return m_node;
}

void Node_attachment::set_node(Node* const node, const std::size_t position)
{
    if (m_node == node) {
        return;
    }
    // The old node's attachment list may be the last owner; keep this alive
    // until the move is complete. (Expired while destructing: no owner then.)
    const std::shared_ptr<Node_attachment> keep_alive = std::static_pointer_cast<Node_attachment>(weak_from_this().lock());
    // Inherited property values before the move; applied after it so the
    // change notifications carry the right old values (D8).
    const erhe::property::Inheritance_snapshot inheritance_snapshot = capture_inheritance_snapshot();
    Node* const old_node = m_node;
    erhe::Item_host* const old_host = (m_node != nullptr) ? m_node->get_item_host() : nullptr;
    m_node = node;
    erhe::Item_host* const new_host = (m_node != nullptr) ? m_node->get_item_host() : nullptr;

    if (old_node != nullptr) {
        old_node->handle_remove_attachment(this);
    }
    if (node != nullptr) {
        ERHE_VERIFY(keep_alive);
        node->handle_add_attachment(keep_alive, position);

        handle_node_update(old_node, node);
    }
    // The node is the attachment's inheritance parent, so the effective
    // active state (doc/usd-compatibility-plan.md X2) moves with it. Done
    // before the host update so a consumer of the bit (an attachment that
    // registers itself with the host) sees the state it is attaching in.
    rederive_active_flag_bits();
    if (new_host != old_host) {
        handle_item_host_update(old_host, new_host);
        if (m_node != nullptr) {
            handle_node_transform_update();
        }
    }
    apply_inheritance_snapshot(inheritance_snapshot);
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
