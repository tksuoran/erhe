#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace erhe::graphics {

class Command_buffer;
class Device;
class Render_pass;
class Gpu_timer_impl;

// One completed measurement of a Gpu_timer: the GPU time and the device frame
// id (Command_buffer::get_frame_id()) of the frame whose command buffer
// recorded it. frame_id 0: no result yet (device frame ids start at 1).
class Gpu_timer_result
{
public:
    uint64_t nanoseconds{0};
    uint64_t frame_id   {0};
};

// A GPU-side wall-clock timer. The result of a measurement becomes available
// via last_result() roughly one or two frames later (after the frame that
// recorded it has completed on the GPU).
//
// Two ways to bracket the measured work:
//
// - Render_pass bound: constructed with a Render_pass, the timer measures the
//   time the GPU spends executing that pass. Begin/end timestamps are emitted
//   automatically from Render_pass::start_render_pass /
//   Render_pass::end_render_pass. The bound Render_pass must outlive the
//   Gpu_timer. When the Render_pass is destroyed, the Gpu_timer is
//   unregistered from it (and any further render pass scopes will not produce
//   timestamps).
//
// - Explicit range: constructed with a Device, the caller brackets a range of
//   work recorded into one Command_buffer with begin() / end() (or the RAII
//   Scoped_gpu_timer). Used for work outside render passes, such as a range of
//   compute dispatches. begin() and end() must be called on the same
//   Command_buffer, at most once each per frame, and must not nest with
//   another explicit-range timer (OpenGL allows one GL_TIME_ELAPSED query at a
//   time). The timestamps are taken after all previously recorded work has
//   completed, so work that overlaps the range boundaries is counted where it
//   finishes.
//
// Both kinds are listed by all_gpu_timers().
class Gpu_timer final
{
public:
    Gpu_timer(Render_pass& render_pass, const char* label);
    Gpu_timer(Device& device, const char* label);
    ~Gpu_timer() noexcept;

    Gpu_timer     (const Gpu_timer&) = delete;
    auto operator=(const Gpu_timer&) = delete;
    Gpu_timer     (Gpu_timer&&)      = delete;
    auto operator=(Gpu_timer&&)      = delete;

    // Last completed measurement, in nanoseconds. May briefly read 0 before
    // the first frame's results are available.
    [[nodiscard]] auto last_result() -> uint64_t;
    // last_result() together with the frame that recorded it, read as one
    // value. A timer whose work was not recorded in a later frame keeps its
    // older result, so a reader keyed by frame compares frame_id.
    [[nodiscard]] auto last_result_with_frame_id() -> Gpu_timer_result;
    [[nodiscard]] auto label      () const -> const char*;

    // Explicit-range timers only (constructed with a Device): record the
    // begin / end timestamp of the measured range into the command buffer.
    void begin(Command_buffer& command_buffer);
    void end  (Command_buffer& command_buffer);

    // Internal: invoked by Render_pass at start/end of the bound pass.
    // Not intended to be called directly by user code.
    void write_begin_timestamp(Command_buffer& command_buffer);
    void write_end_timestamp  (Command_buffer& command_buffer);

    // Internal: invoked by ~Render_pass when the bound pass is destroyed
    // before this timer. After this call the timer becomes inert
    // (write_begin_timestamp / write_end_timestamp do nothing).
    void on_render_pass_destroyed();

    // Snapshot of every live Gpu_timer in the process. Used by
    // Performance_window to discover plots automatically.
    [[nodiscard]] static auto all_gpu_timers() -> std::vector<Gpu_timer*>;

    // Calls callback for every live Gpu_timer while holding the registry
    // lock, so no timer is destroyed during the walk (the pointers of an
    // all_gpu_timers() snapshot can dangle once the lock is released). The
    // callback must not create or destroy a Gpu_timer. Allocation-free
    // (for per-frame readers).
    static void for_each_gpu_timer(const std::function<void(Gpu_timer&)>& callback);

private:
    enum class Binding : unsigned int
    {
        render_pass,
        explicit_range
    };

    Binding                         m_binding;
    Render_pass*                    m_render_pass{nullptr};
    std::unique_ptr<Gpu_timer_impl> m_impl;
};

// RAII bracket for an explicit-range Gpu_timer: begin() at construction,
// end() at destruction, both on the same Command_buffer.
class Scoped_gpu_timer final
{
public:
    Scoped_gpu_timer(Gpu_timer& timer, Command_buffer& command_buffer);
    ~Scoped_gpu_timer() noexcept;

    Scoped_gpu_timer(const Scoped_gpu_timer&) = delete;
    auto operator=  (const Scoped_gpu_timer&) = delete;
    Scoped_gpu_timer(Scoped_gpu_timer&&)      = delete;
    auto operator=  (Scoped_gpu_timer&&)      = delete;

private:
    Gpu_timer&      m_timer;
    Command_buffer& m_command_buffer;
};

} // namespace erhe::graphics
