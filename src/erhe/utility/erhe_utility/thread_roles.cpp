#include "erhe_utility/thread_roles.hpp"

#include "erhe_verify/verify.hpp"

namespace erhe::utility {

namespace {

class Thread_role_state
{
public:
    std::thread::id main_thread       {};
    std::thread::id render_thread     {};
    Frame_phase     main_frame_phase  {Frame_phase::none}; // main thread only
    Frame_phase     render_frame_phase{Frame_phase::none}; // render thread only (main thread in inline mode)
};

// See the synchronization note in thread_roles.hpp.
Thread_role_state s_state{};

// The calling role is the render role only on a dedicated render thread;
// in inline mode the main thread holds both roles (thread_roles.hpp).
[[nodiscard]] auto is_dedicated_render_thread() -> bool
{
    const std::thread::id this_thread = std::this_thread::get_id();
    return
        (s_state.render_thread != std::thread::id{}) &&
        (s_state.render_thread != s_state.main_thread) &&
        (this_thread == s_state.render_thread);
}

[[nodiscard]] auto calling_role_frame_phase() -> Frame_phase
{
    return is_dedicated_render_thread() ? s_state.render_frame_phase : s_state.main_frame_phase;
}

} // anonymous namespace

auto c_str(const Frame_phase phase) -> const char*
{
    switch (phase) {
        case Frame_phase::none:       return "none";
        case Frame_phase::m1_overlap: return "m1_overlap";
        case Frame_phase::m2_prepare: return "m2_prepare";
        case Frame_phase::r_render:   return "r_render";
        case Frame_phase::m3_imgui:   return "m3_imgui";
    }
    return "?";
}

void Thread_roles::set_main_thread(const std::thread::id id)
{
    s_state.main_thread = id;
}

void Thread_roles::set_render_thread(const std::thread::id id)
{
    s_state.render_thread = id;
}

void Thread_roles::set_frame_phase(const Frame_phase phase)
{
    if (is_dedicated_render_thread()) {
        s_state.render_frame_phase = phase;
        return;
    }
    s_state.main_frame_phase = phase;
    if (s_state.render_thread == s_state.main_thread) {
        // Inline mode: the main thread owns both phases.
        s_state.render_frame_phase = phase;
    }
}

auto Thread_roles::get_main_thread() -> std::thread::id
{
    return s_state.main_thread;
}

auto Thread_roles::get_render_thread() -> std::thread::id
{
    return s_state.render_thread;
}

auto Thread_roles::get_frame_phase() -> Frame_phase
{
    return calling_role_frame_phase();
}

auto Thread_roles::is_render_role() -> bool
{
    return is_dedicated_render_thread();
}

void Thread_roles::assert_main_thread()
{
    if (s_state.main_thread == std::thread::id{}) {
        return;
    }
    ERHE_VERIFY(std::this_thread::get_id() == s_state.main_thread);
}

void Thread_roles::assert_render_thread()
{
    if (s_state.render_thread == std::thread::id{}) {
        return;
    }
    ERHE_VERIFY(std::this_thread::get_id() == s_state.render_thread);
}

void Thread_roles::assert_frame_phase(const Frame_phase phase)
{
    if (s_state.render_thread == std::thread::id{}) {
        return;
    }
    const Frame_phase calling_role_phase = calling_role_frame_phase();
    if (calling_role_phase != phase) {
        ERHE_FATAL(
            "%s frame phase is %s, expected %s",
            is_dedicated_render_thread() ? "render" : "main",
            c_str(calling_role_phase),
            c_str(phase)
        );
    }
}

void Thread_roles::assert_main_thread_in_phase(const Frame_phase phase)
{
    if (s_state.main_thread == std::thread::id{}) {
        return;
    }
    ERHE_VERIFY(std::this_thread::get_id() == s_state.main_thread);
    if ((s_state.main_frame_phase != phase) && (s_state.main_frame_phase != Frame_phase::none)) {
        ERHE_FATAL("main thread in frame phase %s, expected %s or none", c_str(s_state.main_frame_phase), c_str(phase));
    }
}

void Thread_roles::assert_main_thread_in_phase(const Frame_phase phase_a, const Frame_phase phase_b)
{
    if (s_state.main_thread == std::thread::id{}) {
        return;
    }
    ERHE_VERIFY(std::this_thread::get_id() == s_state.main_thread);
    const Frame_phase phase = s_state.main_frame_phase;
    if ((phase != phase_a) && (phase != phase_b) && (phase != Frame_phase::none)) {
        ERHE_FATAL("main thread in frame phase %s, expected %s, %s or none", c_str(phase), c_str(phase_a), c_str(phase_b));
    }
}

void Thread_roles::assert_command_recording_allowed()
{
    if (s_state.main_thread == std::thread::id{}) {
        return;
    }
    if (std::this_thread::get_id() == s_state.main_thread) {
        if (s_state.main_frame_phase == Frame_phase::m1_overlap) {
            ERHE_FATAL("main thread obtains a command buffer during m1_overlap");
        }
        if (s_state.main_frame_phase == Frame_phase::m3_imgui) {
            ERHE_FATAL("main thread obtains a command buffer during m3_imgui (ImGui-time work records nothing; doc/editor/coding_rules.md)");
        }
    }
}

} // namespace erhe::utility
