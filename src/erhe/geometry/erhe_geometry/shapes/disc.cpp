#include "erhe_geometry/shapes/disc.hpp"
#include "erhe_geometry/geometry.hpp"

#include <geogram/mesh/mesh.h>

#include <map>

namespace erhe::geometry::shapes {

class Disc_builder
{
public:
    constexpr static float pi      = 3.141592653589793238462643383279502884197169399375105820974944592308f;
    constexpr static float half_pi = 0.5f * pi;

    Disc_builder(
        GEO::Mesh&  mesh,
        const float inner_radius,
        const float outer_radius,
        const int   slice_count,
        const int   stack_count,
        const int   slice_begin,
        const int   slice_end,
        const int   stack_begin,
        const int   stack_end
    )
        : mesh        {mesh}
        , attributes  {mesh}
        , inner_radius{inner_radius}
        , outer_radius{outer_radius}
        , slice_count {slice_count}
        , stack_count {stack_count}
        , slice_begin {slice_begin}
        , slice_end   {slice_end}
        , stack_begin {stack_begin}
        , stack_end   {stack_end}
    {
    }

    void build()
    {
        // Make points
        for (int stack = 0; stack < stack_count; ++stack) {
            const float rel_stack = (stack_count == 1) 
                ? 1.0f 
                : static_cast<float>(stack) / static_cast<float>(stack_count - 1);
            for (int slice = 0; slice <= slice_count; ++slice) {
                const float rel_slice = static_cast<float>(slice) / static_cast<float>(slice_count);
                points[std::make_pair(slice, stack)] = make_point(rel_slice, rel_stack);
            }
        }

        // Special case without center point
        if (stack_count == 1) {
            const GEO::index_t facet = mesh.facets.create_polygon(slice_count);
            attributes.facet_centroid.set(facet, GEO::vec3f{0.0f, 0.0f, 0.0f});
            attributes.facet_normal  .set(facet, GEO::vec3f{0.0f, 0.0f, 1.0f});

            for (int slice = 0; slice < slice_count; ++slice) {
                make_corner(facet, slice, slice, 0);
            }
            return;
        }

        // Make center point if needed
        if (inner_radius == 0.0f) {
            center_vertex = mesh.vertices.create_vertices(1);
            set_pointf(mesh.vertices, center_vertex, GEO::vec3f{0.0f, 0.0f, 0.0f});
        }

        // Quads/triangles
        for (int stack = stack_begin; stack < stack_end - 1; ++stack) {
            const float rel_stack_centroid = (stack_end == 1) 
                ? 0.5f 
                : static_cast<float>(stack) / static_cast<float>(stack_count - 1);

            for (int slice = slice_begin; slice < slice_end; ++slice) {
                const bool         is_triangle        = (stack == 0) && (inner_radius == 0.0);
                const float        rel_slice_centroid = (static_cast<float>(slice) + 0.5f) / static_cast<float>(slice_count);
                const GEO::index_t centroid_vertex    = make_point(rel_slice_centroid, rel_stack_centroid);
                const GEO::index_t facet              = mesh.facets.create_facets(1, is_triangle ? 3 : 4);

                attributes.facet_centroid.set(facet, get_pointf(mesh.vertices, centroid_vertex));
                attributes.facet_normal  .set(facet, GEO::vec3f{0.0f, 0.0f, 1.0f});
                if (is_triangle) {
                    make_corner(facet, 0, slice, stack + 1);
                    make_corner(facet, 1, slice + 1, stack + 1);
                    const GEO::index_t tip_corner_id = make_corner(facet, 2, slice, stack);

                    const GEO::vec2f t1               = attributes.vertex_texcoord_0.get(get_point(slice,     stack));
                    const GEO::vec2f t2               = attributes.vertex_texcoord_0.get(get_point(slice + 1, stack));
                    const GEO::vec2f average_texcoord = (t1 + t2) / 2.0f;
                    attributes.corner_texcoord_0.set(tip_corner_id, average_texcoord);
                } else {
                    make_corner(facet, 0, slice + 1, stack    );
                    make_corner(facet, 1, slice,     stack    );
                    make_corner(facet, 2, slice,     stack + 1);
                    make_corner(facet, 3, slice + 1, stack + 1);
                }
            }
        }
    }

private:
    [[nodiscard]] auto get_point(const int slice, const int stack) -> GEO::index_t
    {
        return points[std::make_pair(slice, stack)];
    }

