#include "erhe_geometry/operation/generate_frame_field_tangents.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_log.hpp"

#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_frame_field.h>

#include <cmath>

namespace erhe::geometry::operation {

namespace {

// Compute per-facet / per-corner tangents and bitangents from a Geogram frame
// field. FrameField::create_from_surface_mesh() requires a triangulated,
// adjacency-connected surface (its smoothness term walks facet-facet adjacency and
// its reference-rotation step indexes corners as c/3), so the field is built on a
// throwaway triangulated copy and sampled per destination facet via the field's
// nearest-frame spatial search. The destination keeps its original (possibly n-gon)
// topology and every other attribute.
void compute_frame_field_tangents(GEO::Mesh& mesh, Mesh_attributes& attributes, const double sharp_angle_threshold)
{
    if (mesh.facets.nb() == 0) {
        log_tangent_gen->warn("Frame field tangents: empty mesh; nothing to do");
        return;
    }

    // Build the all-triangle, double-precision, connected surface the frame field
    // solver needs. Mirror the geogram working-copy setup used by remesh().
    GEO::Mesh tri_mesh;
    tri_mesh.copy(mesh, true);
    tri_mesh.vertices.set_double_precision();
    tri_mesh.facets.triangulate();
    tri_mesh.facets.connect();
    if (tri_mesh.facets.nb() == 0) {
        log_tangent_gen->warn("Frame field tangents: triangulated surface is empty; nothing to do");
        return;
    }

    // use_NN_ defaults to true, so create_from_surface_mesh() builds the spatial
    // search that get_nearest_frame() below queries.
    GEO::FrameField frame_field;
    frame_field.create_from_surface_mesh(tri_mesh, false /*volumetric*/, sharp_angle_threshold);

    for (const GEO::index_t facet : mesh.facets) {
        const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
        if (corner_count < 3) {
            continue;
        }

        const GEO::vec3f center = mesh_facet_centerf(mesh, facet);
        const GEO::vec3f N      = GEO::normalize(mesh_facet_normalf(mesh, facet));

        const double query_point[3] = {center.x, center.y, center.z};
        double       frame[9];
        frame_field.get_nearest_frame(query_point, frame);

        // The frame's first vector U is the in-plane 4-symmetry direction; project
        // it onto the facet plane to get the tangent. Fall back to an arbitrary
        // in-plane direction when U is (near) parallel to the facet normal.
        const GEO::vec3f U   = GEO::vec3f{static_cast<float>(frame[0]), static_cast<float>(frame[1]), static_cast<float>(frame[2])};
        GEO::vec3f       T   = U - GEO::dot(U, N) * N;
        const float      len = std::sqrt(GEO::dot(T, T));
        if (len > 1e-6f) {
            T = (1.0f / len) * T;
        } else {
            T = GEO::normalize(GEO::cross(N, min_axis(N)));
        }
        // Right-handed basis (B = N x T), so the stored handedness sign is +1.
        const GEO::vec3f B        = GEO::cross(N, T);
        const GEO::vec4f tangent4 = GEO::vec4f{T, 1.0f};

        attributes.facet_tangent  .set(facet, tangent4);
        attributes.facet_bitangent.set(facet, B);
        for (const GEO::index_t corner : mesh.facets.corners(facet)) {
            attributes.corner_tangent  .set(corner, tangent4);
            attributes.corner_bitangent.set(corner, B);
        }
    }
}

class Generate_frame_field_tangents : public Geometry_operation
{
public:
    Generate_frame_field_tangents(const Geometry& source, Geometry& destination, double sharp_angle_threshold)
        : Geometry_operation     {source, destination}
        , m_sharp_angle_threshold{sharp_angle_threshold}
    {
    }

    void build();

private:
    double m_sharp_angle_threshold;
};

void Generate_frame_field_tangents::build()
{
    destination.get_attributes().unbind();
    destination_mesh.copy(source_mesh, true);
    destination.get_attributes().bind();
    copy_mesh_attributes();

    // Structural finalize first (connectivity / edges / centroids) so the copied
    // topology is consistent; normals and texture coordinates copied from the
    // source are left untouched, then the frame-field tangents are written.
    destination.process(
        {
            .flags =
                erhe::geometry::Geometry::process_flag_connect |
                erhe::geometry::Geometry::process_flag_build_edges |
                erhe::geometry::Geometry::process_flag_compute_facet_centroids
        }
    );

    compute_frame_field_tangents(destination_mesh, destination.get_attributes(), m_sharp_angle_threshold);
}

} // anonymous namespace

void generate_frame_field_tangents(const Geometry& source, Geometry& destination, const double sharp_angle_threshold)
{
    Generate_frame_field_tangents operation{source, destination, sharp_angle_threshold};
    operation.build();
}

} // namespace erhe::geometry::operation
