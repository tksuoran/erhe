#include "erhe_geometry/shapes/box.hpp"
#include "erhe_geometry/geometry.hpp"

#include <map>

namespace erhe::geometry::shapes {

auto sign(const float x) -> float
{
    return x < 0.0f ? -1.0f : 1.0f;
}

auto signed_pow(const float x, const float p) -> float
{
    return sign(x) * std::pow(std::abs(x), p);
}

void make_box(GEO::Mesh& mesh, const float x_size, const float y_size, const float z_size)
{
    const float x = x_size / 2.0f;
    const float y = y_size / 2.0f;
    const float z = z_size / 2.0f;

    mesh.vertices.create_vertices(8);
    set_pointf(mesh.vertices, 0, GEO::vec3f(-x, -y, -z)); // 0    2------4
    set_pointf(mesh.vertices, 1, GEO::vec3f( x, -y, -z)); // 1   /|     /|
    set_pointf(mesh.vertices, 2, GEO::vec3f(-x,  y, -z)); // 2  6-+----7 |
    set_pointf(mesh.vertices, 3, GEO::vec3f(-x, -y,  z)); // 3  | |    | |
    set_pointf(mesh.vertices, 4, GEO::vec3f( x,  y, -z)); // 4  | |    | |
    set_pointf(mesh.vertices, 5, GEO::vec3f( x, -y,  z)); // 5  | 0----|-1
    set_pointf(mesh.vertices, 6, GEO::vec3f(-x,  y,  z)); // 6  |/     |/
    set_pointf(mesh.vertices, 7, GEO::vec3f( x,  y,  z)); // 7  3------5
    mesh.facets.create_quad(1, 4, 7, 5); // x+
    mesh.facets.create_quad(2, 6, 7, 4); // y+
    mesh.facets.create_quad(3, 5, 7, 6); // z+
    mesh.facets.create_quad(0, 3, 6, 2); // x-
    mesh.facets.create_quad(0, 1, 5, 3); // y-
    mesh.facets.create_quad(0, 2, 4, 1); // z-
}

void make_box(
    GEO::Mesh&  mesh,
    const float min_x,
    const float max_x,
    const float min_y,
    const float max_y,
    const float min_z,
    const float max_z
)
{
    mesh.vertices.create_vertices(8);
    set_pointf(mesh.vertices, 0, GEO::vec3f{min_x, min_y, min_z}); // 0    2------4
    set_pointf(mesh.vertices, 1, GEO::vec3f{max_x, min_y, min_z}); // 1   /|     /|
    set_pointf(mesh.vertices, 2, GEO::vec3f{min_x, max_y, min_z}); // 2  6-+----7 |
    set_pointf(mesh.vertices, 3, GEO::vec3f{min_x, min_y, max_z}); // 3  | |    | |
    set_pointf(mesh.vertices, 4, GEO::vec3f{max_x, max_y, min_z}); // 4  | |    | |
    set_pointf(mesh.vertices, 5, GEO::vec3f{max_x, min_y, max_z}); // 5  | 0----|-1
    set_pointf(mesh.vertices, 6, GEO::vec3f{min_x, max_y, max_z}); // 6  |/     |/
    set_pointf(mesh.vertices, 7, GEO::vec3f{max_x, max_y, max_z}); // 7  3------5
    mesh.facets.create_quad(1, 4, 7, 5); // x+
    mesh.facets.create_quad(2, 6, 7, 4); // y+
    mesh.facets.create_quad(3, 5, 7, 6); // z+
    mesh.facets.create_quad(0, 3, 6, 2); // x-
    mesh.facets.create_quad(0, 1, 5, 3); // y-
    mesh.facets.create_quad(0, 2, 4, 1); // z-
}

void make_box(GEO::Mesh& mesh, const float r)
{
    const float sq3 = std::sqrt(3.0f);
    make_box(mesh, 2.0f * r / sq3, 2.0f * r / sq3, 2.0f * r / sq3);
}

class Box_builder
{
public:
    GEO::Mesh& mesh;
    GEO::vec3f size{0.0f};
    GEO::vec3i div {0};
    float      p   {0.0f};

    std::map<int, GEO::index_t> key_to_vertex;

    Mesh_attributes attributes;

    void ortho_basis_pixar_r1(const GEO::vec3f N, GEO::vec4f& T, GEO::vec4f& B)
    {
        const float      sz = sign(N.z);
        const float      a  = 1.0f / (sz + N.z);
        const float      sx = sz * N.x;
        const float      b  = N.x * N.y * a;
        const GEO::vec3f t_ = GEO::vec3f{sx * N.x * a - 1.f, sz * b, sx};
        const GEO::vec3f b_ = GEO::vec3f{b, N.y * N.y * a - sz, N.y};

        const GEO::vec3f t_xyz = GEO::normalize(t_ - N * GEO::dot(N, t_));
        const float      t_w   = (GEO::dot(GEO::cross(N, t_), b_) < 0.0f) ? -1.0f : 1.0f;
        const GEO::vec3f b_xyz = GEO::normalize(b_ - N * GEO::dot(N, b_));
        const float      b_w   = (GEO::dot(GEO::cross(b_, N), t_) < 0.0f) ? -1.0f : 1.0f;
        T = GEO::vec4f{t_xyz, t_w};
        B = GEO::vec4f{b_xyz, b_w};
    }

