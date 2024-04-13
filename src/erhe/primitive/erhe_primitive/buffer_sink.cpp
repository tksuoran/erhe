#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/buffer_writer.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_raytrace/ibuffer.hpp"

#include "igl/Buffer.h"

namespace erhe::primitive
{

Buffer_sink::~Buffer_sink() noexcept
{
}

Igl_buffer_sink::Igl_buffer_sink(
    igl::IDevice& device,
    igl::IBuffer& vertex_buffer,
    std::size_t   vertex_buffer_offset,
    igl::IBuffer& index_buffer,
    std::size_t   index_buffer_offset
)
    : m_device              {device}
    , m_vertex_buffer       {vertex_buffer}
    , m_vertex_buffer_offset{vertex_buffer_offset}
    , m_index_buffer        {index_buffer}
    , m_index_buffer_offset {index_buffer_offset}
{
}

auto align(const std::size_t value, const std::size_t alignment) -> std::size_t
{
	return ((value + alignment - 1) / alignment) * alignment;
}

auto Igl_buffer_sink::allocate_vertex_buffer(
    const std::size_t vertex_count,
    const std::size_t vertex_element_size
) -> Buffer_range
{
    const auto byte_offset = m_vertex_buffer_offset;
    const auto byte_count  = vertex_count * vertex_element_size;
    m_vertex_buffer_offset += byte_count;

    return Buffer_range{
        .count        = vertex_count,
        .element_size = vertex_element_size,
        .byte_offset  = byte_offset
    };
}

auto Igl_buffer_sink::allocate_index_buffer(
    const std::size_t index_count,
    const std::size_t index_element_size
) -> Buffer_range
{
    m_index_buffer_offset = align(m_index_buffer_offset, index_element_size);
    const auto byte_offset = m_index_buffer_offset;
    const auto byte_count  = index_count * index_element_size;
    m_vertex_buffer_offset += byte_count;

    return Buffer_range{
        .count        = index_count,
        .element_size = index_element_size,
        .byte_offset  = byte_offset
    };
}

void Igl_buffer_sink::enqueue_index_data(std::size_t offset, std::vector<uint8_t>&& data) const
{
    m_index_buffer.upload(data.data(), igl::BufferRange{data.size(), offset});
}

void Igl_buffer_sink::enqueue_vertex_data(std::size_t offset, std::vector<uint8_t>&& data) const
{
    m_vertex_buffer.upload(data.data(), igl::BufferRange{data.size(), offset});
}

void Igl_buffer_sink::buffer_ready(Vertex_buffer_writer& writer) const
{
    m_vertex_buffer.upload(writer.vertex_data.data(), igl::BufferRange{writer.vertex_data.size(), writer.start_offset()});
}

void Igl_buffer_sink::buffer_ready(Index_buffer_writer& writer) const
{
    m_index_buffer.upload(writer.index_data.data(), igl::BufferRange{writer.index_data.size(), writer.start_offset()});
}

Raytrace_buffer_sink::Raytrace_buffer_sink(
    erhe::raytrace::IBuffer& vertex_buffer,
    erhe::raytrace::IBuffer& index_buffer
)
    : m_vertex_buffer{vertex_buffer}
    , m_index_buffer {index_buffer}
{
}

auto Raytrace_buffer_sink::allocate_vertex_buffer(
    const std::size_t vertex_count,
    const std::size_t vertex_element_size
) -> Buffer_range
{
    const auto vertex_byte_offset = m_vertex_buffer.allocate_bytes(
        vertex_count * vertex_element_size,
        vertex_element_size
    );
    return Buffer_range{
        .count        = vertex_count,
        .element_size = vertex_element_size,
        .byte_offset  = vertex_byte_offset
    };
}

auto Raytrace_buffer_sink::allocate_index_buffer(
    const std::size_t index_count,
    const std::size_t index_element_size
) -> Buffer_range
{
    const auto index_byte_offset = m_index_buffer.allocate_bytes(index_count * index_element_size);
    return Buffer_range{
        .count        = index_count,
        .element_size = index_element_size,
        .byte_offset  = index_byte_offset
    };
}

void Raytrace_buffer_sink::enqueue_index_data(std::size_t offset, std::vector<uint8_t>&& data) const
{
    auto buffer_span = m_index_buffer.span();
    auto offset_span = buffer_span.subspan(offset, data.size());
    memcpy(offset_span.data(), data.data(), data.size());
}

void Raytrace_buffer_sink::enqueue_vertex_data(std::size_t offset, std::vector<uint8_t>&& data) const
{
    auto buffer_span = m_vertex_buffer.span();
    auto offset_span = buffer_span.subspan(offset, data.size());
    memcpy(offset_span.data(), data.data(), data.size());
}

void Raytrace_buffer_sink::buffer_ready(Vertex_buffer_writer& writer) const
{
    auto        buffer_span = m_vertex_buffer.span();
    const auto& data        = writer.vertex_data;
    auto        offset_span = buffer_span.subspan(writer.start_offset(), data.size());
    memcpy(offset_span.data(), data.data(), data.size());
}

void Raytrace_buffer_sink::buffer_ready(Index_buffer_writer& writer) const
{
    auto        buffer_span = m_index_buffer.span();
    const auto& data        = writer.index_data;
    auto        offset_span = buffer_span.subspan(writer.start_offset(), data.size());
    memcpy(offset_span.data(), data.data(), data.size());
}

} // namespace erhe::primitive
