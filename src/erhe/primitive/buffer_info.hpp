#pragma once

#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/primitive/enums.hpp"

#include <memory>

namespace erhe::graphics
{
    class Buffer;
    class Vertex_format;
}

namespace erhe::primitive
{

struct Buffer_info
{
    gl::Buffer_usage                               usage       {gl::Buffer_usage::static_draw};
    Normal_style                                   normal_style{Normal_style::corner_normals};
    gl::Draw_elements_type                         index_type  {gl::Draw_elements_type::unsigned_short};
    std::shared_ptr<erhe::graphics::Buffer>        index_buffer;
    std::shared_ptr<erhe::graphics::Buffer>        vertex_buffer;
    std::shared_ptr<erhe::graphics::Vertex_format> vertex_format;
};

}