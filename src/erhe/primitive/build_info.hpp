#pragma once

#include "erhe/primitive/buffer_info.hpp"
#include "erhe/primitive/format_info.hpp"

namespace erhe::primitive
{

class Build_info
{
public:
    Build_info() = default;

    explicit Build_info(Buffer_sink* buffer_sink)
        : buffer{buffer_sink}
    {
    }

    Format_info format;
    Buffer_info buffer;
};

} // namespace erhe::primitive
