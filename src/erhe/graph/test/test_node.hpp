#pragma once

#include "erhe_graph/graph.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/node.hpp"
#include "erhe_graph/pin.hpp"

#include <cstddef>
#include <string_view>

namespace erhe::graph::test {

// Semantic pin keys used by the tests. connect() only accepts links between
// pins with equal keys, so two distinct keys are enough to exercise both the
// accept and the refuse paths.
constexpr std::size_t key_alpha = 1;
constexpr std::size_t key_beta  = 2;

// Minimal concrete Node for tests. Pin creation goes through the
// erhe::graph::Node pin factory helpers, matching how editor node classes
// (e.g. Geometry_graph_node subclasses) build their pins.
class Test_node : public erhe::graph::Node
{
public:
    explicit Test_node(const std::string_view name)
        : Node{name}
    {
    }

    void add_input_pin(const std::size_t key, const std::string_view pin_name)
    {
        base_make_input_pin(key, pin_name);
    }

    void add_output_pin(const std::size_t key, const std::string_view pin_name)
    {
        base_make_output_pin(key, pin_name);
    }

    [[nodiscard]] auto input_pin(const std::size_t slot) -> erhe::graph::Pin*
    {
        return &get_input_pins()[slot];
    }

    [[nodiscard]] auto output_pin(const std::size_t slot) -> erhe::graph::Pin*
    {
        return &get_output_pins()[slot];
    }
};

// Makes a node with one input pin and one output pin, both using key_alpha.
// This is the standard shape for chain / diamond / cycle topology tests.
class Simple_node : public Test_node
{
public:
    explicit Simple_node(const std::string_view name)
        : Test_node{name}
    {
        add_input_pin (key_alpha, "in");
        add_output_pin(key_alpha, "out");
    }
};

// True when node's pins reference link.
[[nodiscard]] inline auto node_references_link(erhe::graph::Node& node, const erhe::graph::Link* link) -> bool
{
    for (erhe::graph::Pin& pin : node.get_input_pins()) {
        for (const erhe::graph::Link* pin_link : pin.get_links()) {
            if (pin_link == link) {
                return true;
            }
        }
    }
    for (erhe::graph::Pin& pin : node.get_output_pins()) {
        for (const erhe::graph::Link* pin_link : pin.get_links()) {
            if (pin_link == link) {
                return true;
            }
        }
    }
    return false;
}

// Count of links across all pins of node.
[[nodiscard]] inline auto pin_link_count(erhe::graph::Node& node) -> std::size_t
{
    std::size_t count = 0;
    for (erhe::graph::Pin& pin : node.get_input_pins()) {
        count += pin.get_links().size();
    }
    for (erhe::graph::Pin& pin : node.get_output_pins()) {
        count += pin.get_links().size();
    }
    return count;
}

} // namespace erhe::graph::test
