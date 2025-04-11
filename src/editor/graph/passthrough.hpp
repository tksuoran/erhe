#pragma once

#include "graph/shader_graph_node.hpp"

namespace editor {

class Passthrough : public Shader_graph_node
{
public:
    Passthrough();
    void evaluate(Shader_graph&) override;
    void imgui() override;
};

} // namespace editor
