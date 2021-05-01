#ifndef index_range_hpp_erhe_mesh
#define index_range_hpp_erhe_mesh

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

#endif
