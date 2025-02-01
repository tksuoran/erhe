#include "erhe_geometry/shapes/regular_polygon.hpp"
#include "erhe_geometry/geometry.hpp"

#include <geogram/mesh/mesh.h>

#include <cmath>  // for sqrt

namespace erhe::geometry::shapes {

void make_triangle(GEO::Mesh& mesh, const double r)
{
    Mesh_attributes attributes{mesh};
    const double a = sqrt(3.0) / 3.0; // 0.57735027
    const double b = sqrt(3.0) / 6.0; // 0.28867513

    mesh.vertices.create_vertices(3);
    mesh.vertices.point(0) = GEO::vec3{r * -b, r * -0.5, 0.0}; // 1.0f, 0.0f
    mesh.vertices.point(1) = GEO::vec3{r *  a, r *  0.0, 0.0}; // 1.0f, 1.0f
    mesh.vertices.point(2) = GEO::vec3{r * -b, r *  0.5, 0.0}; // 0.0f, 1.0f

    GEO::Attribute<GEO::vec2f> vertex_texcoords{mesh.vertices.attributes(), c_texcoord_0};
    attributes.vertex_texcoord_0.set(0, GEO::vec2f{1.0f, 0.0f});
    attributes.vertex_texcoord_0.set(1, GEO::vec2f{1.0f, 1.0f});
    attributes.vertex_texcoord_0.set(2, GEO::vec2f{0.0f, 1.0f});

    mesh.facets.create_triangle(0, 1, 2);
}

void make_quad(GEO::Mesh& mesh, const double edge) 
{
    Mesh_attributes attributes{mesh};
    mesh.vertices.create_vertices(4);
    mesh.vertices.point(0) = GEO::vec3{edge * -0.5, edge * -0.5, 0.0};
    mesh.vertices.point(1) = GEO::vec3{edge *  0.5, edge * -0.5, 0.0};
    mesh.vertices.point(2) = GEO::vec3{edge *  0.5, edge *  0.5, 0.0};
    mesh.vertices.point(3) = GEO::vec3{edge * -0.5, edge *  0.5, 0.0};

    attributes.vertex_texcoord_0.set(0, GEO::vec2f{0.0f, 0.0f});
    attributes.vertex_texcoord_0.set(1, GEO::vec2f{1.0f, 0.0f});
    attributes.vertex_texcoord_0.set(2, GEO::vec2f{1.0f, 1.0f});
    attributes.vertex_texcoord_0.set(3, GEO::vec2f{0.0f, 1.0f});

    mesh.facets.create_quad(0, 1, 2, 3);

    // Double sided - TODO Make back face optional?
    mesh.facets.create_quad(3, 2, 1, 0);

}

void make_rectangle(GEO::Mesh& mesh, const double width, const double height, const bool front_face, const bool back_face)
{
    Mesh_attributes attributes{mesh};
    mesh.vertices.create_vertices(8);

    mesh.vertices.point(0) = GEO::vec3{width * -0.5, height * -0.5, 0.0};
    mesh.vertices.point(1) = GEO::vec3{width *  0.5, height * -0.5, 0.0};
    mesh.vertices.point(2) = GEO::vec3{width *  0.5, height *  0.5, 0.0};
    mesh.vertices.point(3) = GEO::vec3{width * -0.5, height *  0.5, 0.0};

    attributes.vertex_texcoord_0.set(0, GEO::vec2f{0.0f, 0.0f});
    attributes.vertex_texcoord_0.set(1, GEO::vec2f{1.0f, 0.0f});
    attributes.vertex_texcoord_0.set(2, GEO::vec2f{1.0f, 1.0f});
    attributes.vertex_texcoord_0.set(3, GEO::vec2f{0.0f, 1.0f});

    // Texcoords X-flipped
    mesh.vertices.point(4) = GEO::vec3{width * -0.5, height * -0.5, 0.0};
    mesh.vertices.point(5) = GEO::vec3{width *  0.5, height * -0.5, 0.0};
    mesh.vertices.point(6) = GEO::vec3{width *  0.5, height *  0.5, 0.0};
    mesh.vertices.point(7) = GEO::vec3{width * -0.5, height *  0.5, 0.0};

    attributes.vertex_texcoord_0.set(4, GEO::vec2f{1.0f, 0.0f});
    attributes.vertex_texcoord_0.set(5, GEO::vec2f{0.0f, 0.0f});
    attributes.vertex_texcoord_0.set(6, GEO::vec2f{0.0f, 1.0f});
    attributes.vertex_texcoord_0.set(7, GEO::vec2f{1.0f, 1.0f});

    if (front_face) {
        mesh.facets.create_quad(0, 1, 2, 3);
    }
    if (back_face) {
        mesh.facets.create_quad(4, 5, 6, 7);
        //mesh.facets.create_quad(3, 2, 1, 0);
    }
}

} // namespace erhe::geometry::shapes
