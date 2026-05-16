#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_primitive/enums.hpp"

namespace erhe::primitive {

class Vertex_buffer_sink;
class Index_buffer_sink;

class Buffer_info
{
public:
    Normal_style                           normal_style{Normal_style::corner_normals};
    erhe::dataformat::Format               index_type  {erhe::dataformat::Format::format_16_scalar_uint};
    const erhe::dataformat::Vertex_format& vertex_format;
    Vertex_buffer_sink&                    vertex_buffer_sink;
    Index_buffer_sink&                     index_buffer_sink;
    size_t                                 vertex_input_key;
};

} // namesapce erhe::primitive
