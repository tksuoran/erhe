#pragma once

#include <glm/glm.hpp>

#include <array>
#include <vector>

namespace GEO { class Mesh; }
namespace erhe::geometry::shapes { class Sweep_parameters; }

namespace editor {

class Kit_mesh_builder;

// The cross section of a leaf (Q2 of doc/plans/level_kit_more_shapes.md).
enum class Kit_leaf_section : unsigned int {
    flat = 0, // a thin lens
    folded,   // two thin halves sharing the midrib in a V, their outer edges lowered: a feather frond's leaflet rows
    thick,    // a lens of a third of its width: a succulent leaf
    folded_split // folded's former build for comparison: two separate halves crossing at the midrib, capped at the tip
};

// One leaf, frond or blade: a section swept along a cubic Bezier spine, widest from widest_at to widest_end, tapered to a point.
class Kit_leaf
{
public:
    std::array<glm::vec3, 4> spine{};          // the midrib, base to tip
    Kit_leaf_section         section   {Kit_leaf_section::flat};
    float                    width     {0.2f}; // at the widest station, meters
    float                    thickness {0.02f};
    float                    fold      {0.3f}; // folded: the outer edges' drop per meter of half width
    float                    widest_at {0.4f}; // spine parameter of the widest station
    float                    widest_end{0.0f}; // the width holds from widest_at to here (an oblong blade); at most widest_at: one station
    float                    base_width{0.15f}; // share of the width at the base
    float                    roll      {0.0f}; // radians the section turns about the spine: pi / 2 stands the blade on edge
    int                      steps     {5};
    int                      across    {1};    // folded: segments across each half; above 1 the halves arch down toward their edges
};

// Adds the leaf's closed parts to the builder under its current slot (Kit_mesh_builder::add_mesh()); scratch is the
// generator's mesh the sweeps are made in, cleared before each, so its attributes are made once per generator run.
void add_leaf(Kit_mesh_builder& builder, const Kit_leaf& leaf, GEO::Mesh& scratch);

// Sweeps into the scratch mesh, cleared first, and adds the result to the builder.
void add_kit_sweep(Kit_mesh_builder& builder, const erhe::geometry::shapes::Sweep_parameters& parameters, GEO::Mesh& scratch);

} // namespace editor
