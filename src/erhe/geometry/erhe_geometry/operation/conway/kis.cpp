#include "erhe_geometry/operation/conway/kis.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

class Kis : public Geometry_operation
{
public:
    Kis(const Geometry& source, Geometry& destination, float height);

    void build();
    float m_height;
};

Kis::Kis(const Geometry& source, Geometry& destination, float height)
    : Geometry_operation{source, destination}
    , m_height{height}
{
}

void Kis::build()
{
    make_dst_vertices_from_src_vertices();
    make_facet_centroids();

    for (GEO::index_t src_facet : source_mesh.facets) {
        const GEO::index_t src_facet_corner_count = source_mesh.facets.nb_corners(src_facet);
        if (src_facet_corner_count < 3) {
            continue;
        }
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

    // Offset each centroid vertex along the face normal by height
    if (m_height != 0.0f) {
        for (GEO::index_t src_facet : source_mesh.facets) {
            if (src_facet >= m_src_facet_centroid_to_dst_vertex.size()) {
                continue;
            }
            const GEO::index_t dst_vertex = m_src_facet_centroid_to_dst_vertex[src_facet];
            if (dst_vertex == GEO::NO_INDEX) {
                continue;
            }
            const GEO::vec3f normal = mesh_facet_normalf(source_mesh, src_facet);
            const GEO::vec3f pos = get_pointf(destination_mesh.vertices, dst_vertex);
            set_pointf(destination_mesh.vertices, dst_vertex, pos + m_height * normal);
        }
    }
}

void kis(const Geometry& source, Geometry& destination, float height)
{
    Kis operation{source, destination, height};
    operation.build();
}

} // namespace erhe::geometry::operation
