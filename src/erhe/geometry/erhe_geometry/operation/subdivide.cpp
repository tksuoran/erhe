#include "erhe_geometry/operation/subdivide.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

#include <cstdint>
#include <limits>

namespace erhe::geometry::operation {

class Subdivide : public Geometry_operation
{
public:
    Subdivide(const Geometry& source, Geometry& destination);

    void build();
};

Subdivide::Subdivide(const Geometry& source, Geometry& destination)
    : Geometry_operation{source, destination}
{
}

void Subdivide::build()
{
    // Add midpoints to edges and connect to polygon center

    // For each corner in the src facet,
    // add one quad (centroid, previous edge midpoint, corner, next edge midpoint)

    make_dst_vertices_from_src_vertices();
    make_facet_centroids();
    make_edge_midpoints({ 0.5f });

    for (const GEO::index_t src_facet : source_mesh.facets) {
        //if (src_polygon.corner_count == 3)
        //{
        //    Polygon_id new_polygon_id = make_new_polygon_from_polygon(src_polygon_id);
        //    add_polygon_corners(new_polygon_id, src_polygon_id);
        //    continue;
        //}
        const GEO::index_t src_corner_count = source_mesh.facets.nb_corners(src_facet);
        for (GEO::index_t local_src_facet_corner = 0; local_src_facet_corner < src_corner_count; ++local_src_facet_corner) {
            const GEO::index_t prev_src_corner        = source_mesh.facets.corner(src_facet, (local_src_facet_corner + src_corner_count - 1) % src_corner_count);
            const GEO::index_t src_corner             = source_mesh.facets.corner(src_facet, local_src_facet_corner);
            const GEO::index_t next_src_corner        = source_mesh.facets.corner(src_facet, (local_src_facet_corner + 1) % src_corner_count);
            const GEO::index_t a                      = source_mesh.facet_corners.vertex(prev_src_corner);
            const GEO::index_t b                      = source_mesh.facet_corners.vertex(src_corner     );
            const GEO::index_t c                      = source_mesh.facet_corners.vertex(next_src_corner);
            const GEO::index_t previous_edge_midpoint = get_src_edge_new_vertex(a, b, 0);
            const GEO::index_t next_edge_midpoint     = get_src_edge_new_vertex(b, c, 0);
            if (previous_edge_midpoint == GEO::NO_INDEX) {
                continue;
            }
            if (next_edge_midpoint == std::numeric_limits<uint32_t>::max()) {
                continue;
            }
            const GEO::index_t new_dst_facet = make_new_dst_facet_from_src_facet(src_facet, 4);
            make_new_dst_corner_from_dst_vertex        (new_dst_facet, 0, previous_edge_midpoint);
            make_new_dst_corner_from_src_corner        (new_dst_facet, 1, src_corner);
            make_new_dst_corner_from_dst_vertex        (new_dst_facet, 2, next_edge_midpoint);
            make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 3, src_facet);
        }
    }

    post_processing();
}

void subdivide(const Geometry& source, Geometry& destination)
{
    Subdivide operation{source, destination};
    operation.build();
}

} // namespace erhe::geometry::operation
