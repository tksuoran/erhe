#include "erhe_graph/pin.hpp"
#include "erhe_graph/graph.hpp"
#include "erhe_graph/link.hpp"

namespace erhe::graph {

Pin::Pin(Node* owner_node, std::size_t slot, bool is_source, std::size_t key, std::string_view name)
    : m_id        {make_graph_id()}
    , m_key       {key}
    , m_owner_node{owner_node}
    , m_slot      {slot}
    , m_is_source {is_source}
    , m_name      {name}
{
}

Pin::~Pin() = default;


auto Pin::is_sink() const -> bool
{
    return !m_is_source;
}

auto Pin::is_source() const -> bool
{
    return m_is_source;
}

auto Pin::get_id() const -> int
{
    return m_id;
}

auto Pin::get_key() const -> std::size_t
{
    return m_key;
}

auto Pin::get_name() const -> const std::string_view
{
    return m_name;
}

void Pin::add_link(Link* link)
{
    m_links.push_back(link);
}

void Pin::remove_link(Link* link)
{
    auto i = std::find_if(
        m_links.begin(), m_links.end(),
        [link](Link* entry) {
            return entry == link;
        }
    );
    m_links.erase(i);
}

auto Pin::get_links() const -> const std::vector<Link*>&
{
    return m_links;
}

auto Pin::get_links() -> std::vector<Link*>&
{
    return m_links;
}

auto Pin::get_owner_node() const -> Node*
{
    return m_owner_node;
}

auto Pin::get_slot() const -> std::size_t
{
    return m_slot;
}

} // namespace erhe::graph
