#pragma once

#include "texture_graph/texture_graph_node.hpp"

#include "erhe_texgen/compose_node.hpp"

#include <vector>

namespace erhe::texgen {
    class Node_descriptor;
}

namespace editor {

// Deterministic reseed value for a node id (monotonic counter mixed with the
// id; no C++ RNG, per the environment's determinism rule). Shared by the
// per-node Reseed button and the window's graph-level "Reseed all".
[[nodiscard]] auto texture_graph_next_reseed_value(std::size_t node_id) -> float;

// Generic texture graph node driven entirely by an erhe::texgen::Node_descriptor.
//
// Decision 4 in doc/texture-graph-plan.md: "Nodes are data - one generic node
// class". Every MVP node type (uniform color, perlin, voronoi, bricks, shape,
// blend, colorize, transform, brightness/contrast, normal map) is an instance
// of this class holding a reference to its immutable descriptor plus the live
// parameter values. The descriptors are the only thing that differ, so there is
// no per-type C++ subclass (mirrors Geometry_unary_operation_node).
//
// - pins are built from the descriptor (build_pins_from_descriptor), so they
//   cannot drift from the descriptor's inputs / outputs.
// - m_parameter_values is parallel to descriptor.parameters and stores the
//   typed value for each parameter (float / color / enum index / bool / size
//   exponent) in erhe::texgen::Parameter_value.
// - imgui() renders one canvas-safe widget per parameter (steppers for enums,
//   drag fields for floats / colors, a checkbox for bools; no popups).
// - configure() copies the live values into a Compose_node; descriptor()
//   exposes the descriptor. Step 3 uses both to assemble the compose DAG.
class Texture_descriptor_node : public Texture_graph_node
{
public:
    explicit Texture_descriptor_node(const erhe::texgen::Node_descriptor& descriptor);

    // Implements Texture_graph_node
    void evaluate (Texture_graph& graph) override;
    void imgui    () override;
    [[nodiscard]] auto descriptor() const -> const erhe::texgen::Node_descriptor* override;
    void configure(erhe::texgen::Compose_node& compose_node) const override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

    // True when this node's descriptor uses a per-node seed (a Reseed button is
    // shown and the graph-level "Reseed all" targets it).
    [[nodiscard]] auto is_seeded() const -> bool;

private:
    const erhe::texgen::Node_descriptor&         m_descriptor;
    std::vector<erhe::texgen::Parameter_value>   m_parameter_values; // parallel to m_descriptor.parameters
    float                                        m_seed{0.0f};        // feeds $seed when the descriptor is seeded
};

} // namespace editor
