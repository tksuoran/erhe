#include "erhe_geometry/shapes/regular_polyhedron.hpp"
#include "erhe_geometry/geometry.hpp"

#include <geogram/mesh/mesh.h>

#include <algorithm> // for std::min
#include <array>
#include <cmath>  // for sqrt

namespace erhe::geometry::shapes {

namespace {

// Net-based texture coordinates for regular polyhedra, written to slot 0
// (corner_texcoord_0). Texcoords are per-corner so the polyhedron's shared
// vertices need no duplication: each face carries its own UVs. These nets give
// every face a unique, non-overlapping region of the texture.

// Multi-row zig-zag triangle-strip net, normalized to [0,1]^2. Triangles within
// a row share edges (a true unfolding per row); rows > 1 trade single-net
// continuity for squarer, less-stretched cells (e.g. the 20-face icosahedron).
void assign_triangle_strip_texcoords(
    GEO::Mesh&         mesh,
    Mesh_attributes&   attributes,
    const GEO::index_t facet_count,
    const int          rows
)
{
    const float h       = 0.8660254037844386f; // sqrt(3)/2, equilateral row height
    const int   count   = static_cast<int>(facet_count);
    const int   per_row = (count + rows - 1) / rows; // ceil
    const float total_w = (0.5f * static_cast<float>(per_row)) + 0.5f;
    const float total_h = static_cast<float>(rows) * h;

    for (GEO::index_t f = 0; f < facet_count; ++f) {
        const int   index = static_cast<int>(f);
        const int   row   = index / per_row; // 0 = top row
        const int   col   = index % per_row; // position within row
        const int   k     = col / 2;
        const float y0    = static_cast<float>((rows - 1) - row) * h;

        GEO::vec2f p0;
        GEO::vec2f p1;
        GEO::vec2f p2;
        if ((col % 2) == 0) { // up-pointing triangle
            p0 = GEO::vec2f{ static_cast<float>(k),         y0     };
            p1 = GEO::vec2f{ static_cast<float>(k) + 1.0f,  y0     };
            p2 = GEO::vec2f{ static_cast<float>(k) + 0.5f,  y0 + h };
        } else {              // down-pointing triangle
            p0 = GEO::vec2f{ static_cast<float>(k) + 1.0f,  y0     };
            p1 = GEO::vec2f{ static_cast<float>(k) + 0.5f,  y0 + h };
            p2 = GEO::vec2f{ static_cast<float>(k) + 1.5f,  y0 + h };
        }
        attributes.corner_texcoord_0.set(mesh.facets.corner(f, 0), GEO::vec2f{ p0.x / total_w, p0.y / total_h });
        attributes.corner_texcoord_0.set(mesh.facets.corner(f, 1), GEO::vec2f{ p1.x / total_w, p1.y / total_h });
        attributes.corner_texcoord_0.set(mesh.facets.corner(f, 2), GEO::vec2f{ p2.x / total_w, p2.y / total_h });
    }
}

// Octahedral square map: the 4 faces around the +Y apex tile the central
// diamond, the 4 around the -Y apex fold out into the corners, exactly filling
// [0,1]^2. Facet and vertex order must match make_octahedron().
void assign_octahedron_net_texcoords(GEO::Mesh& mesh, Mesh_attributes& attributes)
{
    const GEO::vec2f C { 0.5f, 0.5f }; // center  (both apexes project here / to corners)
    const GEO::vec2f N { 0.5f, 1.0f }; // mid-edges of the central diamond
    const GEO::vec2f E { 1.0f, 0.5f };
    const GEO::vec2f S { 0.5f, 0.0f };
    const GEO::vec2f W { 0.0f, 0.5f };
    const GEO::vec2f BL{ 0.0f, 0.0f }; // square corners (the folded-out -Y faces)
    const GEO::vec2f BR{ 1.0f, 0.0f };
    const GEO::vec2f TR{ 1.0f, 1.0f };
    const GEO::vec2f TL{ 0.0f, 1.0f };
    const GEO::vec2f uv[8][3] = {
        { S,  W, C }, // facet 0 {3,2,0} +Y apex
        { E,  S, C }, // facet 1 {4,3,0} +Y apex
        { N,  E, C }, // facet 2 {5,4,0} +Y apex
        { W,  N, C }, // facet 3 {2,5,0} +Y apex
        { BL, W, S }, // facet 4 {1,2,3} -Y apex
        { BR, S, E }, // facet 5 {1,3,4} -Y apex
        { TR, E, N }, // facet 6 {1,4,5} -Y apex
        { TL, N, W }  // facet 7 {1,5,2} -Y apex
    };
    for (GEO::index_t f = 0; f < 8; ++f) {
        for (GEO::index_t i = 0; i < 3; ++i) {
            attributes.corner_texcoord_0.set(mesh.facets.corner(f, i), uv[f][i]);
        }
    }
}

// Generic per-face net for mixed or non-triangulated solids: each facet is
// packed into its own grid cell. Quads fill the cell; other n-gons are inscribed
// as a regular polygon. Guarantees unique, non-overlapping UVs per face.
void assign_polygon_grid_texcoords(
    GEO::Mesh&         mesh,
    Mesh_attributes&   attributes,
    const GEO::index_t first_facet,
    const GEO::index_t facet_count,
    const int          cols,
    const int          rows
)
{
    const float two_pi  = 6.283185307179586f;
    const float half_pi = 1.5707963267948966f;
    const float cell_w  = 1.0f / static_cast<float>(cols);
    const float cell_h  = 1.0f / static_cast<float>(rows);
    const float margin  = 0.04f; // fraction of a cell, reduces bilinear bleed between faces

    for (GEO::index_t i = 0; i < facet_count; ++i) {
        const GEO::index_t facet = first_facet + i;
        const int          index = static_cast<int>(i);
        const int          col   = index % cols;
        const int          row   = index / cols;
        const float        u0    = (static_cast<float>(col) + margin)        * cell_w;
        const float        u1    = (static_cast<float>(col) + 1.0f - margin) * cell_w;
        const float        v0    = (static_cast<float>(row) + margin)        * cell_h;
        const float        v1    = (static_cast<float>(row) + 1.0f - margin) * cell_h;
        const GEO::index_t n     = mesh.facets.nb_vertices(facet);

        if (n == 4) {
            const GEO::vec2f uv[4] = {
                GEO::vec2f{ u0, v0 },
                GEO::vec2f{ u1, v0 },
                GEO::vec2f{ u1, v1 },
                GEO::vec2f{ u0, v1 }
            };
            for (GEO::index_t j = 0; j < 4; ++j) {
                attributes.corner_texcoord_0.set(mesh.facets.corner(facet, j), uv[j]);
            }
        } else {
            const float cu = 0.5f * (u0 + u1);
            const float cv = 0.5f * (v0 + v1);
            const float r  = 0.5f * std::min(u1 - u0, v1 - v0);
            for (GEO::index_t j = 0; j < n; ++j) {
                const float a = half_pi + ((two_pi * static_cast<float>(j)) / static_cast<float>(n));
                attributes.corner_texcoord_0.set(
                    mesh.facets.corner(facet, j),
                    GEO::vec2f{ cu + (r * std::cos(a)), cv + (r * std::sin(a)) }
                );
            }
        }
    }
}

} // anonymous namespace

void make_cuboctahedron(GEO::Mesh& mesh, const float r)
{
    mesh.vertices.set_double_precision();
    const double sq2 = std::sqrt(2.0);

    const GEO::vec3 vertices[] = {
        { 0,      r,      0          },
        { r / 2,  r / 2,  r * sq2 / 2},
        { r / 2,  r / 2, -r * sq2 / 2},
        { r,      0,      0          },
        { r / 2, -r / 2,  r * sq2 / 2},
        { r / 2, -r / 2, -r * sq2 / 2},
        {     0, -r,      0          },
        {-r / 2, -r / 2,  r * sq2 / 2},
        {-r / 2, -r / 2, -r * sq2 / 2},
        {-r,          0,  0          },
        {-r / 2,  r / 2,  r * sq2 / 2},
        {-r / 2,  r / 2, -r * sq2 / 2}
    };
    const GEO::index_t vertex_count = sizeof(vertices) / sizeof(vertices[0]);
    const GEO::index_t base_vertex = mesh.vertices.create_vertices(sizeof(vertices) / sizeof(vertices[0]));
    for (GEO::index_t i = 0; i < vertex_count; ++i) {
        mesh.vertices.point(base_vertex + i) = vertices[i];
    }

    mesh.facets.create_quad(10,  7, 4,  1);
    mesh.facets.create_quad( 6,  5, 3,  4);
    mesh.facets.create_quad( 1,  3, 2,  0);
    mesh.facets.create_quad( 2,  5, 8, 11);
    mesh.facets.create_quad( 0, 11, 9, 10);
    mesh.facets.create_quad( 9,  8, 6,  7);
    mesh.facets.create_triangle(10, 1,  0);
    mesh.facets.create_triangle( 1, 4,  3);
    mesh.facets.create_triangle( 7, 6,  4);
    mesh.facets.create_triangle( 9, 7, 10);
    mesh.facets.create_triangle( 0, 2, 11);
    mesh.facets.create_triangle( 3, 5,  2);
    mesh.facets.create_triangle( 5, 6,  8);
    mesh.facets.create_triangle(11, 8,  9);

    Mesh_attributes attributes{mesh};
    assign_polygon_grid_texcoords(mesh, attributes, 0, 14, 4, 4);
    mesh.vertices.set_single_precision();
}

void make_dodecahedron(GEO::Mesh& mesh, const float r)
{
    mesh.vertices.set_double_precision();
    {
        const double sq3 = std::sqrt(3.0);
        const double sq5 = std::sqrt(5.0);
        const double a   = 2.0 / (sq3 + sq3 * sq5);
        const double b   = 1.0 / (3.0 * a);
        const GEO::vec3 vertices[] = {
            { r / sq3,  r / sq3,  r / sq3},
            { r / sq3,  r / sq3, -r / sq3},
            { r / sq3, -r / sq3,  r / sq3},
            { r / sq3, -r / sq3, -r / sq3},
            {-r / sq3,  r / sq3,  r / sq3},
            {-r / sq3,  r / sq3, -r / sq3},
            {-r / sq3, -r / sq3,  r / sq3},
            {-r / sq3, -r / sq3, -r / sq3},
            { 0,        r * a,    r * b  },
            { 0,        r * a,   -r * b  },
            { 0,       -r * a,    r * b  },
            { 0,       -r * a,   -r * b  },
            { r * a,    r * b,    0      },
            { r * a,   -r * b,    0      },
            {-r * a,    r * b,    0      },
            {-r * a,   -r * b,    0      },
            { r * b,    0,        r * a  },
            { r * b,    0,       -r * a  },
            {-r * b,    0,        r * a  },
            {-r * b,    0,       -r * a  },
        };
        const GEO::index_t vertex_count = sizeof(vertices) / sizeof(vertices[0]);
        const GEO::index_t base_vertex = mesh.vertices.create_vertices(vertex_count);
        for (GEO::index_t i = 0; i < vertex_count; ++i) {
            mesh.vertices.point(base_vertex + i) = vertices[i];
        }
    }

    {
        const std::array<GEO::index_t, 5> facets[] = {
            { 10,  8, 4, 18,  6},
            {  2, 16, 0,  8, 10},
            { 11,  9, 1, 17,  3},
            {  9, 11, 7, 19,  5},
            { 17, 16, 2, 13,  3},
            {  1, 12, 0, 16, 17},
            {  7, 15, 6, 18, 19},
            { 19, 18, 4, 14,  5},
            { 12, 14, 4,  8,  0},
            {  1,  9, 5, 14, 12},
            {  2, 10, 6, 15, 13},
            { 13, 15, 7, 11,  3}
        };
        const GEO::index_t facet_count = sizeof(facets) / sizeof(facets[0]);
        const GEO::index_t base_facet = mesh.facets.create_facets(facet_count, 5);
        for (GEO::index_t i = 0; i < facet_count; ++i) {
            for (GEO::index_t j = 0; j < 5; ++j) {
                mesh.facets.set_vertex(base_facet + i, j, facets[i][j]);
            }
        }
    }

    Mesh_attributes attributes{mesh};
    assign_polygon_grid_texcoords(mesh, attributes, 0, 12, 4, 3);
    mesh.vertices.set_single_precision();
}

void make_icosahedron(GEO::Mesh& mesh, const float r)
{
    mesh.vertices.set_double_precision();
    {
        const double sq5 = std::sqrt(5.0);
        const double a0  = 2.0 / (1.0 + sq5);
        const double b   = std::sqrt((3.0 + sq5) / (1.0 + sq5));
        const double a   = a0 / b;

        const double points[] = {
             0,      r * a,  r / b,
             0,      r * a, -r / b,
             0,     -r * a,  r / b,
             0,     -r * a, -r / b,
             r * a,  r / b,  0    ,
             r * a, -r / b,  0    ,
            -r * a,  r / b,  0    ,
            -r * a, -r / b,  0    ,
             r / b,  0,      r * a,
             r / b,  0,     -r * a,
            -r / b,  0,      r * a,
            -r / b,  0,     -r * a
        };
        const GEO::index_t point_count = sizeof(points) / (3 * sizeof(points[0]));
        mesh.vertices.assign_points(points, 3, point_count);
    }

    {
        std::array<GEO::index_t, 3> facets[] = {
            { 6,  4, 1 },
            { 4,  6, 0 },
            {10,  2, 0 },
            { 2,  8, 0 },
            { 9,  3, 1 },
            { 3, 11, 1 },
            { 7,  5, 2 },
            { 5,  7, 3 },
            {11, 10, 6 },
            {10, 11, 7 },
            { 8,  9, 4 },
            { 9,  8, 5 },
            { 6, 10, 0 },
            { 8,  4, 0 },
            {11,  6, 1 },
            { 4,  9, 1 },
            { 7, 11, 3 },
            { 9,  5, 3 },
            {10,  7, 2 },
            { 5,  8, 2 }
        };
        const GEO::index_t facet_count = sizeof(facets) / sizeof(facets[0]);
        const GEO::index_t base_facet = mesh.facets.create_facets(facet_count, 3);
        for (GEO::index_t i = 0; i < facet_count; ++i) {
            for (GEO::index_t j = 0; j < 3; ++j) {
                mesh.facets.set_vertex(base_facet + i, j, facets[i][j]);
            }
        }
    }
    mesh.facets.connect();

    Mesh_attributes attributes{mesh};
    assign_triangle_strip_texcoords(mesh, attributes, 20, 4);
    mesh.vertices.set_single_precision();
}

void make_octahedron(GEO::Mesh& mesh, const float r)
{
    mesh.vertices.set_double_precision();
    {
        const GEO::vec3 vertices[] = {
            { 0,  r,  0},
            { 0, -r,  0},
            {-r,  0,  0},
            { 0,  0, -r},
            { r,  0,  0},
            { 0,  0,  r}
        };
        const GEO::index_t vertex_count = sizeof(vertices) / sizeof(vertices[0]);
        const GEO::index_t base_vertex = mesh.vertices.create_vertices(vertex_count);
        for (GEO::index_t i = 0; i < vertex_count; ++i) {
            mesh.vertices.point(base_vertex + i) = vertices[i];
        }
    }
    {
        std::array<GEO::index_t, 3> facets[] = {
            { 3, 2, 0 },
            { 4, 3, 0 },
            { 5, 4, 0 },
            { 2, 5, 0 },
            { 1, 2, 3 },
            { 1, 3, 4 },
            { 1, 4, 5 },
            { 1, 5, 2 }
        };
        const GEO::index_t facet_count = sizeof(facets) / sizeof(facets[0]);
        const GEO::index_t base_facet = mesh.facets.create_facets(facet_count, 3);
        for (GEO::index_t i = 0; i < facet_count; ++i) {
            for (GEO::index_t j = 0; j < 3; ++j) {
                mesh.facets.set_vertex(base_facet + i, j, facets[i][j]);
            }
        }
    }
    mesh.facets.connect();

    Mesh_attributes attributes{mesh};
    assign_octahedron_net_texcoords(mesh, attributes);
    mesh.vertices.set_single_precision();
}

void make_tetrahedron(GEO::Mesh& mesh, float r)
{
    mesh.vertices.set_double_precision();
    {
        const double sq2 = std::sqrt(2.0);
        const double sq3 = std::sqrt(3.0);

        const GEO::vec3 vertices[] = {
            { 0,                    r,        0                  },
            { 0,                   -r / 3.0,  r * 2.0 * sq2 / 3.0},
            {-r * sq3 * sq2 / 3.0, -r / 3.0, -r * sq2 / 3.0      },
            { r * sq3 * sq2 / 3.0, -r / 3.0, -r * sq2 / 3.0      }
        };
        const GEO::index_t vertex_count = sizeof(vertices) / sizeof(vertices[0]);
        const GEO::index_t base_vertex = mesh.vertices.create_vertices(vertex_count);
        for (GEO::index_t i = 0; i < vertex_count; ++i) {
            mesh.vertices.point(base_vertex + i) = vertices[i];
        }
    }

    {
        std::array<GEO::index_t, 3> facets[] = {
            { 2, 1, 0 },
            { 0, 1, 3 },
            { 3, 2, 0 },
            { 1, 2, 3 }
        };
        const GEO::index_t facet_count = sizeof(facets) / sizeof(facets[0]);
        const GEO::index_t base_facet = mesh.facets.create_facets(facet_count, 3);
        for (GEO::index_t i = 0; i < facet_count; ++i) {
            for (GEO::index_t j = 0; j < 3; ++j) {
                mesh.facets.set_vertex(base_facet + i, j, facets[i][j]);
            }
        }
    }
    mesh.facets.connect();

    Mesh_attributes attributes{mesh};
    assign_triangle_strip_texcoords(mesh, attributes, 4, 1);
    mesh.vertices.set_single_precision();
}

void make_cube(GEO::Mesh& mesh, const float r)
{
    mesh.vertices.set_double_precision();
    {
        const double a =  0.5 * r;
        const double b = -0.5 * r;

        const GEO::vec3 vertices[] = {
            { b, b, b }, // 0    2------4
            { a, b, b }, // 1   /|     /|
            { b, a, b }, // 2  6-+----7 |
            { b, b, a }, // 3  | |    | |
            { a, a, b }, // 4  | |    | |
            { a, b, a }, // 5  | 0----|-1
            { b, a, a }, // 6  |/     |/
            { a, a, a }, // 7  3------5
        };
        const GEO::index_t vertex_count = sizeof(vertices) / sizeof(vertices[0]);
        const GEO::index_t base_vertex = mesh.vertices.create_vertices(vertex_count);
        for (GEO::index_t i = 0; i < vertex_count; ++i) {
            mesh.vertices.point(base_vertex + i) = vertices[i];
        }
    }

    {
        std::array<GEO::index_t, 4> facets[] = {
            { 1, 4, 7, 5 }, // x+
            { 2, 6, 7, 4 }, // y+
            { 3, 5, 7, 6 }, // z+
            { 0, 3, 6, 2 }, // x-
            { 0, 1, 5, 3 }, // y-
            { 0, 2, 4, 1 }  // z-
        };
        const GEO::index_t facet_count = sizeof(facets) / sizeof(facets[0]);
        const GEO::index_t base_facet = mesh.facets.create_facets(facet_count, 4);
        for (GEO::index_t i = 0; i < facet_count; ++i) {
            for (GEO::index_t j = 0; j < 4; ++j) {
                mesh.facets.set_vertex(base_facet + i, j, facets[i][j]);
            }
        }
    }
    mesh.facets.connect();

    Mesh_attributes attributes{mesh};
    assign_polygon_grid_texcoords(mesh, attributes, 0, 6, 3, 2);
    mesh.vertices.set_single_precision();
}

} // namespace erhe::geometry::shapes
