#pragma once

#include <glm/glm.hpp>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

enum class Texcoord_projection : int {
    planar      = 0,
    cylindrical = 1,
    spherical   = 2
};

class Project_texcoords_parameters
{
public:
    Texcoord_projection projection{Texcoord_projection::planar};
    // planar: the projection plane's normal axis; cylindrical / spherical:
    // the major axis. 0 = X, 1 = Y, 2 = Z.
    int                 axis      {2};
    glm::vec2           scale     {1.0f, 1.0f};
    glm::vec2           offset    {0.0f, 0.0f};
};

// Overwrite corner texcoord channel 0 with a parametric projection computed
// from the mesh's local-space bounds: planar (bbox-normalized coordinates in
// the plane), cylindrical (azimuth around the axis x height along it) or
// spherical (azimuth x polar angle). Positions, topology and every other
// attribute pass through unchanged, so this runs cleanly AFTER deformations
// (lattice, subdivision) whose inherited parametrization is unusable.
void project_texcoords(const Geometry& source, Geometry& destination, const Project_texcoords_parameters& parameters);

} // namespace erhe::geometry::operation
