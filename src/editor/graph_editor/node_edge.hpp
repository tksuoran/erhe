#pragma once

namespace editor {

// Which node edge a pin set (inputs or outputs) is laid out on. Shared by
// the shader graph node and the graph editor nodes (geometry / texture).
// Only left / right are implemented by the node renderers; top / bottom
// exist for the shader graph's combo but draw nothing yet.
class Node_edge
{
public:
    static constexpr int left   = 0;
    static constexpr int right  = 1;
    static constexpr int top    = 2;
    static constexpr int bottom = 3;
    static constexpr int count  = 4;
};

[[nodiscard]] inline auto get_node_edge_name(const int edge) -> const char*
{
    switch (edge) {
        case Node_edge::left:   return "Left";
        case Node_edge::right:  return "Right";
        case Node_edge::top:    return "Top";
        case Node_edge::bottom: return "Bottom";
        default:                return "?";
    }
}

}
