#include "erhe_geometry/shapes/sphere.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_profile/profile.hpp"

#include <geogram/mesh/mesh.h>

#include <cmath>  // for sqrt
#include <map>

namespace erhe::geometry::shapes {

class Sphere_builder
{
public:
    using scalar = float;

    constexpr static scalar pi      = 3.141592653589793238462643383279502884197169399375105820974944592308f;
    constexpr static scalar half_pi = 0.5 * pi;

    class Vertex_data
    {
    public:
        GEO::vec3f position {0.0f, 0.0f, 0.0f};
        GEO::vec3f normal   {0.0f};
        GEO::vec4f tangent  {0.0f};
        GEO::vec3f bitangent{0.0f};
        GEO::vec2f texcoord {0.0f};
    };

    Sphere_builder(GEO::Mesh& mesh, const scalar radius, const int slice_count, const int stack_count)
        : mesh       {mesh}
        , attributes {mesh}
        , radius     {radius}
        , slice_count{slice_count}
        , stack_count{stack_count}
    {
    }

    void build()
    {
        ERHE_PROFILE_FUNCTION();

        // X = anisotropy strength (0.0 .. 1.0)
        // Y = apply texture coordinate to anisotropy (0.0 .. 1.0)
        const GEO::vec2f non_anisotropic        {0.0f, 0.0f}; // Used on tips
        const GEO::vec2f anisotropic_no_texcoord{1.0f, 0.0f}; // Used on lateral surface

        GEO::index_t vertex = mesh.vertices.create_vertices((stack_count - 1) * slice_count + 2);
        for (int stack = 1; stack < stack_count; ++stack) {
            const scalar rel_stack = static_cast<scalar>(stack) / static_cast<scalar>(stack_count);
            for (int slice = 0; slice < slice_count; ++slice) {
                const scalar rel_slice = static_cast<scalar>(slice) / static_cast<scalar>(slice_count);
                sphere_vertex(vertex, rel_slice, rel_stack);
                key_to_vertex[std::make_pair(slice, stack)] = vertex;
                vertex++;
            }
        }

        // Bottom fan
        bottom_vertex = vertex++; //mesh.vertices.create_vertices(1);
        set_pointf(mesh.vertices, bottom_vertex, GEO::vec3f{0.0f, -radius, 0.0f});
        attributes.vertex_normal.set(bottom_vertex, GEO::vec3f{0.0f,-1.0f, 0.0f});
        GEO::index_t facet = mesh.facets.create_triangles(slice_count);
        for (int slice = 0; slice < slice_count; ++slice) {
            const int          stack              = 1;
            const scalar       rel_slice_centroid = (static_cast<scalar>(slice) + scalar{0.5}) / static_cast<scalar>(slice_count);
            const scalar       rel_stack_centroid = (static_cast<scalar>(stack) - scalar{0.5}) / static_cast<scalar>(slice_count);
            const Vertex_data  average_data       = make_vertex_data(rel_slice_centroid, 0.0);
            const GEO::index_t tip_corner         = make_corner(facet, 0, slice, 0);
            make_corner(facet, 1, slice,     stack);
            make_corner(facet, 2, slice + 1, stack);

            const GEO::index_t v0 = get_vertex(slice,     stack);
            const GEO::index_t v1 = get_vertex(slice + 1, stack);

            attributes.corner_normal       .set(tip_corner, average_data.normal);
            attributes.corner_tangent      .set(tip_corner, average_data.tangent);
            attributes.corner_bitangent    .set(tip_corner, average_data.bitangent);
            attributes.corner_texcoord_0   .set(tip_corner, average_data.texcoord);
            attributes.corner_aniso_control.set(tip_corner, non_anisotropic);

            const Vertex_data centroid_data = make_vertex_data(rel_slice_centroid, rel_stack_centroid);
            const auto flat_centroid_location =
                (
                    get_pointf(mesh.vertices, v0) +
                    get_pointf(mesh.vertices, v1) +
                    GEO::vec3f{0.0f, -radius, 0.0f}
                ) / 3.0f;
            attributes.facet_centroid     .set(facet, GEO::vec3f{flat_centroid_location});
            //attributes.facet_normal       .set(facet, centroid_data.normal);
            attributes.facet_aniso_control.set(facet, anisotropic_no_texcoord);
            facet++;
        }

        // Middle quads, bottom up
        facet = mesh.facets.create_quads((stack_count - 2) * slice_count);
        for (int stack = 1; stack < (stack_count - 1); ++stack) {
            const scalar rel_stack_centroid = (static_cast<scalar>(stack) + scalar{0.5}) / static_cast<scalar>(stack_count);

            for (int slice = 0; slice < slice_count; ++slice) {
                const scalar rel_slice_centroid = (static_cast<scalar>(slice) + scalar{0.5}) / static_cast<scalar>(slice_count);

                make_corner(facet, 0, slice + 1, stack    );
                make_corner(facet, 1, slice,     stack    );
                make_corner(facet, 2, slice,     stack + 1);
                make_corner(facet, 3, slice + 1, stack + 1);

                const Vertex_data centroid_data = make_vertex_data(rel_slice_centroid, rel_stack_centroid);
                const auto flat_centroid_location = 
                    (
                        get_pointf(mesh.vertices, get_vertex(slice + 1, stack    )) +
                        get_pointf(mesh.vertices, get_vertex(slice,     stack    )) +
                        get_pointf(mesh.vertices, get_vertex(slice,     stack + 1)) +
                        get_pointf(mesh.vertices, get_vertex(slice + 1, stack + 1))
                    ) / 4.0f;
                attributes.facet_centroid     .set(facet, GEO::vec3f{flat_centroid_location});
                //attributes.facet_normal       .set(facet, centroid_data.normal);
                attributes.facet_aniso_control.set(facet, anisotropic_no_texcoord);
                facet++;
            }
        }

        // Top fan
        top_vertex = vertex++; // mesh.vertices.create_vertices(1);
        set_pointf(mesh.vertices, top_vertex, GEO::vec3f{0.0, radius, 0.0});
        attributes.vertex_normal.set(top_vertex, GEO::vec3f{0.0f, 1.0f, 0.0f});
        facet = mesh.facets.create_triangles(slice_count);
        for (int slice = 0; slice < slice_count; ++slice) {
            const int    stack              = stack_count - 1;
            const scalar rel_slice_centroid = (static_cast<scalar>(slice) + scalar{0.5}) / static_cast<scalar>(slice_count);
            const scalar rel_stack_centroid = (static_cast<scalar>(stack) + scalar{0.5}) / static_cast<scalar>(stack_count);

            const Vertex_data average_data = make_vertex_data(rel_slice_centroid, 1.0);

            make_corner(facet, 0, slice + 1, stack);
            make_corner(facet, 1, slice,     stack);
            const GEO::index_t tip_corner = make_corner(facet, 2, slice, stack_count);

            attributes.corner_normal       .set(tip_corner, average_data.normal);
            attributes.corner_tangent      .set(tip_corner, average_data.tangent);
            attributes.corner_bitangent    .set(tip_corner, average_data.bitangent);
            attributes.corner_texcoord_0   .set(tip_corner, average_data.texcoord);
            attributes.corner_aniso_control.set(tip_corner, non_anisotropic);

            const Vertex_data centroid_data = make_vertex_data(rel_slice_centroid, rel_stack_centroid);

            const GEO::index_t v0 = get_vertex(slice,     stack);
            const GEO::index_t v1 = get_vertex(slice + 1, stack);
            const GEO::vec3f position_p0  = get_pointf(mesh.vertices, v0);
            const GEO::vec3f position_p1  = get_pointf(mesh.vertices, v1);
            const GEO::vec3f position_tip = GEO::vec3f{0.0f, radius, 0.0f};

            const auto flat_centroid_location = (position_p0 + position_p1 + position_tip) / 3.0f;

            attributes.facet_centroid     .set(facet, GEO::vec3f{flat_centroid_location});
            //attributes.facet_normal       .set(facet, centroid_data.normal);
            attributes.facet_aniso_control.set(facet, anisotropic_no_texcoord);
            ++facet;
        }
    }

private:
    GEO::Mesh&      mesh;
    Mesh_attributes attributes;
    const scalar    radius;
    const int       slice_count;
    const int       stack_count;

