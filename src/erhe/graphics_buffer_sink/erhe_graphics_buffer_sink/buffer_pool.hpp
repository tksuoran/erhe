#pragma once

#include "erhe_buffer/free_list_allocator.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/debug_label.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace erhe::graphics {
    class Buffer;
    class Buffer_transfer_queue;
    class Device;
}

namespace erhe::graphics_buffer_sink {

class Pool_block
{
public:
    erhe::graphics::Buffer*           buffer;     // non-owning view; backing storage may be in m_owned_buffers
    erhe::buffer::Free_list_allocator allocator;
};

class Buffer_pool_block_create_info
{
public:
    erhe::graphics::Buffer_usage usage                              {0};
    uint64_t                     required_memory_property_bit_mask  {0};
    uint64_t                     preferred_memory_property_bit_mask {0};
    std::size_t                  block_size_bytes                   {0};
    std::size_t                  max_blocks                         {0};
    std::string                  debug_label_prefix                 {};
};

// One pool of GPU buffer storage with first-fit allocation across one or more
// blocks. A block is a (Buffer, Free_list_allocator) pair.
//
// Two construction modes:
//
// 1. Lazy: pool starts empty and grows by creating a new Buffer block when an
//    allocate() request would not fit any existing block. The pool owns the
//    Buffers it creates. Use the Buffer_pool_block_create_info constructor.
//
// 2. Manual: pool starts empty and accepts externally-owned Buffer instances
//    via add_existing_block(). allocate() never grows. Use the bare
//    constructor and call add_existing_block() before any allocate().
//
// `stream_index` is the value stamped into Buffer_range::stream for every
// allocation served by this pool (0 for index/edge-line pools, the vertex
// stream binding slot for vertex pools).
class Buffer_pool
{
public:
    // Lazy pool that creates and owns buffers on demand.
    Buffer_pool(
        erhe::graphics::Device&                graphics_device,
        erhe::graphics::Buffer_transfer_queue& transfer_queue,
        std::size_t                            stream_index,
        Buffer_pool_block_create_info          block_create_info
    );

    // Manual pool that only accepts externally-owned blocks.
    Buffer_pool(
        erhe::graphics::Buffer_transfer_queue& transfer_queue,
        std::size_t                            stream_index
    );

    ~Buffer_pool();

    // Register a Buffer that this pool already has access to. The pool does not
    // own the Buffer; ownership stays with whoever constructed it. Used by the
    // edge-line path while Mesh_memory still owns that buffer eagerly.
    void add_existing_block(erhe::graphics::Buffer* buffer, std::size_t capacity_bytes);

    [[nodiscard]] auto allocate(std::size_t count, std::size_t element_size) -> erhe::primitive::Buffer_sink_allocation;

    // Forward `data` to the buffer transfer queue against `buffer`.
    void enqueue_data(erhe::graphics::Buffer* buffer, std::size_t offset, std::vector<uint8_t>&& data) const;

    [[nodiscard]] auto get_first_buffer            () const -> erhe::graphics::Buffer*;
    [[nodiscard]] auto get_used_byte_count         () const -> std::size_t;
    [[nodiscard]] auto get_available_byte_count    (std::size_t alignment) const -> std::size_t;
    [[nodiscard]] auto get_block_count             () const -> std::size_t;
    [[nodiscard]] auto get_stream_index            () const -> std::size_t { return m_stream_index; }

private:
    [[nodiscard]] auto allocate_locked   (std::size_t allocation_byte_count, std::size_t allocation_alignment) -> std::optional<std::pair<Pool_block*, std::size_t>>;
    auto              grow_block_locked  (std::size_t min_capacity_bytes) -> Pool_block*;

    erhe::graphics::Device*                               m_graphics_device{nullptr}; // null in manual mode
    erhe::graphics::Buffer_transfer_queue&                m_transfer_queue;
    std::size_t                                           m_stream_index;
    std::optional<Buffer_pool_block_create_info>          m_block_create_info;
    mutable ERHE_PROFILE_MUTEX(std::mutex,                m_mutex);
    std::vector<std::unique_ptr<erhe::graphics::Buffer>>  m_owned_buffers;
    std::vector<std::unique_ptr<Pool_block>>              m_blocks;
};

} // namespace erhe::graphics_buffer_sink
