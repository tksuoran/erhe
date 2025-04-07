#include "graph/shader_graph.hpp"
#include "graph/shader_graph_node.hpp"
#include "windows/sheet_window.hpp"

namespace editor {

void Shader_graph::evaluate(Sheet* sheet)
{
    sort();
    m_sheet = sheet;
    for (erhe::graph::Node* node : m_nodes) {
        Shader_graph_node* shader_graph_node = dynamic_cast<Shader_graph_node*>(node);
        shader_graph_node->evaluate(*this);
    }
    m_sheet = nullptr;
}

auto Shader_graph::load(int row, int column) const -> double
{
    if (m_sheet == nullptr) {
        return 0.0;
    }
    return m_sheet->get_value(row, column);
}

void Shader_graph::store(int row, int column, double value)
{
    if (m_sheet == nullptr) {
        return;
    }
    m_sheet->set_value(row, column, value);
    m_sheet->evaluate_expressions();
}


} // namespace editor
