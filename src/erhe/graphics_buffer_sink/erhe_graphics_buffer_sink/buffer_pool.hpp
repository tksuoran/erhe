#pragma once

#include "erhe_buffer/free_list_allocator.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_profile/profile.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace erhe::graphics {
    class Buffer;
    class Buffer_transfer_queue;
}

namespace erhe::graphics_buffer_sink {

class Pool_block
{
public:
    erhe::graphics::Buffer*           buffer;
    erhe::buffer::Free_list_allocator allocator;
};

// One pool of GPU buffer storage with first-fit allocation across one or more
// blocks. A block is a (Buffer, Free_list_allocator) pair. For Step 2 of the
// mesh_memory plan a pool always holds exactly one block, contributed by
// Mesh_memory via add_existing_block(); Step 3 introduces lazy growth so the
// allocate() walk can grow the block list on demand.
//
// `stream_index` is the value stamped into Buffer_range::stream for every
// allocation served by this pool (0 for index/edge-line pools, the vertex
// stream binding slot for vertex pools).
class Buffer_pool
{
public:
    Buffer_pool(erhe::graphics::Buffer_transfer_queue& transfer_queue, std::size_t stream_index);

    // Register a Buffer that this pool already has access to. The pool does not
    // own the Buffer (Step 2); ownership stays with whoever constructed it.
    // capacity_bytes is the allocator window over the buffer's storage.
    void add_existing_block(erhe::graphics::Buffer* buffer, std::size_t capacity_bytes);

    [[nodiscard]] auto allocate(std::size_t count, std::size_t element_size) -> erhe::primitive::Buffer_sink_allocation;

    // Forward `data` to the buffer transfer queue. The buffer is the destination
    // GPU buffer (looked up by the caller from Buffer_range::buffer). For the
    // legacy enqueue path that only knows a stream and an offset, the sink uses
    // get_first_buffer() to pick the destination.
    void enqueue_data(erhe::graphics::Buffer* buffer, std::size_t offset, std::vector<uint8_t>&& data) const;

    [[nodiscard]] auto get_first_buffer            () const -> erhe::graphics::Buffer*;
    [[nodiscard]] auto get_used_byte_count         () const -> std::size_t;
    [[nodiscard]] auto get_available_byte_count    (std::size_t alignment) const -> std::size_t;
    [[nodiscard]] auto get_block_count             () const -> std::size_t;
    [[nodiscard]] auto get_stream_index            () const -> std::size_t { return m_stream_index; }

private:
    erhe::graphics::Buffer_transfer_queue&         m_transfer_queue;
    std::size_t                                    m_stream_index;
    mutable ERHE_PROFILE_MUTEX(std::mutex,         m_mutex);
    std::vector<std::unique_ptr<Pool_block>>       m_blocks;
};

} // namespace erhe::graphics_buffer_sink
