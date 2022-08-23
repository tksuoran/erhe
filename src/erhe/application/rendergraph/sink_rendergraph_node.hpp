#pragma once

#include "erhe/application/rendergraph/rendergraph_node.hpp"

#include <memory>
#include <string_view>

namespace erhe::application {

/// <summary>
/// Base class for rendergraph nodes that only consume input without any outputs.
/// </summary>
/// Sink rendergraph nodes only consumes input, it cannot have any outputs and cannot
/// be used as input for other nodes
class Sink_rendergraph_node
    : public Rendergraph_node
{
public:
    static constexpr std::string_view c_type_name{"Sink_rendergraph_node"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    explicit Sink_rendergraph_node(const std::string_view name);

    [[nodiscard]] auto type_name() const -> std::string_view override { return c_type_name; }
    [[nodiscard]] auto type_hash() const -> uint32_t         override { return c_type_hash; }
    void execute_rendergraph_node() override;

    auto outputs_allowed() const -> bool override;
};

} // namespace erhe::application
