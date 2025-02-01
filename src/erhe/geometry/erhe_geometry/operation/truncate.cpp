#include "erhe_geometry/operation/truncate.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

#include <fmt/format.h>

#include <geogram/mesh/mesh_geometry.h>

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
        const GEO::index_t               new_dst_facet    = destination_mesh.facets.create_polygon(src_corner_count);

        //// Debug_src_vertex_entry debug_src_vertex_entry;
        //// debug_src_vertex_entry.src_vertex = src_vertex;
        //// debug_src_vertex_entry.dst_facet = new_dst_facet;
        for (GEO::index_t local_src_vertex_corner = 0; local_src_vertex_corner < src_corner_count; ++local_src_vertex_corner) {
            const GEO::index_t src_corner      = src_corners[local_src_vertex_corner];
            const GEO::index_t src_facet       = source.get_corner_facet(src_corner);
            const GEO::index_t next_src_corner = source_mesh.facets.next_corner_around_facet(src_facet, src_corner);
            const GEO::index_t next_src_vertex = source_mesh.facet_corners.vertex(next_src_corner);
            const GEO::index_t edge_slot       = 0;
            const GEO::index_t edge_midpoint   = get_src_edge_new_vertex(src_vertex, next_src_vertex, edge_slot);
            make_new_dst_corner_from_dst_vertex(new_dst_facet, local_src_vertex_corner, edge_midpoint);            
            //// debug_src_vertex_entry.corners.push_back(edge_midpoint);
        }
        //// debug_src_vertex_entries.push_back(debug_src_vertex_entry);
    }

    // New faces from old faces, new face corner for each old corner edge 'midpoint'
    for (const GEO::index_t src_facet : source_mesh.facets) {
        const GEO::index_t src_corner_count = source_mesh.facets.nb_corners(src_facet);
        const GEO::index_t new_dst_facet    = destination_mesh.facets.create_polygon(src_corner_count * 2);
        GEO::index_t dst_corner = 0;
        //// Debug_src_facet_entry debug_src_facet_entry;
        //// debug_src_facet_entry.src_facet = src_facet;
        //// debug_src_facet_entry.dst_facet = new_dst_facet;
        for (GEO::index_t local_src_facet_corner = 0; local_src_facet_corner < src_corner_count; ++local_src_facet_corner) {
            const GEO::index_t src_corner      = source_mesh.facets.corner(src_facet, local_src_facet_corner);
            const GEO::index_t next_src_corner = source_mesh.facets.corner(src_facet, (local_src_facet_corner + 1) % src_corner_count);
            const GEO::index_t src_vertex      = source_mesh.facet_corners.vertex(src_corner     );
            const GEO::index_t next_src_vertex = source_mesh.facet_corners.vertex(next_src_corner);
            GEO::index_t a                     = get_src_edge_new_vertex(src_vertex, next_src_vertex, 0);
            GEO::index_t b                     = get_src_edge_new_vertex(src_vertex, next_src_vertex, 1);
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


#if 0
    struct Debug_src_vertex_entry
    {
        GEO::index_t src_vertex;
        GEO::index_t dst_facet;
        std::vector<GEO::index_t> corners;
    };
    std::vector<Debug_src_vertex_entry> debug_src_vertex_entries;

    struct Debug_src_facet_entry
    {
        GEO::index_t src_facet;
        GEO::index_t dst_facet;
        std::vector<GEO::index_t> corners;
    };
    std::vector<Debug_src_facet_entry> debug_src_facet_entries;




    //for (const Debug_src_vertex_entry& entry : debug_src_vertex_entries) {
    //    const GEO::vec3 p_src_vertex         = source_mesh.vertices.point(entry.src_vertex);
    //    const GEO::vec3 p_dst_facet_centroid = GEO::Geom::mesh_facet_center(destination_mesh, entry.dst_facet);
    //    const GEO::vec3 p_dst_facet_normal   = GEO::normalize(GEO::Geom::mesh_facet_normal(destination_mesh, entry.dst_facet));
    //    const glm::vec3 C                    = to_glm_vec3(p_src_vertex);
    //    glm::vec3 prev = C;
    //    int local_corner = 0;
    //    for (GEO::index_t dst_corner_vertex : entry.corners) {
    //        const GEO::vec3 p_dst_corner_vertex = destination_mesh.vertices.point(dst_corner_vertex);
    //        const glm::vec3 V = to_glm_vec3(p_dst_corner_vertex);
    //        destination.add_debug_line(dst_corner_vertex, entry.dst_facet, prev, V, glm::vec4{0.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
    //        destination.add_debug_line(dst_corner_vertex, entry.dst_facet, C,    V, glm::vec4{0.0f, 0.5f, 0.5f, 0.5f}, 1.0f);
    //        destination.add_debug_text(dst_corner_vertex, entry.dst_facet, V, 0xffffffff, fmt::format("{}:{}", dst_corner_vertex, local_corner));
    //        source     .add_debug_line(entry.src_vertex,  GEO::NO_INDEX,   prev, V, glm::vec4{0.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
    //        source     .add_debug_line(entry.src_vertex,  GEO::NO_INDEX,   C,    V, glm::vec4{0.0f, 0.5f, 0.5f, 0.5f}, 1.0f);
    //        source     .add_debug_text(entry.src_vertex,  GEO::NO_INDEX,   V, 0xffffffff, fmt::format("{}:{}", entry.src_vertex, local_corner));
    //        prev = V;
    //        ++local_corner;
    //    }
    //}
    for (const Debug_src_facet_entry& entry : debug_src_facet_entries) {
        const GEO::vec3 p_src_facet_centroid = GEO::Geom::mesh_facet_center(source_mesh, entry.src_facet);
        const glm::vec3 C                    = to_glm_vec3(p_src_facet_centroid);
        glm::vec3 prev = C;
        int local_corner = 0;
        for (GEO::index_t dst_corner_vertex : entry.corners) {
            const GEO::vec3 p_dst_corner_vertex = destination_mesh.vertices.point(dst_corner_vertex);
            const glm::vec3 V = to_glm_vec3(p_dst_corner_vertex);
            //destination.add_debug_line(GEO::NO_INDEX, entry.dst_facet, prev, V, glm::vec4{0.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
            destination.add_debug_line(GEO::NO_INDEX, entry.dst_facet, C,    V, glm::vec4{0.0f, 0.5f, 0.5f, 0.5f}, 1.0f);
            destination.add_debug_text(GEO::NO_INDEX, entry.dst_facet, V, 0xffffffff, fmt::format("{}.{}", entry.dst_facet, local_corner));
            //source     .add_debug_line(GEO::NO_INDEX, entry.src_facet, prev, V, glm::vec4{0.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
            source     .add_debug_line(GEO::NO_INDEX, entry.src_facet, C,    V, glm::vec4{0.0f, 0.5f, 0.5f, 0.5f}, 1.0f);
            source     .add_debug_text(GEO::NO_INDEX, entry.src_facet, V, 0xffffffff, fmt::format("{}.{}", entry.src_facet, local_corner));
            prev = V;
            ++local_corner;
        }
    }

#endif
