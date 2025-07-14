#pragma once

#include "erhe_primitive/buffer_range.hpp"

#include <vector>
#include <cstdint>

namespace erhe::graphics {
    class Buffer;
    class Buffer_transfer_queue;
}

namespace erhe::buffer {
    class Cpu_buffer;
}

namespace erhe::primitive {

class Build_context;
class Index_buffer_writer;
class Vertex_buffer_writer;

class Buffer_sink
{
public:
    virtual ~Buffer_sink() noexcept;

    [[nodiscard]] virtual auto allocate_vertex_buffer(std::size_t stream, std::size_t vertex_count, std::size_t vertex_element_size) -> Buffer_range = 0;
    [[nodiscard]] virtual auto allocate_index_buffer (std::size_t index_count, std::size_t index_element_size) -> Buffer_range = 0;

    virtual void enqueue_vertex_data            (std::size_t stream, std::size_t offset, std::vector<uint8_t>&& data) const = 0;
    virtual void enqueue_index_data             (std::size_t offset, std::vector<uint8_t>&& data) const                     = 0;
    virtual void buffer_ready                   (Vertex_buffer_writer& writer) const                                        = 0;
    virtual void buffer_ready                   (Index_buffer_writer&  writer) const                                        = 0;
    virtual auto get_used_vertex_byte_count     (std::size_t stream) const -> std::size_t                                   = 0;
    virtual auto get_available_vertex_byte_count(std::size_t stream, std::size_t alignment) const -> std::size_t            = 0;
    virtual auto get_used_index_byte_count      () const -> std::size_t                                                     = 0;
    virtual auto get_available_index_byte_count (std::size_t alignment) const -> std::size_t                                = 0;
};

class Cpu_buffer_sink : public Buffer_sink
{
public:
    Cpu_buffer_sink(std::initializer_list<erhe::buffer::Cpu_buffer*> vertex_buffers, erhe::buffer::Cpu_buffer& index_buffer);

    auto allocate_vertex_buffer(std::size_t stream, std::size_t vertex_count, std::size_t vertex_element_size) -> Buffer_range override;
    auto allocate_index_buffer (std::size_t index_count, std::size_t index_element_size) -> Buffer_range override;

    void enqueue_vertex_data            (std::size_t stream, std::size_t offset, std::vector<uint8_t>&& data) const override;
    void enqueue_index_data             (std::size_t offset, std::vector<uint8_t>&& data) const                     override;
    void buffer_ready                   (Vertex_buffer_writer& writer) const                                        override;
    void buffer_ready                   (Index_buffer_writer&  writer) const                                        override;
    auto get_used_vertex_byte_count     (std::size_t stream) const -> std::size_t                                   override;
    auto get_available_vertex_byte_count(std::size_t stream, std::size_t alignment) const -> std::size_t            override;
    auto get_used_index_byte_count      () const -> std::size_t                                                     override;
    auto get_available_index_byte_count (std::size_t alignment) const -> std::size_t                                override;

private:
    std::vector<erhe::buffer::Cpu_buffer*> m_vertex_buffers;
    erhe::buffer::Cpu_buffer&              m_index_buffer;
};

} // namespace erhe::primitive
