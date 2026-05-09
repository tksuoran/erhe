#include "erhe_graphics_buffer_sink/graphics_buffer_sink.hpp"

#include "erhe_primitive/buffer_writer.hpp"

namespace erhe::graphics_buffer_sink {

Graphics_buffer_sink::Graphics_buffer_sink(
    std::initializer_list<Buffer_pool*> vertex_pools,
    Buffer_pool*                        index_pool,
    Buffer_pool*                        edge_line_vertex_pool
)
    : m_vertex_pools         {vertex_pools.begin(), vertex_pools.end()}
    , m_index_pool           {index_pool}
    , m_edge_line_vertex_pool{edge_line_vertex_pool}
{
}

auto Graphics_buffer_sink::allocate_vertex_buffer(
    const std::size_t stream,
    const std::size_t vertex_count,
    const std::size_t vertex_element_size
) -> erhe::primitive::Buffer_sink_allocation
{
    return m_vertex_pools.at(stream)->allocate(vertex_count, vertex_element_size);
}

auto Graphics_buffer_sink::allocate_index_buffer(
    const std::size_t index_count,
    const std::size_t index_element_size
) -> erhe::primitive::Buffer_sink_allocation
{
    return m_index_pool->allocate(index_count, index_element_size);
}

auto Graphics_buffer_sink::allocate_edge_line_vertex_buffer(
    const std::size_t vertex_count,
    const std::size_t vertex_element_size
) -> erhe::primitive::Buffer_sink_allocation
{
    if (m_edge_line_vertex_pool == nullptr) {
        return {};
    }
    return m_edge_line_vertex_pool->allocate(vertex_count, vertex_element_size);
}

void Graphics_buffer_sink::enqueue_vertex_data(const std::size_t stream, const std::size_t offset, std::vector<uint8_t>&& data) const
{
    Buffer_pool* pool = m_vertex_pools.at(stream);
    pool->enqueue_data(pool->get_first_buffer(), offset, std::move(data));
}

void Graphics_buffer_sink::enqueue_index_data(const std::size_t offset, std::vector<uint8_t>&& data) const
{
    m_index_pool->enqueue_data(m_index_pool->get_first_buffer(), offset, std::move(data));
}

void Graphics_buffer_sink::enqueue_edge_line_vertex_data(const std::size_t offset, std::vector<uint8_t>&& data) const
{
    if (m_edge_line_vertex_pool == nullptr) {
        return;
    }
    m_edge_line_vertex_pool->enqueue_data(m_edge_line_vertex_pool->get_first_buffer(), offset, std::move(data));
}

void Graphics_buffer_sink::buffer_ready(erhe::primitive::Vertex_buffer_writer& writer) const
{
    Buffer_pool* pool = m_vertex_pools.at(writer.stream);
    pool->enqueue_data(writer.buffer_range.buffer, writer.start_offset(), std::move(writer.vertex_data));
}

void Graphics_buffer_sink::buffer_ready(erhe::primitive::Index_buffer_writer& writer) const
{
    m_index_pool->enqueue_data(writer.buffer_range.buffer, writer.start_offset(), std::move(writer.index_data));
}

auto Graphics_buffer_sink::get_used_vertex_byte_count(const std::size_t stream) const -> std::size_t
{
    return m_vertex_pools.at(stream)->get_used_byte_count();
}

auto Graphics_buffer_sink::get_available_vertex_byte_count(const std::size_t stream, const std::size_t alignment) const -> std::size_t
{
    return m_vertex_pools.at(stream)->get_available_byte_count(alignment);
}

auto Graphics_buffer_sink::get_used_index_byte_count() const -> std::size_t
{
    return m_index_pool->get_used_byte_count();
}

auto Graphics_buffer_sink::get_available_index_byte_count(const std::size_t alignment) const -> std::size_t
{
    return m_index_pool->get_available_byte_count(alignment);
}

auto Graphics_buffer_sink::get_available_edge_line_vertex_byte_count(const std::size_t alignment) const -> std::size_t
{
    if (m_edge_line_vertex_pool == nullptr) {
        return 0;
    }
    return m_edge_line_vertex_pool->get_available_byte_count(alignment);
}

} // namespace erhe::graphics_buffer_sink
