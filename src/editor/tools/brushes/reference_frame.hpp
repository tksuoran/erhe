#pragma once

#include "erhe_geometry//geometry.hpp"

#include <glm/glm.hpp>
#include <cstdint>

namespace editor {

class Reference_frame
{
public:
    Reference_frame();

    Reference_frame(const GEO::Mesh& mesh, GEO::index_t facet, GEO::index_t face_offset, GEO::index_t corner_offset);

    void transform_by(const GEO::mat4f& m);

    [[nodiscard]] auto transform(float hover_distance) const -> GEO::mat4f;
    [[nodiscard]] auto scale    () const -> float;

    GEO::index_t m_corner_count {0};
    GEO::index_t m_face_offset  {0};
    GEO::index_t m_corner_offset{0};
    GEO::index_t m_facet        {0};
    GEO::index_t m_corner       {0};
    float        m_scale        {0.0f};
    GEO::vec3f   m_centroid     {0.0f, 0.0f, 0.0f}; // facet centroid
    GEO::vec3f   m_position     {1.0f, 0.0f, 0.0f}; // one of facet corner vertices
    GEO::vec3f   m_B            {0.0f, 0.0f, 1.0f};
    GEO::vec3f   m_T            {1.0f, 0.0f, 0.0f};
    GEO::vec3f   m_N            {0.0f, 1.0f, 0.0f};
};

}
