#include "erhe_geometry/operation/join.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_verify//verify.hpp"

#include <sstream>

namespace erhe::geometry::operation {

class Join : public Geometry_operation
{
public:
    Join(const Geometry& source, Geometry& destination);

    void build();
};

Join::Join(const Geometry& source, Geometry& destination)
    : Geometry_operation{source, destination}
{
}

void Join::build()
{
    make_dst_vertices_from_src_vertices();
    make_facet_centroids();
    // TODO At least assert these are available
    // build_src_edge_to_src_facets();

    for (GEO::index_t src_edge : source_mesh.edges) {
        const std::vector<GEO::index_t>& src_facets = source.get_edge_facets(src_edge);
        if (src_facets.size() != 2) {
            continue;
        }
        const GEO::index_t src_vertex_a = source_mesh.edges.vertex(src_edge, 0);
        const GEO::index_t src_vertex_b = source_mesh.edges.vertex(src_edge, 1);
        const GEO::index_t dst_vertex_a = m_vertex_src_to_dst[src_vertex_a];
        const GEO::index_t dst_vertex_b = m_vertex_src_to_dst[src_vertex_b];
        const GEO::index_t src_facet_l  = src_facets[0];
        const GEO::index_t src_facet_r  = src_facets[1];
        bool l_forward {false}; // a, b
        bool l_backward{false}; // b, a
        std::stringstream l_ss;
        const GEO::index_t l_corner_count = source_mesh.facets.nb_corners(src_facet_l);
        for (GEO::index_t local_src_facet_corner = 0; local_src_facet_corner < l_corner_count; ++local_src_facet_corner) {
            const GEO::index_t prev_src_corner = source_mesh.facets.corner(src_facet_l, (local_src_facet_corner + l_corner_count - 1) % l_corner_count);
            const GEO::index_t src_corner      = source_mesh.facets.corner(src_facet_l, local_src_facet_corner);
            const GEO::index_t next_src_corner = source_mesh.facets.corner(src_facet_l, (local_src_facet_corner + 1) % l_corner_count);
            const GEO::index_t prev_src_vertex = source_mesh.facet_corners.vertex(prev_src_corner);
            const GEO::index_t src_vertex      = source_mesh.facet_corners.vertex(src_corner     );
            const GEO::index_t next_src_vertex = source_mesh.facet_corners.vertex(next_src_corner);
            l_ss << src_vertex << " ";
            if (src_vertex == src_vertex_a) {
                if (prev_src_vertex == src_vertex_b) {
                    l_backward = true;
                    ERHE_VERIFY(l_forward != l_backward);
                }
                if (next_src_vertex == src_vertex_b) {
                    l_forward = true;
                    ERHE_VERIFY(l_forward != l_backward);
                }
            }
        }
        bool r_forward {false};
        bool r_backward{false};
        std::stringstream r_ss;
        const GEO::index_t r_corner_count = source_mesh.facets.nb_corners(src_facet_r);
        for (GEO::index_t local_src_facet_corner = 0; local_src_facet_corner < r_corner_count; ++local_src_facet_corner) {
            const GEO::index_t prev_src_corner = source_mesh.facets.corner(src_facet_r, (local_src_facet_corner + r_corner_count - 1) % r_corner_count);
            const GEO::index_t src_corner      = source_mesh.facets.corner(src_facet_r, local_src_facet_corner);
            const GEO::index_t next_src_corner = source_mesh.facets.corner(src_facet_r, (local_src_facet_corner + 1) % r_corner_count);
            const GEO::index_t prev_src_vertex = source_mesh.facet_corners.vertex(prev_src_corner);
            const GEO::index_t src_vertex      = source_mesh.facet_corners.vertex(src_corner     );
            const GEO::index_t next_src_vertex = source_mesh.facet_corners.vertex(next_src_corner);
            r_ss << src_vertex << " ";
            if (src_vertex == src_vertex_a) {
                if (prev_src_vertex == src_vertex_b) {
                    r_backward = true;
                    ERHE_VERIFY(r_forward != r_backward);
                }
                if (next_src_vertex == src_vertex_b) {
                    r_forward = true;
                    ERHE_VERIFY(r_forward != r_backward);
                }
            }
        }
        ERHE_VERIFY(l_forward != l_backward);
        ERHE_VERIFY(r_forward != r_backward);
        ERHE_VERIFY(l_forward != r_forward);

        const GEO::index_t new_dst_facet = destination_mesh.facets.create_quads(1);
        if (l_forward) {
            make_new_dst_corner_from_dst_vertex        (new_dst_facet, 0, dst_vertex_a);
            make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 1, src_facet_r );
            make_new_dst_corner_from_dst_vertex        (new_dst_facet, 2, dst_vertex_b);
            make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 3, src_facet_l );
        } else {
            make_new_dst_corner_from_dst_vertex        (new_dst_facet, 0, dst_vertex_a);
            make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 1, src_facet_l );
            make_new_dst_corner_from_dst_vertex        (new_dst_facet, 2, dst_vertex_b);
            make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 3, src_facet_r );
        }
    }

    post_processing();
}

void join(const Geometry& source, Geometry& destination)
{
    Join operation{source, destination};
    operation.build();
}


} // namespace erhe::geometry::operation
