#pragma once

#include "erhe/gl/strong_gl_enums.hpp"
#include <cstddef>

namespace erhe::primitive
{

struct Index_range
{
    gl::Primitive_type primitive_type{gl::Primitive_type::triangles};
    size_t             first_index{0};
    size_t             index_count{0};
};

} // namespace erhe::primitive
