#pragma once

#include "erhe_circular_ring_buffer/circular_ring_buffer_algorithm.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_utility/debug_label.hpp"

namespace erhe::graphics {

class Ring_buffer_create_info
{
public:
    std::size_t                size             {0};
    Ring_buffer_usage          ring_buffer_usage{Ring_buffer_usage::None};
    Buffer_usage               buffer_usage     {static_cast<Buffer_usage>(0x03ffu)}; // all usage bits including transfer_src/transfer_dst
    erhe::utility::Debug_label debug_label      {};
};

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

    void frame_completed(uint64_t completed_frame);

private:
    Device&                                                  m_device;
    Ring_buffer_usage                                        m_ring_buffer_usage;

    std::unique_ptr<Buffer>                                  m_buffer;
    std::vector<std::byte>                                   m_cpu_buffer; // Shadow buffer for non-persistent mode

    erhe::circular_ring_buffer::Circular_ring_buffer_algorithm m_algorithm;
};

} // namespace erhe::graphics
