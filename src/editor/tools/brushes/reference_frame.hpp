#pragma once

#include <geogram/mesh/mesh.h>

#include <glm/glm.hpp>
#include <cstdint>

namespace editor {

class Reference_frame
{
public:
    Reference_frame();

    Reference_frame(const GEO::Mesh& mesh, GEO::index_t facet, GEO::index_t face_offset, GEO::index_t corner_offset);

    void transform_by(const GEO::mat4& m);

    [[nodiscard]] auto transform(double hover_distance) const -> GEO::mat4;
    [[nodiscard]] auto scale    () const -> double;

    GEO::index_t m_corner_count {0};
    GEO::index_t m_face_offset  {0};
    GEO::index_t m_corner_offset{0};
    GEO::index_t m_facet        {0};
    GEO::index_t m_corner       {0};
    double       m_scale        {0.0};
    GEO::vec3    m_centroid     {0.0, 0.0, 0.0}; // facet centroid
    GEO::vec3    m_position     {1.0, 0.0, 0.0}; // one of facet corner vertices
    GEO::vec3    m_B            {0.0, 0.0, 1.0};
    GEO::vec3    m_T            {1.0, 0.0, 0.0};
    GEO::vec3    m_N            {0.0, 1.0, 0.0};
};

}
