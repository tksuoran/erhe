#pragma once

#include "graph/shader_graph_node.hpp"

namespace editor {

class Subtract : public Shader_graph_node
{
public:
    Subtract();

    void evaluate(Shader_graph&) override;
    void imgui   () override;
};

} // namespace editor
