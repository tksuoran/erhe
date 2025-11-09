#pragma once

#include "erhe_graphics/ring_buffer.hpp"

namespace erhe::graphics {

struct Ring_buffer_sync_entry
{
    uint64_t    waiting_for_frame{0};
    std::size_t wrap_count {0};
    size_t      byte_offset{0};
    size_t      byte_count {0};
};

class Ring_buffer_impl final
{
public:
    Ring_buffer_impl(Device& device, Ring_buffer& ring_buffer, const Ring_buffer_create_info& create_info);
    ~Ring_buffer_impl();

    void sanity_check();
    void get_size_available_for_write(
        std::size_t  required_alignment,
        std::size_t& out_alignment_byte_count_without_wrap,
        std::size_t& out_available_byte_count_without_wrap,
        std::size_t& out_available_byte_count_with_wrap
    ) const;
    [[nodiscard]] auto acquire(std::size_t required_alignment, Ring_buffer_usage ring_buffer_usage, std::size_t byte_count) -> Ring_buffer_range;
    [[nodiscard]] auto match  (Ring_buffer_usage ring_buffer_usage) const -> bool;

    // For Ring_buffer_range
    void flush(std::size_t byte_offset, std::size_t byte_count);
    void close(std::size_t byte_offset, std::size_t byte_write_count);
    void make_sync_entry(std::size_t wrap_count, std::size_t byte_offset, std::size_t byte_count);

    [[nodiscard]] auto get_buffer() -> Buffer*;
    [[nodiscard]] auto get_name  () const -> const std::string&;

    // For Device
    void frame_completed(uint64_t frame);

private:
    void wrap_write();

    Device&                 m_device;
    Ring_buffer&            m_ring_buffer;
    Ring_buffer_usage       m_ring_buffer_usage;

    std::unique_ptr<Buffer> m_buffer;

    std::size_t             m_map_offset           {0};
    std::size_t             m_write_position       {0};
    std::size_t             m_write_wrap_count     {1};
    std::size_t             m_last_write_wrap_count{1}; // for handling write wraps wraps
    std::size_t             m_read_wrap_count      {0};
    std::size_t             m_read_offset          {0}; // This is the first offset where we cannot write

    std::string             m_name;

    std::vector<Ring_buffer_sync_entry> m_sync_entries;
};

} // namespace erhe::graphics
