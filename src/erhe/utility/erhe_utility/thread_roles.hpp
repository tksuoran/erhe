#pragma once

#include <thread>

namespace erhe::utility {

// The three phases of one editor frame (doc/editor/render_thread.md
// section 2.1). none is outside any frame: before the main loop starts,
// between frames, during shutdown.
enum class Frame_phase : unsigned int {
    none = 0,
    m1_overlap,  // main thread, overlaps the render phase of the previous frame
    m2_prepare,  // main thread, render thread idle
    r_render,    // render thread, main thread in M1 or M3 of the same / next frame
    m3_imgui     // main thread, the ImGui windows draw while the render thread renders the same frame
};

[[nodiscard]] auto c_str(Frame_phase phase) -> const char*;

// Process-wide thread roles and the frame phase of each role, for the
// thread ownership asserts of doc/editor/render_thread.md section 2.7. The
// asserting functions live in several erhe libraries, so the roles live
// here.
//
// A program that never sets the roles (unit tests, GPU tests, examples)
// gets no-op asserts: every assert passes while the id it compares against
// is the default std::thread::id{}. The phase asserts are no-ops until the
// render role is set, which happens when the frame loop starts; anything
// before that (start-up, the editor's init pump) is outside the frame
// model.
//
// Roles and phases: there is one phase per role, the main phase and the
// render phase. The calling role is the render role when the calling
// thread is the render thread and the render thread is not the main
// thread (dedicated render thread); every other caller, the main thread
// included, has the main role. In inline mode (render thread == main
// thread) the main thread holds both roles and owns both values:
// set_frame_phase() from the main thread writes the main phase, and also
// the render phase while the render thread is the main thread. The editor
// sets the main phase to none before the kick, and its render phase
// function sets r_render / none on whichever thread runs it; in inline
// mode those writes land in both phases. From the dedicated render thread
// set_frame_phase() writes the render phase only. get_frame_phase() and
// assert_frame_phase() use the calling role's phase;
// assert_main_thread_in_phase() and assert_command_recording_allowed() use
// the main phase. A worker thread has the main role and would read the
// main phase racily, so get_frame_phase() and assert_frame_phase() are for
// the two role threads only.
//
// Synchronization: none here, by design. The main id is set once, before
// any other thread is created. The render id is set once by the role
// thread before any frame: in inline mode by the main thread at the start
// of the frame loop, in dedicated mode by the render thread itself inside
// Render_thread's constructor handshake (mutex + condition variable), so
// the main thread's first read follows the write. Worker and server
// threads read only the main id (assert_command_recording_allowed stops
// there). Each phase is written and read by one thread only: the main
// phase by the main thread, the render phase by the render thread (by the
// main thread in inline mode, where both roles are one thread).
class Thread_roles
{
public:
    static void set_main_thread  (std::thread::id id);
    static void set_render_thread(std::thread::id id);
    // Sets the calling role's phase (see the class comment).
    static void set_frame_phase  (Frame_phase phase);

    [[nodiscard]] static auto get_main_thread  () -> std::thread::id;
    [[nodiscard]] static auto get_render_thread() -> std::thread::id;
    // The calling role's phase.
    [[nodiscard]] static auto get_frame_phase  () -> Frame_phase;
    // True when the calling role is the render role: the calling thread is
    // the render thread and the render thread is not the main thread (see
    // the class comment). False on every other thread, in inline mode and
    // while the roles are unset.
    [[nodiscard]] static auto is_render_role   () -> bool;

    // ERHE_VERIFY the calling thread is the main thread (no-op when unset).
    static void assert_main_thread();

    // ERHE_VERIFY the calling thread is the render thread (no-op when unset).
    static void assert_render_thread();

    // Fatal (ERHE_FATAL, names the role, its phase and the expected one) unless the calling role's
    // phase is the given one; no-op until the render role is set.
    static void assert_frame_phase(Frame_phase phase);

    // Fatal unless the calling thread is the main thread and the main
    // phase is the given one or none (outside any frame: start-up, scene
    // load, shutdown). No-op when the main role is unset.
    static void assert_main_thread_in_phase(Frame_phase phase);
    // Either of two phases (the ImGui windows draw in m2_prepare without the
    // ImGui overlap, in m3_imgui with it).
    static void assert_main_thread_in_phase(Frame_phase phase_a, Frame_phase phase_b);

    // ERHE_VERIFY a command buffer may be obtained / recorded on the
    // calling thread now: the main thread records nothing while the main
    // phase is m1_overlap
    // (doc/editor/render_thread.md section 2.3). Any other thread and any
    // other phase passes (init pump, tests, worker slots).
    static void assert_command_recording_allowed();
};

} // namespace erhe::utility
