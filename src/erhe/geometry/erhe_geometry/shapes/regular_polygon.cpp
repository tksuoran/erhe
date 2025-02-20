#include "erhe_geometry/shapes/regular_polygon.hpp"
#include "erhe_geometry/geometry.hpp"

#include <geogram/mesh/mesh.h>

#include <cmath>  // for sqrt

namespace erhe::geometry::shapes {

void make_triangle(GEO::Mesh& mesh, const float r)
{
    Mesh_attributes attributes{mesh};
    const float a = std::sqrt(3.0f) / 3.0f; // 0.57735027
    const float b = std::sqrt(3.0f) / 6.0f; // 0.28867513

    mesh.vertices.create_vertices(3);
    set_pointf(mesh.vertices, 0, GEO::vec3f{r * -b, r * -0.5f, 0.0f}); // 1.0f, 0.0f
    set_pointf(mesh.vertices, 1, GEO::vec3f{r *  a, r *  0.0f, 0.0f}); // 1.0f, 1.0f
    set_pointf(mesh.vertices, 2, GEO::vec3f{r * -b, r *  0.5f, 0.0f}); // 0.0f, 1.0f

    GEO::Attribute<GEO::vec2f> vertex_texcoords{mesh.vertices.attributes(), c_texcoord_0};
    attributes.vertex_texcoord_0.set(0, GEO::vec2f{1.0f, 0.0f});
    attributes.vertex_texcoord_0.set(1, GEO::vec2f{1.0f, 1.0f});
    attributes.vertex_texcoord_0.set(2, GEO::vec2f{0.0f, 1.0f});

    mesh.facets.create_triangle(0, 1, 2);
}

void make_quad(GEO::Mesh& mesh, const float edge) 
{
    Mesh_attributes attributes{mesh};
    mesh.vertices.create_vertices(4);
    set_pointf(mesh.vertices, 0, GEO::vec3f{edge * -0.5f, edge * -0.5f, 0.0});
    set_pointf(mesh.vertices, 1, GEO::vec3f{edge *  0.5f, edge * -0.5f, 0.0});
    set_pointf(mesh.vertices, 2, GEO::vec3f{edge *  0.5f, edge *  0.5f, 0.0});
    set_pointf(mesh.vertices, 3, GEO::vec3f{edge * -0.5f, edge *  0.5f, 0.0});

    attributes.vertex_texcoord_0.set(0, GEO::vec2f{0.0f, 0.0f});
    attributes.vertex_texcoord_0.set(1, GEO::vec2f{1.0f, 0.0f});
    attributes.vertex_texcoord_0.set(2, GEO::vec2f{1.0f, 1.0f});
    attributes.vertex_texcoord_0.set(3, GEO::vec2f{0.0f, 1.0f});

    mesh.facets.create_quad(0, 1, 2, 3);

    // Double sided - TODO Make back face optional?
    mesh.facets.create_quad(3, 2, 1, 0);

}

void make_rectangle(GEO::Mesh& mesh, const float width, const float height, const bool front_face, const bool back_face)
{
    Mesh_attributes attributes{mesh};
    mesh.vertices.create_vertices(8);

    set_pointf(mesh.vertices, 0, GEO::vec3f{width * -0.5f, height * -0.5f, 0.0f});
    set_pointf(mesh.vertices, 1, GEO::vec3f{width *  0.5f, height * -0.5f, 0.0f});
    set_pointf(mesh.vertices, 2, GEO::vec3f{width *  0.5f, height *  0.5f, 0.0f});
    set_pointf(mesh.vertices, 3, GEO::vec3f{width * -0.5f, height *  0.5f, 0.0f});

    attributes.vertex_texcoord_0.set(0, GEO::vec2f{0.0f, 0.0f});
    attributes.vertex_texcoord_0.set(1, GEO::vec2f{1.0f, 0.0f});
    attributes.vertex_texcoord_0.set(2, GEO::vec2f{1.0f, 1.0f});
    attributes.vertex_texcoord_0.set(3, GEO::vec2f{0.0f, 1.0f});

    // Texcoords X-flipped
    set_pointf(mesh.vertices, 4, GEO::vec3f{width * -0.5f, height * -0.5f, 0.0f});
    set_pointf(mesh.vertices, 5, GEO::vec3f{width *  0.5f, height * -0.5f, 0.0f});
    set_pointf(mesh.vertices, 6, GEO::vec3f{width *  0.5f, height *  0.5f, 0.0f});
    set_pointf(mesh.vertices, 7, GEO::vec3f{width * -0.5f, height *  0.5f, 0.0f});

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
