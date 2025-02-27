#pragma once

#include "erhe_primitive/buffer_sink.hpp"

namespace erhe::graphics {
    class Buffer;
    class Buffer_transfer_queue;
}

namespace erhe::graphics_buffer_sink {

class Graphics_buffer_sink : public erhe::primitive::Buffer_sink
{
public:
    Graphics_buffer_sink(
        erhe::graphics::Buffer_transfer_queue&         buffer_transfer_queue,
        std::initializer_list<erhe::graphics::Buffer*> vertex_buffers,
        erhe::graphics::Buffer&                        index_buffer
    );

    [[nodiscard]] auto allocate_vertex_buffer(std::size_t stream, std::size_t vertex_count, std::size_t vertex_element_size) -> erhe::primitive::Buffer_range override;
    [[nodiscard]] auto allocate_index_buffer (std::size_t index_count, std::size_t index_element_size) -> erhe::primitive::Buffer_range override;

    void enqueue_vertex_data(std::size_t stream, std::size_t offset, std::vector<uint8_t>&& data) const override;
    void enqueue_index_data (std::size_t offset, std::vector<uint8_t>&& data) const override;
    void buffer_ready       (erhe::primitive::Vertex_buffer_writer& writer) const   override;
    void buffer_ready       (erhe::primitive::Index_buffer_writer&  writer) const   override;

private:
    erhe::graphics::Buffer_transfer_queue& m_buffer_transfer_queue;
    std::vector<erhe::graphics::Buffer*>   m_vertex_buffers;
    erhe::graphics::Buffer&                m_index_buffer;
};

} // namespace erhe::graphics_buffer_sink
