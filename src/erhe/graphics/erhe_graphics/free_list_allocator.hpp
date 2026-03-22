#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <vector>

namespace erhe::graphics {

class Free_list_allocator
{
public:
    explicit Free_list_allocator(std::size_t capacity);

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

    void merge_adjacent_blocks();

    std::size_t              m_capacity;
    std::size_t              m_used{0};
    std::size_t              m_allocation_count{0};
    std::vector<Free_block>  m_free_blocks;
    mutable std::mutex       m_mutex;
};

} // namespace erhe::graphics
