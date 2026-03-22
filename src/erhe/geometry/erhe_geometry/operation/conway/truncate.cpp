#include "erhe_geometry/operation/conway/truncate.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

class Truncate : public Geometry_operation
{
public:
    Truncate(const Geometry& source, Geometry& destination);

    void build();
};

Truncate::Truncate(const Geometry& source, Geometry& destination)
    : Geometry_operation{source, destination}
{
}

void Truncate::build()
{
    // Trisect each old edge by generating two new points.
    const float t0 = 1.0f / 3.0f;
    const float t1 = 2.0f / 3.0f;

    // TODO At least assert these are available
    // 
    // build_src_vertex_to_src_corners();
    // build_src_corner_to_src_facet();
    make_edge_midpoints( {t0, t1} );

    // New facets from old vertices, new facet corner for each old vertex corner edge
    // 'midpoint' that is closest to the corner
    for (GEO::index_t src_vertex : source_mesh.vertices) {
        const std::vector<GEO::index_t>& src_corners      = source.get_vertex_corners(src_vertex);
        const GEO::index_t               src_corner_count = static_cast<GEO::index_t>(src_corners.size());
        if (src_corner_count < 3) {
            continue;
        }

        // Collect all edge midpoints before creating the facet
        std::vector<GEO::index_t> midpoints;
        midpoints.reserve(src_corner_count);
        bool all_valid = true;
        for (GEO::index_t i = 0; i < src_corner_count; ++i) {
            const GEO::index_t src_corner = src_corners[i];
            const GEO::index_t src_facet  = source.get_corner_facet(src_corner);
            if (src_facet >= source_mesh.facets.nb()) {
                all_valid = false;
                break;
            }
            const GEO::index_t next_src_corner = source_mesh.facets.next_corner_around_facet(src_facet, src_corner);
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

        const GEO::index_t new_dst_facet = destination_mesh.facets.create_polygon(src_corner_count);
        for (GEO::index_t i = 0; i < src_corner_count; ++i) {
            make_new_dst_corner_from_dst_vertex(new_dst_facet, i, midpoints[i]);
        }
    }

    // New faces from old faces, new face corner for each old corner edge 'midpoint'
    for (const GEO::index_t src_facet : source_mesh.facets) {
        const GEO::index_t src_corner_count = source_mesh.facets.nb_corners(src_facet);
        if (src_corner_count < 3) {
            continue;
        }

        // Collect all edge midpoint pairs before creating the facet
        std::vector<std::pair<GEO::index_t, GEO::index_t>> edge_pairs;
        edge_pairs.reserve(src_corner_count);
        bool all_valid = true;
        for (GEO::index_t i = 0; i < src_corner_count; ++i) {
            const GEO::index_t src_corner      = source_mesh.facets.corner(src_facet, i);
            const GEO::index_t next_src_corner = source_mesh.facets.corner(src_facet, (i + 1) % src_corner_count);
            const GEO::index_t src_vertex      = source_mesh.facet_corners.vertex(src_corner);
            const GEO::index_t next_src_vertex = source_mesh.facet_corners.vertex(next_src_corner);
            GEO::index_t a = get_src_edge_new_vertex(src_vertex, next_src_vertex, 0);
            GEO::index_t b = get_src_edge_new_vertex(src_vertex, next_src_vertex, 1);
            if (a == GEO::NO_VERTEX || b == GEO::NO_VERTEX) {
                all_valid = false;
                break;
            }
            edge_pairs.emplace_back(a, b);
        }
        if (!all_valid) {
            continue;
        }

        const GEO::index_t new_dst_facet = destination_mesh.facets.create_polygon(src_corner_count * 2);
        GEO::index_t dst_corner = 0;
        for (const auto& [a, b] : edge_pairs) {
            make_new_dst_corner_from_dst_vertex(new_dst_facet, dst_corner++, a);
            make_new_dst_corner_from_dst_vertex(new_dst_facet, dst_corner++, b);
            //// debug_src_facet_entry.corners.push_back(a);
            //// debug_src_facet_entry.corners.push_back(b);
        }
        //// debug_src_facet_entries.push_back(debug_src_facet_entry);
    }

    post_processing();
}

void truncate(const Geometry& source, Geometry& destination)
{
    Truncate operation{source, destination};
    operation.build();
}

} // namespace erhe::geometry::operation
