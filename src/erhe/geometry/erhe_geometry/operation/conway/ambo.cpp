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
            const std::vector<GEO::index_t>& src_corners     = source.get_vertex_corners(src_vertex);
            const GEO::index_t               src_corner_count = static_cast<GEO::index_t>(src_corners.size());
            if (src_corner_count < 3) {
                log_operation->warn("ambo: skipping vertex {} with {} corners (need >= 3)", src_vertex, src_corner_count);
                continue;
            }
            const GEO::index_t new_dst_facet = destination_mesh.facets.create_polygon(src_corner_count);
            bool facet_ok = true;
            for (GEO::index_t local_facet_corner = 0; local_facet_corner < src_corner_count; local_facet_corner++) {
                const GEO::index_t src_corner = src_corners[local_facet_corner];
                const GEO::index_t src_facet  = source.get_corner_facet(src_corner);
                if (src_facet >= source_mesh.facets.nb()) {
                    log_operation->warn("ambo: vertex {} corner {} references invalid facet {}", src_vertex, src_corner, src_facet);
                    facet_ok = false;
                    break;
                }
                const GEO::index_t src_next_corner = source_mesh.facets.next_corner_around_facet(src_facet, src_corner);
                const GEO::index_t src_next_vertex = source_mesh.facet_corners.vertex(src_next_corner);
                const GEO::index_t edge_midpoint   = get_src_edge_new_vertex(src_vertex, src_next_vertex, 0);
                if (edge_midpoint >= destination_mesh.vertices.nb()) {
                    log_operation->warn("ambo: vertex {} edge midpoint ({}, {}) = {} out of range ({})",
                        src_vertex, src_vertex, src_next_vertex, edge_midpoint, destination_mesh.vertices.nb());
                    facet_ok = false;
                    break;
                }
                log_operation->trace(
                    "ambo src vertex {}, dst facet {}, local corner {}, dst midpoint vertex {}, src facet {}, next vertex {}",
                    src_vertex, new_dst_facet, local_facet_corner, edge_midpoint, src_facet, src_next_vertex
                );
                make_new_dst_corner_from_dst_vertex(new_dst_facet, local_facet_corner, edge_midpoint);
            }
            if (!facet_ok) {
                log_operation->warn("ambo: produced degenerate facet from vertex {}, will be cleaned by sanitize", src_vertex);
            }
        }

        // New faces from old faces, new face corner for each old corner edge midpoint
        for (GEO::index_t src_facet : source_mesh.facets) {
            const GEO::index_t src_facet_corner_count = source_mesh.facets.nb_corners(src_facet);
            if (src_facet_corner_count < 3) {
                log_operation->warn("ambo: skipping facet {} with {} corners (need >= 3)", src_facet, src_facet_corner_count);
                continue;
            }
            const GEO::index_t new_dst_facet = destination_mesh.facets.create_polygon(src_facet_corner_count);
            for (GEO::index_t local_src_facet_corner = 0; local_src_facet_corner < src_facet_corner_count; ++local_src_facet_corner) {
                const GEO::index_t src_corner      = source_mesh.facets.corner(src_facet, local_src_facet_corner);
                const GEO::index_t next_src_corner = source_mesh.facets.corner(src_facet, (local_src_facet_corner + 1) % src_facet_corner_count);
                const GEO::index_t src_vertex      = source_mesh.facet_corners.vertex(src_corner);
                const GEO::index_t next_src_vertex = source_mesh.facet_corners.vertex(next_src_corner);
                const GEO::index_t edge_midpoint   = get_src_edge_new_vertex(src_vertex, next_src_vertex, 0);
                if (edge_midpoint >= destination_mesh.vertices.nb()) {
                    log_operation->warn("ambo: facet {} edge midpoint ({}, {}) = {} out of range ({})",
                        src_facet, src_vertex, next_src_vertex, edge_midpoint, destination_mesh.vertices.nb());
                    continue;
                }
                log_operation->trace(
                    "ambo src facet {}, dst facet {}, local corner {}, dst midpoint vertex {} src vertex {}, src next vertex {}",
                    src_facet, new_dst_facet, local_src_facet_corner, edge_midpoint, src_vertex, next_src_vertex
                );
                make_new_dst_corner_from_dst_vertex(new_dst_facet, local_src_facet_corner, edge_midpoint);
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
