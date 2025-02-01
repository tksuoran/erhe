#include "erhe_geometry/operation/reverse.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

class Reverse : public Geometry_operation
{
public:
    Reverse(const Geometry& source, Geometry& destination);

    void build();
};

Reverse::Reverse(const Geometry& source, Geometry& destination)
    : Geometry_operation{source, destination}
{
}

void Reverse::build()
{
    destination_mesh.copy(source_mesh, true);
    copy_mesh_attributes();

    for (GEO::index_t facet : destination_mesh.facets) {
        destination_mesh.facets.flip(facet);
    }

    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_facet_centroids |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;
    destination.process(flags);
}

void reverse(const Geometry& source, Geometry& destination)
{
    Reverse operation{source, destination};
    operation.build();
}

} // namespace erhe::geometry::operation
