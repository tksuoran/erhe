#include "erhe_geometry/operation/kis.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

class Kis : public Geometry_operation
{
public:
    Kis(const Geometry& source, Geometry& destination);

    void build();
};

Kis::Kis(const Geometry& source, Geometry& destination)
    : Geometry_operation{source, destination}
{
}

void Kis::build()
{
    make_dst_vertices_from_src_vertices();
    make_facet_centroids();

    for (GEO::index_t src_facet : source_mesh.facets) {
        const GEO::index_t src_facet_corner_count = source_mesh.facets.nb_corners(src_facet);
        for (GEO::index_t local_src_facet_corner = 0; local_src_facet_corner < src_facet_corner_count; ++local_src_facet_corner) {
            const GEO::index_t src_corner      = source_mesh.facets.corner(src_facet, local_src_facet_corner);
            const GEO::index_t next_src_corner = source_mesh.facets.corner(src_facet, (local_src_facet_corner + 1) % src_facet_corner_count);
            const GEO::index_t new_dst_facet   = destination_mesh.facets.create_triangles(1);
            make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 0, src_facet);
            make_new_dst_corner_from_src_corner        (new_dst_facet, 1, src_corner);
            make_new_dst_corner_from_src_corner        (new_dst_facet, 2, next_src_corner);
        }
    }

    post_processing();
}

void kis(const Geometry& source, Geometry& destination)
{
    Kis operation{source, destination};
    operation.build();
}

} // namespace erhe::geometry::operation
