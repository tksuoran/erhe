#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

namespace editor {

// Node wrapping a parameterless geometry operation with the canonical
// void op(const Geometry& source, Geometry& destination) signature
// (triangulate, normalize, reverse, repair, ambo, dual, ...).
// The label doubles as the node type shown in the UI.
class Geometry_unary_operation_node : public Geometry_graph_node
{
public:
    using Operation = void (*)(const erhe::geometry::Geometry& source, erhe::geometry::Geometry& destination);

    Geometry_unary_operation_node(const char* label, Operation operation);

    void evaluate(Geometry_graph&) override;
    void imgui   () override;

private:
    Operation m_operation;
};

}
