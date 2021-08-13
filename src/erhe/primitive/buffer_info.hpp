#pragma once

#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/primitive/enums.hpp"

#include <memory>

namespace erhe::graphics
{
    class Buffer;
    class Vertex_format;
}

namespace erhe::raytrace
{
    class Buffer;
}

namespace erhe::primitive
{

class Buffer_info
{
public:
    gl::Buffer_usage                               usage        {gl::Buffer_usage::static_draw};
    Normal_style                                   normal_style {Normal_style::corner_normals};
    gl::Draw_elements_type                         index_type   {gl::Draw_elements_type::unsigned_short};
    std::shared_ptr<erhe::graphics::Vertex_format> vertex_format;
    std::shared_ptr<erhe::graphics::Buffer>        gl_index_buffer;
    std::shared_ptr<erhe::graphics::Buffer>        gl_vertex_buffer;
    std::shared_ptr<erhe::graphics::Buffer>        embree_index_buffer;
    std::shared_ptr<erhe::graphics::Buffer>        embree_vertex_buffer;
};

}