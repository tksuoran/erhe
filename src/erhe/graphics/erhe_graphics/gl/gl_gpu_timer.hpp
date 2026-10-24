#pragma once

#include "erhe_graphics/gl/gl_objects.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_profile/profile.hpp"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace erhe::graphics {

class Command_buffer;
class Device;

// OpenGL GPU timer backend. Each timer owns a small ring buffer of
// GL_TIMESTAMP query pairs. write_begin_timestamp and write_end_timestamp
// each record a timestamp (glQueryCounter) into the current ring slot. After
// the GPU completes the work the result is polled in
// Gpu_timer_impl::end_frame() (driven from Gl_device::end_frame) and the
// difference is stored as m_last_result.
//
// Timestamps, unlike GL_TIME_ELAPSED queries (of which only one may be active
// at a time), nest freely: several timers may be bound to one Render_pass,
// and explicit-range timers (Gpu_timer::begin / end) may enclose timed
// render passes or each other, as on the Vulkan and Metal backends.
//
// Main-thread-only by design: query objects are per-context (unshared), the
// per-timer state is a ring of queries no per-context slot can represent,
// and nothing wants GPU timing off the drawing thread. Creation and the
// begin/end timestamps assert the main context.
class Gpu_timer_impl
{
public:
    Gpu_timer_impl(Device& device, const char* label);
    ~Gpu_timer_impl() noexcept;

    Gpu_timer_impl(const Gpu_timer_impl&) = delete;
    auto operator=(const Gpu_timer_impl&) = delete;
    Gpu_timer_impl(Gpu_timer_impl&&)      = delete;
    auto operator=(Gpu_timer_impl&&)      = delete;

    [[nodiscard]] auto last_result() -> Gpu_timer_result;
    [[nodiscard]] auto label      () const -> const char*;

    void write_begin_timestamp(Command_buffer& command_buffer);
    void write_end_timestamp  (Command_buffer& command_buffer);

    // Walk every live Gpu_timer_impl and poll for available query results.
    // Called from Gl_device::end_frame; the registry exists for this walk.
    static void end_frame();

private:
    void create();
    void poll  ();

    class Query
    {
    public:
        std::optional<Gl_query> begin_query{};
        std::optional<Gl_query> end_query  {};
        bool                    pending    {false};
        uint64_t                frame_id   {0};  // Command_buffer frame id of the measured work
    };

    static constexpr std::size_t                      s_count = 4;
    static ERHE_PROFILE_MUTEX_DECLARATION(std::mutex, s_mutex);
    static std::vector<Gpu_timer_impl*>               s_all_gpu_timers;
    static std::size_t                                s_index;

    Device*                    m_device     {nullptr};
    std::array<Query, s_count> m_queries;
    Gpu_timer_result           m_last_result{};
    const char*                m_label      {nullptr};
};

} // namespace erhe::graphics
