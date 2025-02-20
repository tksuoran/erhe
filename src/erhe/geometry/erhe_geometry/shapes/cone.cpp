// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_geometry/shapes/cone.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_profile/profile.hpp"

#include <map>

namespace erhe::geometry::shapes {

// Stack numbering example:          
//                                   
// 0 1 2 3 4                         
// . . . . .                         
// |\. . . .                         
// | \ . . .                         
// | :\. . .                         
// | : \ . .                         
// | : :\. .        Y                
// | : : \ .        ^                
// | : : :\.        |                
// | : : : *        O---> X          
// | : : :/                          
// | : : /                           
// | : :/                            
// | : /                             
// | :/    ^                         
// | /     top: stack == stack_count 
// |/           rel_stacl = 1.0      
//              singular in this case
// ^                                 
// bottom: stack == 0                
//         rel_stack == 0            
//         not singular in this case 

class Conical_frustum_builder
{
public:
    constexpr static float pi = 3.141592653589793238462643383279502884197169399375105820974944592308f;
    constexpr static float half_pi = 0.5f * pi;

    class Vertex_data
    {
    public:
        GEO::vec3f position {0.0f};
        GEO::vec3f normal   {0.0f};
        GEO::vec4f tangent  {0.0f};
        GEO::vec3f bitangent{0.0f};
        GEO::vec2f texcoord {0.0f};
    };

    Conical_frustum_builder(
        GEO::Mesh&  mesh,
        const float min_x,
        const float max_x,
        const float bottom_radius,
        const float top_radius,
        const bool  use_bottom,
        const bool  use_top,
        const int   slice_count,
        const int   stack_count
    )
        : mesh           {mesh}
        , min_x          {min_x}
        , max_x          {max_x}
        , bottom_radius  {bottom_radius}
        , top_radius     {top_radius}
        , use_bottom     {use_bottom}
        , use_top        {use_top}
        , slice_count    {slice_count}
        , stack_count    {stack_count}
        , bottom_singular{bottom_radius == 0.0f}
        , top_singular   {top_radius    == 0.0f}
        , attributes     {mesh}
    {
        geo_assert(slice_count != 0);
        geo_assert(stack_count != 0);
    }

