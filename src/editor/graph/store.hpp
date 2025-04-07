#pragma once

#include "graph/shader_graph_node.hpp"

namespace editor {

class Store : public Shader_graph_node
{
public:
    Store();

    void evaluate(Shader_graph& graph) override;
    void imgui   () override;

private:
    int     m_row{0};
    int     m_col{0};
    Payload m_payload;
};

} // namespace editor
