#include "erhe_graph/node.hpp"
#include "erhe_graph/graph.hpp"
#include "erhe_graph/pin.hpp"

namespace erhe::graph {

// A copied node replicates the source's pin structure (direction, key, name,
// slot) but is an independent graph entity: its pins are owned by the copy
// and carry no links (aliasing the source's Link* would let peer pins and
// the graph dangle), and it has a fresh graph id.
Node::Node(const Node& other)
    : Item           {other}
    , m_graph_node_id{make_graph_id()}
{
    copy_pins_from(other);
}

Node& Node::operator=(const Node& other)
{
    if (this == &other) {
        return *this;
    }
    Item::operator=(other);
    // Keep this node's own graph id: assignment replaces contents, not identity.
    m_input_pins.clear();
    m_output_pins.clear();
    copy_pins_from(other);
    return *this;
}

void Node::copy_pins_from(const Node& other)
{
    for (const Pin& pin : other.m_input_pins) {
        base_make_input_pin(pin.get_key(), pin.get_name());
        m_input_pins.back().set_multi_link(pin.allows_multiple_links());
    }
    for (const Pin& pin : other.m_output_pins) {
        base_make_output_pin(pin.get_key(), pin.get_name());
    }
}

Node::Node()
    : Item           {}
    , m_graph_node_id{make_graph_id()}
{
}

Node::Node(const std::string_view name)
    : Item           {name}
    , m_graph_node_id{make_graph_id()}
{
}

Node::~Node() noexcept
{
}

auto Node::node_from_this() -> std::shared_ptr<Node>
{
    return std::static_pointer_cast<Node>(shared_from_this());
}

auto Node::get_graph_id() const -> int
{
    return m_graph_node_id;
}

auto Node::get_input_pins() const -> const etl::vector<Pin, max_pin_count>&
{
    return m_input_pins;
}

auto Node::get_input_pins() -> etl::vector<Pin, max_pin_count>&
{
    return m_input_pins;
}

auto Node::get_output_pins() const -> const etl::vector<Pin, max_pin_count>&
{
    return m_output_pins;
}

auto Node::get_output_pins() -> etl::vector<Pin, max_pin_count>&
{
    return m_output_pins;
}

void Node::base_make_input_pin(std::size_t key, std::string_view name)
{
    const std::size_t slot = m_input_pins.size();
    m_input_pins.emplace_back(erhe::graph::Pin{this, slot, false, key, name});
}

void Node::base_make_output_pin(std::size_t key, std::string_view name)
{
    const std::size_t slot = m_output_pins.size();
    m_output_pins.emplace_back(erhe::graph::Pin{this, slot, true, key, name});
}

} // namespace erhe::graph