    // rel_stack is in range 0..1
    auto make_point(const float rel_slice, const float rel_stack) -> GEO::index_t
    {
        const float phi                 = pi * 2.0f * rel_slice;
        const float sin_phi             = std::sin(phi);
        const float cos_phi             = std::cos(phi);
        const float one_minus_rel_stack = 1.0f - rel_stack;

        const GEO::vec3f position{
            one_minus_rel_stack * (outer_radius * cos_phi) + rel_stack * (inner_radius * cos_phi),
            one_minus_rel_stack * (outer_radius * sin_phi) + rel_stack * (inner_radius * sin_phi),
            0.0f
        };

        const float s = rel_slice;
        const float t = rel_stack;

        const GEO::index_t vertex = mesh.vertices.create_vertex();
        set_pointf(mesh.vertices, vertex, position);
        attributes.vertex_normal    .set(vertex, GEO::vec3f{0.0f, 0.0f, 1.0f});
        attributes.vertex_texcoord_0.set(vertex, GEO::vec2f{s, t});

        return vertex;
    }

    auto make_corner(const GEO::index_t facet, const GEO::index_t local_facet_corner, const int slice, const int stack) -> GEO::index_t
    {
        const float rel_slice           = static_cast<float>(slice) / static_cast<float>(slice_count);
        const float rel_stack           = (stack_count == 1) ? 1.0f : static_cast<float>(stack) / static_cast<float>(stack_count - 1);
        const bool  is_slice_seam       = (slice == 0) || (slice == slice_count);
        const bool  is_center           = (stack == 0) && (inner_radius == 0.0);
        const bool  is_uv_discontinuity = is_slice_seam || is_center;

        GEO::index_t vertex;
        if (is_center) {
            vertex = center_vertex;
        } else {
            if (slice == slice_count) {
                vertex = points[std::make_pair(0, stack)];
            } else {
                vertex = points[std::make_pair(slice, stack)];
            }
        }

        mesh.facets.set_vertex(facet, local_facet_corner, vertex);
        const GEO::index_t corner = mesh.facets.corner(facet, local_facet_corner);

        if (is_uv_discontinuity) {
            const auto s = static_cast<float>(rel_slice);
            const auto t = static_cast<float>(rel_stack);
            attributes.corner_texcoord_0.set(corner, GEO::vec2f{s, t});
        }

        return corner;
    }

    GEO::Mesh&      mesh;
    Mesh_attributes attributes;
    float           inner_radius;
    float           outer_radius;
    int             slice_count;
    int             stack_count;
    int             slice_begin;
    int             slice_end;
    int             stack_begin;
    int             stack_end;
    GEO::index_t    center_vertex{0};
    std::map<std::pair<int, int>, GEO::index_t> points;
};

void make_disc(GEO::Mesh& mesh, const float outer_radius, const float inner_radius, const int slice_count, const int stack_count)
{
    Disc_builder builder{mesh, outer_radius, inner_radius, slice_count, stack_count, 0, slice_count, 0, stack_count};
    builder.build();
}

void make_disc(
    GEO::Mesh&  mesh,
    const float outer_radius,
    const float inner_radius,
    const int   slice_count,
    const int   stack_count,
    const int   slice_begin,
    const int   slice_end,
    const int   stack_begin,
    const int   stack_end
)
{
    Disc_builder builder{mesh, outer_radius, inner_radius, slice_count, stack_count, slice_begin, slice_end, stack_begin, stack_end};
    builder.build();
}

} // namespace erhe::geometry::shapes
