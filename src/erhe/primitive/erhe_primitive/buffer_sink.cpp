#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/buffer_writer.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_buffer/ibuffer.hpp"

#include <optional>

namespace erhe::primitive {

Buffer_sink::~Buffer_sink() noexcept
{
}

Cpu_buffer_sink::Cpu_buffer_sink(std::initializer_list<erhe::buffer::Cpu_buffer*> vertex_buffers, erhe::buffer::Cpu_buffer& index_buffer)
    : m_vertex_buffers{vertex_buffers}
    , m_index_buffer{index_buffer}
{
}

auto Cpu_buffer_sink::allocate_vertex_buffer(const std::size_t stream, const std::size_t vertex_count, const std::size_t vertex_element_size) -> Buffer_range
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    const std::size_t                allocation_byte_count = vertex_count * vertex_element_size;
    const std::size_t                allocation_alignment  = vertex_element_size;
    erhe::buffer::Cpu_buffer*        vertex_buffer         = m_vertex_buffers.at(stream);
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

    return Buffer_range{
        .count        = vertex_count,
        .element_size = vertex_element_size,
        .byte_offset  = byte_offset_opt.value()
    };
}

auto Cpu_buffer_sink::allocate_index_buffer(const std::size_t index_count, const std::size_t index_element_size) -> Buffer_range
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

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

    return Buffer_range{
        .count        = index_count,
        .element_size = index_element_size,
        .byte_offset  = byte_offset_opt.value()
    };
}

void Cpu_buffer_sink::enqueue_index_data(const std::size_t offset, std::vector<uint8_t>&& data) const 
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    auto buffer_span = m_index_buffer.get_span();
    auto offset_span = buffer_span.subspan(offset, data.size());
    memcpy(offset_span.data(), data.data(), data.size());
}

void Cpu_buffer_sink::enqueue_vertex_data(const std::size_t stream, const std::size_t offset, std::vector<uint8_t>&& data) const
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    auto buffer_span = m_vertex_buffers.at(stream)->get_span();
    auto offset_span = buffer_span.subspan(offset, data.size());
    memcpy(offset_span.data(), data.data(), data.size());
}

void Cpu_buffer_sink::buffer_ready(Vertex_buffer_writer& writer) const
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    auto        buffer_span = m_vertex_buffers.at(writer.stream)->get_span();
    const auto& data        = writer.vertex_data;
    auto        offset_span = buffer_span.subspan(writer.start_offset(), data.size());
    memcpy(offset_span.data(), data.data(), data.size());
}

void Cpu_buffer_sink::buffer_ready(Index_buffer_writer& writer) const
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    auto        buffer_span = m_index_buffer.get_span();
    const auto& data        = writer.index_data;
    auto        offset_span = buffer_span.subspan(writer.start_offset(), data.size());
    memcpy(offset_span.data(), data.data(), data.size());
}

auto Cpu_buffer_sink::get_used_vertex_byte_count(std::size_t stream) const -> std::size_t
{
    return m_vertex_buffers.at(stream)->get_used_byte_count();
}

auto Cpu_buffer_sink::get_available_vertex_byte_count(std::size_t stream, std::size_t alignment) const -> std::size_t
{
    return m_vertex_buffers.at(stream)->get_available_byte_count(alignment);
}

auto Cpu_buffer_sink::get_used_index_byte_count() const -> std::size_t
{
    return m_index_buffer.get_used_byte_count();
}

auto Cpu_buffer_sink::get_available_index_byte_count(std::size_t alignment) const -> std::size_t
{
    return m_index_buffer.get_available_byte_count(alignment);
}


} // namespace erhe::primitive
