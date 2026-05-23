#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/buffer_writer.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_buffer/ibuffer.hpp"
#include "erhe_verify/verify.hpp"

#include <optional>

namespace erhe::primitive {

Vertex_buffer_sink::~Vertex_buffer_sink() noexcept = default;

Index_buffer_sink::~Index_buffer_sink() noexcept = default;

Cpu_vertex_buffer_sink::Cpu_vertex_buffer_sink(std::initializer_list<erhe::buffer::Cpu_buffer*> vertex_buffers)
    : m_vertex_buffers{vertex_buffers}
{
}

auto Cpu_vertex_buffer_sink::allocate_vertex_buffer_range(
    const erhe::dataformat::Vertex_stream& vertex_stream,
    const std::size_t                      vertex_count
) -> Buffer_sink_allocation
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    const std::size_t                allocation_byte_count = vertex_count * vertex_stream.stride;
    const std::size_t                allocation_alignment  = vertex_stream.stride;
    erhe::buffer::Cpu_buffer*        vertex_buffer         = m_vertex_buffers.at(vertex_stream.binding);
    const std::optional<std::size_t> byte_offset_opt       = vertex_buffer->allocate_bytes(allocation_byte_count, allocation_alignment);
    if (!byte_offset_opt.has_value()) {
        log_primitive->error(
            "Cpu_buffer_sink::allocate_vertex_buffer(): Out of memory requesting {} bytes, currently allocated {}, total size {}, free {} bytes",
            allocation_byte_count,
            vertex_buffer->get_used_byte_count(),
            vertex_buffer->get_capacity_byte_count(),
            vertex_buffer->get_available_byte_count(allocation_alignment)
        );
        return {};
    }

    const std::size_t byte_offset = byte_offset_opt.value();
    return Buffer_sink_allocation{
        .range = Buffer_range{
            .count        = vertex_count,
            .element_size = vertex_stream.stride,
            .byte_offset  = byte_offset,
            .pool_id      = 0,
            .buffer_id    = vertex_stream.binding
        },
        .allocation = erhe::buffer::Buffer_allocation{vertex_buffer->get_allocator(), byte_offset, allocation_byte_count}
    };
}

void Cpu_vertex_buffer_sink::enqueue_vertex_data(const Buffer_range& buffer_range, std::vector<uint8_t>&& data)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    ERHE_VERIFY(buffer_range.pool_id == 0);
    ERHE_VERIFY(buffer_range.buffer_id < m_vertex_buffers.size());

    auto buffer_span = m_vertex_buffers.at(buffer_range.buffer_id)->get_span();
    auto offset_span = buffer_span.subspan(buffer_range.byte_offset, data.size());
    memcpy(offset_span.data(), data.data(), data.size());
}

void Cpu_vertex_buffer_sink::vertex_writer_ready(Vertex_buffer_writer& writer)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    auto        buffer_span = m_vertex_buffers.at(writer.stream)->get_span();
    const auto& data        = writer.vertex_data;
    auto        offset_span = buffer_span.subspan(writer.start_offset(), data.size());
    memcpy(offset_span.data(), data.data(), data.size());
}

/////////////////////////////////////////////////////

Cpu_index_buffer_sink::Cpu_index_buffer_sink(erhe::buffer::Cpu_buffer& index_buffer)
    : m_index_buffer  {index_buffer}
{
}

auto Cpu_index_buffer_sink::allocate_index_buffer_range(
    const erhe::dataformat::Format index_format,
    const std::size_t              index_count
) -> Buffer_sink_allocation
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    const std::size_t                index_element_size = erhe::dataformat::get_format_size_bytes(index_format);
    const std::size_t                allocation_byte_count = index_count * index_element_size;
    const std::size_t                allocation_alignment  = index_element_size;
    const std::optional<std::size_t> byte_offset_opt       = m_index_buffer.allocate_bytes(allocation_byte_count, allocation_alignment);
    if (!byte_offset_opt.has_value()) {
        log_primitive->error(
            "Cpu_buffer_sink::allocate_index_buffer(): Out of memory requesting {} bytes, currently allocated {}, total size {}, free {} bytes",
            allocation_byte_count,
            m_index_buffer.get_used_byte_count(),
            m_index_buffer.get_capacity_byte_count(),
            m_index_buffer.get_available_byte_count(allocation_alignment)
        );
        return {};
    }

    const std::size_t byte_offset = byte_offset_opt.value();
    return Buffer_sink_allocation{
        .range = Buffer_range{
            .count        = index_count,
            .element_size = index_element_size,
            .byte_offset  = byte_offset,
            .pool_id      = 0,
            .buffer_id    = 0
        },
        .allocation = erhe::buffer::Buffer_allocation{m_index_buffer.get_allocator(), byte_offset, allocation_byte_count}
    };
}

void Cpu_index_buffer_sink::enqueue_index_data(const Buffer_range& buffer_range, std::vector<uint8_t>&& data)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    auto buffer_span = m_index_buffer.get_span();
    auto offset_span = buffer_span.subspan(buffer_range.byte_offset, data.size());
    memcpy(offset_span.data(), data.data(), data.size());
}

void Cpu_index_buffer_sink::index_writer_ready(Index_buffer_writer& writer)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    auto        buffer_span = m_index_buffer.get_span();
    const auto& data        = writer.index_data;
    auto        offset_span = buffer_span.subspan(writer.start_offset(), data.size());
    memcpy(offset_span.data(), data.data(), data.size());
}

} // namespace erhe::primitive
