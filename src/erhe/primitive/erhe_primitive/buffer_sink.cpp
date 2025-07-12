#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/buffer_writer.hpp"
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
    const std::optional<std::size_t> byte_offset_opt = m_vertex_buffers.at(stream)->allocate_bytes(vertex_count * vertex_element_size, vertex_element_size);
    if (!byte_offset_opt.has_value()) {
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
    const std::optional<std::size_t> byte_offset_opt = m_index_buffer.allocate_bytes(index_count * index_element_size);
    if (!byte_offset_opt.has_value()) {
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
    auto buffer_span = m_index_buffer.get_span();
    auto offset_span = buffer_span.subspan(offset, data.size());
    memcpy(offset_span.data(), data.data(), data.size());
}

void Cpu_buffer_sink::enqueue_vertex_data(const std::size_t stream, const std::size_t offset, std::vector<uint8_t>&& data) const
{
    auto buffer_span = m_vertex_buffers.at(stream)->get_span();
    auto offset_span = buffer_span.subspan(offset, data.size());
    memcpy(offset_span.data(), data.data(), data.size());
}

void Cpu_buffer_sink::buffer_ready(Vertex_buffer_writer& writer) const
{
    auto        buffer_span = m_vertex_buffers.at(writer.stream)->get_span();
    const auto& data        = writer.vertex_data;
    auto        offset_span = buffer_span.subspan(writer.start_offset(), data.size());
    memcpy(offset_span.data(), data.data(), data.size());
}

void Cpu_buffer_sink::buffer_ready(Index_buffer_writer& writer) const
{
    auto        buffer_span = m_index_buffer.get_span();
    const auto& data        = writer.index_data;
    auto        offset_span = buffer_span.subspan(writer.start_offset(), data.size());
    memcpy(offset_span.data(), data.data(), data.size());
}

} // namespace erhe::primitive
