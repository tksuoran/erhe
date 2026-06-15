#pragma once

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

// Isotropic / anisotropic surface remeshing (Geogram remesh_smooth, CVT).
// anisotropy == 0 -> isotropic remesh (dim 3).
// anisotropy >  0 -> anisotropic remesh (dim 6); larger values elongate
//                    triangles more strongly along low-curvature directions.
// regenerate_attributes: when true, process() recomputes smooth vertex normals
//                    and facet texture coordinates; when false only structural
//                    finalization runs (remesh produces new topology, so the
//                    mesh then has no normals / texture coordinates).
void remesh(const Geometry& source, Geometry& destination, unsigned int target_point_count, double anisotropy, bool regenerate_attributes);

// Vertex-clustering decimation (Geogram mesh_decimate_vertex_clustering).
// nb_bins is the grid resolution: higher -> more detail retained.
// regenerate_attributes: see remesh().
void decimate(const Geometry& source, Geometry& destination, unsigned int nb_bins, bool regenerate_attributes);

// Topology-preserving Taubin lambda/mu Laplacian smoothing of vertex positions.
// iterations is the number of lambda/mu cycles; strength is the per-step Laplacian
// factor lambda in [0,1] (the inflating mu step is derived from it).
// regenerate_attributes: when true, normals and texture coordinates are recomputed
//                    for the moved positions; when false the source attributes are
//                    preserved (valid here because the topology is unchanged).
void smooth(const Geometry& source, Geometry& destination, unsigned int iterations, float strength, bool regenerate_attributes);

} // namespace erhe::geometry::operation
