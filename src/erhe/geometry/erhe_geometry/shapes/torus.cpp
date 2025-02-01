#include "erhe_geometry/shapes/torus.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_profile/profile.hpp"

#include <geogram/mesh/mesh.h>

#include <map>
#include <vector>

namespace erhe::geometry::shapes {

class Torus_builder
{
public:
    Torus_builder(
        GEO::Mesh&   mesh,
        const double major_radius,
        const double minor_radius,
        const int    major_axis_steps,
        const int    minor_axis_steps
    )
        : mesh            {mesh}
        , attributes      {mesh}
        , major_radius    {major_radius}
        , minor_radius    {minor_radius}
        , major_axis_steps{major_axis_steps}
        , minor_axis_steps{minor_axis_steps}
    {
    }

    void build()
    {
        ERHE_PROFILE_FUNCTION();

        // X = anisotropy strength
        // Y = apply texture coordinate to anisotropy
        // TODO Make this default, used whan not present, to avoid having to set here?
        const GEO::vec2f anisotropic_no_texcoord{1.0f, 0.0f}; // all of torus

        GEO::index_t vertex = mesh.vertices.create_vertices(major_axis_steps * minor_axis_steps);
        for (int major = 0; major < major_axis_steps; ++major) {
            const auto rel_major = static_cast<double>(major) / static_cast<double>(major_axis_steps);
            for (int minor = 0; minor < minor_axis_steps; ++minor) {
                const auto rel_minor = static_cast<double>(minor) / static_cast<double>(minor_axis_steps);
                torus_vertex(vertex, rel_major, rel_minor);
                key_to_vertex.push_back(vertex);
                vertex++;
            }
        }

        GEO::index_t facet = 0;
        {
            ERHE_PROFILE_SCOPE("Torus_builder create_quads");
            facet = mesh.facets.create_quads(major_axis_steps * minor_axis_steps);
        }
        for (int major = 0; major < major_axis_steps; ++major) {
            const int  next_major         = (major + 1);
            const auto rel_major          = static_cast<double>(major     ) / static_cast<double>(major_axis_steps);
            const auto rel_next_major     = static_cast<double>(next_major) / static_cast<double>(major_axis_steps);
            const auto rel_centroid_major = (rel_major + rel_next_major) / 2.0;

            for (int minor = 0; minor < minor_axis_steps; ++minor) {
                const int  next_minor         = (minor + 1);
                const auto rel_minor          = static_cast<double>(minor     ) / static_cast<double>(minor_axis_steps);
                const auto rel_next_minor     = static_cast<double>(next_minor) / static_cast<double>(minor_axis_steps);
                const auto rel_centroid_minor = (rel_minor + rel_next_minor) / 2.0;

                const Vertex_data  centroid_data = make_vertex_data(rel_centroid_major, rel_centroid_minor);

                make_corner(facet, 0, next_major, minor);
                make_corner(facet, 1, major,      minor);
                make_corner(facet, 2, major,      next_minor);
                make_corner(facet, 3, next_major, next_minor);

                const GEO::vec3 flat_centroid_location = (1.0f / 4.0f) *
                    (
                        mesh.vertices.point(get_vertex(major + 1, minor    )) +
                        mesh.vertices.point(get_vertex(major,     minor    )) +
                        mesh.vertices.point(get_vertex(major,     minor + 1)) +
                        mesh.vertices.point(get_vertex(major + 1, minor + 1))
                    );
                attributes.facet_centroid     .set(facet, GEO::vec3f{flat_centroid_location});
                //attributes.facet_normal       .set(facet, centroid_data.normal);
                attributes.facet_aniso_control.set(facet, anisotropic_no_texcoord);
                ++facet;
            }
        }
    }

private:
    constexpr static double pi = 3.141592653589793238462643383279502884197169399375105820974944592308;
    constexpr static double half_pi = 0.5 * pi;

    class Vertex_data
    {
    public:
        GEO::vec3  position {0.0};
        GEO::vec3f normal   {0.0f};
        GEO::vec4f tangent  {0.0f};
        GEO::vec3f bitangent{0.0f};
        GEO::vec2f texcoord {0.0f};
    };

    GEO::Mesh&      mesh;
    Mesh_attributes attributes;
    double          major_radius;
    double          minor_radius;
    int             major_axis_steps;
    int             minor_axis_steps;
    std::vector<GEO::index_t> key_to_vertex;

