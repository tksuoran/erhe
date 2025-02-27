#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_primitive/enums.hpp"

namespace erhe::primitive {

class Buffer_sink;

class Buffer_info
{
public:
    Normal_style                           normal_style {Normal_style::corner_normals};
    erhe::dataformat::Format               index_type   {erhe::dataformat::Format::format_16_scalar_uint};
    const erhe::dataformat::Vertex_format& vertex_format;
    Buffer_sink&                           buffer_sink;
};

} // namesapce erhe::primitive
