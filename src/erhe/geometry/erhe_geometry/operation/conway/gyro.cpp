#include "erhe_geometry/operation/conway/gyro.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

class Gyro : public Geometry_operation
{
public:
    Gyro(const Geometry& source, Geometry& destination, float ratio);

    void build();
    float m_ratio;
};

Gyro::Gyro(const Geometry& source, Geometry& destination, float ratio)
    : Geometry_operation{source, destination}
    , m_ratio{ratio}
{
}

void Gyro::build()
{
    // For each corner in the src facet, create a pentagon from:
    // (prev edge midpoint 0, prev edge midpoint 1, corner vertex,
    //  next edge midpoint 0, face centroid)
    // ratio controls edge split positions (ratio and 1-ratio).

    make_dst_vertices_from_src_vertices();
    make_facet_centroids();
    make_edge_midpoints({m_ratio, 1.0f - m_ratio});

    for (const GEO::index_t src_facet : source_mesh.facets) {
        const GEO::index_t src_corner_count = source_mesh.facets.nb_corners(src_facet);
        if (src_corner_count < 3) {
            continue;
        }
        for (GEO::index_t local_src_facet_corner = 0; local_src_facet_corner < src_corner_count; ++local_src_facet_corner) {
            const GEO::index_t prev_src_corner          = source_mesh.facets.corner(src_facet, (local_src_facet_corner + src_corner_count - 1) % src_corner_count);
            const GEO::index_t src_corner               = source_mesh.facets.corner(src_facet, local_src_facet_corner);
            const GEO::index_t next_src_corner          = source_mesh.facets.corner(src_facet, (local_src_facet_corner + 1) % src_corner_count);
            const GEO::index_t a                        = source_mesh.facet_corners.vertex(prev_src_corner);
            const GEO::index_t b                        = source_mesh.facet_corners.vertex(src_corner     );
            const GEO::index_t c                        = source_mesh.facet_corners.vertex(next_src_corner);
            const GEO::index_t previous_edge_midpoint_0 = get_src_edge_new_vertex(a, b, 0);
            const GEO::index_t previous_edge_midpoint_1 = get_src_edge_new_vertex(a, b, 1);
            const GEO::index_t next_edge_midpoint_0     = get_src_edge_new_vertex(b, c, 0);
            if (previous_edge_midpoint_0 == GEO::NO_VERTEX ||
                previous_edge_midpoint_1 == GEO::NO_VERTEX ||
                next_edge_midpoint_0     == GEO::NO_VERTEX) {
                continue;
            }
            const GEO::index_t new_dst_facet = make_new_dst_facet_from_src_facet(src_facet, 5);
            make_new_dst_corner_from_dst_vertex        (new_dst_facet, 0, previous_edge_midpoint_0);
            make_new_dst_corner_from_dst_vertex        (new_dst_facet, 1, previous_edge_midpoint_1);
            make_new_dst_corner_from_src_corner        (new_dst_facet, 2, src_corner);
            make_new_dst_corner_from_dst_vertex        (new_dst_facet, 3, next_edge_midpoint_0);
            make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 4, src_facet);
        }
    }

    post_processing();
}

void gyro(const Geometry& source, Geometry& destination, float ratio)
{
    Gyro operation{source, destination, ratio};
    operation.build();
}


} // namespace erhe::geometry::operation
