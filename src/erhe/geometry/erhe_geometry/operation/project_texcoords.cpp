#include "erhe_geometry/operation/project_texcoords.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

#include <geogram/mesh/mesh.h>

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace erhe::geometry::operation {

namespace {

class Project_texcoords : public Geometry_operation
{
public:
    Project_texcoords(const Geometry& source, Geometry& destination, const Project_texcoords_parameters& parameters)
        : Geometry_operation{source, destination}
        , m_parameters      {parameters}
    {
    }

    void build();

private:
    const Project_texcoords_parameters& m_parameters;
};

void Project_texcoords::build()
{
    destination.get_attributes().unbind();
    destination_mesh.copy(source_mesh, true);
    destination.get_attributes().bind();
    copy_mesh_attributes();

    // Local-space bounds drive the normalization so one projected tile spans
    // the whole mesh regardless of its size.
    glm::vec3 bounds_min{ std::numeric_limits<float>::max()};
    glm::vec3 bounds_max{-std::numeric_limits<float>::max()};
    for (GEO::index_t vertex : destination_mesh.vertices) {
        const GEO::vec3f p = get_pointf(destination_mesh.vertices, vertex);
        const glm::vec3  position{p.x, p.y, p.z};
        bounds_min = glm::min(bounds_min, position);
        bounds_max = glm::max(bounds_max, position);
    }
    const glm::vec3 extent = glm::max(bounds_max - bounds_min, glm::vec3{1.0e-9f});
    const glm::vec3 center = 0.5f * (bounds_min + bounds_max);

    const int axis   = std::clamp(m_parameters.axis, 0, 2);
    const int u_axis = (axis + 1) % 3;
    const int v_axis = (axis + 2) % 3;

    const auto project = [&](const glm::vec3& p) -> glm::vec2 {
        switch (m_parameters.projection) {
            case Texcoord_projection::cylindrical: {
                const float u = 0.5f + std::atan2(p[v_axis] - center[v_axis], p[u_axis] - center[u_axis]) / glm::two_pi<float>();
                const float v = (p[axis] - bounds_min[axis]) / extent[axis];
                return glm::vec2{u, v};
            }
            case Texcoord_projection::spherical: {
                // Normalize per-axis so squashed meshes still span the poles.
                const glm::vec3 d = (p - center) / (0.5f * extent);
                const float u = 0.5f + std::atan2(d[v_axis], d[u_axis]) / glm::two_pi<float>();
                const float length = glm::length(d);
                const float v = (length > 1.0e-9f)
                    ? std::acos(std::clamp(d[axis] / length, -1.0f, 1.0f)) / glm::pi<float>()
                    : 0.5f;
                return glm::vec2{u, v};
            }
            case Texcoord_projection::planar:
            default: {
                return glm::vec2{
                    (p[u_axis] - bounds_min[u_axis]) / extent[u_axis],
                    (p[v_axis] - bounds_min[v_axis]) / extent[v_axis]
                };
            }
        }
    };

    const bool has_azimuth_seam = m_parameters.projection != Texcoord_projection::planar;
    Mesh_attributes& attributes = destination.get_attributes();
    std::vector<glm::vec2> facet_uvs;
    for (GEO::index_t facet : destination_mesh.facets) {
        const GEO::index_t corner_count = destination_mesh.facets.nb_corners(facet);
        facet_uvs.clear();
        float u_min = std::numeric_limits<float>::max();
        float u_max = -std::numeric_limits<float>::max();
        for (GEO::index_t local = 0; local < corner_count; ++local) {
            const GEO::index_t corner = destination_mesh.facets.corner(facet, local);
            const GEO::index_t vertex = destination_mesh.facet_corners.vertex(corner);
            const GEO::vec3f   p      = get_pointf(destination_mesh.vertices, vertex);
            const glm::vec2    uv     = project(glm::vec3{p.x, p.y, p.z});
            facet_uvs.push_back(uv);
            u_min = std::min(u_min, uv.x);
            u_max = std::max(u_max, uv.x);
        }
        // Azimuth wrap: a facet spanning the atan2 seam mixes u ~ 0 with
        // u ~ 1; lift the low side so the facet stays contiguous (the
        // repeat-wrap sampler brings it back into range).
        if (has_azimuth_seam && ((u_max - u_min) > 0.5f)) {
            for (glm::vec2& uv : facet_uvs) {
                if (uv.x < 0.5f) {
                    uv.x += 1.0f;
                }
            }
        }
        for (GEO::index_t local = 0; local < corner_count; ++local) {
            const GEO::index_t corner = destination_mesh.facets.corner(facet, local);
            const glm::vec2    uv     = facet_uvs[local] * m_parameters.scale + m_parameters.offset;
            attributes.corner_texcoord_0.set(corner, GEO::vec2f{uv.x, uv.y});
        }
    }

    // Positions and topology are untouched; nothing regenerates texcoords
    // (process_flag_generate_facet_texture_coordinates would overwrite the
    // projection).
    const uint64_t flags =
        Geometry::process_flag_connect |
        Geometry::process_flag_build_edges |
        Geometry::process_flag_compute_facet_centroids;
    destination.process({.flags = flags});
}

} // anonymous namespace

void project_texcoords(const Geometry& source, Geometry& destination, const Project_texcoords_parameters& parameters)
{
    Project_texcoords operation{source, destination, parameters};
    operation.build();
}

} // namespace erhe::geometry::operation
