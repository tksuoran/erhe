#pragma once

#include "erhe_circular_ring_buffer/circular_ring_buffer_algorithm.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_utility/debug_label.hpp"

#include <mutex>

namespace erhe::graphics {

class Ring_buffer_create_info
{
public:
    std::size_t                size             {0};
    Ring_buffer_usage          ring_buffer_usage{Ring_buffer_usage::None};
    Buffer_usage               buffer_usage     {static_cast<Buffer_usage>(0x03ffu)}; // all usage bits including transfer_src/transfer_dst
    erhe::utility::Debug_label debug_label      {};
};

// Whether Ring_buffer::try_acquire may wrap to the start of the buffer.
enum class Ring_buffer_wrap : unsigned int
{
    not_allowed = 0,
    allowed
};

// Thread safety: every method may be called from any thread. m_mutex guards
// the allocation state (the circular algorithm, the pending reads, the last
// used frame); the GPU buffer and the CPU shadow are written only through
// the ranges, each owned by one thread. Ring_buffer_pool hands each role its
// own buffers (ring_buffer_pool.hpp), so one buffer's ranges are acquired
// and released in frame order; a range may still be released on another
// thread (a completion handler on the render thread releasing a main role
// readback range), which is why the bookkeeping is locked here.
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
    // Checks the space (without a wrap, or with one when wrap allows it) and
    // acquires in one step under m_mutex; an empty range (get_buffer() ==
    // nullptr) when byte_count does not fit.
    [[nodiscard]] auto try_acquire(std::size_t required_alignment, Ring_buffer_usage usage, std::size_t byte_count, Ring_buffer_wrap wrap) -> Ring_buffer_range;
    [[nodiscard]] auto match  (Ring_buffer_usage ring_buffer_usage) const -> bool;

    [[nodiscard]] auto get_ring_buffer_usage() const -> Ring_buffer_usage;
    [[nodiscard]] auto get_capacity_byte_count() const -> std::size_t;

    // Mark every released range complete and reclaim its space. Only valid
    // when the caller guarantees the GPU has consumed every released range
    // (e.g. after a fence wait on the submit that recorded all consumers)
    // and no acquired range is still open.
    void complete_all_syncs();

    // True when all space is free and no sync entry is outstanding, i.e.
    // no in-flight GPU work references this buffer.
    [[nodiscard]] auto is_idle() const -> bool;

    // Device frame index of the most recent acquire(); 0 if never acquired.
    [[nodiscard]] auto get_last_used_frame() const -> uint64_t;

    // For Ring_buffer_range
    void flush(std::size_t byte_offset, std::size_t byte_count);
    void close(std::size_t byte_offset, std::size_t byte_write_count);
    void make_sync_entry(std::size_t wrap_count, std::size_t byte_offset, std::size_t byte_count);

    [[nodiscard]] auto get_buffer() -> Buffer*;

    void frame_completed(uint64_t completed_frame);

private:
    [[nodiscard]] auto acquire_locked(std::size_t required_alignment, Ring_buffer_usage usage, std::size_t byte_count) -> Ring_buffer_range;

    // Non-persistent CPU_read readback: the GPU writes the buffer during a
    // frame; once that frame's fence signals we download the written region
    // into m_cpu_buffer (the shadow the acquired span points into) so the
    // consumer's completion handler can read it. Tracked at acquire time
    // because the range is not released until inside that handler.
    class Pending_read
    {
    public:
        uint64_t    frame      {0};
        std::size_t byte_offset {0};
        std::size_t byte_count {0};
    };

    Device&                                                  m_device;
    Ring_buffer_usage                                        m_ring_buffer_usage;
    mutable std::mutex                                       m_mutex;
    uint64_t                                                 m_last_used_frame{0};

    std::unique_ptr<Buffer>                                  m_buffer;
    std::vector<std::byte>                                   m_cpu_buffer; // Shadow buffer for non-persistent mode
    std::vector<Pending_read>                                m_pending_reads; // Non-persistent CPU_read downloads

    erhe::circular_ring_buffer::Circular_ring_buffer_algorithm m_algorithm;
};

} // namespace erhe::graphics
