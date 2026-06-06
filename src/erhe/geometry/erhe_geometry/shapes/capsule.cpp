#include "erhe_geometry/shapes/capsule.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_profile/profile.hpp"

#include <geogram/mesh/mesh.h>

#include <cmath>
#include <map>

namespace erhe::geometry::shapes {

// Capsule on the Y axis, centered at origin: a cylindrical mid-section of the
// given length with hemispherical caps of the given radius at both ends.
//
// Stack indexing, with M = hemisphere_stack_count and stack_count = 2 * M + 1:
//   stack 0           = bottom pole (singular vertex at y = -length / 2 - radius)
//   stack 1 .. M      = bottom hemisphere rings; stack M is the bottom junction
//                       ring at y = -length / 2
//   stack M + 1       = top junction ring at y = +length / 2; the cylinder body
//                       is the single quad row between stacks M and M + 1
//   stack M + 1 .. 2M = top hemisphere rings
//   stack 2M + 1      = top pole (singular vertex at y = +length / 2 + radius)
//
// The junction rings are ordinary interior rings shared by the cap and body
// quads; at theta == 0 the hemisphere normal equals the radial cylinder
// normal, so shading is continuous across the junctions by construction.
class Capsule_builder
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

    Capsule_builder(GEO::Mesh& mesh, const scalar radius, const scalar length, const int slice_count, const int hemisphere_stack_count)
        : mesh                  {mesh}
        , attributes            {mesh}
        , radius                {radius}
        , length                {length}
        , half_length           {scalar{0.5} * length}
        , slice_count           {slice_count}
        , hemisphere_stack_count{hemisphere_stack_count}
        , stack_count           {2 * hemisphere_stack_count + 1}
        , cap_arc               {radius * half_pi}
        , total_arc             {scalar{2.0} * cap_arc + length}
    {
        geo_assert(slice_count >= 3);
        geo_assert(hemisphere_stack_count >= 1);
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
            for (int slice = 0; slice < slice_count; ++slice) {
                const scalar rel_slice = static_cast<scalar>(slice) / static_cast<scalar>(slice_count);
                capsule_vertex(vertex, rel_slice, static_cast<scalar>(stack));
                key_to_vertex[std::make_pair(slice, stack)] = vertex;
                vertex++;
            }
        }

        // Bottom fan
        bottom_vertex = vertex++;
        set_pointf(mesh.vertices, bottom_vertex, GEO::vec3f{0.0f, -half_length - radius, 0.0f});
        attributes.vertex_normal.set(bottom_vertex, GEO::vec3f{0.0f, -1.0f, 0.0f});
        GEO::index_t facet = mesh.facets.create_triangles(slice_count);
        for (int slice = 0; slice < slice_count; ++slice) {
            const int          stack              = 1;
            const scalar       rel_slice_centroid = (static_cast<scalar>(slice) + scalar{0.5}) / static_cast<scalar>(slice_count);
            const Vertex_data  average_data       = make_vertex_data(rel_slice_centroid, scalar{0.0});
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

            const GEO::vec3f flat_centroid_location =
                (
                    get_pointf(mesh.vertices, v0) +
                    get_pointf(mesh.vertices, v1) +
                    GEO::vec3f{0.0f, -half_length - radius, 0.0f}
                ) / 3.0f;
            attributes.facet_centroid     .set(facet, flat_centroid_location);
            attributes.facet_aniso_control.set(facet, anisotropic_no_texcoord);
            facet++;
        }

        // Middle quads, bottom up: bottom hemisphere rows, the single cylinder
        // body row (stack M -> M + 1), then top hemisphere rows
        facet = mesh.facets.create_quads((stack_count - 2) * slice_count);
        for (int stack = 1; stack < (stack_count - 1); ++stack) {
            for (int slice = 0; slice < slice_count; ++slice) {
                make_corner(facet, 0, slice + 1, stack    );
                make_corner(facet, 1, slice,     stack    );
                make_corner(facet, 2, slice,     stack + 1);
                make_corner(facet, 3, slice + 1, stack + 1);

                const GEO::vec3f flat_centroid_location =
                    (
                        get_pointf(mesh.vertices, get_vertex(slice + 1, stack    )) +
                        get_pointf(mesh.vertices, get_vertex(slice,     stack    )) +
                        get_pointf(mesh.vertices, get_vertex(slice,     stack + 1)) +
                        get_pointf(mesh.vertices, get_vertex(slice + 1, stack + 1))
                    ) / 4.0f;
                attributes.facet_centroid     .set(facet, flat_centroid_location);
                attributes.facet_aniso_control.set(facet, anisotropic_no_texcoord);
                facet++;
            }
        }

