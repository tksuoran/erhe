#pragma once

#include <cstddef>

namespace erhe::buffer {

class Free_list_allocator;

class Buffer_allocation
{
public:
    Buffer_allocation();
    Buffer_allocation(Free_list_allocator& allocator, std::size_t byte_offset, std::size_t byte_count);
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

    Free_list_allocator* m_allocator  {nullptr};
    std::size_t          m_byte_offset{0};
    std::size_t          m_byte_count {0};
};

} // namespace erhe::buffer
