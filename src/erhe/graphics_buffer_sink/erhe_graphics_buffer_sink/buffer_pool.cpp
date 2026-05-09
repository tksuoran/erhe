#include "erhe_graphics_buffer_sink/buffer_pool.hpp"

#include "erhe_buffer/buffer_allocation.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_primitive/buffer_range.hpp"

#include <optional>

namespace erhe::graphics_buffer_sink {

Buffer_pool::Buffer_pool(erhe::graphics::Buffer_transfer_queue& transfer_queue, const std::size_t stream_index)
    : m_transfer_queue{transfer_queue}
    , m_stream_index  {stream_index}
{
}

void Buffer_pool::add_existing_block(erhe::graphics::Buffer* buffer, const std::size_t capacity_bytes)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    auto block = std::make_unique<Pool_block>(Pool_block{
        .buffer    = buffer,
        .allocator = erhe::buffer::Free_list_allocator{capacity_bytes}
    });
    m_blocks.push_back(std::move(block));
}

auto Buffer_pool::allocate(const std::size_t count, const std::size_t element_size) -> erhe::primitive::Buffer_sink_allocation
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    const std::size_t allocation_byte_count = count * element_size;
    const std::size_t allocation_alignment  = element_size;

    for (const std::unique_ptr<Pool_block>& block : m_blocks) {
        const std::optional<std::size_t> byte_offset_opt = block->allocator.allocate(allocation_byte_count, allocation_alignment);
        if (!byte_offset_opt.has_value()) {
            continue;
        }
        const std::size_t byte_offset = byte_offset_opt.value();
        return erhe::primitive::Buffer_sink_allocation{
            .range = erhe::primitive::Buffer_range{
                .count        = count,
                .element_size = element_size,
                .byte_offset  = byte_offset,
                .stream       = m_stream_index,
                .buffer       = block->buffer
            },
            .allocation = erhe::buffer::Buffer_allocation{block->allocator, byte_offset, allocation_byte_count}
        };
    }
    // No existing block had room. Step 3 will grow the pool here; for now the
    // allocation simply fails (Buffer_sink_allocation default-constructs to a
    // zero-sized range, which the primitive builder treats as out-of-memory).
    return {};
}

void Buffer_pool::enqueue_data(erhe::graphics::Buffer* buffer, const std::size_t offset, std::vector<uint8_t>&& data) const
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    m_transfer_queue.enqueue(buffer, offset, std::move(data));
}

auto Buffer_pool::get_first_buffer() const -> erhe::graphics::Buffer*
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    if (m_blocks.empty()) {
        return nullptr;
    }
    return m_blocks.front()->buffer;
}

auto Buffer_pool::get_used_byte_count() const -> std::size_t
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    std::size_t used = 0;
    for (const std::unique_ptr<Pool_block>& block : m_blocks) {
        used += block->allocator.get_used();
    }
    return used;
}

auto Buffer_pool::get_available_byte_count(const std::size_t alignment) const -> std::size_t
{
    static_cast<void>(alignment);
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    std::size_t available = 0;
    for (const std::unique_ptr<Pool_block>& block : m_blocks) {
        available += block->allocator.get_free();
    }
    return available;
}

auto Buffer_pool::get_block_count() const -> std::size_t
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    return m_blocks.size();
}

} // namespace erhe::graphics_buffer_sink
