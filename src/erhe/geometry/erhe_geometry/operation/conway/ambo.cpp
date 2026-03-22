#include "erhe_geometry/operation/conway/ambo.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/geometry_log.hpp"

namespace erhe::geometry::operation {

class Ambo : public Geometry_operation
{
public:
    Ambo(const Geometry& source, Geometry& destination)
        : Geometry_operation{source, destination}
    {
    }

    void build()
    {
        make_edge_midpoints({ 0.5f });

        // New facets from old vertices, new facet corner for each old point corner edge midpoint
        for (GEO::index_t src_vertex : source_mesh.vertices) {
            const std::vector<GEO::index_t>& src_corners      = source.get_vertex_corners(src_vertex);
            const GEO::index_t               src_corner_count = static_cast<GEO::index_t>(src_corners.size());
            if (src_corner_count < 3) {
                continue;
            }

            // Collect all midpoints before creating the facet
            std::vector<GEO::index_t> midpoints;
            midpoints.reserve(src_corner_count);
            bool all_valid = true;
            for (GEO::index_t i = 0; i < src_corner_count; ++i) {
                const GEO::index_t src_corner      = src_corners[i];
                const GEO::index_t src_facet       = source.get_corner_facet(src_corner);
                if (src_facet >= source_mesh.facets.nb()) {
                    all_valid = false;
                    break;
                }
                const GEO::index_t src_next_corner = source_mesh.facets.next_corner_around_facet(src_facet, src_corner);
                const GEO::index_t src_next_vertex = source_mesh.facet_corners.vertex(src_next_corner);
                const GEO::index_t edge_midpoint   = get_src_edge_new_vertex(src_vertex, src_next_vertex, 0);
                if (edge_midpoint == GEO::NO_VERTEX) {
                    all_valid = false;
                    break;
                }
                midpoints.push_back(edge_midpoint);
            }
            if (!all_valid) {
                continue;
            }

            const GEO::index_t new_dst_facet = destination_mesh.facets.create_polygon(src_corner_count);
            for (GEO::index_t i = 0; i < src_corner_count; ++i) {
                make_new_dst_corner_from_dst_vertex(new_dst_facet, i, midpoints[i]);
            }
        }

        // New faces from old faces, new face corner for each old corner edge midpoint
        for (GEO::index_t src_facet : source_mesh.facets) {
            const GEO::index_t src_facet_corner_count = source_mesh.facets.nb_corners(src_facet);
            if (src_facet_corner_count < 3) {
                continue;
            }

            // Collect all midpoints before creating the facet
            std::vector<GEO::index_t> midpoints;
            midpoints.reserve(src_facet_corner_count);
            bool all_valid = true;
            for (GEO::index_t i = 0; i < src_facet_corner_count; ++i) {
                const GEO::index_t src_corner      = source_mesh.facets.corner(src_facet, i);
                const GEO::index_t next_src_corner = source_mesh.facets.corner(src_facet, (i + 1) % src_facet_corner_count);
                const GEO::index_t src_vertex      = source_mesh.facet_corners.vertex(src_corner);
                const GEO::index_t next_src_vertex = source_mesh.facet_corners.vertex(next_src_corner);
                const GEO::index_t edge_midpoint   = get_src_edge_new_vertex(src_vertex, next_src_vertex, 0);
                if (edge_midpoint == GEO::NO_VERTEX) {
                    all_valid = false;
                    break;
                }
                midpoints.push_back(edge_midpoint);
            }
            if (!all_valid) {
                continue;
            }

            const GEO::index_t new_dst_facet = destination_mesh.facets.create_polygon(src_facet_corner_count);
            for (GEO::index_t i = 0; i < src_facet_corner_count; ++i) {
                make_new_dst_corner_from_dst_vertex(new_dst_facet, i, midpoints[i]);
            }
        }

        post_processing();
    }
};


void ambo(const Geometry& source, Geometry& destination)
{
    Ambo operation{source, destination};
    operation.build();
}

} // namespace erhe::geometry::operation
