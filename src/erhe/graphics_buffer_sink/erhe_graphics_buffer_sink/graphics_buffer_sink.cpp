#include "erhe_graphics_buffer_sink/graphics_buffer_sink.hpp"
#include "erhe_primitive/buffer_writer.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"

namespace erhe::graphics_buffer_sink {

Graphics_buffer_sink::Graphics_buffer_sink(
    erhe::graphics::Buffer_transfer_queue&         buffer_transfer_queue,
    std::initializer_list<erhe::graphics::Buffer*> vertex_buffers,
    erhe::graphics::Buffer*                        index_buffer
)
    : m_buffer_transfer_queue{buffer_transfer_queue}
    , m_index_buffer_allocator{index_buffer}
{
    for (erhe::graphics::Buffer* vb : vertex_buffers) {
        m_vertex_buffer_allocators.emplace_back(vb);
    }
}

auto Graphics_buffer_sink::allocate_vertex_buffer(const std::size_t stream, const std::size_t vertex_count, const std::size_t vertex_element_size) -> erhe::primitive::Buffer_range
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    const std::size_t                 allocation_byte_count   = vertex_count * vertex_element_size;
    const std::size_t                 allocation_alignment    = vertex_element_size;
    erhe::graphics::Buffer_allocator& vertex_buffer_allocator = m_vertex_buffer_allocators.at(stream);
    const std::optional<std::size_t>  byte_offset_opt         = vertex_buffer_allocator.allocate_bytes(allocation_byte_count, allocation_alignment);
    if (!byte_offset_opt.has_value()) {
        // TODO log here?
        // log->error(
        //     "Graphics_buffer_sink::allocate_vertex_buffer(): Out of memory requesting {} bytes, currently allocated {}, total size {}, free {} bytes",
        //     allocation_byte_count,
        //     vertex_buffer->get_used_byte_count(),
        //     vertex_buffer->get_capacity_byte_count(),
        //     vertex_buffer->get_available_byte_count(allocation_alignment)
        // );
        return {};
    }

    return erhe::primitive::Buffer_range{
        .count        = vertex_count,
        .element_size = vertex_element_size,
        .byte_offset  = byte_offset_opt.value(),
        .stream       = stream
    };
}

auto Graphics_buffer_sink::allocate_index_buffer(
    const std::size_t index_count,
    const std::size_t index_element_size
) -> erhe::primitive::Buffer_range
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    const std::optional<std::size_t> byte_offset_opt = m_index_buffer_allocator.allocate_bytes(index_count * index_element_size);
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
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    m_buffer_transfer_queue.enqueue(m_index_buffer_allocator.get_buffer(), offset, std::move(data));
}

void Graphics_buffer_sink::enqueue_vertex_data(const std::size_t stream, const std::size_t offset, std::vector<uint8_t>&& data) const
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    m_buffer_transfer_queue.enqueue(m_vertex_buffer_allocators.at(stream).get_buffer(), offset, std::move(data));
}

void Graphics_buffer_sink::buffer_ready(erhe::primitive::Vertex_buffer_writer& writer) const
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    m_buffer_transfer_queue.enqueue(m_vertex_buffer_allocators.at(writer.stream).get_buffer(), writer.start_offset(), std::move(writer.vertex_data));
}

void Graphics_buffer_sink::buffer_ready(erhe::primitive::Index_buffer_writer& writer) const
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    m_buffer_transfer_queue.enqueue(m_index_buffer_allocator.get_buffer(), writer.start_offset(), std::move(writer.index_data));
}

auto Graphics_buffer_sink::get_used_vertex_byte_count(std::size_t stream) const -> std::size_t
{
    return m_vertex_buffer_allocators.at(stream).get_used_byte_count();
}

auto Graphics_buffer_sink::get_available_vertex_byte_count(std::size_t stream, std::size_t alignment) const -> std::size_t
{
    return m_vertex_buffer_allocators.at(stream).get_available_byte_count(alignment);
}

auto Graphics_buffer_sink::get_used_index_byte_count() const -> std::size_t
{
    return m_index_buffer_allocator.get_used_byte_count();
}

auto Graphics_buffer_sink::get_available_index_byte_count (std::size_t alignment) const -> std::size_t
{
    return m_index_buffer_allocator.get_available_byte_count(alignment);
}

} // namespace erhe::graphics_buffer_sink
