#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

namespace editor {

class Math_node : public Geometry_graph_node
{
public:
    enum class Math_operation : int {
        add      = 0,
        subtract = 1,
        multiply = 2,
        divide   = 3,
        power    = 4,
        minimum  = 5,
        maximum  = 6,
        absolute = 7,
        square_root = 8,
        sine     = 9,
        cosine   = 10
    };

    Math_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;

private:
    Math_operation m_operation{Math_operation::add};
    float          m_a{0.0f};
    float          m_b{0.0f};
};

}