    void build()
    {
        // X = anisotropy strength
        // Y = apply texture coordinate to anisotropy
        const GEO::vec2f non_anisotropic          {0.0f, 0.0f}; // Used on tips
        const GEO::vec2f anisotropic_no_texcoord  {1.0f, 0.0f}; // Used on lateral surface
        const GEO::vec2f anisotropic_with_texcoord{1.0f, 1.0f}; // Used on bottom / top ends

        // Points
        const int stack_bottom              = 0;
        const int stack_top                 = stack_count;
        const int stack_non_singular_bottom = bottom_singular ? 1               : 0;
        const int stack_non_singular_top    = top_singular    ? stack_count - 1 : stack_count;
        const int vertex_extent_1           = stack_non_singular_top - stack_non_singular_bottom + 1;
        const int vertex_extent_2           = slice_count;
        const int tip_vertex_count          = (bottom_singular ? 1 : 0) + (top_singular ? 1 : 0);
        const int vertex_count              = vertex_extent_1 * vertex_extent_2 + tip_vertex_count;
        
        GEO::index_t vertex = mesh.vertices.create_vertices(vertex_count);
        for (int stack = stack_non_singular_bottom; stack <= stack_non_singular_top; ++stack) {
            const float rel_stack = static_cast<float>(stack) / static_cast<float>(stack_count);
            for (int slice = 0; slice < slice_count; ++slice) {
                const float rel_slice = static_cast<float>(slice) / static_cast<float>(slice_count);
                cone_vertex(vertex, rel_slice, rel_stack);
                key_to_vertex[std::make_pair(slice, stack)] = vertex;
                ++vertex;
            }
        }
        ERHE_VERIFY(static_cast<int>(vertex + tip_vertex_count) == static_cast<int>(vertex_count));

        if (bottom_singular) {
            // Bottom vertex triangle fan
            bottom_vertex = vertex++;
            set_pointf(mesh.vertices, bottom_vertex, GEO::vec3f{min_x, 0.0f, 0.0f}); // Apex
            attributes.vertex_normal       .set(bottom_vertex, GEO::vec3f{-1.0f, 0.0f, 0.0f});
            attributes.vertex_normal_smooth.set(bottom_vertex, GEO::vec3f{-1.0f, 0.0f, 0.0f});
            const GEO::index_t facet_start = mesh.facets.create_triangles(slice_count);
            const GEO::index_t facet_end = facet_start + slice_count;
            GEO::index_t facet = facet_start;
            for (int slice = 0; slice < slice_count; ++slice) {
                const int  stack              = stack_non_singular_bottom;
                const auto rel_slice_centroid = (static_cast<float>(slice) + 0.5f) / static_cast<float>(slice_count);
                const auto rel_stack_centroid = (static_cast<float>(stack) - 0.5f) / static_cast<float>(slice_count);

                const Vertex_data  average_data = make_vertex_data(rel_slice_centroid, 0.0);

                make_corner(facet, 0, slice + 1, stack);
                make_corner(facet, 1, slice,     stack);
                const GEO::index_t tip_corner_id = make_corner(facet, 2, slice, stack_bottom);

                const GEO::index_t p0 = get_vertex(slice,     stack);
                const GEO::index_t p1 = get_vertex(slice + 1, stack);

                attributes.corner_normal       .set(tip_corner_id, average_data.normal);
                attributes.corner_tangent      .set(tip_corner_id, average_data.tangent);
                attributes.corner_bitangent    .set(tip_corner_id, average_data.bitangent);
                attributes.corner_texcoord_0   .set(tip_corner_id, average_data.texcoord);
                attributes.corner_aniso_control.set(tip_corner_id, non_anisotropic);

                const Vertex_data centroid_data = make_vertex_data(rel_slice_centroid, rel_stack_centroid);
                const GEO::vec3f flat_centroid_location = (
                    get_pointf(mesh.vertices, p0) + 
                    get_pointf(mesh.vertices, p1) +
                    GEO::vec3f{min_x, 0.0f, 0.0f}
                ) / 3.0f;
                attributes.facet_centroid     .set(facet, GEO::vec3f{flat_centroid_location});
                //attributes.facet_normal       .set(facet, centroid_data.normal);
                attributes.facet_aniso_control.set(facet, anisotropic_no_texcoord);
                ++facet;
            }
            ERHE_VERIFY(facet == facet_end);
        } else {
            if (use_bottom) {
                // Bottom facet
                const GEO::index_t facet        = mesh.facets.create_polygon(slice_count);
                const GEO::vec3f   facet_normal = GEO::vec3f{-1.0f, 0.0f, 0.0f};
                attributes.facet_centroid     .set(facet, GEO::vec3f{static_cast<float>(min_x), 0.0f, 0.0f});
                attributes.facet_normal       .set(facet, facet_normal);
                attributes.facet_aniso_control.set(facet, anisotropic_with_texcoord);

                for (int slice = 0; slice < slice_count; ++slice) {
                    GEO::index_t vertex_ = GEO::NO_INDEX;
                    make_corner(facet, slice, slice, 0, true, &vertex_);
                    const GEO::vec3f vertex_normal        = attributes.vertex_normal.get(vertex_);
                    const GEO::vec3f vertex_normal_smooth = GEO::normalize(facet_normal + vertex_normal);
                    attributes.vertex_normal_smooth.set(vertex_, vertex_normal_smooth);
                }

                generate_mesh_facet_texture_coordinates(mesh, facet, attributes);
            } else {
                // No bottom
            }
        }

        // Middle quads, bottom up
        {
            int quad_extent_1 = stack_non_singular_top - stack_non_singular_bottom;
            int quad_extent_2 = slice_count;
            int quad_count    = quad_extent_1 * quad_extent_2;
            const GEO::index_t facet_start = mesh.facets.create_quads(quad_count);
            const GEO::index_t facet_end   = facet_start + quad_count;
            GEO::index_t       facet       = facet_start;
            for (int stack = stack_non_singular_bottom; stack < stack_non_singular_top; ++stack) {
                const float rel_stack_centroid = (static_cast<float>(stack) + 0.5f) / static_cast<float>(stack_count);

                for (int slice = 0; slice < slice_count; ++slice) {
                    const auto rel_slice_centroid = (static_cast<float>(slice) + 0.5f) / static_cast<float>(slice_count);

                    make_corner(facet, 0, slice + 1, stack    );
                    make_corner(facet, 1, slice,     stack    );
                    make_corner(facet, 2, slice,     stack + 1);
                    make_corner(facet, 3, slice + 1, stack + 1);

                    const Vertex_data centroid_data = make_vertex_data(rel_slice_centroid, rel_stack_centroid);
                    const GEO::vec3f flat_centroid_location = (1.0f / 4.0f) *
                        (
                            get_pointf(mesh.vertices, get_vertex(slice + 1, stack + 1)) +
                            get_pointf(mesh.vertices, get_vertex(slice,     stack + 1)) +
                            get_pointf(mesh.vertices, get_vertex(slice,     stack    )) +
                            get_pointf(mesh.vertices, get_vertex(slice + 1, stack    )) 
                        );
                    attributes.facet_centroid     .set(facet, GEO::vec3f{flat_centroid_location});
                    //attributes.facet_normal       .set(facet, centroid_data.normal);
                    attributes.facet_aniso_control.set(facet, anisotropic_no_texcoord);
                    facet++;
                }
            }
            ERHE_VERIFY(facet == facet_end);
        }

        // Top parts
        if (top_singular) {
            // Top point triangle fan
            top_vertex = vertex++;
            set_pointf(mesh.vertices, top_vertex, GEO::vec3f{max_x, 0.0f, 0.0f}); //  apex
            attributes.vertex_normal       .set(top_vertex, GEO::vec3f{1.0f, 0.0f, 0.0f});
            attributes.vertex_normal_smooth.set(top_vertex, GEO::vec3f{1.0f, 0.0f, 0.0f});

            const GEO::index_t facet_start = mesh.facets.create_triangles(slice_count);
            const GEO::index_t facet_end   = facet_start + slice_count;
            GEO::index_t       facet       = facet_start;
            for (int slice = 0; slice < slice_count; ++slice) {
                const int  stack              = stack_non_singular_top;
                const auto rel_slice_centroid = (static_cast<float>(slice) + 0.5f) / static_cast<float>(slice_count);
                const auto rel_stack_centroid = (static_cast<float>(stack) + 0.5f) / static_cast<float>(stack_count);

                const Vertex_data average_data = make_vertex_data(rel_slice_centroid, 1.0);

                const GEO::index_t tip_corner = make_corner(facet, 2, slice, stack_top);
                make_corner(facet, 1, slice,     stack);
                make_corner(facet, 0, slice + 1, stack);

                attributes.corner_normal       .set(tip_corner, average_data.normal);
                attributes.corner_tangent      .set(tip_corner, average_data.tangent);
                attributes.corner_bitangent    .set(tip_corner, average_data.bitangent);
                attributes.corner_texcoord_0   .set(tip_corner, average_data.texcoord);
                attributes.corner_aniso_control.set(tip_corner, non_anisotropic);

                const Vertex_data centroid_data = make_vertex_data(rel_slice_centroid, rel_stack_centroid);

                const GEO::index_t p0           = get_vertex(slice,     stack);
                const GEO::index_t p1           = get_vertex(slice + 1, stack);
                const GEO::vec3f   position_p0  = get_pointf(mesh.vertices, p0);
                const GEO::vec3f   position_p1  = get_pointf(mesh.vertices, p1);
                const GEO::vec3f   position_tip = GEO::vec3f{max_x, 0.0f, 0.0f};

                const GEO::vec3f   flat_centroid_location = (1.0f / 3.0f) * (position_p0 + position_p1 + position_tip);

                attributes.facet_centroid     .set(facet, GEO::vec3f{flat_centroid_location});
                //attributes.facet_normal       .set(facet, centroid_data.normal);
                attributes.facet_aniso_control.set(facet, anisotropic_no_texcoord);
                ++facet;
            }
            ERHE_VERIFY(facet == facet_end);
        } else {
            if (use_top) {
                // Top facet
                const GEO::index_t facet        = mesh.facets.create_polygon(slice_count);
                const GEO::vec3f   facet_normal = GEO::vec3f{1.0f, 0.0f, 0.0f};

                attributes.facet_centroid     .set(facet, GEO::vec3f{static_cast<float>(max_x), 0.0f, 0.0f});
                attributes.facet_normal       .set(facet, facet_normal);
                attributes.facet_aniso_control.set(facet, anisotropic_with_texcoord);

                for (int slice = 0; slice < slice_count; ++slice) {
                    const int reverse_slice = slice_count - 1 - slice;
                    GEO::index_t vertex_ = GEO::NO_INDEX;
                    make_corner(facet, slice, reverse_slice, stack_top, true, &vertex_);
                    const GEO::vec3f vertex_normal        = attributes.vertex_normal.get(vertex_);
                    const GEO::vec3f vertex_normal_smooth = GEO::normalize(facet_normal + vertex_normal);
                    attributes.vertex_normal_smooth.set(vertex_, vertex_normal_smooth);
                }

                generate_mesh_facet_texture_coordinates(mesh, facet, attributes);
            } else {
                // no top
            }
        }
    }

private:
    auto make_vertex_data(const float rel_slice, const float rel_stack) const -> Vertex_data
    {
        const float phi                 = pi * 2.0f * rel_slice;
        const float sin_phi             = std::sin(phi);
        const float cos_phi             = std::cos(phi);
        const float one_minus_rel_stack = 1.0f - rel_stack;

        const GEO::vec3f position{
            one_minus_rel_stack * (min_x                  ) + rel_stack * (max_x),
            one_minus_rel_stack * (bottom_radius * sin_phi) + rel_stack * (top_radius * sin_phi),
            one_minus_rel_stack * (bottom_radius * cos_phi) + rel_stack * (top_radius * cos_phi)
        };

        const GEO::vec3f bottom{
            min_x,
            bottom_radius * sin_phi,
            bottom_radius * cos_phi
        };

        const GEO::vec3f top{
            max_x,
            top_radius * sin_phi,
            top_radius * cos_phi
        };

        const GEO::vec3f Bd = GEO::normalize(top - bottom); // generatrix
        const GEO::vec3f B  = GEO::vec3f(Bd);
        const GEO::vec3f T{
            0.0f,
            static_cast<float>(std::sin(phi + 0.5 * pi)),
            static_cast<float>(std::cos(phi + 0.5 * pi))
        };
        const GEO::vec3f N0 = GEO::cross(B, T);
        const GEO::vec3f N  = GEO::normalize(N0);

        const GEO::vec3f t_xyz = GEO::normalize(T - N * GEO::dot(N, T));
        const float      t_w   = (GEO::dot(GEO::cross(N, T), B) < 0.0f) ? -1.0f : 1.0f;
        const GEO::vec3f b_xyz = GEO::normalize(B - N * GEO::dot(N, B));
        //const float      b_w   = (GEO::dot(GEO::cross(B, N), T) < 0.0f) ? -1.0f : 1.0f;

        const float s = rel_slice;
        const float t = rel_stack;

        return Vertex_data{
            .position  = position,
            .normal    = N,
            .tangent   = GEO::vec4f{t_xyz.x, t_xyz.y, t_xyz.z, t_w},
            .bitangent = GEO::vec3f{b_xyz}, // , b_w},
            .texcoord  = GEO::vec2f{s, t}
        };
    }

