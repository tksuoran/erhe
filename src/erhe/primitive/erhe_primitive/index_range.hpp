#pragma once

#include "erhe_primitive/enums.hpp"

#include <cstddef>

namespace erhe::primitive {

/// Holds index range for specified GL primitive type
class Index_range
{
public:
    [[nodiscard]] auto get_primitive_count() const -> std::size_t;
    [[nodiscard]] auto get_point_count    () const -> std::size_t;
    [[nodiscard]] auto get_line_count     () const -> std::size_t;
    [[nodiscard]] auto get_triangle_count () const -> std::size_t;

    Primitive_type primitive_type{Primitive_type::triangles};
    std::size_t    first_index   {0};
    std::size_t    index_count   {0};
};

} // namespace erhe::primitive
