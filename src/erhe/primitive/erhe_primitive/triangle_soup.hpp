#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_primitive/enums.hpp"

#include <cstdint>
#include <vector>

namespace GEO {
    class Mesh;
}

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

void mesh_from_triangle_soup(const Triangle_soup& triangle_soup, GEO::Mesh& mesh, Element_mappings& element_mappings);

} // namespace erhe::primitive