    auto make_vertex_data(const double rel_major, const double rel_minor) const -> Vertex_data
    {
        ERHE_PROFILE_FUNCTION();

        const double R         = major_radius;
        const double r         = minor_radius;
        const double theta     = pi * 2.0 * rel_major;
        const double phi       = pi * 2.0 * rel_minor;
        const double sin_theta = std::sin(theta);
        const double cos_theta = std::cos(theta);
        const double sin_phi   = std::sin(phi);
        const double cos_phi   = std::cos(phi);

        const double vx = (R + r * cos_phi) * cos_theta;
        const double vz = (R + r * cos_phi) * sin_theta;
        const double vy =      r * sin_phi;
        const GEO::vec3 V{vx, vy, vz};

        const double tx = -sin_theta;
        const double tz =  cos_theta;
        const double ty = 0.0f;
        const GEO::vec3 T{tx, ty, tz};

        const double bx = -sin_phi * cos_theta;
        const double bz = -sin_phi * sin_theta;
        const double by =  cos_phi;
        const GEO::vec3 B{bx, by, bz};
        const GEO::vec3 N = GEO::normalize(GEO::cross(B, T));

        const GEO::vec3 t_xyz = GEO::normalize(T - N * GEO::dot(N, T));
        const double    t_w   = (GEO::dot(GEO::cross(N, T), B) < 0.0) ? -1.0 : 1.0;
        const GEO::vec3 b_xyz = GEO::normalize(B - N * GEO::dot(N, B));
        //const float      b_w   = (glm::dot(glm::cross(B, N), T) < 0.0f) ? -1.0f : 1.0f;

        const GEO::vec4 tangent = GEO::vec4{t_xyz, t_w};

        return Vertex_data{
            .position  = V,
            .normal    = GEO::vec3f{N},
            .tangent   = GEO::vec4f{tangent},
            .bitangent = GEO::vec3f{b_xyz},
            .texcoord  = GEO::vec2f{static_cast<float>(rel_major), static_cast<float>(rel_minor)}
        };
    }

    void torus_vertex(const GEO::index_t vertex, const double rel_major, const double rel_minor)
    {
        ERHE_PROFILE_FUNCTION();

        const Vertex_data  data                = make_vertex_data(rel_major, rel_minor);
        const bool         is_uv_discontinuity = (rel_major == 1.0) || (rel_minor == 1.0);

        mesh.vertices.point(vertex) = data.position;

        attributes.vertex_normal   .set(vertex, data.normal);
        attributes.vertex_tangent  .set(vertex, data.tangent);
        attributes.vertex_bitangent.set(vertex, data.bitangent);
        if (!is_uv_discontinuity) {
            attributes.vertex_texcoord_0.set(vertex, data.texcoord);
        }
    }

    [[nodiscard]] auto get_vertex(int major, int minor) -> GEO::index_t
    {
        const bool is_major_seam = (major == major_axis_steps);
        const bool is_minor_seam = (minor == minor_axis_steps);

        if (is_major_seam) {
            major = 0;
        }

        if (is_minor_seam) {
            minor = 0;
        }

        return key_to_vertex[
            (
                static_cast<size_t>(major) * static_cast<size_t>(minor_axis_steps)
            ) +
            static_cast<size_t>(minor)
        ];
    }

    void make_corner(const GEO::index_t facet, const GEO::index_t local_facet_corner, int major, int minor)
    {
        ERHE_PROFILE_FUNCTION();

        const auto rel_major           = static_cast<double>(major) / static_cast<double>(major_axis_steps);
        const auto rel_minor           = static_cast<double>(minor) / static_cast<double>(minor_axis_steps);
        const bool is_major_seam       = (major == major_axis_steps);
        const bool is_minor_seam       = (minor == minor_axis_steps);
        const bool is_uv_discontinuity = is_major_seam || is_minor_seam;

        if (is_major_seam) {
            major = 0;
        }

        if (is_minor_seam) {
            minor = 0;
        }

        const GEO::index_t vertex = key_to_vertex[
            (
                static_cast<size_t>(major) * static_cast<size_t>(minor_axis_steps)
            ) +
            static_cast<size_t>(minor)
        ];

        mesh.facets.set_vertex(facet, local_facet_corner, vertex);
        const GEO::index_t corner = mesh.facets.corner(facet, local_facet_corner);

        if (is_uv_discontinuity) {
            const auto t = static_cast<float>(rel_minor);
            const auto s = static_cast<float>(rel_major);
            attributes.corner_texcoord_0.set(corner, GEO::vec2f{s, t});
        }
    }

};

void make_torus(GEO::Mesh& mesh, const double major_radius, const double minor_radius, const int major_axis_steps, const int minor_axis_steps)
{
    ERHE_PROFILE_FUNCTION();

    Torus_builder builder{mesh, major_radius, minor_radius, major_axis_steps, minor_axis_steps};
    builder.build();
}

} // namespace erhe::geometry::shapes
