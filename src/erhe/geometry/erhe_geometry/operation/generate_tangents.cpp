#include "erhe_geometry/operation/normalize.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

class Generate_tangents : public Geometry_operation
{
public:
    Generate_tangents(const Geometry& source, Geometry& destination);

    void build();
};

Generate_tangents::Generate_tangents(const Geometry& source, Geometry& destination)
    : Geometry_operation{source, destination}
{
}

void Generate_tangents::build()
{
    destination.get_attributes().unbind();
    destination_mesh.copy(source_mesh, true);
    destination.get_attributes().bind();
    copy_mesh_attributes();

    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_generate_tangents;
    destination.process(flags);
}

void generate_tangents(const Geometry& source, Geometry& destination)
{
    Generate_tangents operation{source, destination};
    operation.build();
}

} // namespace erhe::geometry::operation
