#include "erhe_geometry/shapes/box.hpp"
#include "erhe_geometry/geometry.hpp"

#include <algorithm>
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

// Grid indices run 0..cells per axis (cells = subdivisions + 1 >= 1):
// index 0 is the axis minimum face, index cells the maximum face -
// subdivisions = 0 places vertices only at the min/max corners of that
// axis. Positions distribute by signed_pow of the normalized [-1, 1]
// coordinate (the p "power" shaping); texture coordinates keep the
// repeating one-tile-per-grid-step-per-half-extent-unit scale of the
// original generator (values run past [0, 1] on purpose).
class Box_builder
{
public:
    GEO::Mesh& mesh;
    GEO::vec3f half_size{};
    GEO::vec3i cells    {};
    float      p        {0.0f};

    std::map<int, GEO::index_t> key_to_vertex;

    Mesh_attributes attributes;

    [[nodiscard]] auto vertex_key(const int x, const int y, const int z) const -> int
    {
        return x + (cells.x + 1) * (y + (cells.y + 1) * z);
    }

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

    // Normalized [-1, 1] coordinate of grid index i on an axis with the
    // given cell count, power-shaped.
    [[nodiscard]] auto shaped_rel(const int i, const int cell_count) const -> float
    {
        const float n = 2.0f * static_cast<float>(i) / static_cast<float>(cell_count) - 1.0f;
        return signed_pow(n, p);
    }

    // Texture coordinate of grid index i: spans -half_extent at i = 0 to
    // +half_extent at i = cell_count, so one tile covers one unit of extent.
    [[nodiscard]] auto uv_rel(const int i, const int cell_count, const float half_extent) const -> float
    {
        const float centered = static_cast<float>(i) - 0.5f * static_cast<float>(cell_count);
        const float result = 2.0f * half_extent * centered / static_cast<float>(cell_count);
        return result;
    }

    auto make_vertex(const int x, const int y, const int z) -> GEO::index_t
    {
        const int key = vertex_key(x, y, z);

        const auto i = key_to_vertex.find(key);
        if (i != key_to_vertex.end()) {
            return i->second;
        }

        const float x_p = shaped_rel(x, cells.x) * half_size.x;
        const float y_p = shaped_rel(y, cells.y) * half_size.y;
        const float z_p = shaped_rel(z, cells.z) * half_size.z;

        const GEO::index_t vertex_id = mesh.vertices.create_vertex();
        set_pointf(mesh.vertices, vertex_id, GEO::vec3f{x_p, y_p, z_p});
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
        const GEO::index_t vertex_id = key_to_vertex[vertex_key(x, y, z)];
        mesh.facets.set_vertex(facet, local_facet_corner, vertex_id);
        const GEO::index_t corner_id = mesh.facets.corner(facet, local_facet_corner);
        // Every box surface vertex lies on at least one face-extreme plane
        // (faces meet at edges/corners with differing normals and texture
        // coordinates), so all attributes live on the CORNER domain.
        attributes.corner_normal    .set(corner_id, n);
        attributes.corner_texcoord_0.set(corner_id, GEO::vec2f{s, t});
        GEO::vec4f B;
        GEO::vec4f T;
        ortho_basis_pixar_r1(n, B, T);
        attributes.corner_tangent  .set(corner_id, T);
        attributes.corner_bitangent.set(corner_id, GEO::vec3f{B.x, B.y, B.z});

        return corner_id;
    }

    Box_builder(GEO::Mesh& mesh, const GEO::vec3f half_size, const GEO::vec3i cells, const float p)
        : mesh      {mesh}
        , half_size {half_size}
        , cells     {cells}
        , p         {p}
        , attributes{mesh}
    {
    }

