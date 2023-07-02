#pragma once

#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/primitive/enums.hpp"

#include <memory>

namespace erhe::graphics
{
    class Vertex_format;
}

namespace erhe::primitive
{

class Buffer_sink;

class Buffer_info
{
public:
    gl::Buffer_usage                     usage        {gl::Buffer_usage::static_draw};
    Normal_style                         normal_style {Normal_style::corner_normals};
    gl::Draw_elements_type               index_type   {gl::Draw_elements_type::unsigned_short};
    const erhe::graphics::Vertex_format& vertex_format;
    Buffer_sink&                         buffer_sink;
};


}