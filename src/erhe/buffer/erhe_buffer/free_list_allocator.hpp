#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <vector>

namespace erhe::buffer {

class Free_list_allocator
{
public:
    explicit Free_list_allocator(std::size_t capacity);
    Free_list_allocator(Free_list_allocator&& other) noexcept;
    Free_list_allocator& operator=(Free_list_allocator&& other) noexcept;
    Free_list_allocator(const Free_list_allocator&) = delete;
    Free_list_allocator& operator=(const Free_list_allocator&) = delete;

    auto allocate(std::size_t byte_count, std::size_t alignment) -> std::optional<std::size_t>;
    void free   (std::size_t byte_offset, std::size_t byte_count);

    [[nodiscard]] auto get_capacity()         const -> std::size_t;
    [[nodiscard]] auto get_used()             const -> std::size_t;
    [[nodiscard]] auto get_free()             const -> std::size_t;
    [[nodiscard]] auto get_allocation_count() const -> std::size_t;

private:
    class Free_block
    {
    public:
        std::size_t offset;
        std::size_t size;
    };

    std::size_t              m_capacity;
    std::size_t              m_used{0};
    std::size_t              m_allocation_count{0};
    std::vector<Free_block>  m_free_blocks;
    mutable std::mutex       m_mutex;
};

} // namespace erhe::buffer
