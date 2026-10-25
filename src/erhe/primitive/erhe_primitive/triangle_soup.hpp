#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_primitive/enums.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <glm/glm.hpp>

namespace GEO { class Mesh; }

namespace erhe::primitive {

class Element_mappings;

class Triangle_soup
{
public:
    [[nodiscard]] auto get_vertex_count() const -> std::size_t;
    [[nodiscard]] auto get_index_count() const -> std::size_t;

    erhe::dataformat::Vertex_format vertex_format;
    std::vector<uint8_t>            vertex_data;
    std::vector<uint32_t>           index_data;
    Primitive_type                  primitive_type{Primitive_type::triangles};
};

// A copy of `source` whose color attribute is `color` at every vertex: the
// whole-surface color a `Gprim.display_color` names, built into the vertex
// data because that is where the renderers read a mesh's own color from
// (Buffer_mesh::has_vertex_colors). A soup that already carries a color
// attribute has its values overwritten; one that does not gets the attribute
// appended to its first stream, so the copy's stride and offsets differ from
// the source's. Null when `source` has no vertex data to copy.
//
// A copy rather than an edit in place: a Triangle_soup is shared by every
// Primitive built from it, and the color belongs to one mesh.
[[nodiscard]] auto make_triangle_soup_with_constant_color(
    const Triangle_soup& source,
    const glm::vec4&     color
) -> std::shared_ptr<Triangle_soup>;

// The colocation of a soup's vertices into mesh points (geogram vertices):
// vertex_points[v] is the point of soup vertex v, for every vertex of the
// soup (Triangle_soup::get_vertex_count()). A vertex no index references
// has no point and carries no_point. This is the payload of the
// ERHE_mesh_topology glTF extension (doc/gltf_extensions/ERHE_mesh_topology.md).
class Vertex_colocation
{
public:
    static constexpr uint32_t no_point = 0xffffffffu;

    std::vector<uint32_t> vertex_points;
    uint32_t              point_count{0};
};

// Converts the soup to a mesh. Vertices within 1e-5 of each other become one
// point, found by GEO::Geom::colocate() under erhe::geometry::geogram_lock();
// points are numbered in the order of their first reference by ascending
// vertex index. Triangles that collapse onto fewer than three points are
// dropped (Element_mappings::triangle_to_mesh_facet holds GEO::NO_INDEX for
// them).
void mesh_from_triangle_soup(const Triangle_soup& triangle_soup, GEO::Mesh& mesh, Element_mappings& element_mappings);

// As above, and also returns the colocation the conversion used, which the
// overload below reproduces the mesh from.
void mesh_from_triangle_soup(
    const Triangle_soup& triangle_soup,
    GEO::Mesh&           mesh,
    Element_mappings&    element_mappings,
    Vertex_colocation&   out_colocation
);

// Converts the soup with a supplied colocation instead of colocate(): takes
// no geogram lock and runs no geogram algorithm. vertex_points has one entry
// per soup vertex; every index-referenced vertex names a point below
// point_count, every point is named by at least one index-referenced vertex,
// and an unreferenced vertex carries no_point or an id below point_count.
// The point order is the caller's (the first-reference order check of
// ERHE_mesh_topology is the loader's). A point is created at the position of
// its first referenced vertex, and then, as in the colocate path, takes the
// position of its last one. Everything after the colocation is the colocate
// path's code, so the colocation the overload above returns gives a
// bit-identical mesh and mappings. Returns false, with mesh and
// element_mappings untouched, when the soup has no indices, an index is out
// of range, the soup has no position attribute or more than one vertex
// stream, or vertex_points / point_count break the rules above.
[[nodiscard]] auto mesh_from_triangle_soup(
    const Triangle_soup&            triangle_soup,
    std::span<const uint32_t>       vertex_points,
    uint32_t                        point_count,
    GEO::Mesh&                      mesh,
    Element_mappings&               element_mappings
) -> bool;

} // namespace erhe::primitive
