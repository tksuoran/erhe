#include "erhe_geometry/operation/normalize.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

class Normalize : public Geometry_operation
{
public:
    Normalize(const Geometry& source, Geometry& destination);

    void build();
};

Normalize::Normalize(const Geometry& source, Geometry& destination)
    : Geometry_operation{source, destination}
{
}

void Normalize::build()
{
    destination_mesh.copy(source_mesh, true);
    copy_mesh_attributes();

    for (GEO::index_t vertex : destination_mesh.vertices) {
        GEO::vec3f p = get_pointf(destination_mesh.vertices, vertex);
        set_pointf(destination_mesh.vertices, vertex, GEO::normalize(p));
    }

    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_facet_centroids |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;
    destination.process(flags);
}

void normalize(const Geometry& source, Geometry& destination)
{
    Normalize operation{source, destination};
    operation.build();
}

} // namespace erhe::geometry::operation
