#include "erhe_geometry/operation/smooth_normals.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

class Smooth_normals : public Geometry_operation
{
public:
    Smooth_normals(const Geometry& source, Geometry& destination);

    void build();
};

Smooth_normals::Smooth_normals(const Geometry& source, Geometry& destination)
    : Geometry_operation{source, destination}
{
}

void Smooth_normals::build()
{
    destination.get_attributes().unbind();
    destination_mesh.copy(source_mesh, true);
    destination.get_attributes().bind();
    copy_mesh_attributes();

    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_facet_centroids |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals;
    destination.process({.flags = flags});
}

void smooth_normals(const Geometry& source, Geometry& destination)
{
    Smooth_normals operation{source, destination};
    operation.build();
}

} // namespace erhe::geometry::operation
