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
    Buffer_info() = default;

    explicit Buffer_info(Buffer_sink* buffer_sink)
        : buffer_sink{buffer_sink}
    {
    }

    Buffer_info(const Buffer_info& other)
        : usage        {other.usage}
        , normal_style {other.normal_style}
        , index_type   {other.index_type}
        , vertex_format{other.vertex_format}
        , buffer_sink  {other.buffer_sink}
    {
    }

    Buffer_info(Buffer_info&& other) noexcept
        : usage        {other.usage}
        , normal_style {other.normal_style}
        , index_type   {other.index_type}
        , vertex_format{other.vertex_format}
        , buffer_sink  {other.buffer_sink}
    {
    }

    auto operator=(const Buffer_info& other) -> Buffer_info&
    {
        usage         = other.usage;
        normal_style  = other.normal_style;
        index_type    = other.index_type;
        vertex_format = other.vertex_format;
        buffer_sink   = other.buffer_sink;
        return *this;
    }

    auto operator=(Buffer_info&& other) noexcept -> Buffer_info&
    {
        usage         = other.usage;
        normal_style  = other.normal_style;
        index_type    = other.index_type;
        vertex_format = other.vertex_format;
        buffer_sink   = other.buffer_sink;
        return *this;
    }

    gl::Buffer_usage                               usage        {gl::Buffer_usage::static_draw};
    Normal_style                                   normal_style {Normal_style::corner_normals};
    gl::Draw_elements_type                         index_type   {gl::Draw_elements_type::unsigned_short};
    std::shared_ptr<erhe::graphics::Vertex_format> vertex_format;
    Buffer_sink*                                   buffer_sink  {nullptr};
};


}