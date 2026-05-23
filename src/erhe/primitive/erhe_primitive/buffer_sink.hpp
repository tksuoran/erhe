#pragma once

#include "erhe_buffer/buffer_allocation.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_primitive/buffer_range.hpp"
#include "erhe_profile/profile.hpp"

#include <cstdint>
#include <mutex>
#include <vector>

namespace erhe::graphics {
    class Buffer;
    class Buffer_transfer_queue;
}

namespace erhe::buffer     { class Cpu_buffer; }
namespace erhe::dataformat { class Vertex_stream; }

namespace erhe::primitive {

class Build_context;
class Index_buffer_writer;
class Vertex_buffer_writer;

class Buffer_sink_allocation
{
public:
    Buffer_range                    range;
    erhe::buffer::Buffer_allocation allocation;
};

class Vertex_buffer_sink
{
public:
    virtual ~Vertex_buffer_sink() noexcept;

    [[nodiscard]] virtual auto allocate_vertex_buffer_range(const erhe::dataformat::Vertex_stream& vertex_stream, std::size_t vertex_count) -> Buffer_sink_allocation = 0;
                  virtual void enqueue_vertex_data         (const Buffer_range& buffer_range, std::vector<uint8_t>&& data) = 0;
                  virtual void vertex_writer_ready         (Vertex_buffer_writer& writer) = 0;
};

class Index_buffer_sink
{
public:
    virtual ~Index_buffer_sink() noexcept;

    [[nodiscard]] virtual auto allocate_index_buffer_range(erhe::dataformat::Format index_format, std::size_t index_count) -> Buffer_sink_allocation = 0;
                  virtual void enqueue_index_data         (const Buffer_range& buffer_range, std::vector<uint8_t>&& data) = 0;
                  virtual void index_writer_ready         (Index_buffer_writer& writer) = 0;
};

class Cpu_vertex_buffer_sink : public Vertex_buffer_sink
{
public:
    Cpu_vertex_buffer_sink(std::initializer_list<erhe::buffer::Cpu_buffer*> vertex_buffers);

    auto allocate_vertex_buffer_range(const erhe::dataformat::Vertex_stream& vertex_stream, std::size_t vertex_count) -> Buffer_sink_allocation override;
    void enqueue_vertex_data         (const Buffer_range& buffer_range, std::vector<uint8_t>&& data) override;
    void vertex_writer_ready         (Vertex_buffer_writer& writer)                                  override;

private:
    mutable ERHE_PROFILE_MUTEX(std::mutex, m_mutex);
    std::vector<erhe::buffer::Cpu_buffer*> m_vertex_buffers;
};

class Cpu_index_buffer_sink : public Index_buffer_sink
{
public:
    Cpu_index_buffer_sink(erhe::buffer::Cpu_buffer& index_buffer);

    auto allocate_index_buffer_range(erhe::dataformat::Format index_format, std::size_t index_count) -> Buffer_sink_allocation override;
    void enqueue_index_data         (const Buffer_range& buffer_range, std::vector<uint8_t>&& data) override;
    void index_writer_ready         (Index_buffer_writer&  writer)                                  override;

private:
    mutable ERHE_PROFILE_MUTEX(std::mutex, m_mutex);
    erhe::buffer::Cpu_buffer&              m_index_buffer;
};

} // namespace erhe::primitive
