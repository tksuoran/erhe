#pragma once

#include "graph/shader_graph_node.hpp"

namespace editor {

class Add : public Shader_graph_node
{
public:
    Add();
    void evaluate(Shader_graph&) override;
    void imgui() override;
};

} // namespace editor
