#pragma once

#include "graph/shader_graph_node.hpp"

namespace editor {

class Divide : public Shader_graph_node
{
public:
    Divide();

    void evaluate(Shader_graph&) override;
    void imgui   () override;
};

} // namespace editor