    auto make_vertex(
        const int        x,
        const int        y,
        const int        z,
        const GEO::vec3f n,
        const float      s,
        const float      t
    ) -> GEO::index_t
    {
        const int key =
            x * (div.z * 4) +
            y * (div.x * 4 * div.z * 4) +
            z;

        const auto i = key_to_vertex.find(key);
        if (i != key_to_vertex.end()) {
            return i->second;
        }

        const float rel_x  = static_cast<float>(x) / static_cast<float>(div.x);
        const float rel_y  = static_cast<float>(y) / static_cast<float>(div.y);
        const float rel_z  = static_cast<float>(z) / static_cast<float>(div.z);
        const float rel_xp = signed_pow(rel_x, p);
        const float rel_yp = signed_pow(rel_y, p);
        const float rel_zp = signed_pow(rel_z, p);

        const float x_p = rel_xp * size.x;
        const float y_p = rel_yp * size.y;
        const float z_p = rel_zp * size.z;

        const GEO::index_t vertex_id = mesh.vertices.create_vertex();
        const bool         is_discontinuity =
            (x == -div.x) || (x == div.x) ||
            (y == -div.y) || (y == div.y) ||
            (z == -div.z) || (z == div.z);

        set_pointf(mesh.vertices, vertex_id, GEO::vec3f{x_p, y_p, z_p});
        if (!is_discontinuity) {
            attributes.vertex_normal    .set(vertex_id, n);
            attributes.vertex_texcoord_0.set(vertex_id, GEO::vec2f{static_cast<float>(s), static_cast<float>(t)});

            GEO::vec4f T;
            GEO::vec4f B;
            ortho_basis_pixar_r1(n, T, B);
            attributes.vertex_tangent  .set(vertex_id, T);
            attributes.vertex_bitangent.set(vertex_id, GEO::vec3f{B.x, B.y, B.z});
        }

        key_to_vertex[key] = vertex_id;

        return vertex_id;
    }

    auto make_corner(
        const GEO::index_t facet,
        const GEO::index_t local_facet_corner,
        const int          x,
        const int          y,
        const int          z,
        const GEO::vec3f   n,
        const float        s,
        const float        t
    ) -> GEO::index_t
    {
        const int key =
            y * (div.x * 4 * div.z * 4) +
            x * (div.z * 4) +
            z;

        const GEO::index_t vertex_id = key_to_vertex[key];
        mesh.facets.set_vertex(facet, local_facet_corner, vertex_id);
        const GEO::index_t corner_id = mesh.facets.corner(facet, local_facet_corner);
        const bool      is_discontinuity =
            (x == -div.x) || (x == div.x) ||
            (y == -div.y) || (y == div.y) ||
            (z == -div.z) || (z == div.z);
        if (is_discontinuity) {
            attributes.corner_normal    .set(corner_id, n);
            attributes.corner_texcoord_0.set(corner_id, GEO::vec2f{s, t});
            GEO::vec4f B;
            GEO::vec4f T;
            ortho_basis_pixar_r1(n, B, T); // TODO Check why vertex/corner are different, on
            attributes.corner_tangent  .set(corner_id, T);
            attributes.corner_bitangent.set(corner_id, GEO::vec3f{B.x, B.y, B.z});
        }

        return corner_id;
    }

    Box_builder(GEO::Mesh& mesh, const GEO::vec3f size, const GEO::vec3i div, const float p)
        : mesh      {mesh}
        , size      {size}
        , div       {div}
        , p         {p}
        , attributes{mesh}
    {
    }

