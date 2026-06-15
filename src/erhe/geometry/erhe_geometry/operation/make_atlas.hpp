#pragma once

#include <cstddef>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

// Chart parameterizer used to flatten each chart. Mirrors GEO::ChartParameterizer
// so the editor does not need to include Geogram parameterization headers.
enum class Atlas_parameterizer {
    projection,    // projection on least-squares fitted plane
    lscm,          // Least Squares Conformal Maps
    spectral_lscm, // spectral LSCM (less distorted than lscm)
    abf            // Angle-Based Flattening++ (best quality, Geogram default)
};

// Packer used to organize the charts in texture space. Mirrors GEO::ChartPacker.
enum class Atlas_packer {
    none,   // no packing
    tetris, // built-in "Tetris" packer
    xatlas  // XAtlas library (Geogram default)
};

// Generate a UV texture atlas for the source surface using Geogram's
// mesh_make_atlas() and write the resulting per-corner UVs into the destination's
// corner texture-coordinate channel selected by usage_index (0 or 1), overwriting
// whatever was in that channel. Topology and every other attribute (including the
// other texture-coordinate channel, normals and colors) are preserved.
//
// hard_angles_threshold: edges whose dihedral angle exceeds this (in degrees)
//                        become chart boundaries.
void make_atlas(
    const Geometry&     source,
    Geometry&           destination,
    std::size_t         usage_index,
    double              hard_angles_threshold,
    Atlas_parameterizer parameterizer,
    Atlas_packer        packer);

} // namespace erhe::geometry::operation
