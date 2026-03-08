#include "erhe_rendergraph/sink_rendergraph_node.hpp"

namespace erhe::rendergraph {

Sink_rendergraph_node::Sink_rendergraph_node(Rendergraph& rendergraph, const erhe::utility::Debug_label debug_label)
    : Rendergraph_node{rendergraph, debug_label}
{
}

void Sink_rendergraph_node::execute_rendergraph_node()
{
}

auto Sink_rendergraph_node::outputs_allowed() const -> bool
{
    return false;
}

} // namespace erhe::rendergraph
