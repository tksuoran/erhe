#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_primitive/enums.hpp"

#include <cstdint>
#include <memory>
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

void mesh_from_triangle_soup(const Triangle_soup& triangle_soup, GEO::Mesh& mesh, Element_mappings& element_mappings);

} // namespace erhe::primitive
