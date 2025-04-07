#pragma once

#include "graph/shader_graph_node.hpp"

namespace editor {

class Multiply : public Shader_graph_node
{
public:
    Multiply();

    void evaluate(Shader_graph&) override;
    void imgui   () override;
};

} // namespace editor
