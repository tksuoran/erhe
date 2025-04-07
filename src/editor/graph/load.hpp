#pragma once

#include "graph/shader_graph_node.hpp"

namespace editor {

class Load : public Shader_graph_node
{
public:
    Load();

    void evaluate(Shader_graph&) override;
    void imgui   () override;

private:
    int     m_row{0};
    int     m_col{0};
    Payload m_payload;
};

} // namespace editor
