#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace erhe::circular_ring_buffer {

class Circular_ring_buffer_algorithm
{
public:
    explicit Circular_ring_buffer_algorithm(std::size_t capacity_byte_count);

    class Allocation
    {
    public:
        std::size_t wrap_count {0};
        std::size_t byte_offset{0};
        std::size_t byte_count {0};
    };

    void get_size_available_for_write(
        std::size_t  required_alignment,
        std::size_t& out_alignment_byte_count_without_wrap,
        std::size_t& out_available_byte_count_without_wrap,
        std::size_t& out_available_byte_count_with_wrap
    ) const;

    // byte_count == 0 means "as much as possible in the current segment".
    [[nodiscard]] auto acquire(
        std::size_t required_alignment,
        std::size_t byte_count
    ) -> std::optional<Allocation>;

    void make_sync_entry(
        std::uint64_t frame_index,
        std::size_t   wrap_count,
        std::size_t   byte_offset,
        std::size_t   byte_count
    );

    void frame_completed(std::uint64_t completed_frame);

    [[nodiscard]] auto get_capacity_byte_count() const -> std::size_t;
    [[nodiscard]] auto get_write_position     () const -> std::size_t;
    [[nodiscard]] auto get_write_wrap_count   () const -> std::size_t;
    [[nodiscard]] auto get_read_offset        () const -> std::size_t;
    [[nodiscard]] auto get_read_wrap_count    () const -> std::size_t;
    [[nodiscard]] auto get_sync_entry_count   () const -> std::size_t;

    void assert_invariants() const;

private:
    void wrap_write();

    class Sync_entry
    {
    public:
        std::uint64_t waiting_for_frame{0};
        std::size_t   wrap_count {0};
        std::size_t   byte_offset{0};
        std::size_t   byte_count {0};
    };

    std::size_t             m_capacity_byte_count{0};
    std::size_t             m_write_position     {0};
    std::size_t             m_write_wrap_count   {1};
    std::size_t             m_read_wrap_count    {0};
    std::size_t             m_read_offset        {0};
    std::vector<Sync_entry> m_sync_entries;
};

} // namespace erhe::circular_ring_buffer
