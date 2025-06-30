#include "erhe_geometry/operation/catmull_clark_subdivision.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::geometry::operation {

class Catmull_clark_subdivision : public Geometry_operation
{
public:
    Catmull_clark_subdivision(const Geometry& source, Geometry& destination);
    void build();
};

// E = average of two neighboring facet vertices and original endpoints
//
// Compute P'             F = average F of all n facet vertices for facets touching P
//                        R = average R of all n edge midpoints for edges touching P
//      F + 2R + (n-3)P   P = old point location
// P' = ----------------
//            n           -> F weight is     1/n
//                        -> R weight is     2/n
//      F   2R   (n-3)P   -> P weight is (n-3)/n
// P' = - + -- + ------
//      n    n      n
//
// For each corner in the src facet, add one quad
// (centroid, previous edge 'edge midpoint', corner, next edge 'edge midpoint')
Catmull_clark_subdivision::Catmull_clark_subdivision(const Geometry& source, Geometry& destination)
    : Geometry_operation{source, destination}
{
}

void Catmull_clark_subdivision::build()
{
    // TODO At least assert these are available
    // 
    // build_src_vertex_to_src_corners();
    // build_src_edge_to_src_facets();

    //                       (n-3)P
    // Make initial P's with ------
    //                          n
    {
        for (GEO::index_t vertex : source_mesh.vertices) {
            const std::vector<GEO::index_t>& corners = source.get_vertex_corners(vertex);
            if (corners.size() >= 3) {
                const float n = static_cast<float>(corners.size());
                // n = 0   -> centroid points, safe to skip
                // n = 1,2 -> ?
                // n = 3   -> ?
                const float weight = (n - 3.0f) / n;
                make_new_dst_vertex_from_src_vertex(weight, vertex);
            } else {
                make_new_dst_vertex_from_src_vertex(1.0f, vertex);
            }
        }
    }

    // Make new edge midpoints
    // "average of two neighboring facet vertices and original endpoints"
    // Add midpoint (R) to each new endpoints
    //   R = average R of all n edge midpoints for edges touching P
    //  2R  we add both edge end points with weight 1 so total edge weight is 2
    //  --
    //   n
    {
        ERHE_VERIFY(m_src_edge_to_dst_vertex.empty());
        GEO::index_t new_dst_vertex = destination_mesh.vertices.create_vertices(source_mesh.edges.nb());
        for (GEO::index_t src_edge : source_mesh.edges) {
            const GEO::index_t src_vertex_a = source_mesh.edges.vertex(src_edge, 0);
            const GEO::index_t src_vertex_b = source_mesh.edges.vertex(src_edge, 1);
            const std::pair<GEO::index_t, GEO::index_t> src_edge_key = std::make_pair(src_vertex_a, src_vertex_b);
            m_src_edge_to_dst_vertex[src_edge_key].push_back(new_dst_vertex);
            add_vertex_source(new_dst_vertex, 1.0f, src_vertex_a);
            add_vertex_source(new_dst_vertex, 1.0f, src_vertex_b);

            for (GEO::index_t src_facet : source.get_edge_facets(src_edge)) {
                const GEO::index_t src_facet_corner_count = source_mesh.facets.nb_corners(src_facet);
                const auto weight = 1.0f / static_cast<float>(src_facet_corner_count);
                add_facet_centroid(new_dst_vertex, weight, src_facet);
            }
            const GEO::index_t new_dst_vertex_a = m_vertex_src_to_dst[src_vertex_a];
            const GEO::index_t new_dst_vertex_b = m_vertex_src_to_dst[src_vertex_b];

            const float n_a = static_cast<float>(source.get_vertex_corners(src_vertex_a).size());
            const float n_b = static_cast<float>(source.get_vertex_corners(src_vertex_a).size());
            ERHE_VERIFY(n_a != 0.0f);
            ERHE_VERIFY(n_b != 0.0f);
            const float weight_a = 1.0f / n_a;
            const float weight_b = 1.0f / n_b;
            add_vertex_source(new_dst_vertex_a, weight_a, src_vertex_a);
            add_vertex_source(new_dst_vertex_a, weight_a, src_vertex_b);
            add_vertex_source(new_dst_vertex_b, weight_b, src_vertex_a);
            add_vertex_source(new_dst_vertex_b, weight_b, src_vertex_b);
            ++new_dst_vertex;
        }
    }

    {
        for (GEO::index_t src_facet : source_mesh.facets) {
            make_new_dst_vertex_from_src_facet_centroid(src_facet);

            // Add facet centroids (F) to all corners' vertex sources
            // F = average F of all n facet vertices for faces touching P
            //  F    <- because F is average of all centroids, it adds extra /n
            // ---
            //  n
            const auto corner_weight = 1.0f / static_cast<float>(source_mesh.facets.nb_vertices(src_facet));
            for (GEO::index_t src_corner : source_mesh.facets.corners(src_facet)) {
                const GEO::index_t               src_vertex         = source_mesh.facet_corners.vertex(src_corner);
                const GEO::index_t               dst_vertex         = m_vertex_src_to_dst[src_vertex];
                const std::vector<GEO::index_t>& src_vertex_corners = source.get_vertex_corners(src_vertex);
                const float                      vertex_weight      = 1.0f / static_cast<float>(src_vertex_corners.size());
                add_facet_centroid(dst_vertex, vertex_weight * vertex_weight * corner_weight, src_facet);
            }
        }
    }

    // Subdivide facets, clone (and corners);
    {
        for (GEO::index_t src_facet : source_mesh.facets) {
            for (GEO::index_t local_facet_corner = 0, corner_count = source_mesh.facets.nb_vertices(src_facet); local_facet_corner < corner_count; ++ local_facet_corner) {
                const GEO::index_t prev_local_facet_corner = (local_facet_corner + corner_count - 1) % corner_count;
                const GEO::index_t next_local_facet_corner = (local_facet_corner +                1) % corner_count;
                const GEO::index_t prev_src_corner         = source_mesh.facets.corner(src_facet, prev_local_facet_corner);
                const GEO::index_t src_corner              = source_mesh.facets.corner(src_facet, local_facet_corner);
                const GEO::index_t next_corner             = source_mesh.facets.corner(src_facet, next_local_facet_corner);
                const GEO::index_t prev_src_vertex         = source_mesh.facet_corners.vertex(prev_src_corner);
                const GEO::index_t src_vertex              = source_mesh.facet_corners.vertex(src_corner);
                const GEO::index_t next_src_vertex         = source_mesh.facet_corners.vertex(next_corner);
                const GEO::index_t previous_edge_midpoint  = get_src_edge_new_vertex(prev_src_vertex, src_vertex, 0);
                const GEO::index_t next_edge_midpoint      = get_src_edge_new_vertex(src_vertex, next_src_vertex, 0);
                const GEO::index_t new_dst_facet           = make_new_dst_facet_from_src_facet(src_facet, 4);
                make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 0, src_facet);
                make_new_dst_corner_from_dst_vertex        (new_dst_facet, 1, previous_edge_midpoint);
                make_new_dst_corner_from_src_corner        (new_dst_facet, 2, src_corner);
                make_new_dst_corner_from_dst_vertex        (new_dst_facet, 3, next_edge_midpoint);
            }
        }
    }

    post_processing();
}

void catmull_clark_subdivision(const Geometry& source, Geometry& destination)
{
    Catmull_clark_subdivision operation{source, destination};
    operation.build();
}

} // namespace erhe::geometry::operation
