#include "erhe_geometry/shapes/regular_polyhedron.hpp"

#include <geogram/mesh/mesh.h>

#include <array>
#include <cmath>  // for sqrt

namespace erhe::geometry::shapes {

void make_cuboctahedron(GEO::Mesh& mesh, const double r)
{
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
}

void make_dodecahedron(GEO::Mesh& mesh, const double r)
{
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
}

void make_icosahedron(GEO::Mesh& mesh, const double r)
{
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
}

void make_octahedron(GEO::Mesh& mesh, const double r)
{
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
}

void make_tetrahedron(GEO::Mesh& mesh, double r)
{
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
}

void make_cube(GEO::Mesh& mesh, const double r)
{
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
}

} // namespace erhe::geometry::shapes
