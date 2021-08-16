#pragma once

#include "erhe/primitive/buffer_sink.hpp"
#include "erhe/primitive/buffer_info.hpp"
#include "erhe/primitive/buffer_writer.hpp"
#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/raytrace/buffer.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::primitive
{

Gl_buffer_sink::Gl_buffer_sink(erhe::graphics::Buffer_transfer_queue&         buffer_transfer_queue,
                               const std::shared_ptr<erhe::graphics::Buffer>& vertex_buffer,
                               const std::shared_ptr<erhe::graphics::Buffer>& index_buffer)
    : m_buffer_transfer_queue{buffer_transfer_queue}
    , m_vertex_buffer        {vertex_buffer}
    , m_index_buffer         {index_buffer}
{
}

auto Gl_buffer_sink::allocate_vertex_buffer(const size_t vertex_count,
                                            const size_t vertex_element_size) -> Buffer_range
{
    VERIFY(m_vertex_buffer);
    //if (!m_vertex_buffer)
    //{
    //    constexpr gl::Buffer_storage_mask storage_mask = gl::Buffer_storage_mask::map_write_bit;
    //    m_vertex_buffer = std::make_shared<erhe::graphics::Buffer>(gl::Buffer_target::array_buffer,
    //                                                               vertex_count * vertex_element_size,
    //                                                               storage_mask);
    //}

    const auto byte_offset = m_vertex_buffer->allocate_bytes(vertex_count * vertex_element_size,
                                                         vertex_element_size);

    return Buffer_range{vertex_count,
                        vertex_element_size,
                        byte_offset};
}

auto Gl_buffer_sink::allocate_index_buffer(const size_t index_count,
                                           const size_t index_element_size) -> Buffer_range
{
    VERIFY(m_index_buffer);
    //if (!m_index_buffer)
    //{
    //    constexpr gl::Buffer_storage_mask storage_mask = gl::Buffer_storage_mask::map_write_bit;
    //    m_index_buffer = std::make_shared<erhe::graphics::Buffer>(gl::Buffer_target::element_array_buffer,
    //                                                              m_index_count * m_index_element_size,
    //                                                              storage_mask);
    //}

    const auto index_byte_offset = m_index_buffer->allocate_bytes(index_count * index_element_size);

    return Buffer_range{index_count,
                        index_element_size,
                        index_byte_offset};
}

void Gl_buffer_sink::buffer_ready(Vertex_buffer_writer& writer) const
{
    m_buffer_transfer_queue.enqueue(m_vertex_buffer.get(),
                                    writer.start_offset(),
                                    std::move(writer.vertex_data));
}

void Gl_buffer_sink::buffer_ready(Index_buffer_writer&  writer) const
{
    m_buffer_transfer_queue.enqueue(m_index_buffer.get(),
                                    writer.start_offset(),
                                    std::move(writer.index_data));
}

Embree_buffer_sink::Embree_buffer_sink(const std::shared_ptr<erhe::raytrace::Buffer>& vertex_buffer,
                                       const std::shared_ptr<erhe::raytrace::Buffer>& index_buffer)
    : m_vertex_buffer{vertex_buffer}
    , m_index_buffer {index_buffer}
{
}

auto Embree_buffer_sink::allocate_vertex_buffer(const size_t vertex_count,
                                                const size_t vertex_element_size) -> Buffer_range
{
    VERIFY(m_vertex_buffer != nullptr);
    //if (!m_vertex_buffer)
    //{
    //    m_vertex_buffer = std::make_shared<erhe::raytrace::Buffer>(vertex_count * vertex_element_size);
    //}    return m_vertex_byte_offset;
    const auto vertex_byte_offset = m_vertex_buffer->allocate_bytes(vertex_count * vertex_element_size,
                                                                    vertex_element_size);
    return Buffer_range{vertex_count, vertex_element_size, vertex_byte_offset};
}

auto Embree_buffer_sink::allocate_index_buffer(const size_t index_count,
                                               const size_t index_element_size) -> Buffer_range
{
    VERIFY(m_index_buffer != nullptr);
    //if (!m_index_buffer)
    //{
    //    m_index_buffer = std::make_shared<erhe::raytrace::Buffer>(m_index_count * m_index_element_size);
    //}
    const auto index_byte_offset = m_index_buffer->allocate_bytes(index_count * index_element_size);
    return Buffer_range{index_count, index_element_size, index_byte_offset};
}

void Embree_buffer_sink::buffer_ready(Vertex_buffer_writer& writer) const
{
    auto        buffer_span = m_vertex_buffer->span();
    const auto& data        = writer.vertex_data;
    auto        offset_span = buffer_span.subspan(writer.start_offset(), data.size());
    memcpy(offset_span.data(), data.data(), data.size());
}

void Embree_buffer_sink::buffer_ready(Index_buffer_writer&  writer) const
{
    auto        buffer_span = m_index_buffer->span();
    const auto& data        = writer.index_data;
    auto        offset_span = buffer_span.subspan(writer.start_offset(), data.size());
    memcpy(offset_span.data(), data.data(), data.size());
}


} // namespace erhe::primitive
