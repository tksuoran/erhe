#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"

namespace erhe::graphics {

class Ring_buffer_create_info
{
public:
    std::size_t       size             {0};
    Ring_buffer_usage ring_buffer_usage{Ring_buffer_usage::None};
    Buffer_usage      buffer_usage     {0xff}; // TODO
    const char*       debug_label      {nullptr};
};

class Ring_buffer_impl;

class Ring_buffer
{
public:
    Ring_buffer(Device& device, const Ring_buffer_create_info& create_info);
    ~Ring_buffer() noexcept;

    void get_size_available_for_write(
        std::size_t  required_alignment,
        std::size_t& out_alignment_byte_count_without_wrap,
        std::size_t& out_available_byte_count_without_wrap,
        std::size_t& out_available_byte_count_with_wrap
    ) const;
    [[nodiscard]] auto acquire(std::size_t required_alignment, Ring_buffer_usage usage, std::size_t byte_count) -> Ring_buffer_range;
    [[nodiscard]] auto match  (Ring_buffer_usage ring_buffer_usage) const -> bool;

    // For Ring_buffer_range
    void flush(std::size_t byte_offset, std::size_t byte_count);
    void close(std::size_t byte_offset, std::size_t byte_write_count);
    void make_sync_entry(std::size_t wrap_count, std::size_t byte_offset, std::size_t byte_count);

    [[nodiscard]] auto get_buffer() -> Buffer*;
    [[nodiscard]] auto get_name  () const -> const std::string&;

    void frame_completed(uint64_t completed_frame);

private:
    void sanity_check();
    void wrap_write  ();

    struct Ring_buffer_sync_entry
    {
        uint64_t    waiting_for_frame{0};
        std::size_t wrap_count {0};
        size_t      byte_offset{0};
        size_t      byte_count {0};
    };


    Device&                 m_device;
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
