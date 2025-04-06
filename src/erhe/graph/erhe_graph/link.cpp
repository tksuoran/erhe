#include "erhe_graph/graph.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"

namespace erhe::graph {

Link::Link()
    : m_id{make_graph_id()}
{
}

Link::Link(Link&& old) = default;

Link& Link::operator=(Link&& old) = default;

Link::Link(Pin* source, Pin* sink)
    : m_id    {make_graph_id()}
    , m_source{source}
    , m_sink  {sink}
{
}

Link::~Link()
{
}

auto Link::get_id() const -> int
{
    return m_id;
}

auto Link::get_source() const -> Pin* {
    return m_source;
}

auto Link::get_sink() const -> Pin*
{
    return m_sink;
}

auto Link::is_connected() const -> bool
{
    return (m_source != nullptr) && (m_sink != nullptr);
}

void Link::disconnect()
{
    m_source->remove_link(this);
    m_sink  ->remove_link(this);
    m_source = nullptr;
    m_sink   = nullptr;
}

} // namespace erhe::graph