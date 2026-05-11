#include "erhe_graphics_buffer_sink/buffer_pool.hpp"

#include "erhe_buffer/buffer_allocation.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_primitive/buffer_range.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <string>

namespace erhe::graphics_buffer_sink {

Buffer_pool::Buffer_pool(
    erhe::graphics::Device&                graphics_device,
    erhe::graphics::Buffer_transfer_queue& transfer_queue,
    const std::size_t                      stream_index,
    Buffer_pool_block_create_info          block_create_info
)
    : m_graphics_device  {&graphics_device}
    , m_transfer_queue   {transfer_queue}
    , m_stream_index     {stream_index}
    , m_block_create_info{std::move(block_create_info)}
{
}

Buffer_pool::Buffer_pool(
    erhe::graphics::Buffer_transfer_queue& transfer_queue,
    const std::size_t                      stream_index
)
    : m_transfer_queue {transfer_queue}
    , m_stream_index   {stream_index}
{
}

Buffer_pool::~Buffer_pool() = default;

void Buffer_pool::add_existing_block(erhe::graphics::Buffer* buffer, const std::size_t capacity_bytes)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    auto block = std::make_unique<Pool_block>(Pool_block{
        .buffer    = buffer,
        .allocator = erhe::buffer::Free_list_allocator{capacity_bytes}
    });
    m_blocks.push_back(std::move(block));
}

auto Buffer_pool::allocate_locked(
    const std::size_t allocation_byte_count,
    const std::size_t allocation_alignment
) -> std::optional<std::pair<Pool_block*, std::size_t>>
{
    for (const std::unique_ptr<Pool_block>& block : m_blocks) {
        const std::optional<std::size_t> byte_offset_opt = block->allocator.allocate(allocation_byte_count, allocation_alignment);
        if (byte_offset_opt.has_value()) {
            return std::make_pair(block.get(), byte_offset_opt.value());
        }
    }
    return std::nullopt;
}

auto Buffer_pool::grow_block_locked(const std::size_t min_capacity_bytes) -> Pool_block*
{
    if (!m_block_create_info.has_value() || (m_graphics_device == nullptr)) {
        return nullptr;
    }
    const Buffer_pool_block_create_info& info = m_block_create_info.value();
    if (m_blocks.size() >= info.max_blocks) {
        return nullptr;
    }
    const std::size_t capacity_bytes = std::max(info.block_size_bytes, min_capacity_bytes);
    const std::string label          = info.debug_label_prefix + " block " + std::to_string(m_blocks.size());

    erhe::graphics::Buffer_create_info buffer_ci{
        .capacity_byte_count                    = capacity_bytes,
        .memory_allocation_create_flag_bit_mask = 0,
        .usage                                  = info.usage,
        .required_memory_property_bit_mask      = info.required_memory_property_bit_mask,
        .preferred_memory_property_bit_mask     = info.preferred_memory_property_bit_mask,
        .init_data                              = nullptr,
        .debug_label                            = erhe::utility::Debug_label{label}
    };

    auto buffer = std::make_unique<erhe::graphics::Buffer>(*m_graphics_device, buffer_ci);
    erhe::graphics::Buffer* buffer_ptr = buffer.get();
    m_owned_buffers.push_back(std::move(buffer));

    auto block = std::make_unique<Pool_block>(Pool_block{
        .buffer    = buffer_ptr,
        .allocator = erhe::buffer::Free_list_allocator{capacity_bytes}
    });
    Pool_block* block_ptr = block.get();
    m_blocks.push_back(std::move(block));
    return block_ptr;
}

auto Buffer_pool::allocate(const std::size_t count, const std::size_t element_size) -> erhe::primitive::Buffer_sink_allocation
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    const std::size_t allocation_byte_count = count * element_size;
    const std::size_t allocation_alignment  = element_size;

    std::optional<std::pair<Pool_block*, std::size_t>> hit = allocate_locked(allocation_byte_count, allocation_alignment);
    if (!hit.has_value()) {
        // Try growing.
        Pool_block* new_block = grow_block_locked(allocation_byte_count);
        if (new_block == nullptr) {
            return {};
        }
        hit = allocate_locked(allocation_byte_count, allocation_alignment);
        if (!hit.has_value()) {
            return {};
        }
    }

    Pool_block*       block       = hit.value().first;
    const std::size_t byte_offset = hit.value().second;
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