    void cone_vertex(const GEO::index_t vertex, const float rel_slice, const float rel_stack)
    {
        const Vertex_data data = make_vertex_data(rel_slice, rel_stack);
        set_pointf(mesh.vertices, vertex, data.position);

        attributes.vertex_normal       .set(vertex, data.normal);
        attributes.vertex_normal_smooth.set(vertex, data.normal);
        attributes.vertex_tangent      .set(vertex, data.tangent);
        attributes.vertex_bitangent    .set(vertex, data.bitangent);
        attributes.vertex_texcoord_0   .set(vertex, data.texcoord);
    }

    auto make_corner(const GEO::index_t facet, const GEO::index_t facet_local_corner, const int slice, const int stack) -> GEO::index_t
    {
        return make_corner(facet, facet_local_corner, slice, stack, false);
    }

    [[nodiscard]] auto get_vertex(const int slice, const int stack) -> GEO::index_t
    {
        if ((stack == stack_count) && top_singular) {
            ERHE_VERIFY(top_vertex != GEO::NO_INDEX);
            return top_vertex;
        } else if ((stack == 0) && bottom_singular) {
            ERHE_VERIFY(bottom_vertex != GEO::NO_INDEX);
            return bottom_vertex;
        } else if (slice == slice_count) {
            return key_to_vertex[std::make_pair(0, stack)];
        } else {
            return key_to_vertex[std::make_pair(slice, stack)];
        }
    }

