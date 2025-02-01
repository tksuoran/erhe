#include "erhe_geometry/operation/sqrt3_subdivision.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

class Sqrt3_subdivision : public Geometry_operation
{
public:
    Sqrt3_subdivision(const Geometry& source, Geometry& destination);

    void build();
};

Sqrt3_subdivision::Sqrt3_subdivision(const Geometry& source, Geometry& destination)
    : Geometry_operation{source, destination}
{
}

//  Sqrt(3): Replace edges with two triangles
//  For each corner in the src facet, add one triangle
//  (centroid, corner, opposite centroid)
//
//  Centroids:
//
//  (1) q := 1/3 (p_i + p_j + p_k)
//
//  Refine old vertices:
//
//  (2) S(p) := (1 - alpha_n) p + alpha_n 1/n SUM p_i
//
//  (6) alpha_n = (4 - 2 cos(2Pi/n)) / 9
void Sqrt3_subdivision::build()
{
    constexpr static float pi = 3.141592653589793238462643383279502884197169399375105820974944592308f;

    // TODO At least assert these are available
    // build_src_vertex_to_src_corners();

    for (GEO::index_t src_vertex : source_mesh.vertices) {
        const std::vector<GEO::index_t> src_corners      = source.get_vertex_corners(src_vertex);
        const float                     n                = static_cast<float>(src_corners.size());
        const float                     alpha            = (4.0f - 2.0f * std::cos(2.0f * pi / n)) / 9.0f;
        const float                     alpha_per_n      = alpha / static_cast<float>(n);
        const float                     alpha_complement = 1.0f - alpha;
        const GEO::index_t              new_dst_vertex   = make_new_dst_vertex_from_src_vertex(alpha_complement, src_vertex);
        add_vertex_ring(new_dst_vertex, alpha_per_n, src_vertex);
    }

    make_facet_centroids();

    for (const GEO::index_t src_facet : source_mesh.facets) {
        const GEO::index_t src_corner_count = source_mesh.facets.nb_corners(src_facet);
        for (GEO::index_t local_src_facet_corner = 0; local_src_facet_corner < src_corner_count; ++local_src_facet_corner) {
            const GEO::index_t src_corner         = source_mesh.facets.corner(src_facet, local_src_facet_corner);
            const GEO::index_t local_src_edge     = local_src_facet_corner;
            const GEO::index_t opposite_src_facet = source_mesh.facets.adjacent(src_facet, local_src_edge);
            if (opposite_src_facet == GEO::NO_INDEX) {
                continue;
            }
            const GEO::index_t new_dst_facet = make_new_dst_facet_from_src_facet(src_facet, 3);
            make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 0, src_facet);
            make_new_dst_corner_from_src_corner        (new_dst_facet, 1, src_corner);
            make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 2, opposite_src_facet);
        }
    }

    post_processing();
}

void sqrt3_subdivision(const Geometry& source, Geometry& destination)
{
    Sqrt3_subdivision operation{source, destination};
    operation.build();
}

} // namespace erhe::geometry::operation
