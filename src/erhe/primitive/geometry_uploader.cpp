#pragma once

#include "erhe/primitive/geometry_uploader.hpp"
#include "erhe/primitive/buffer_writer.hpp"
#include "erhe/primitive/primitive_geometry.hpp"

namespace erhe::primitive
{

Geometry_uploader::Geometry_uploader(const Format_info& format_info,
                                     const Buffer_info& buffer_info)
    : format_info{format_info}
    , buffer_info{buffer_info}
{
}

Geometry_uploader::~Geometry_uploader()
{
}

Geometry_uploader::Geometry_uploader(const Geometry_uploader& other)
    : format_info{other.format_info}
    , buffer_info{other.buffer_info}
{
}

Geometry_uploader::Geometry_uploader(Geometry_uploader&& other) noexcept
    : format_info{other.format_info}
    , buffer_info{other.buffer_info}
{
}

Gl_geometry_uploader::Gl_geometry_uploader(erhe::graphics::Buffer_transfer_queue& queue,
                                           const Format_info&                     format_info,
                                           const Buffer_info&                     buffer_info)
    : queue            {queue}
    , Geometry_uploader{format_info, buffer_info}
{
}

Gl_geometry_uploader::~Gl_geometry_uploader()
{
}

Gl_geometry_uploader::Gl_geometry_uploader(const Gl_geometry_uploader& other)
    : Geometry_uploader{other}
    , queue            {other.queue}
{
}

Gl_geometry_uploader::Gl_geometry_uploader(Gl_geometry_uploader&& other) noexcept
    : Geometry_uploader{other}
    , queue            {other.queue}
{
}


void Gl_geometry_uploader::end(Vertex_buffer_writer& writer) const
{
    queue.enqueue(writer.primitive_geometry.gl_vertex_buffer.get(),
                  writer.primitive_geometry.vertex_byte_offset,
                  std::move(writer.vertex_data));
}

void Gl_geometry_uploader::end(Index_buffer_writer& writer) const
{
    queue.enqueue(writer.primitive_geometry.gl_index_buffer.get(),
                  writer.primitive_geometry.index_byte_offset,
                  std::move(writer.index_data));
}

Embree_geometry_uploader::Embree_geometry_uploader(erhe::raytrace::Device& device,
                                                   const Format_info&      format_info,
                                                   const Buffer_info&      buffer_info)
    : device           {device}
    , Geometry_uploader{format_info, buffer_info}
{
}

Embree_geometry_uploader::~Embree_geometry_uploader()
{
}

Embree_geometry_uploader::Embree_geometry_uploader(const Embree_geometry_uploader& other)
    : Geometry_uploader{other}
    , device           {other.device}
{
}

Embree_geometry_uploader::Embree_geometry_uploader(Embree_geometry_uploader&& other) noexcept
    : Geometry_uploader{other}
    , device           {other.device}
{
}


void Embree_geometry_uploader::end(Vertex_buffer_writer& writer) const
{
    auto*       buffer = writer.primitive_geometry.embree_vertex_buffer.get();
    const auto  offset = writer.primitive_geometry.vertex_byte_offset;
    const auto& data   = writer.vertex_data;
    // TODO
}

void Embree_geometry_uploader::end(Index_buffer_writer& writer) const
{
    auto*       buffer = writer.primitive_geometry.embree_index_buffer.get();
    const auto  offset = writer.primitive_geometry.index_byte_offset;
    const auto& data   = writer.index_data;
    // TODO
}

} // namespace erhe::primitive
