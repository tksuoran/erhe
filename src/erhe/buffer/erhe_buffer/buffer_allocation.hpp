#pragma once

#include <cstddef>

namespace erhe::buffer {

// Receives Buffer_allocation releases. Buffer_allocation never frees memory
// itself: it hands the range back to the owner it was allocated from, and
// the owner decides WHEN the range becomes reusable. Free_list_allocator
// implements this with an immediate free (CPU-side buffers). GPU pools
// (erhe::scene_renderer::Pool_block) implement it by retiring the range
// until every frame in flight that may still read it has completed on the
// GPU. Called from any thread; implementations must be thread-safe.
class Buffer_allocation_owner
{
public:
    virtual ~Buffer_allocation_owner() noexcept = default;

    virtual void release_allocation(std::size_t byte_offset, std::size_t byte_count) noexcept = 0;
};

class Buffer_allocation
{
public:
    Buffer_allocation();
    Buffer_allocation(Buffer_allocation_owner& owner, std::size_t byte_offset, std::size_t byte_count);
    ~Buffer_allocation();

    Buffer_allocation(Buffer_allocation&& other) noexcept;
    Buffer_allocation& operator=(Buffer_allocation&& other) noexcept;
    Buffer_allocation(const Buffer_allocation&) = delete;
    Buffer_allocation& operator=(const Buffer_allocation&) = delete;

    [[nodiscard]] auto get_byte_offset() const -> std::size_t;
    [[nodiscard]] auto get_byte_count()  const -> std::size_t;
    [[nodiscard]] auto is_valid()        const -> bool;

private:
    void release();

    Buffer_allocation_owner* m_owner      {nullptr};
    std::size_t              m_byte_offset{0};
    std::size_t              m_byte_count {0};
};

} // namespace erhe::buffer
