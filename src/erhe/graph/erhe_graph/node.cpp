#include "erhe_graph/node.hpp"
#include "erhe_graph/graph.hpp"
#include "erhe_graph/pin.hpp"

namespace erhe::graph {

auto Node::get_static_type() -> uint64_t
{
    return erhe::Item_type::graph_node;
}

auto Node::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Node::get_type_name() const -> std::string_view
{
    return static_type_name;
}

Node::Node(const Node&) = default;
Node& Node::operator=(const Node&) = default;

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

auto Node::get_name() const -> const std::string_view
{
    return m_name;
}

auto Node::get_input_pins() const -> const std::vector<Pin>&
{
    return m_input_pins;
}

auto Node::get_input_pins() -> std::vector<Pin>&
{
    return m_input_pins;
}

auto Node::get_output_pins() const -> const std::vector<Pin>&
{
    return m_output_pins;
}

auto Node::get_output_pins() -> std::vector<Pin>&
{
    return m_output_pins;
}

void Node::base_make_input_pin(std::size_t key, std::string_view name)
{
    const std::size_t slot = m_input_pins.size();
    m_input_pins.emplace_back(std::move(erhe::graph::Pin{this, slot, false, key, name}));
}

void Node::base_make_output_pin(std::size_t key, std::string_view name)
{
    const std::size_t slot = m_output_pins.size();
    m_output_pins.emplace_back(std::move(erhe::graph::Pin{this, slot, true, key, name}));
}

} // namespace erhe::graph
