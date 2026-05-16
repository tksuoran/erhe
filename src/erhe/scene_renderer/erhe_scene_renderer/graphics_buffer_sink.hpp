#if 0
#pragma once

#include "erhe_scene_renderer/buffer_pool.hpp"
#include "erhe_primitive/buffer_sink.hpp"

#include <vector>

namespace erhe::scene_renderer {

class Buffer_pool;
class Mesh_memory;

class Graphics_vertex_buffer_sink : public erhe::primitive::Vertex_buffer_sink
{
public:
    Graphics_vertex_buffer_sink(
        erhe::graphics::Buffer_transfer_queue& transfer_queue,
        Mesh_memory&                           mesh_memory
    );

    auto allocate    (const erhe::dataformat::Vertex_stream& vertex_stream, std::size_t vertex_count) -> erhe::primitive::Buffer_sink_allocation override;
    void enqueue     (const erhe::primitive::Buffer_range& buffer_range, std::vector<uint8_t>&& data) const override;
    void buffer_ready(erhe::primitive::Vertex_buffer_writer& writer) const override;

private:
    erhe::graphics::Buffer_transfer_queue& m_transfer_queue;
    Mesh_memory&                           m_mesh_memory;
};

class Graphics_index_buffer_sink : public erhe::primitive::Index_buffer_sink
{
public:
    Graphics_index_buffer_sink(
        erhe::graphics::Buffer_transfer_queue& transfer_queue,
        Mesh_memory&                           mesh_memory
    );

    auto allocate    (const erhe::dataformat::Format index_format, std::size_t index_count) -> erhe::primitive::Buffer_sink_allocation override;
    void enqueue     (const erhe::primitive::Buffer_range& buffer_range, std::vector<uint8_t>&& data) const override;
    void buffer_ready(erhe::primitive::Index_buffer_writer& writer) const override;

private:
    erhe::graphics::Buffer_transfer_queue& m_transfer_queue;
    Mesh_memory&                           m_mesh_memory;
};
} // namespace erhe::scene_renderer
#endif