#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

namespace editor {

class Conway_node : public Geometry_graph_node
{
public:
    enum class Conway_operation : int {
        ambo     = 0,
        dual     = 1,
        join     = 2,
        kis      = 3,
        meta     = 4,
        subdivide= 5,
        truncate = 6,
        chamfer  = 7,
        gyro     = 8
    };

    Conway_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;

private:
    Conway_operation m_operation     {Conway_operation::dual};
    float            m_kis_height    {0.0f};
    float            m_truncate_ratio{1.0f / 3.0f};
    float            m_chamfer_ratio {0.25f};
    float            m_gyro_ratio    {1.0f / 3.0f};
};

}
