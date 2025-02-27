#include "erhe_graphics_buffer_sink/graphics_buffer_sink.hpp"
#include "erhe_primitive/buffer_writer.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"

namespace erhe::graphics_buffer_sink {

Graphics_buffer_sink::Graphics_buffer_sink(
    erhe::graphics::Buffer_transfer_queue&         buffer_transfer_queue,
    std::initializer_list<erhe::graphics::Buffer*> vertex_buffers,
    erhe::graphics::Buffer&                        index_buffer
)
    : m_buffer_transfer_queue{buffer_transfer_queue}
    , m_vertex_buffers       {vertex_buffers}
    , m_index_buffer         {index_buffer}
{
}

auto Graphics_buffer_sink::allocate_vertex_buffer(const std::size_t stream, const std::size_t vertex_count, const std::size_t vertex_element_size) -> erhe::primitive::Buffer_range
{
    const std::optional<std::size_t> byte_offset_opt = m_vertex_buffers.at(stream)->allocate_bytes(vertex_count * vertex_element_size, vertex_element_size);
    if (!byte_offset_opt.has_value()) {
        return {};
    }

    return erhe::primitive::Buffer_range{
        .count        = vertex_count,
        .element_size = vertex_element_size,
        .byte_offset  = byte_offset_opt.value(),
        .stream       = stream
    };
}

auto Graphics_buffer_sink::allocate_index_buffer(const std::size_t index_count, const std::size_t index_element_size) -> erhe::primitive::Buffer_range
{
    const std::optional<std::size_t> byte_offset_opt = m_index_buffer.allocate_bytes(index_count * index_element_size);
    if (!byte_offset_opt.has_value()) {
        return {};
    }

    return erhe::primitive::Buffer_range{
        .count        = index_count,
        .element_size = index_element_size,
        .byte_offset  = byte_offset_opt.value()
    };
}

void Graphics_buffer_sink::enqueue_index_data(const std::size_t offset, std::vector<uint8_t>&& data) const
{
    m_buffer_transfer_queue.enqueue(m_index_buffer, offset, std::move(data));
}

void Graphics_buffer_sink::enqueue_vertex_data(const std::size_t stream, const std::size_t offset, std::vector<uint8_t>&& data) const
{
    m_buffer_transfer_queue.enqueue(*m_vertex_buffers.at(stream), offset, std::move(data));
}

void Graphics_buffer_sink::buffer_ready(erhe::primitive::Vertex_buffer_writer& writer) const
{
    m_buffer_transfer_queue.enqueue(*m_vertex_buffers.at(writer.stream), writer.start_offset(), std::move(writer.vertex_data));
}

void Graphics_buffer_sink::buffer_ready(erhe::primitive::Index_buffer_writer& writer) const
{
    m_buffer_transfer_queue.enqueue(m_index_buffer, writer.start_offset(), std::move(writer.index_data));
}

} // namespace erhe::graphics_buffer_sink
