#pragma once

#include <glm/glm.hpp>

#include <array>
#include <string_view>
#include <vector>

namespace erhe::renderer
{

enum class Primitive_color_source : unsigned int
{
    id_offset = 0,
    mesh_wireframe_color,
    constant_color,
};

static constexpr std::array<std::string_view, 3> c_primitive_color_source_strings =
{
    "ID Offset",
    "Mesh Wireframe color",
    "Constant Color"
};

static constexpr std::array<const char*, 3> c_primitive_color_source_strings_data{
    std::apply(
        [](auto&&... s)
        {
            return std::array{s.data()...};
        },
        c_primitive_color_source_strings
    )
};

Primitive_color_source primitive_color_source  {Primitive_color_source::constant_color};
glm::vec4              primitive_constant_color{1.0f, 1.0f, 1.0f, 1.0f};

} // namespace erhe::renderer