    void build()
    {
        int x;
        int y;
        int z;

        // Generate vertices
        const GEO::vec3f unit_x(1.0f, 0.0f, 0.0f);
        const GEO::vec3f unit_y(0.0f, 1.0f, 0.0f);
        const GEO::vec3f unit_z(0.0f, 0.0f, 1.0f);
        for (x = -div.x; x <= div.x; x++) {
            const float rel_x = 0.5f + static_cast<float>(x) / size.x;
            for (z = -div.z; z <= div.z; z++) {
                const float rel_z = 0.5f + static_cast<float>(z) / size.z;
                make_vertex(x,  div.y, z,  unit_y, rel_x, rel_z);
                make_vertex(x, -div.y, z, -unit_y, rel_x, rel_z);
            }
            for (y = -div.y; y <= div.y; y++) {
                const float rel_y = 0.5f + static_cast<float>(y) / size.y;
                make_vertex(x, y,  div.z,  unit_z, rel_x, rel_y);
                make_vertex(x, y, -div.z, -unit_z, rel_x, rel_y);
            }
        }

        // Left and right
        for (z = -div.z; z <= div.z; z++) {
            const float rel_z = 0.5f + static_cast<float>(z) / size.z;
            for (y = -div.y; y <= div.y; y++) {
                const float rel_y = 0.5f + static_cast<float>(y) / size.y;
                make_vertex( div.x, y, z,  unit_x, rel_y, rel_z);
                make_vertex(-div.x, y, z, -unit_x, rel_y, rel_z);
            }
        }

        // Generate quads
        for (x = -div.x; x < div.x; x++) {
            const float rel_x1 = 0.5f + static_cast<float>(x) / size.x;
            const float rel_x2 = 0.5f + static_cast<float>(x + 1) / size.x;
            for (z = -div.z; z < div.z; z++) {
                const float rel_z1 = 0.5f + static_cast<float>(z) / size.z;
                const float rel_z2 = 0.5f + static_cast<float>(z + 1) / size.z;

                const GEO::index_t top_facet = mesh.facets.create_quads(1);
                make_corner(top_facet, 0, x,     div.y, z,     unit_y, rel_x1, rel_z1);
                make_corner(top_facet, 1, x,     div.y, z + 1, unit_y, rel_x1, rel_z2);
                make_corner(top_facet, 2, x + 1, div.y, z + 1, unit_y, rel_x2, rel_z2);
                make_corner(top_facet, 3, x + 1, div.y, z,     unit_y, rel_x2, rel_z1);

                const GEO::index_t bottom_facet = mesh.facets.create_quads(1);
                make_corner(bottom_facet, 0, x + 1, -div.y, z,     -unit_y, rel_x2, rel_z1);
                make_corner(bottom_facet, 1, x + 1, -div.y, z + 1, -unit_y, rel_x2, rel_z2);
                make_corner(bottom_facet, 2, x,     -div.y, z + 1, -unit_y, rel_x1, rel_z2);
                make_corner(bottom_facet, 3, x,     -div.y, z,     -unit_y, rel_x1, rel_z1);

                attributes.facet_normal.set(top_facet,     unit_y);
                attributes.facet_normal.set(bottom_facet, -unit_y);
            }
            for (y = -div.y; y < div.y; y++) {
                const float rel_y1 = 0.5f + static_cast<float>(y    ) / size.y;
                const float rel_y2 = 0.5f + static_cast<float>(y + 1) / size.y;

                const GEO::index_t back_facet = mesh.facets.create_quads(1);
                make_corner(back_facet, 0, x + 1, y,     div.z, unit_z, rel_x2, rel_y1);
                make_corner(back_facet, 1, x + 1, y + 1, div.z, unit_z, rel_x2, rel_y2);
                make_corner(back_facet, 2, x,     y + 1, div.z, unit_z, rel_x1, rel_y2);
                make_corner(back_facet, 3, x,     y,     div.z, unit_z, rel_x1, rel_y1);

                const GEO::index_t front_facet = mesh.facets.create_quads(1);
                make_corner(front_facet, 0, x,     y,     -div.z, -unit_z, rel_x1, rel_y1);
                make_corner(front_facet, 1, x,     y + 1, -div.z, -unit_z, rel_x1, rel_y2);
                make_corner(front_facet, 2, x + 1, y + 1, -div.z, -unit_z, rel_x2, rel_y2);
                make_corner(front_facet, 3, x + 1, y,     -div.z, -unit_z, rel_x2, rel_y1);

                attributes.facet_normal.set(back_facet,   unit_z);
                attributes.facet_normal.set(front_facet, -unit_z);
            }
        }

        for (z = -div.z; z < div.z; z++) {
            const float rel_z1 = 0.5f + static_cast<float>(z    ) / size.z;
            const float rel_z2 = 0.5f + static_cast<float>(z + 1) / size.z;

            for (y = -div.y; y < div.y; y++) {
                const float rel_y1 = 0.5f + static_cast<float>(y    ) / size.y;
                const float rel_y2 = 0.5f + static_cast<float>(y + 1) / size.y;

                const GEO::index_t right_facet = mesh.facets.create_quads(1);
                make_corner(right_facet, 0, div.x, y + 1, z,     unit_x, rel_y2, rel_z1);
                make_corner(right_facet, 1, div.x, y + 1, z + 1, unit_x, rel_y2, rel_z2);
                make_corner(right_facet, 2, div.x, y,     z + 1, unit_x, rel_y1, rel_z2);
                make_corner(right_facet, 3, div.x, y,     z,     unit_x, rel_y1, rel_z1);

                const GEO::index_t left_facet = mesh.facets.create_quads(1);
                make_corner(left_facet, 0, -div.x, y,     z,     -unit_x, rel_y1, rel_z1);
                make_corner(left_facet, 1, -div.x, y,     z + 1, -unit_x, rel_y1, rel_z2);
                make_corner(left_facet, 2, -div.x, y + 1, z + 1, -unit_x, rel_y2, rel_z2);
                make_corner(left_facet, 3, -div.x, y + 1, z,     -unit_x, rel_y2, rel_z1);

                attributes.facet_normal.set(right_facet,  unit_x);
                attributes.facet_normal.set(left_facet,  -unit_x);
            }
        }
    }
};

void make_box(GEO::Mesh& mesh, const GEO::vec3f size, const GEO::vec3i div, const float p)
{
    Box_builder builder{mesh, GEO::vec3f{size / 2.0f}, div, p};
    builder.build();
}

} // namespace erhe::geometry::shapes
