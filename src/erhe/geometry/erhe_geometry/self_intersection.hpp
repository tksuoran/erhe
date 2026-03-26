#pragma once

namespace GEO { class Mesh; }

namespace erhe::geometry {

// Checks if any two non-adjacent facets of the mesh intersect.
// Triangulates facets internally (fan triangulation) for the test.
// Returns true if any self-intersection is found.
//
// This is a generic mesh quality check that can be used after any
// geometry operation. Uses O(n^2) pair-wise triangle testing, so
// it is suitable for validation and testing, not real-time use.
auto has_self_intersections(const GEO::Mesh& mesh) -> bool;

} // namespace erhe::geometry
