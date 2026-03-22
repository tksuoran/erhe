#include "erhe_graphics/free_list_allocator.hpp"

#include <algorithm>

namespace erhe::graphics {

Free_list_allocator::Free_list_allocator(const std::size_t capacity)
    : m_capacity{capacity}
{
    m_free_blocks.push_back(Free_block{.offset = 0, .size = capacity});
}

auto Free_list_allocator::allocate(
    const std::size_t byte_count,
    const std::size_t alignment
) -> std::optional<std::size_t>
{
    if (byte_count == 0) {
        return std::optional<std::size_t>{0};
    }

    std::lock_guard<std::mutex> lock{m_mutex};

    for (std::size_t i = 0; i < m_free_blocks.size(); ++i) {
        Free_block& block = m_free_blocks[i];

        // Align the offset within this block
        const std::size_t aligned_offset   = ((block.offset + alignment - 1) / alignment) * alignment;
        const std::size_t alignment_waste  = aligned_offset - block.offset;
        const std::size_t required_size    = alignment_waste + byte_count;

        if (block.size < required_size) {
            continue;
        }

        // Found a fit. Split the block.
        const std::size_t remaining = block.size - required_size;

        if (remaining > 0) {
            // Shrink the free block to the remainder after the allocation
            block.offset = aligned_offset + byte_count;
            block.size   = remaining;

            // If there was alignment waste at the front, add a free block for it
            if (alignment_waste > 0) {
                const Free_block waste_block{
                    .offset = block.offset - required_size,
                    .size   = alignment_waste
                };
                m_free_blocks.insert(m_free_blocks.begin() + static_cast<std::ptrdiff_t>(i), waste_block);
            }
        } else {
            // Exact fit or only alignment waste remains
            if (alignment_waste > 0) {
                block.offset = aligned_offset - alignment_waste;
                block.size   = alignment_waste;
            } else {
                m_free_blocks.erase(m_free_blocks.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }

        m_used += byte_count;
        ++m_allocation_count;
        return std::optional<std::size_t>{aligned_offset};
    }

    return std::nullopt;
}

void Free_list_allocator::free(
    const std::size_t byte_offset,
    const std::size_t byte_count
)
{
    if (byte_count == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock{m_mutex};

    // Insert the freed block in sorted order
    const Free_block freed{.offset = byte_offset, .size = byte_count};

    std::vector<Free_block>::iterator insert_pos = std::lower_bound(
        m_free_blocks.begin(),
        m_free_blocks.end(),
        freed,
        [](const Free_block& a, const Free_block& b) {
            return a.offset < b.offset;
        }
    );
    insert_pos = m_free_blocks.insert(insert_pos, freed);

    m_used -= byte_count;
    --m_allocation_count;

    // Merge with next block if adjacent
    std::vector<Free_block>::iterator next = insert_pos + 1;
    if (next != m_free_blocks.end()) {
        if ((insert_pos->offset + insert_pos->size) == next->offset) {
            insert_pos->size += next->size;
            m_free_blocks.erase(next);
        }
    }

    // Merge with previous block if adjacent
    if (insert_pos != m_free_blocks.begin()) {
        std::vector<Free_block>::iterator prev = insert_pos - 1;
        if ((prev->offset + prev->size) == insert_pos->offset) {
            prev->size += insert_pos->size;
            m_free_blocks.erase(insert_pos);
        }
    }
}

auto Free_list_allocator::get_capacity() const -> std::size_t
{
    return m_capacity;
}

auto Free_list_allocator::get_used() const -> std::size_t
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return m_used;
}

auto Free_list_allocator::get_free() const -> std::size_t
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return m_capacity - m_used;
}

auto Free_list_allocator::get_allocation_count() const -> std::size_t
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return m_allocation_count;
}

} // namespace erhe::graphics
