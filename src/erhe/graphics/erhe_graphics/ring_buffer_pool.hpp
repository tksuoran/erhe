#pragma once

#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace erhe::graphics {

class Device;
class Ring_buffer;

// The ring buffers behind Device::allocate_ring_buffer_entry(), shared by
// every backend (doc/erhe/ring_buffer_memory.md). An allocation first tries
// the buffers of its usage class without a wrap, then with a wrap, and only
// then creates a buffer: the first of a usage class with 4x headroom, later
// ones (spills, created while every existing buffer is full of in-flight
// ranges) sized to the request. frame_completed() retires the frame's ranges
// and reclaims spills that sat idle for idle_frame_threshold frames, keeping
// one warm buffer per usage class, so a load spike does not grow the pool for
// the rest of the process.
class Ring_buffer_pool
{
public:
    static constexpr uint64_t    idle_frame_threshold     = 16;
    static constexpr std::size_t warn_buffer_count        = 8;

    Ring_buffer_pool(Device& device, std::size_t min_buffer_size);
    ~Ring_buffer_pool() noexcept;
    Ring_buffer_pool(const Ring_buffer_pool&) = delete;
    Ring_buffer_pool& operator=(const Ring_buffer_pool&) = delete;

    [[nodiscard]] auto allocate(std::size_t required_alignment, Ring_buffer_usage usage, std::size_t byte_count) -> Ring_buffer_range;
    void frame_completed(uint64_t completed_frame);
    // Destroys every buffer; the backend calls this before draining its
    // completion handlers (buffer destructors enqueue some).
    void clear();

    [[nodiscard]] auto get_buffer_count() const -> std::size_t;
    [[nodiscard]] auto get_total_byte_count() const -> std::size_t;

private:
    Device&                                   m_device;
    std::size_t                               m_min_buffer_size;
    std::vector<std::unique_ptr<Ring_buffer>> m_ring_buffers;
};

} // namespace erhe::graphics
