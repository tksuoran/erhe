#pragma once

#include "graph/shader_graph_node.hpp"

namespace editor {

class Constant : public Shader_graph_node
{
public:
    Constant();

    void evaluate(Shader_graph&) override;
    void imgui   () override;

    Payload m_payload;
};

} // namespace editor
