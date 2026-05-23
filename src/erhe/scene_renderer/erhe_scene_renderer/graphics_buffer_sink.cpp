#if 0
#include "erhe_scene_renderer/graphics_buffer_sink.hpp"
#include "erhe_scene_renderer/buffer_pool.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"

#include "erhe_primitive/buffer_writer.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene_renderer {

Graphics_vertex_buffer_sink::Graphics_vertex_buffer_sink(
    erhe::graphics::Buffer_transfer_queue& transfer_queue,
    std::vector<Buffer_pool*>              vertex_pools
)
    : m_transfer_queue{transfer_queue}
    , m_vertex_pools  {std::move(vertex_pools)}
{
}

auto Graphics_vertex_buffer_sink::allocate(
    const erhe::dataformat::Vertex_stream& vertex_stream,
    const std::size_t                      vertex_count
) -> erhe::primitive::Buffer_sink_allocation
{
    for (Buffer_pool* pool : m_vertex_pools) {
        if (pool->is_compatible(vertex_stream)) {
            return pool->allocate(vertex_count);
        }
    }
    ERHE_FATAL("No compatible vertex buffer pool found");
    return {};
}

void Graphics_vertex_buffer_sink::enqueue(const erhe::primitive::Buffer_range& buffer_range, std::vector<uint8_t>&& data) const
{
    Buffer_pool*            buffer_pool = m_vertex_pools.at(buffer_range.pool_id);
    erhe::graphics::Buffer* buffer      = buffer_pool->get_buffer(buffer_range.buffer_id);
    m_transfer_queue.enqueue(buffer, buffer_range.byte_offset, std::move(data));
}

void Graphics_vertex_buffer_sink::buffer_ready(erhe::primitive::Vertex_buffer_writer& writer) const
{
    Buffer_pool*            buffer_pool = m_vertex_pools.at(writer.buffer_range.pool_id);
    erhe::graphics::Buffer* buffer      = buffer_pool->get_buffer(writer.buffer_range.buffer_id);
    m_transfer_queue.enqueue(buffer, writer.start_offset(), std::move(writer.vertex_data));
}

///////////////////////////////////////////////////////////

Graphics_index_buffer_sink::Graphics_index_buffer_sink(
    erhe::graphics::Buffer_transfer_queue& transfer_queue,
    std::vector<Buffer_pool*>              index_pools
)
    : m_transfer_queue{transfer_queue}
    , m_index_pools   {std::move(index_pools)}
{
}

auto Graphics_index_buffer_sink::allocate(
    const erhe::dataformat::Format index_format,
    const std::size_t              index_count
) -> erhe::primitive::Buffer_sink_allocation
{
    for (Buffer_pool* pool : m_index_pools) {
        if (pool->is_compatible(index_format)) {
            return pool->allocate(index_count);
        }
    }
    ERHE_FATAL("No compatible index buffer pool found");
    return {};
}

void Graphics_index_buffer_sink::enqueue(const erhe::primitive::Buffer_range& buffer_range, std::vector<uint8_t>&& data) const
{
    Buffer_pool*            buffer_pool = m_index_pools.at(buffer_range.pool_id);
    erhe::graphics::Buffer* buffer      = buffer_pool->get_buffer(buffer_range.buffer_id);
    m_transfer_queue.enqueue(buffer, buffer_range.byte_offset, std::move(data));
}

void Graphics_index_buffer_sink::buffer_ready(erhe::primitive::Index_buffer_writer& writer) const
{
    Buffer_pool*            buffer_pool = m_index_pools.at(writer.buffer_range.pool_id);
    erhe::graphics::Buffer* buffer      = buffer_pool->get_buffer(writer.buffer_range.buffer_id);
    m_transfer_queue.enqueue(buffer, writer.start_offset(), std::move(writer.index_data));
}

} // namespace erhe::scene_renderer

#endif