    auto make_corner(const GEO::index_t facet, const GEO::index_t local_facet_corner, const int slice, const int stack, const bool base, GEO::index_t* p_vertex = nullptr) -> GEO::index_t
    {
        const float rel_slice = static_cast<float>(slice) / static_cast<float>(slice_count);
        const float rel_stack = static_cast<float>(stack) / static_cast<float>(stack_count);

        const bool is_slice_seam       = (slice == 0) || (slice == slice_count);
        const bool is_bottom           = (stack == 0);
        const bool is_top              = (stack == stack_count);
        const bool is_uv_discontinuity = is_slice_seam || is_bottom || is_top;

        const GEO::index_t vertex = get_vertex(slice, stack);
        if (p_vertex != nullptr) {
            *p_vertex = vertex;
        }
        mesh.facets.set_vertex(facet, local_facet_corner, vertex);
        const GEO::index_t corner = mesh.facets.corner(facet, local_facet_corner);
        if (is_uv_discontinuity) {
            float s, t;
            if (base) {
                const float phi                 = pi * 2.0f * rel_slice;
                const float sin_phi             = std::sin(phi);
                const float cos_phi             = std::cos(phi);
                const float one_minus_rel_stack = 1.0f - rel_stack;

                s = static_cast<float>(one_minus_rel_stack * sin_phi + rel_stack * sin_phi);
                t = static_cast<float>(one_minus_rel_stack * cos_phi + rel_stack * cos_phi);
            } else {
                s = static_cast<float>(rel_slice);
                t = static_cast<float>(rel_stack);
            }
            attributes.corner_texcoord_0.set(corner, GEO::vec2f{s, t});
        }

        if (is_top || is_bottom) {
            if (base && is_bottom && !bottom_singular && use_bottom) {
                attributes.corner_normal   .set(corner, GEO::vec3f{-1.0f, 0.0f, 0.0f});
                attributes.corner_tangent  .set(corner, bottom_singular ? GEO::vec4f{0.0f, 0.0f, 0.0f, 0.0f} : GEO::vec4f{0.0f, 1.0f, 0.0f, 1.0f});
                attributes.corner_bitangent.set(corner, bottom_singular ? GEO::vec3f{0.0f, 0.0f, 0.0f}       : GEO::vec3f{0.0f, 0.0f, 1.0f});
            }

            if (base && is_top && !top_singular && use_top) {
                attributes.corner_normal   .set(corner, GEO::vec3f{1.0f, 0.0f, 0.0f});
                attributes.corner_tangent  .set(corner, bottom_singular ? GEO::vec4f{0.0f, 0.0f, 0.0f, 0.0f} : GEO::vec4f{0.0f, 1.0f, 0.0f, 1.0f});
                attributes.corner_bitangent.set(corner, bottom_singular ? GEO::vec3f{0.0f, 0.0f, 0.0f}       : GEO::vec3f{0.0f, 0.0f, 1.0f});
            }
        }

        return corner;
    }

