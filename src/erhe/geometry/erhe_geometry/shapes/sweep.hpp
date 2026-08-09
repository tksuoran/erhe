#pragma once

#include <glm/glm.hpp>

#include <vector>

namespace GEO { class Mesh; }

namespace erhe::geometry::shapes {

// Sweep a closed 2D cross-section polyline along a 3D bezier spine.
//
// The profile lives in the plane perpendicular to the spine: local x maps to
// the transported "side" axis, local y to the transported "up" axis, and the
// implied +z faces along the spine tangent. A COUNTER-CLOCKWISE profile
// produces outward-facing walls. Frames are parallel-transported along the
// spine (stable on straight runs, unlike Frenet frames).
//
// Sharp profile corners survive as real polyline corners; smooth arcs are
// authored as densely sampled points. taper scales the whole cross-section
// per station (piecewise-linear (t, scale) keys); a final scale near zero
// collapses the last ring into a single tip vertex (pointed leaf tips) and
// suppresses the end cap. profile_end, when non-empty (same point count),
// is linearly morphed toward along the spine. twist rotates the profile
// about the spine, linearly over its length.
class Sweep_parameters
{
public:
    std::vector<glm::vec2> profile;              // closed CCW cross-section polyline, >= 3 points
    std::vector<glm::vec2> profile_end;          // optional morph target (same point count)
    std::vector<glm::vec3> spine;                // bezier control points, >= 2
    int                    spine_steps{16};      // segments along the spine, >= 1
    std::vector<glm::vec2> taper;                // (t, scale) keys, ascending t; empty = 1.0
    float                  twist      {0.0f};    // radians over the full spine
    bool                   start_cap  {true};
    bool                   end_cap    {true};
};

void make_sweep(GEO::Mesh& mesh, const Sweep_parameters& parameters);

} // namespace erhe::geometry::shapes
