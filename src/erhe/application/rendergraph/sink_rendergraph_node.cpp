#include "erhe/application/rendergraph/sink_rendergraph_node.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::application {

Sink_rendergraph_node::Sink_rendergraph_node(const std::string_view name)
    : Rendergraph_node{name}
{
}

void Sink_rendergraph_node::execute_rendergraph_node()
{
}

auto Sink_rendergraph_node::outputs_allowed() const -> bool
{
    return false;
}

} // namespace erhe::application