        // Top fan
        top_vertex = vertex++;
        set_pointf(mesh.vertices, top_vertex, GEO::vec3f{0.0f, half_length + radius, 0.0f});
        attributes.vertex_normal.set(top_vertex, GEO::vec3f{0.0f, 1.0f, 0.0f});
        facet = mesh.facets.create_triangles(slice_count);
        for (int slice = 0; slice < slice_count; ++slice) {
            const int    stack              = stack_count - 1;
            const scalar rel_slice_centroid = (static_cast<scalar>(slice) + scalar{0.5}) / static_cast<scalar>(slice_count);

            const Vertex_data average_data = make_vertex_data(rel_slice_centroid, static_cast<scalar>(stack_count));

            make_corner(facet, 0, slice + 1, stack);
            make_corner(facet, 1, slice,     stack);
            const GEO::index_t tip_corner = make_corner(facet, 2, slice, stack_count);

            attributes.corner_normal       .set(tip_corner, average_data.normal);
            attributes.corner_tangent      .set(tip_corner, average_data.tangent);
            attributes.corner_bitangent    .set(tip_corner, average_data.bitangent);
            attributes.corner_texcoord_0   .set(tip_corner, average_data.texcoord);
            attributes.corner_aniso_control.set(tip_corner, non_anisotropic);

            const GEO::index_t v0 = get_vertex(slice,     stack);
            const GEO::index_t v1 = get_vertex(slice + 1, stack);
            const GEO::vec3f position_p0  = get_pointf(mesh.vertices, v0);
            const GEO::vec3f position_p1  = get_pointf(mesh.vertices, v1);
            const GEO::vec3f position_tip = GEO::vec3f{0.0f, half_length + radius, 0.0f};

            const GEO::vec3f flat_centroid_location = (position_p0 + position_p1 + position_tip) / 3.0f;

            attributes.facet_centroid     .set(facet, flat_centroid_location);
            attributes.facet_aniso_control.set(facet, anisotropic_no_texcoord);
            ++facet;
        }
    }

private:
    GEO::Mesh&      mesh;
    Mesh_attributes attributes;
    const scalar    radius;
    const scalar    length;      // length of the cylindrical mid-section only
    const scalar    half_length;
    const int       slice_count;
    const int       hemisphere_stack_count;
    const int       stack_count; // 2 * hemisphere_stack_count + 1
    const scalar    cap_arc;     // profile arc length of one hemisphere cap
    const scalar    total_arc;   // profile arc length from pole to pole

    std::map<std::pair<int, int>, GEO::index_t> key_to_vertex;
    GEO::index_t                                top_vertex   {0};
    GEO::index_t                                bottom_vertex{0};

    // Texture coordinate v, monotonic in stack and proportional to the profile
    // arc length, so texel density stays even across the caps and the body.
    [[nodiscard]] auto v_of_stack(const scalar stack) const -> scalar
    {
        const scalar m = static_cast<scalar>(hemisphere_stack_count);
        scalar arc;
        if (stack <= m) {
            arc = cap_arc * (stack / m);
        } else if (stack >= m + scalar{1.0}) {
            arc = cap_arc + length + cap_arc * ((stack - (m + scalar{1.0})) / m);
        } else {
            arc = cap_arc + (stack - m) * length;
        }
        return arc / total_arc;
    }

    auto make_vertex_data(const scalar rel_slice, const scalar stack) const -> Vertex_data
    {
        const scalar phi     = pi * scalar{2.0} * rel_slice;
        const scalar sin_phi = std::sin(phi);
        const scalar cos_phi = std::cos(phi);

        const scalar m = static_cast<scalar>(hemisphere_stack_count);

        // Piecewise profile: latitude angle theta is 0 at both junction rings
        scalar theta;
        scalar center_y;
        if (stack <= m) {
            // Bottom hemisphere: pole (stack == 0) up to the bottom junction ring (stack == m)
            theta    = -half_pi + half_pi * (stack / m);
            center_y = -half_length;
        } else if (stack >= m + scalar{1.0}) {
            // Top hemisphere: top junction ring (stack == m + 1) up to the pole (stack == stack_count)
            theta    = half_pi * ((stack - (m + scalar{1.0})) / m);
            center_y = half_length;
        } else {
            // Cylinder body between the junction rings (fractional stacks)
            theta    = scalar{0.0};
            center_y = -half_length + (stack - m) * length;
        }

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
        const auto yP = static_cast<float>(center_y + radius * N_y);
        const auto zP = static_cast<float>(radius * N_z);

        const auto s = static_cast<float>(rel_slice);
        const auto t = static_cast<float>(v_of_stack(stack));

        return Vertex_data{
            .position  = GEO::vec3f{xP, yP, zP},
            .normal    = N,
            .tangent   = GEO::vec4f{T, 1.0f},
            .bitangent = B,
            .texcoord  = GEO::vec2f{s, t}
        };
    }

    void capsule_vertex(const GEO::index_t vertex, const scalar rel_slice, const scalar stack)
    {
        const Vertex_data data = make_vertex_data(rel_slice, stack);
        set_pointf(mesh.vertices, vertex, data.position);
        attributes.vertex_normal    .set(vertex, data.normal);
        attributes.vertex_tangent   .set(vertex, data.tangent);
        attributes.vertex_bitangent .set(vertex, data.bitangent);
        attributes.vertex_texcoord_0.set(vertex, data.texcoord);
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

        const bool is_slice_seam       = (slice == slice_count);
        const bool is_bottom           = (stack == 0);
        const bool is_top              = (stack == stack_count);
        const bool is_uv_discontinuity = is_slice_seam || is_bottom || is_top;

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
            const auto s = static_cast<float>(static_cast<scalar>(slice) / static_cast<scalar>(slice_count));
            const auto t = static_cast<float>(v_of_stack(static_cast<scalar>(stack)));
            attributes.corner_texcoord_0.set(corner, GEO::vec2f{s, t});
        }

        return corner;
    }
};

void make_capsule(GEO::Mesh& mesh, const float radius, const float length, const int slice_count, const int hemisphere_stack_count)
{
    Capsule_builder builder{
        mesh,
        static_cast<Capsule_builder::scalar>(radius),
        static_cast<Capsule_builder::scalar>(length),
        slice_count,
        hemisphere_stack_count
    };
    builder.build();
}

} // namespace erhe::geometry::shapes