    std::map<std::pair<int, int>, GEO::index_t> key_to_vertex;
    GEO::index_t                                top_vertex{0};
    GEO::index_t                                bottom_vertex{0};

    auto make_vertex_data(const scalar rel_slice, const scalar rel_stack) const -> Vertex_data
    {
        const scalar phi       = pi * scalar{2.0} * rel_slice;
        const scalar sin_phi   = std::sin(phi);
        const scalar cos_phi   = std::cos(phi);

        const scalar theta     = pi * rel_stack - half_pi;
        const scalar sin_theta = std::sin(theta);
        const scalar cos_theta = std::cos(theta);

        const auto N_x = static_cast<float>(cos_theta * cos_phi);
        const auto N_y = static_cast<float>(sin_theta);
        const auto N_z = static_cast<float>(cos_theta * sin_phi);

        const auto T_x = static_cast<float>(-sin_phi);
        const auto T_y = static_cast<float>(0.0f);
        const auto T_z = static_cast<float>(cos_phi);

        const GEO::vec3f N{N_x, N_y, N_z};
        const GEO::vec3f T{T_x, T_y, T_z};
        const GEO::vec3f B = GEO::normalize(GEO::cross(N, T));

        const auto xP = static_cast<float>(radius * N_x);
        const auto yP = static_cast<float>(radius * N_y);
        const auto zP = static_cast<float>(radius * N_z);

        const auto s = static_cast<float>(rel_slice);
        const auto t = static_cast<float>(rel_stack);

        return Vertex_data{
            .position  = GEO::vec3f{xP, yP, zP},
            .normal    = N,
            .tangent   = GEO::vec4f{T, 1.0f},
            .bitangent = B,
            .texcoord  = GEO::vec2f{s, t}
        };
    }

