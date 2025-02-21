#pragma once

#include "erhe_geometry/geometry.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_gl/wrapper_enums.hpp"

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

    erhe::graphics::Vertex_format vertex_format;
    gl::Primitive_type            type;
    std::vector<uint8_t>          vertex_data;
    std::vector<uint32_t>         index_data;
};

void mesh_from_triangle_soup(const Triangle_soup& triangle_soup, GEO::Mesh& mesh, Element_mappings& element_mappings);

} // namespace erhe::primitive
