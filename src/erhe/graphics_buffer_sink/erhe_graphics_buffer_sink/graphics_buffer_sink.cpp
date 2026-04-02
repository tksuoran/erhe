#include "erhe_graphics_buffer_sink/graphics_buffer_sink.hpp"
#include "erhe_primitive/buffer_writer.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"

namespace erhe::graphics_buffer_sink {

Graphics_buffer_sink::Graphics_buffer_sink(
    erhe::graphics::Buffer_transfer_queue&         buffer_transfer_queue,
    std::initializer_list<erhe::graphics::Buffer*> vertex_buffers,
    erhe::graphics::Buffer*                        index_buffer,
    erhe::graphics::Buffer*                        edge_line_vertex_buffer
)
    : m_buffer_transfer_queue    {buffer_transfer_queue}
    , m_index_buffer             {index_buffer}
    , m_index_allocator          {index_buffer->get_capacity_byte_count()}
    , m_edge_line_vertex_buffer  {edge_line_vertex_buffer}
    , m_edge_line_vertex_allocator{
        edge_line_vertex_buffer != nullptr
            ? std::make_unique<erhe::buffer::Free_list_allocator>(edge_line_vertex_buffer->get_capacity_byte_count())
            : std::unique_ptr<erhe::buffer::Free_list_allocator>{}
    }
{
    for (erhe::graphics::Buffer* vb : vertex_buffers) {
        m_vertex_entries.push_back(Vertex_buffer_entry{
            .buffer    = vb,
            .allocator = erhe::buffer::Free_list_allocator{vb->get_capacity_byte_count()}
        });
    }
}

auto Graphics_buffer_sink::allocate_vertex_buffer(
    const std::size_t stream,
    const std::size_t vertex_count,
    const std::size_t vertex_element_size
) -> erhe::primitive::Buffer_sink_allocation
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    const std::size_t                allocation_byte_count = vertex_count * vertex_element_size;
    const std::size_t                allocation_alignment  = vertex_element_size;
    Vertex_buffer_entry&             entry                 = m_vertex_entries.at(stream);
    const std::optional<std::size_t> byte_offset_opt       = entry.allocator.allocate(allocation_byte_count, allocation_alignment);
    if (!byte_offset_opt.has_value()) {
        return {};
    }

    const std::size_t byte_offset = byte_offset_opt.value();
    return erhe::primitive::Buffer_sink_allocation{
        .range = erhe::primitive::Buffer_range{
            .count        = vertex_count,
            .element_size = vertex_element_size,
            .byte_offset  = byte_offset,
            .stream       = stream
        },
        .allocation = erhe::buffer::Buffer_allocation{entry.allocator, byte_offset, allocation_byte_count}
    };
}

auto Graphics_buffer_sink::allocate_index_buffer(
    const std::size_t index_count,
    const std::size_t index_element_size
) -> erhe::primitive::Buffer_sink_allocation
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    const std::size_t                allocation_byte_count = index_count * index_element_size;
    const std::optional<std::size_t> byte_offset_opt       = m_index_allocator.allocate(allocation_byte_count, index_element_size);
    if (!byte_offset_opt.has_value()) {
        return {};
    }

    const std::size_t byte_offset = byte_offset_opt.value();
    return erhe::primitive::Buffer_sink_allocation{
        .range = erhe::primitive::Buffer_range{
            .count        = index_count,
            .element_size = index_element_size,
            .byte_offset  = byte_offset
        },
        .allocation = erhe::buffer::Buffer_allocation{m_index_allocator, byte_offset, allocation_byte_count}
    };
}

void Graphics_buffer_sink::enqueue_index_data(const std::size_t offset, std::vector<uint8_t>&& data) const
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    m_buffer_transfer_queue.enqueue(m_index_buffer, offset, std::move(data));
}

void Graphics_buffer_sink::enqueue_vertex_data(const std::size_t stream, const std::size_t offset, std::vector<uint8_t>&& data) const
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    m_buffer_transfer_queue.enqueue(m_vertex_entries.at(stream).buffer, offset, std::move(data));
}

void Graphics_buffer_sink::buffer_ready(erhe::primitive::Vertex_buffer_writer& writer) const
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    m_buffer_transfer_queue.enqueue(m_vertex_entries.at(writer.stream).buffer, writer.start_offset(), std::move(writer.vertex_data));
}

void Graphics_buffer_sink::buffer_ready(erhe::primitive::Index_buffer_writer& writer) const
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    m_buffer_transfer_queue.enqueue(m_index_buffer, writer.start_offset(), std::move(writer.index_data));
}

auto Graphics_buffer_sink::get_used_vertex_byte_count(const std::size_t stream) const -> std::size_t
{
    return m_vertex_entries.at(stream).allocator.get_used();
}

auto Graphics_buffer_sink::get_available_vertex_byte_count(const std::size_t stream, const std::size_t alignment) const -> std::size_t
{
    static_cast<void>(alignment);
    return m_vertex_entries.at(stream).allocator.get_free();
}

auto Graphics_buffer_sink::get_used_index_byte_count() const -> std::size_t
{
    return m_index_allocator.get_used();
}

auto Graphics_buffer_sink::get_available_index_byte_count(const std::size_t alignment) const -> std::size_t
{
    static_cast<void>(alignment);
    return m_index_allocator.get_free();
}

auto Graphics_buffer_sink::get_vertex_allocator(const std::size_t stream) -> erhe::buffer::Free_list_allocator&
{
    return m_vertex_entries.at(stream).allocator;
}

auto Graphics_buffer_sink::get_index_allocator() -> erhe::buffer::Free_list_allocator&
{
    return m_index_allocator;
}

auto Graphics_buffer_sink::allocate_edge_line_vertex_buffer(
    const std::size_t vertex_count,
    const std::size_t vertex_element_size
) -> erhe::primitive::Buffer_sink_allocation
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    if ((m_edge_line_vertex_buffer == nullptr) || !m_edge_line_vertex_allocator) {
        return {};
    }

    const std::size_t                allocation_byte_count = vertex_count * vertex_element_size;
    const std::size_t                allocation_alignment  = vertex_element_size;
    const std::optional<std::size_t> byte_offset_opt       = m_edge_line_vertex_allocator->allocate(allocation_byte_count, allocation_alignment);
    if (!byte_offset_opt.has_value()) {
        return {};
    }

    const std::size_t byte_offset = byte_offset_opt.value();
    return erhe::primitive::Buffer_sink_allocation{
        .range = erhe::primitive::Buffer_range{
            .count        = vertex_count,
            .element_size = vertex_element_size,
            .byte_offset  = byte_offset
        },
        .allocation = erhe::buffer::Buffer_allocation{*m_edge_line_vertex_allocator, byte_offset, allocation_byte_count}
    };
}

void Graphics_buffer_sink::enqueue_edge_line_vertex_data(const std::size_t offset, std::vector<uint8_t>&& data) const
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    if (m_edge_line_vertex_buffer != nullptr) {
        m_buffer_transfer_queue.enqueue(m_edge_line_vertex_buffer, offset, std::move(data));
    }
}

auto Graphics_buffer_sink::get_available_edge_line_vertex_byte_count(const std::size_t alignment) const -> std::size_t
{
    static_cast<void>(alignment);
    if (!m_edge_line_vertex_allocator) {
        return 0;
    }
    return m_edge_line_vertex_allocator->get_free();
}

auto Graphics_buffer_sink::get_edge_line_vertex_allocator() -> erhe::buffer::Free_list_allocator*
{
    return m_edge_line_vertex_allocator.get();
}

} // namespace erhe::graphics_buffer_sink
