#include "erhe/primitive/buffer_sink.hpp"
#include "erhe/primitive/buffer_info.hpp"
#include "erhe/primitive/buffer_writer.hpp"
#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/raytrace/ibuffer.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::primitive
{

Buffer_sink::~Buffer_sink() noexcept
{
}

Gl_buffer_sink::Gl_buffer_sink(
    erhe::graphics::Buffer_transfer_queue& buffer_transfer_queue,
    erhe::graphics::Buffer&                vertex_buffer,
    erhe::graphics::Buffer&                index_buffer
)
    : m_buffer_transfer_queue{buffer_transfer_queue}
    , m_vertex_buffer        {vertex_buffer}
    , m_index_buffer         {index_buffer}
{
}

auto Gl_buffer_sink::allocate_vertex_buffer(
    const std::size_t vertex_count,
    const std::size_t vertex_element_size
) -> Buffer_range
{
    const auto byte_offset = m_vertex_buffer.allocate_bytes(
        vertex_count * vertex_element_size,
        vertex_element_size
    );

    return Buffer_range{
        .count        = vertex_count,
        .element_size = vertex_element_size,
        .byte_offset  = byte_offset
    };
}

auto Gl_buffer_sink::allocate_index_buffer(
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

void Gl_buffer_sink::enqueue_index_data(std::size_t offset, std::vector<uint8_t>&& data) const
{
    m_buffer_transfer_queue.enqueue(
        m_index_buffer,
        offset,
        std::move(data)
    );
}

void Gl_buffer_sink::enqueue_vertex_data(std::size_t offset, std::vector<uint8_t>&& data) const
{
    m_buffer_transfer_queue.enqueue(
        m_vertex_buffer,
        offset,
        std::move(data)
    );
}

void Gl_buffer_sink::buffer_ready(Vertex_buffer_writer& writer) const
{
    m_buffer_transfer_queue.enqueue(
        m_vertex_buffer,
        writer.start_offset(),
        std::move(writer.vertex_data)
    );
}

void Gl_buffer_sink::buffer_ready(Index_buffer_writer& writer) const
{
    m_buffer_transfer_queue.enqueue(
        m_index_buffer,
        writer.start_offset(),
        std::move(writer.index_data)
    );
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
