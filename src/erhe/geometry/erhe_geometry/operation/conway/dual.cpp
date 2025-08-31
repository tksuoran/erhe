#include "erhe_geometry/operation/conway/dual.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

// Conway dual operator
class Dual : public Geometry_operation
{
public:
    Dual(const Geometry& source, Geometry& destination);

    void build();
};

Dual::Dual(const Geometry& source, Geometry& destination)
    : Geometry_operation{source, destination}
{
}

void Dual::build()
{
    // TODO At least assert these are available
    // build_src_vertex_to_src_corners();
    // build_src_corner_to_src_facet();
    make_facet_centroids();

    // New facets from old verticess, new facet corner for each old vertex corner
    for (GEO::index_t src_vertex : source_mesh.vertices) {
        const std::vector<GEO::index_t>& src_corners = source.get_vertex_corners(src_vertex);
        const GEO::index_t src_corner_count = static_cast<GEO::index_t>(src_corners.size());
        const GEO::index_t new_dst_facet = destination_mesh.facets.create_polygon(src_corner_count);
        for (GEO::index_t local_facet_corner = 0; local_facet_corner < src_corner_count; ++local_facet_corner) {
            const GEO::index_t src_corner = src_corners[local_facet_corner];
            const GEO::index_t src_facet = source.get_corner_facet(src_corner);
            make_new_dst_corner_from_src_facet_centroid(new_dst_facet, local_facet_corner, src_facet);
        }
    }

    post_processing();
}

void dual(const Geometry& source, Geometry& destination)
{
    Dual operation{source, destination};
    operation.build();
}


} // namespace erhe::geometry::operation
