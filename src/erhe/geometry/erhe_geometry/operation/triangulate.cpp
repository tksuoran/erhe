#include "erhe_geometry/operation/triangulate.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

class Triangulate : public Geometry_operation
{
public:
    Triangulate(const Geometry& source, Geometry& destination);

    void build();
};

Triangulate::Triangulate(const Geometry& source, Geometry& destination)
    : Geometry_operation{source, destination}
{
}

void Triangulate::build()
{
    make_dst_vertices_from_src_vertices();
    make_facet_centroids();

    for (const GEO::index_t src_facet : source_mesh.facets) {
        const GEO::index_t src_corner_count = source_mesh.facets.nb_corners(src_facet);
        if (src_corner_count == 3) {
            const GEO::index_t new_dst_facet = make_new_dst_facet_from_src_facet(src_facet, 3);
            add_facet_corners(new_dst_facet, src_facet);
            continue;
        }

        for (GEO::index_t local_src_facet_corner = 0; local_src_facet_corner < src_corner_count; ++local_src_facet_corner) {
            const GEO::index_t new_dst_facet   = make_new_dst_facet_from_src_facet(src_facet, 3);
            const GEO::index_t src_corner      = source_mesh.facets.corner(src_facet, local_src_facet_corner);
            const GEO::index_t next_src_corner = source_mesh.facets.corner(src_facet, (local_src_facet_corner + 1) % src_corner_count);
            make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 0, src_facet);
            make_new_dst_corner_from_src_corner        (new_dst_facet, 1, src_corner);
            make_new_dst_corner_from_src_corner        (new_dst_facet, 2, next_src_corner);
        }
    }

    post_processing();
}

void triangulate(const Geometry& source, Geometry& destination)
{
    Triangulate operation{source, destination};
    operation.build();
}

} // namespace erhe::geometry::operation
