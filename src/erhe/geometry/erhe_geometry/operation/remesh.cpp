#include "erhe_geometry/operation/remesh.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_remesh.h>     // remesh_smooth
#include <geogram/mesh/mesh_decimate.h>   // mesh_decimate_vertex_clustering
#include <geogram/mesh/mesh_geometry.h>   // set_anisotropy

#include <algorithm>
#include <vector>

namespace erhe::geometry::operation {

namespace {

// Structural flags, plus normal / texture-coordinate regeneration when requested.
[[nodiscard]] auto remesh_process_flags(const bool regenerate_attributes) -> uint64_t
{
    uint64_t flags =
        Geometry::process_flag_connect |
        Geometry::process_flag_build_edges |
        Geometry::process_flag_compute_facet_centroids;
    if (regenerate_attributes) {
        flags |=
            Geometry::process_flag_compute_smooth_vertex_normals |
            Geometry::process_flag_generate_facet_texture_coordinates;
    }
    return flags;
}

} // anonymous namespace

// Surface remeshing (isotropic or anisotropic) via Geogram's centroidal
// Voronoi tessellation. remesh_smooth() needs a non-const, triangulated input
// surface in double precision and writes a brand new surface into M_out, so we
// build a private working copy of the source and let the result replace the
// destination mesh wholesale. Like Repair, no source/destination provenance is
// tracked: post_processing() leaves the Geogram-written positions untouched
// (empty source tables -> zero weight -> skipped) and, when requested,
// regenerates normals and facet texture coordinates.
class Remesh : public Geometry_operation
{
public:
    Remesh(const Geometry& source, Geometry& destination, unsigned int target_point_count, double anisotropy, bool regenerate_attributes)
        : Geometry_operation    {source, destination}
        , m_target_point_count  {target_point_count}
        , m_anisotropy          {anisotropy}
        , m_regenerate_attributes{regenerate_attributes}
    {
    }

    void build();

private:
    unsigned int m_target_point_count;
    double       m_anisotropy;
    bool         m_regenerate_attributes;
};

void Remesh::build()
{
    GEO::Mesh input;
    input.copy(source_mesh, true);
    input.vertices.set_double_precision();
    input.facets.triangulate();

    const bool                anisotropic = (m_anisotropy > 0.0);
    const GEO::coord_index_t  dim         = anisotropic ? GEO::coord_index_t{6} : GEO::coord_index_t{3};
    if (anisotropic) {
        // Raises the embedding to 6D (normals scaled by m_anisotropy) so the
        // CVT elongates triangles along low-curvature directions.
        GEO::set_anisotropy(input, m_anisotropy);
    }

    destination.get_attributes().unbind();
    // remesh_smooth writes the result surface via MeshVertices::assign_points(),
    // which requires double precision; the destination starts single precision.
    destination_mesh.vertices.set_double_precision();
    GEO::remesh_smooth(input, destination_mesh, m_target_point_count, dim);
    if (destination_mesh.vertices.dimension() != 3) {
        // Drop the anisotropic 6D embedding, keep the 3D positions.
        destination_mesh.vertices.set_dimension(3);
    }
    destination_mesh.vertices.set_single_precision();
    destination.get_attributes().bind();

    post_processing(remesh_process_flags(m_regenerate_attributes));
}

void remesh(const Geometry& source, Geometry& destination, unsigned int target_point_count, double anisotropy, bool regenerate_attributes)
{
    Remesh operation{source, destination, target_point_count, anisotropy, regenerate_attributes};
    operation.build();
}

// Vertex-clustering decimation via Geogram. Operates in place on a triangulated
// double-precision copy of the source; the result replaces the destination mesh.
class Decimate : public Geometry_operation
{
public:
    Decimate(const Geometry& source, Geometry& destination, unsigned int nb_bins, bool regenerate_attributes)
        : Geometry_operation     {source, destination}
        , m_nb_bins              {nb_bins}
        , m_regenerate_attributes{regenerate_attributes}
    {
    }

