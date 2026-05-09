#pragma once

#include "erhe_graphics_buffer_sink/buffer_pool.hpp"
#include "erhe_primitive/buffer_sink.hpp"

#include <vector>

namespace erhe::graphics_buffer_sink {

// Graphics_buffer_sink adapts the primitive build pipeline (which speaks the
// Buffer_sink interface) to a set of Buffer_pools that own the GPU storage.
// Allocation requests forward to the matching pool; data uploads forward to
// the pool's enqueue_data() which routes through the buffer transfer queue.
//
// The sink does not own the pools -- they live in Mesh_memory and must outlive
// the sink. The sink stores raw pointers to keep ownership clear.
class Graphics_buffer_sink : public erhe::primitive::Buffer_sink
{
public:
    Graphics_buffer_sink(
        std::vector<Buffer_pool*> vertex_pools,
        Buffer_pool*              index_pool,
        Buffer_pool*              edge_line_vertex_pool = nullptr
    );

    [[nodiscard]] auto allocate_vertex_buffer                   (std::size_t stream, std::size_t vertex_count, std::size_t vertex_element_size) -> erhe::primitive::Buffer_sink_allocation override;
    [[nodiscard]] auto allocate_index_buffer                    (std::size_t index_count, std::size_t index_element_size) -> erhe::primitive::Buffer_sink_allocation override;
    [[nodiscard]] auto allocate_edge_line_vertex_buffer         (std::size_t vertex_count, std::size_t vertex_element_size) -> erhe::primitive::Buffer_sink_allocation override;
    [[nodiscard]] auto get_used_vertex_byte_count               (std::size_t stream) const -> std::size_t                        override;
    [[nodiscard]] auto get_available_vertex_byte_count          (std::size_t stream, std::size_t alignment) const -> std::size_t override;
    [[nodiscard]] auto get_used_index_byte_count                () const -> std::size_t                                          override;
    [[nodiscard]] auto get_available_index_byte_count           (std::size_t alignment) const -> std::size_t                     override;
    [[nodiscard]] auto get_available_edge_line_vertex_byte_count(std::size_t alignment) const -> std::size_t                     override;

    void enqueue_vertex_data                      (std::size_t stream, std::size_t offset, std::vector<uint8_t>&& data) const override;
    void enqueue_index_data                       (std::size_t offset, std::vector<uint8_t>&& data)                     const override;
    void enqueue_edge_line_vertex_data            (std::size_t offset, std::vector<uint8_t>&& data)                     const override;
    void buffer_ready                             (erhe::primitive::Vertex_buffer_writer& writer)                       const override;
    void buffer_ready                             (erhe::primitive::Index_buffer_writer&  writer)                       const override;

private:
    std::vector<Buffer_pool*> m_vertex_pools;
    Buffer_pool*              m_index_pool;
    Buffer_pool*              m_edge_line_vertex_pool;
};

} // namespace erhe::graphics_buffer_sink