    void sphere_vertex(const GEO::index_t vertex, const scalar rel_slice, const scalar rel_stack)
    {
        const Vertex_data  data   = make_vertex_data(rel_slice, rel_stack);
        set_pointf(mesh.vertices, vertex, data.position);
        attributes.vertex_normal    .set(vertex, data.normal);
        attributes.vertex_tangent   .set(vertex, data.tangent);
        attributes.vertex_bitangent .set(vertex, data.bitangent);
        attributes.vertex_texcoord_0.set(vertex, data.texcoord);
        //// GEO::vec3f n_  = attributes.vertex_normal    .get(vertex);
        //// GEO::vec4f t_  = attributes.vertex_tangent   .get(vertex);
        //// GEO::vec3f b_  = attributes.vertex_bitangent .get(vertex);
        //// GEO::vec2f tc_ = attributes.vertex_texcoord_0.get(vertex);
        //// static_cast<void>(n_);
        //// static_cast<void>(t_);
        //// static_cast<void>(b_);
        //// static_cast<void>(tc_);
        //// static int counter = 0;
        //// ++counter;
    }

    [[nodiscard]] auto get_vertex(int slice, int stack) -> GEO::index_t
    {
        const bool is_bottom = (stack == 0);
        const bool is_top    = (stack == stack_count);

        if (is_top) {
            return top_vertex;
        } else if (is_bottom) {
            return bottom_vertex;
        } else if (slice == slice_count) {
            return key_to_vertex[std::make_pair(0, stack)];
        } else {
            return key_to_vertex[std::make_pair(slice, stack)];
        }
    }

    auto make_corner(const GEO::index_t facet, const GEO::index_t local_facet_corner, const int slice, const int stack) -> GEO::index_t
    {
        geo_assert(slice >= 0);

        const scalar rel_slice           = static_cast<scalar>(slice) / static_cast<scalar>(slice_count);
        const scalar rel_stack           = static_cast<scalar>(stack) / static_cast<scalar>(stack_count);
        const bool   is_slice_seam       = /*(slice == 0) ||*/ (slice == slice_count);
        const bool   is_bottom           = (stack == 0);
        const bool   is_top              = (stack == stack_count);
        const bool   is_uv_discontinuity = is_slice_seam || is_bottom || is_top;

        GEO::index_t vertex;
        if (is_top) {
            vertex = top_vertex;
        } else if (is_bottom) {
            vertex = bottom_vertex;
        } else if (slice == slice_count) {
            vertex = key_to_vertex[std::make_pair(0, stack)];
        } else {
            vertex = key_to_vertex[std::make_pair(slice, stack)];
        }

        mesh.facets.set_vertex(facet, local_facet_corner, vertex);
        const GEO::index_t corner = mesh.facets.corner(facet, local_facet_corner);

        if (is_uv_discontinuity) {
            const auto s = rel_slice;
            const auto t = rel_stack;
            attributes.corner_texcoord_0.set(corner, GEO::vec2f{s, t});
        }

        return corner;
    }
};

void make_sphere(GEO::Mesh& mesh, const float radius, const unsigned int slice_count, const unsigned int stack_division)
{
    Sphere_builder builder{
        mesh,
        static_cast<Sphere_builder::scalar>(radius),
        static_cast<int>(slice_count),
        static_cast<int>(stack_division)
    };
    builder.build();
}

} // namespace erhe::geometry::shapes
