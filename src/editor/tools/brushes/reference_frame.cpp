#include "tools/brushes/reference_frame.hpp"

#include "editor_log.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_verify/verify.hpp"

#include <geogram/mesh/mesh_geometry.h>

namespace editor {

Reference_frame::Reference_frame() = default;

Reference_frame::Reference_frame(
    const GEO::Mesh&   mesh,
    const GEO::index_t facet,
    const GEO::index_t face_offset,
    const GEO::index_t in_corner_offset
)
    : m_face_offset{face_offset}
    , m_facet      {std::min(facet, mesh.facets.nb() - 1)}
{
    m_corner_count = mesh.facets.nb_corners(facet);
    if (m_corner_count == 0) {
        log_brush->warn("Polygon with 0 corners");
        return;
    }

    m_corner_offset = in_corner_offset % m_corner_count;
    m_corner        = mesh.facets.corner(facet, m_corner_offset);

    const GEO::index_t next_corner   = mesh.facets.next_corner_around_facet(m_facet, m_corner);
    const GEO::index_t vertex        = mesh.facet_corners.vertex(m_corner);
    const GEO::index_t next_vertex   = mesh.facet_corners.vertex(next_corner);
                       m_N           = GEO::normalize(mesh_facet_normalf(mesh, m_facet));
                       m_centroid    = mesh_facet_centerf(mesh, m_facet);
                       m_position    = get_pointf(mesh.vertices, vertex);
    const GEO::vec3f   next_position = get_pointf(mesh.vertices, next_vertex);
    const GEO::vec3f   edge_midpoint = 0.5f * (m_position + next_position);

    m_scale = GEO::distance(m_centroid, m_position);

    m_T = GEO::normalize(edge_midpoint - m_centroid);
    m_B = GEO::normalize(GEO::cross(m_N, m_T));
    m_N = GEO::normalize(GEO::cross(m_T, m_B));
    m_T = GEO::normalize(GEO::cross(m_B, m_N));
}

void Reference_frame::transform_by(const GEO::mat4f& transform)
{
    // TODO Use adjugate as normal_transform
    //// GEO::mat4 inverse;
    //// transform.compute_inverse(inve  rse);
    //// const GEO::mat4 normal_transform = inverse.transpose();
    m_centroid = GEO::vec3f{transform * GEO::vec4f{m_centroid, 1.0f}};
    m_position = GEO::vec3f{transform * GEO::vec4f{m_position, 1.0f}};
    m_scale    = GEO::distance(m_centroid, m_position);
    m_B        = GEO::vec3f{transform * GEO::vec4f{m_B, 0.0f}};
    m_T        = GEO::vec3f{transform * GEO::vec4f{m_T, 0.0f}};
    m_N        = GEO::vec3f{transform * GEO::vec4f{m_N, 0.0f}};
    m_B        = GEO::normalize(GEO::cross(m_N, m_T));
    m_N        = GEO::normalize(GEO::cross(m_T, m_B));
    m_T        = GEO::normalize(GEO::cross(m_B, m_N));
}

auto Reference_frame::scale() const -> float
{
    return m_scale;
}

auto Reference_frame::transform(float hover_distance) const -> GEO::mat4f
{
    const GEO::vec3f O = m_centroid + hover_distance * m_N;
    return GEO::mat4f{
        {
            { -m_T.x, -m_N.x, -m_B.x, O.x  },
            { -m_T.y, -m_N.y, -m_B.y, O.y  },
            { -m_T.z, -m_N.z, -m_B.z, O.z  },
            {   0.0f,   0.0f,   0.0f, 1.0f }
        }
    };
}

} // namespace editor
