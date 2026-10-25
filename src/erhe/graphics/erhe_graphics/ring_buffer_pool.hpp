#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"

#include <cstddef>
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
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
//
// Thread safety and roles: every method may be called from any thread;
// m_mutex guards the buffer lists (lock order: m_mutex, then a buffer's own
// mutex, then the device's completion handler list, which ~Ring_buffer
// reaches). The buffers are kept per thread role
// (erhe::utility::Thread_roles::is_render_role()): with frame pipelining the
// main role acquires ranges for frame N+1 while the render role acquires
// ranges for frame N, and a ring buffer retires its space in acquisition
// order on the assumption that ranges of an older frame never follow ranges
// of a newer one. Each role's frames are in order, so each role allocates
// from its own buffers (worker threads share the main role's). In inline
// mode, and while the roles are unset, every allocation is the main role's.
// Ring_buffer_pool::get_usage(): the pool's buffers and how much of them
// unretired ranges hold (Device_debug_state).
class Ring_buffer_pool_usage
{
public:
    std::size_t buffer_count     {0};
    std::size_t busy_buffer_count{0}; // !Ring_buffer::is_idle()
    std::size_t bytes_in_use     {0}; // sum of Ring_buffer::get_bytes_in_use()
    std::size_t capacity_bytes   {0}; // sum of Ring_buffer::get_capacity_byte_count()
};

class Ring_buffer_pool
{
public:
    static constexpr uint64_t    idle_frame_threshold     = 16;
    static constexpr std::size_t warn_buffer_count        = 8;
    // Completed frames after which the size mark of an upload staging
    // request that found no room is dropped if nothing that large retried.
    static constexpr uint64_t    upload_staging_wait_expiry_frames = 8;

    Ring_buffer_pool(Device& device, std::size_t min_buffer_size);
    ~Ring_buffer_pool() noexcept;
    Ring_buffer_pool(const Ring_buffer_pool&) = delete;
    Ring_buffer_pool& operator=(const Ring_buffer_pool&) = delete;

    [[nodiscard]] auto allocate(std::size_t required_alignment, Ring_buffer_usage usage, std::size_t byte_count) -> Ring_buffer_range;

    // Host-visible staging for GPU uploads that may wait a frame (the
    // budgeted asset-load uploads), with a hard cap: one CPU_write ring of
    // capacity_byte_count bytes, never spilled. Returns an EMPTY range when
    // the ring has no room right now - the caller retries next frame, after
    // frame completion has retired older ranges. A request larger than the
    // whole cap is served by a one-shot buffer sized to it, and only while
    // the ring and any earlier one-shot buffer are idle, so the staged
    // bytes in flight never exceed max(cap, largest single request). The
    // ring takes the capacity of the request; a different capacity
    // replaces it once it is idle (requests wait until then). Main role
    // only: the ring retires ranges in acquisition order.
    [[nodiscard]] auto allocate_upload_staging(std::size_t required_alignment, std::size_t byte_count, std::size_t capacity_byte_count) -> Ring_buffer_range;
    [[nodiscard]] auto get_upload_staging_usage() const -> Upload_staging_usage;
    void frame_completed(uint64_t completed_frame);
    // Destroys every buffer; the backend calls this before draining its
    // completion handlers (buffer destructors enqueue some).
    void clear();

    [[nodiscard]] auto get_buffer_count() const -> std::size_t;
    [[nodiscard]] auto get_total_byte_count() const -> std::size_t;
    [[nodiscard]] auto get_usage() const -> Ring_buffer_pool_usage;

private:
    static constexpr std::size_t s_role_count = 2; // main, render

    [[nodiscard]] static auto get_calling_role_index() -> std::size_t;
    void frame_completed_in_role(std::vector<std::unique_ptr<Ring_buffer>>& ring_buffers, uint64_t completed_frame);
    [[nodiscard]] auto get_upload_staging_bytes_in_use_locked() const -> std::size_t;

    enum class Upload_staging_wait : unsigned int {
        forget   = 0, // refused for a reason that is not lack of room
        remember = 1  // refused for lack of room: smaller requests wait behind it
    };

    Device&                                                             m_device;
    std::size_t                                                         m_min_buffer_size;
    mutable std::mutex                                                  m_mutex;
    std::array<std::vector<std::unique_ptr<Ring_buffer>>, s_role_count> m_ring_buffers;
    std::unique_ptr<Ring_buffer>                                        m_upload_staging;          // the capped ring
    std::unique_ptr<Ring_buffer>                                        m_upload_staging_oversize; // one-shot, for a request larger than the cap
    Upload_staging_usage                                                m_upload_staging_usage;
    std::size_t                                                         m_upload_staging_waiting_byte_count{0}; // size of the request that found no room, 0 when none waits
    uint64_t                                                            m_upload_staging_waiting_frame     {0}; // m_last_completed_frame when it last retried
    uint64_t                                                            m_last_completed_frame             {0};
};

} // namespace erhe::graphics
