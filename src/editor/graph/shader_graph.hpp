#pragma once

#include "erhe_graph/graph.hpp"

namespace editor {

class Sheet;

class Shader_graph : public erhe::graph::Graph
{
public:
    void evaluate(Sheet* sheet);
    auto load    (int row, int column) const -> double;
    void store   (int row, int column, double value);

private:
    Sheet* m_sheet{nullptr};
};

} // namespace editor
