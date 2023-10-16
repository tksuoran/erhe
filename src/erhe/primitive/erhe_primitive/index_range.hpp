#pragma once

//#include "erhe_gl/wrapper_enums.hpp"
#include "igl/Buffer.h"

#include <cstddef>

namespace erhe::primitive
{

/// Holds index range for specified GL primitive type
class Index_range
{
public:
    igl::PrimitiveType primitive_type{igl::PrimitiveType::Triangle};
    std::size_t        first_index   {0};
    std::size_t        index_count   {0};
};

} // namespace erhe::primitive