    void build()
    {
        int x;
        int y;
        int z;

        // Generate vertices (surface grid; interior of each face plus the
        // shared edges/corners - make_vertex dedupes by grid key)
        for (x = 0; x <= cells.x; x++) {
            for (z = 0; z <= cells.z; z++) {
                make_vertex(x, cells.y, z);
                make_vertex(x, 0,       z);
            }
            for (y = 0; y <= cells.y; y++) {
                make_vertex(x, y, cells.z);
                make_vertex(x, y, 0      );
            }
        }
        for (z = 0; z <= cells.z; z++) {
            for (y = 0; y <= cells.y; y++) {
                make_vertex(cells.x, y, z);
                make_vertex(0,       y, z);
            }
        }

        // Generate quads
        const GEO::vec3f unit_x(1.0f, 0.0f, 0.0f);
        const GEO::vec3f unit_y(0.0f, 1.0f, 0.0f);
        const GEO::vec3f unit_z(0.0f, 0.0f, 1.0f);
        for (x = 0; x < cells.x; x++) {
            const float rel_x1 = uv_rel(x,     cells.x, half_size.x);
            const float rel_x2 = uv_rel(x + 1, cells.x, half_size.x);
            for (z = 0; z < cells.z; z++) {
                const float rel_z1 = uv_rel(z,     cells.z, half_size.z);
                const float rel_z2 = uv_rel(z + 1, cells.z, half_size.z);

                const GEO::index_t top_facet = mesh.facets.create_quads(1);
                make_corner(top_facet, 0, x,     cells.y, z,     unit_y, rel_x1, rel_z1);
                make_corner(top_facet, 1, x,     cells.y, z + 1, unit_y, rel_x1, rel_z2);
                make_corner(top_facet, 2, x + 1, cells.y, z + 1, unit_y, rel_x2, rel_z2);
                make_corner(top_facet, 3, x + 1, cells.y, z,     unit_y, rel_x2, rel_z1);

                const GEO::index_t bottom_facet = mesh.facets.create_quads(1);
                make_corner(bottom_facet, 0, x + 1, 0, z,     -unit_y, rel_x2, rel_z1);
                make_corner(bottom_facet, 1, x + 1, 0, z + 1, -unit_y, rel_x2, rel_z2);
                make_corner(bottom_facet, 2, x,     0, z + 1, -unit_y, rel_x1, rel_z2);
                make_corner(bottom_facet, 3, x,     0, z,     -unit_y, rel_x1, rel_z1);

                attributes.facet_normal.set(top_facet,     unit_y);
                attributes.facet_normal.set(bottom_facet, -unit_y);
            }
            for (y = 0; y < cells.y; y++) {
                const float rel_y1 = uv_rel(y,     cells.y, half_size.y);
                const float rel_y2 = uv_rel(y + 1, cells.y, half_size.y);

                const GEO::index_t back_facet = mesh.facets.create_quads(1);
                make_corner(back_facet, 0, x + 1, y,     cells.z, unit_z, rel_x2, rel_y1);
                make_corner(back_facet, 1, x + 1, y + 1, cells.z, unit_z, rel_x2, rel_y2);
                make_corner(back_facet, 2, x,     y + 1, cells.z, unit_z, rel_x1, rel_y2);
                make_corner(back_facet, 3, x,     y,     cells.z, unit_z, rel_x1, rel_y1);

                const GEO::index_t front_facet = mesh.facets.create_quads(1);
                make_corner(front_facet, 0, x,     y,     0, -unit_z, rel_x1, rel_y1);
                make_corner(front_facet, 1, x,     y + 1, 0, -unit_z, rel_x1, rel_y2);
                make_corner(front_facet, 2, x + 1, y + 1, 0, -unit_z, rel_x2, rel_y2);
                make_corner(front_facet, 3, x + 1, y,     0, -unit_z, rel_x2, rel_y1);

                attributes.facet_normal.set(back_facet,   unit_z);
                attributes.facet_normal.set(front_facet, -unit_z);
            }
        }

        for (z = 0; z < cells.z; z++) {
            const float rel_z1 = uv_rel(z,     cells.z, half_size.z);
            const float rel_z2 = uv_rel(z + 1, cells.z, half_size.z);

            for (y = 0; y < cells.y; y++) {
                const float rel_y1 = uv_rel(y,     cells.y, half_size.y);
                const float rel_y2 = uv_rel(y + 1, cells.y, half_size.y);

                const GEO::index_t right_facet = mesh.facets.create_quads(1);
                make_corner(right_facet, 0, cells.x, y + 1, z,     unit_x, rel_y2, rel_z1);
                make_corner(right_facet, 1, cells.x, y + 1, z + 1, unit_x, rel_y2, rel_z2);
                make_corner(right_facet, 2, cells.x, y,     z + 1, unit_x, rel_y1, rel_z2);
                make_corner(right_facet, 3, cells.x, y,     z,     unit_x, rel_y1, rel_z1);

                const GEO::index_t left_facet = mesh.facets.create_quads(1);
                make_corner(left_facet, 0, 0, y,     z,     -unit_x, rel_y1, rel_z1);
                make_corner(left_facet, 1, 0, y,     z + 1, -unit_x, rel_y1, rel_z2);
                make_corner(left_facet, 2, 0, y + 1, z + 1, -unit_x, rel_y2, rel_z2);
                make_corner(left_facet, 3, 0, y + 1, z,     -unit_x, rel_y2, rel_z1);

                attributes.facet_normal.set(right_facet,  unit_x);
                attributes.facet_normal.set(left_facet,  -unit_x);
            }
        }
    }
};

void make_box(GEO::Mesh& mesh, const GEO::vec3f size, const GEO::vec3i subdivisions, const float p)
{
    const GEO::vec3i cells{
        std::max(0, subdivisions.x) + 1,
        std::max(0, subdivisions.y) + 1,
        std::max(0, subdivisions.z) + 1
    };
    Box_builder builder{mesh, GEO::vec3f{size / 2.0f}, cells, p};
    builder.build();
}

} // namespace erhe::geometry::shapes