    GEO::Mesh&       mesh;
    float            min_x          {0.0f};
    float            max_x          {0.0f};
    float            bottom_radius  {0.0f};
    float            top_radius     {0.0f};
    bool             use_bottom     {false};
    bool             use_top        {false};
    int              slice_count    {0};
    int              stack_count    {0};
    bool             bottom_singular{false};
    bool             top_singular   {false};
    std::map<std::pair<int, int>, GEO::index_t> key_to_vertex;
    GEO::index_t     top_vertex   {GEO::NO_INDEX};
    GEO::index_t     bottom_vertex{GEO::NO_INDEX};
    Mesh_attributes  attributes;
};

void make_conical_frustum(
    GEO::Mesh&  mesh,
    const float min_x,
    const float max_x,
    const float bottom_radius,
    const float top_radius,
    const bool  use_bottom,
    const bool  use_top,
    const int   slice_count,
    const int   stack_division
)
{
    Conical_frustum_builder builder{
        mesh,
        min_x,
        max_x,
        bottom_radius,
        top_radius,
        use_bottom,
        use_top,
        slice_count,
        stack_division
    };
    builder.build();
}

void make_cone(
    GEO::Mesh&  mesh,
    const float min_x,
    const float max_x,
    const float bottom_radius,
    const bool  use_bottom,
    const int   slice_count,
    const int   stack_division
)
{

    Conical_frustum_builder builder{
        mesh,
        min_x,          // min x
        max_x,          // max x
        bottom_radius,  // bottom raidus
        0.0,            // top radius
        use_bottom,     // use_bottom
        false,          // use top
        slice_count,    // stack count
        stack_division  // stack division
    };
    builder.build();
}

void make_cylinder(
    GEO::Mesh&  mesh,
    const float min_x,
    const float max_x,
    const float radius,
    const bool  use_bottom,
    const bool  use_top,
    const int   slice_count,
    const int   stack_division
)
{
    Conical_frustum_builder builder{mesh, min_x, max_x, radius, radius, use_bottom, use_top, slice_count, stack_division};
    builder.build();
}

} // namespace erhe::geometry::shapes
