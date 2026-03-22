#include "erhe_buffer/free_list_allocator.hpp"

#include <algorithm>

namespace erhe::buffer {

Free_list_allocator::Free_list_allocator(const std::size_t capacity)
    : m_capacity{capacity}
{
    m_free_blocks.push_back(Free_block{.offset = 0, .size = capacity});
}

Free_list_allocator::Free_list_allocator(Free_list_allocator&& other) noexcept
    : m_capacity        {other.m_capacity}
    , m_used            {other.m_used}
    , m_allocation_count{other.m_allocation_count}
    , m_free_blocks     {std::move(other.m_free_blocks)}
{
    other.m_capacity         = 0;
    other.m_used             = 0;
    other.m_allocation_count = 0;
}

Free_list_allocator& Free_list_allocator::operator=(Free_list_allocator&& other) noexcept
{
    if (this != &other) {
        m_capacity         = other.m_capacity;
        m_used             = other.m_used;
        m_allocation_count = other.m_allocation_count;
        m_free_blocks      = std::move(other.m_free_blocks);
        other.m_capacity         = 0;
        other.m_used             = 0;
        other.m_allocation_count = 0;
    }
    return *this;
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

        const std::size_t aligned_offset  = ((block.offset + alignment - 1) / alignment) * alignment;
        const std::size_t alignment_waste = aligned_offset - block.offset;
        const std::size_t required_size   = alignment_waste + byte_count;

        if (block.size < required_size) {
            continue;
        }

        const std::size_t remaining = block.size - required_size;

        if (remaining > 0) {
            block.offset = aligned_offset + byte_count;
            block.size   = remaining;

            if (alignment_waste > 0) {
                Free_block waste_block{
                    .offset = aligned_offset - alignment_waste,
                    .size   = alignment_waste
                };
                m_free_blocks.insert(
                    m_free_blocks.begin() + static_cast<std::ptrdiff_t>(i),
                    waste_block
                );
            }
        } else {
            if (alignment_waste > 0) {
                block.offset = aligned_offset - alignment_waste;
                block.size   = alignment_waste;
            } else {
                m_free_blocks.erase(
                    m_free_blocks.begin() + static_cast<std::ptrdiff_t>(i)
                );
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

    Free_block freed{.offset = byte_offset, .size = byte_count};

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

} // namespace erhe::buffer
