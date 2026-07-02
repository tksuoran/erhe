#pragma once

namespace erhe::geometry::operation {

// Post-processing level a geometry operation runs after building its result.
// full_default regenerates render attributes (smooth vertex normals, facet
// texture coordinates) in addition to rebuilding structure; structural_only
// rebuilds just connectivity, edges and facet centroids. Iterated chains
// (e.g. the editor's subdivide node) request structural_only for the
// intermediate iterations, whose normals / texture coordinates would be
// discarded and re-derived from positions by the next iteration anyway; the
// FINAL iteration must keep full_default so the result carries them. The
// final result is identical either way: positions and preserved
// (interpolated) attribute channels do not depend on the regenerated
// channels, and the final full post-processing overwrites the regenerated
// channels completely (see doc/catmull_clark.md, item 11).
enum class Post_processing : unsigned int {
    full_default = 0,
    structural_only,
};

} // namespace erhe::geometry::operation
