#include "tools/brushes/reference_frame.hpp"
#include "tools/grid.hpp"

#include "editor_log.hpp"

#include "erhe_geometry/geometry.hpp"

#include <glm/gtx/matrix_operation.hpp>

namespace editor {

Reference_frame::Reference_frame() = default;

Reference_frame::Reference_frame(
    const GEO::Mesh&   mesh,
    const GEO::index_t facet,
    const GEO::index_t face_offset,
    const GEO::index_t in_corner_offset0,
    Frame_orientation  frame_orientation
)
    : m_frame_orientation{frame_orientation}
    , m_frame_source     {Frame_source::derived_from_surface_points}
    , m_face_offset      {face_offset}
    , m_facet            {std::min(facet, mesh.facets.nb() - 1)}
{
    m_corner_count = mesh.facets.nb_corners(facet);
    if (m_corner_count == 0) {
        log_brush->warn("Polygon with 0 corners");
        return;
    }

    m_corner_offset0 = in_corner_offset0 % m_corner_count;
    m_corner_offset1 = (in_corner_offset0 + 1) % m_corner_count;
    m_corner0        = mesh.facets.corner(facet, m_corner_offset0);
    m_corner1        = mesh.facets.corner(facet, m_corner_offset1);

    const GEO::index_t vertex0     = mesh.facet_corners.vertex(m_corner0);
    const GEO::index_t vertex1     = mesh.facet_corners.vertex(m_corner1);
                       m_N         = GEO::normalize(mesh_facet_normalf(mesh, m_facet));
                       m_centroid  = mesh_facet_centerf(mesh, m_facet);
                       m_position0 = get_pointf(mesh.vertices, vertex0);
                       m_position1 = get_pointf(mesh.vertices, vertex1);
    const GEO::vec3f   midpoint    = 0.5f * (m_position0 + m_position1);

    m_scale = 0.5f * (GEO::distance(m_centroid, m_position0) + GEO::distance(m_centroid, m_position1));

    if (frame_orientation == Frame_orientation::in) {
        m_N = -m_N;
    }

    m_T = GEO::normalize(midpoint - m_centroid);
    m_B = GEO::normalize(GEO::cross(m_N, m_T));
    m_N = GEO::normalize(GEO::cross(m_T, m_B));
    m_T = GEO::normalize(GEO::cross(m_B, m_N));
}

Reference_frame::Reference_frame(const Grid& grid, const GEO::vec3f& position)
    : m_frame_orientation{Frame_orientation::out}
    , m_frame_source     {Frame_source::derived_from_surface_points}
{
    const glm::vec3 snapped_grid_position = grid.snap_world_position(to_glm_vec3(position));
    const glm::vec3 grid_normal           = grid.normal_in_world();
    const glm::vec3 grid_tangent          = grid.tangent_in_world();
    const glm::vec3 grid_bitangent        = grid.bitangent_in_world();

    m_centroid = to_geo_vec3f(snapped_grid_position);
    m_scale    = grid.get_cell_size(); // * grid.cell_div();
    m_T        = to_geo_vec3f(grid_tangent);
    m_B        = to_geo_vec3f(grid_bitangent);
    m_N        = to_geo_vec3f(grid_normal);
}

void Reference_frame::transform_by(const GEO::mat4f& transform)
{
    switch (m_frame_source) {
        case Frame_source::derived_from_surface_points: {
            m_centroid  = GEO::vec3f{transform * GEO::vec4f{m_centroid, 1.0f}};
            m_position0 = GEO::vec3f{transform * GEO::vec4f{m_position0, 1.0f}};
            m_position1 = GEO::vec3f{transform * GEO::vec4f{m_position1, 1.0f}};
            m_scale     = 0.5f * (GEO::distance(m_centroid, m_position0) + GEO::distance(m_centroid, m_position1));
            m_B         = GEO::vec3f{transform * GEO::vec4f{m_B, 0.0f}};
            m_T         = GEO::vec3f{transform * GEO::vec4f{m_T, 0.0f}};
            m_N         = GEO::vec3f{transform * GEO::vec4f{m_N, 0.0f}};
            m_B         = GEO::normalize(GEO::cross(m_N, m_T));
            m_N         = GEO::normalize(GEO::cross(m_T, m_B));
            m_T         = GEO::normalize(GEO::cross(m_B, m_N));
            break;
        }

        case Frame_source::direct_tangent_frame: {
            const glm::mat4  transform_        = to_glm_mat4(transform);
            const glm::mat4  normal_transform_ = glm::transpose(glm::adjugate(transform_));
            const GEO::mat4f normal_transform  = to_geo_mat4f(normal_transform_);
            m_T     = GEO::vec3f{transform * GEO::vec4f{m_T, 0.0f}};
            m_B     = GEO::vec3f{transform * GEO::vec4f{m_B, 0.0f}};
            m_N     = GEO::vec3f{transform * GEO::vec4f{m_N, 0.0f}};
            m_scale = 0.5f * (GEO::length(m_T) + GEO::length(m_B));
            break;
        }

        default: {
            log_brush->warn("Reference frame transform_by: invalid frame source");
            return;
        }
    }
    // TODO Use adjugate as normal_transform
    //// GEO::mat4 inverse;
    //// transform.compute_inverse(inve  rse);
    //// const GEO::mat4 normal_transform = inverse.transpose();
}

auto Reference_frame::scale() const -> float
{
    return m_scale;
}

auto Reference_frame::is_valid() const -> bool
{
    return (m_frame_source != Frame_source::invalid) && (m_frame_orientation != Frame_orientation::invalid);
}

auto Reference_frame::transform(float hover_distance) const -> GEO::mat4f
{
    const GEO::vec3f O = m_centroid + hover_distance * m_N;
    return GEO::mat4f{
        {
            { m_T.x, m_N.x, m_B.x, O.x  },
            { m_T.y, m_N.y, m_B.y, O.y  },
            { m_T.z, m_N.z, m_B.z, O.z  },
            {  0.0f,  0.0f,  0.0f, 1.0f }
        }
    };
}

} // namespace editor
