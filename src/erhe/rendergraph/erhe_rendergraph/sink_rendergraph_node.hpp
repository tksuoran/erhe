#pragma once

#include "erhe_rendergraph/rendergraph_node.hpp"

#include <string_view>

namespace erhe::rendergraph {

/// <summary>
/// Base class for rendergraph nodes that only consume input without any outputs.
/// </summary>
/// Sink rendergraph nodes only consumes input, it cannot have any outputs and cannot
/// be used as input for other nodes
class Sink_rendergraph_node : public Rendergraph_node
{
public:
    static constexpr std::string_view c_type_name{"Sink_rendergraph_node"};

    Sink_rendergraph_node(Rendergraph& rendergraph, const std::string_view name);

    auto get_type_name           () const -> std::string_view override { return c_type_name; }
    void execute_rendergraph_node() override;
    auto outputs_allowed         () const -> bool override;
};

} // namespace erhe::rendergraph
