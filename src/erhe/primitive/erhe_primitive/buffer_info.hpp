#pragma once

#include "erhe_gl/wrapper_enums.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_primitive/enums.hpp"

namespace erhe::graphics {
    class Vertex_format;
}

namespace erhe::primitive {

class Buffer_sink;

class Buffer_info
{
public:
    gl::Buffer_usage                     usage        {gl::Buffer_usage::static_draw};
    Normal_style                         normal_style {Normal_style::corner_normals};
    erhe::dataformat::Format             index_type   {erhe::dataformat::Format::format_16_scalar_uint};
    const erhe::graphics::Vertex_format& vertex_format;
    Buffer_sink&                         buffer_sink;
};

} // namesapce erhe::primitive
