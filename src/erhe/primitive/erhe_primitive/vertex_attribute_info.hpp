#pragma once

#include "erhe_dataformat/vertex_format.hpp"

#include <cstddef>
#include <limits>

namespace erhe::primitive {

class Vertex_attribute_info
{
public:
    Vertex_attribute_info();
    Vertex_attribute_info(
        const erhe::dataformat::Vertex_format&   vertex_format,
        erhe::dataformat::Vertex_attribute_usage usage,
        unsigned int                             usage_index
    );

    [[nodiscard]] auto is_valid() -> bool;

    const erhe::dataformat::Vertex_attribute* attribute{nullptr};
    erhe::dataformat::Format                  format   {erhe::dataformat::Format::format_undefined};
    std::size_t                               binding  {std::numeric_limits<std::size_t>::max()};
    std::size_t                               offset   {std::numeric_limits<std::size_t>::max()};
    std::size_t                               size     {0};
};

} // namespace erhe::primitive