    void build();

private:
    unsigned int m_nb_bins;
    bool         m_regenerate_attributes;
};

void Decimate::build()
{
    destination.get_attributes().unbind();
    destination_mesh.copy(source_mesh, true);
    destination_mesh.vertices.set_double_precision();
    destination_mesh.facets.triangulate();
    // mesh_decimate_vertex_clustering() is a surface (facet) operation: when it
    // clusters and deletes merged vertices it only remaps facet-corner
    // connectivity, never the independent edge store. erhe's processed source
    // meshes carry a populated edge list (Geometry::build_edges()), and copy()
    // brings it along, so the merged vertices leave those edges dangling.
    // MeshVertices::delete_elements() then rewrites the dangling endpoints to
    // NO_VERTEX, and the mesh_repair() that runs at the end of decimation
    // asserts in MeshVertices::remove_isolated() when it dereferences
    // to_delete[NO_VERTEX]. The edges are irrelevant to decimation and
    // post_processing() rebuilds them, so drop them from the working mesh first.
    destination_mesh.edges.clear();
    GEO::mesh_decimate_vertex_clustering(destination_mesh, m_nb_bins, GEO::MESH_DECIMATE_DEFAULT);
    destination_mesh.vertices.set_single_precision();
    destination.get_attributes().bind();

    post_processing(remesh_process_flags(m_regenerate_attributes));
}

void decimate(const Geometry& source, Geometry& destination, unsigned int nb_bins, bool regenerate_attributes)
{
    Decimate operation{source, destination, nb_bins, regenerate_attributes};
    operation.build();
}

// Topology-preserving Laplacian (umbrella-operator) smoothing using Taubin's
// lambda/mu scheme. Geogram's GEO::mesh_smooth() solves a global least-squares
// Laplacian system that locks only vertices flagged in a "selection" attribute;
// with no locked vertices (the case for the editor's typical closed meshes,
// which have no border) that system's null space is the constant (collapse to a
// point) solution, so it would collapse the mesh. The explicit Taubin form below
// is well posed for both open and closed meshes and barely shrinks volume, while
// keeping the original polygon topology (no triangulation, no new vertices), so
// the source attributes stay valid and can be preserved.
class Smooth : public Geometry_operation
{
public:
    Smooth(const Geometry& source, Geometry& destination, unsigned int iterations, float strength, bool regenerate_attributes)
        : Geometry_operation     {source, destination}
        , m_iterations           {iterations}
        , m_strength             {strength}
        , m_regenerate_attributes{regenerate_attributes}
    {
    }

    void build();

private:
    unsigned int m_iterations;
    float        m_strength;
    bool         m_regenerate_attributes;
};

void Smooth::build()
{
    destination.get_attributes().unbind();
    destination_mesh.copy(source_mesh, true);
    destination_mesh.vertices.set_double_precision();

    const double lambda = static_cast<double>(m_strength);
    if ((m_iterations > 0) && (lambda > 0.0)) {
        // Taubin lambda/mu: alternate a shrinking Laplacian step (lambda > 0) with
        // an inflating step (mu < -lambda) so volume is preserved. mu is derived
        // from lambda via the pass-band frequency k_PB: 1/lambda + 1/mu = k_PB.
        const double k_pb = 0.1;
        const double mu   = 1.0 / (k_pb - (1.0 / lambda)); // negative, |mu| > lambda for lambda < 1/k_PB

        const GEO::index_t        vertex_count = destination_mesh.vertices.nb();
        std::vector<GEO::vec3>    neighbor_sum(vertex_count);
        std::vector<GEO::index_t> neighbor_count(vertex_count);

        const unsigned int pass_count = m_iterations * 2u;
        for (unsigned int pass = 0; pass < pass_count; ++pass) {
            const double factor = ((pass % 2u) == 0u) ? lambda : mu;
            std::fill(neighbor_sum.begin(),   neighbor_sum.end(),   GEO::vec3{0.0, 0.0, 0.0});
            std::fill(neighbor_count.begin(), neighbor_count.end(), GEO::index_t{0});

            // Uniform umbrella operator: each directed facet edge (corner -> next)
            // contributes its endpoint w as a neighbor of v.
            for (GEO::index_t facet : destination_mesh.facets) {
                const GEO::index_t corner_count = destination_mesh.facets.nb_corners(facet);
                for (GEO::index_t local_corner = 0; local_corner < corner_count; ++local_corner) {
                    const GEO::index_t corner      = destination_mesh.facets.corner(facet, local_corner);
                    const GEO::index_t next_corner = destination_mesh.facets.next_corner_around_facet(facet, corner);
                    const GEO::index_t v           = destination_mesh.facet_corners.vertex(corner);
                    const GEO::index_t w           = destination_mesh.facet_corners.vertex(next_corner);
                    neighbor_sum[v] += destination_mesh.vertices.point(w);
                    ++neighbor_count[v];
                }
            }

            for (GEO::index_t v : destination_mesh.vertices) {
                if (neighbor_count[v] == 0) {
                    continue;
                }
                const GEO::vec3 average = (1.0 / static_cast<double>(neighbor_count[v])) * neighbor_sum[v];
                GEO::vec3&      point   = destination_mesh.vertices.point(v);
                point = point + factor * (average - point);
            }
        }
    }

    destination_mesh.vertices.set_single_precision();
    destination.get_attributes().bind();

    post_processing(remesh_process_flags(m_regenerate_attributes));
}

void smooth(const Geometry& source, Geometry& destination, unsigned int iterations, float strength, bool regenerate_attributes)
{
    Smooth operation{source, destination, iterations, strength, regenerate_attributes};
    operation.build();
}

} // namespace erhe::geometry::operation
