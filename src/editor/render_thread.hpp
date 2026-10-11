#pragma once

#include "config/generated/render_thread_mode.hpp"

#include "erhe_graphics/swapchain.hpp"
#include "erhe_xr/frame_timing.hpp"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace erhe::graphics {
    class Command_buffer;
    class Texture;
}

namespace editor {

class Scene_root;

// Per-frame data that crosses the frame phases (doc/editor/render_thread.md
// sections 2.1 and 2.5): the overlap phase (M1) writes the device frame id, the
// pacer's display advance, present target and present-wait id, the frame
// number, the host time, the fixed-step time accumulations and the OpenXR
// wait result (Frame_timing), the prepare phase (M2) the upload command
// buffer, the render decision, the resize request, the lightmap atlas
// binding and the scene the indirect diffuse producers tick on, and the
// render phase (R) reads them. R owns the swapchain and its
// frame command buffer. Fixed size; one instance owned by Editor, rewritten
// each frame, copied into Render_thread by kick(), so M1 of the next frame
// rewrites it while R renders from the copy.
class Frame_packet
{
public:
    uint64_t                              frame_number         {0};  // Time frame number after M1's prepare_update
    uint64_t                              device_frame_id      {0};  // Device::wait_for_frame_start() result (M1); M2 acquires the slot for it, R sets the present target for it
    int64_t                               display_advance_ns   {-1}; // FR4 routing, from M1's pacer decision for this frame; < 0 = wall clock
    double                                present_target_time  {0.0}; // FR3 target present time from M1's pacer decision (reference-clock seconds); 0 = none
    double                                present_hold_until   {0.0}; // C15 present-request holdback deadline from M1's pacer decision; 0 = none
    std::int64_t                          present_wait_id      {-1};  // FR5 present-wait clamp: the frame id R waits to be displayed, from M1's pacer decision for this frame when frame pacing is enforced; -1 = no wait
    int64_t                               frame_host_system_time_ns{0}; // Time::get_host_system_time_ns() after M1's prepare_update; R's animated selection outline reads it
    erhe::xr::Frame_timing                headset_frame_timing {};   // M1's xrWaitFrame result; R begins the OpenXR frame with it
    float                                 host_system_dt_s     {0.0f};
    int64_t                               host_system_time_ns  {0};
    erhe::graphics::Command_buffer*       upload_command_buffer{nullptr}; // recorded in M2, submitted first in R
    bool                                  should_render        {false}; // M2: the window is visible; R engages the swapchain only then
    erhe::graphics::Frame_begin_info      frame_begin_info     {};
    int                                   window_width         {0};  // the desktop window size M2 saw; R sizes the window ImGui host's swapchain render pass from it
    int                                   window_height        {0};
    std::shared_ptr<erhe::graphics::Texture> lightmap_texture   {};  // M2: App_rendering's lightmap atlas binding; R hands it to the forward renderer
    bool                                  ray_trace_enabled    {false}; // M2: Ray_trace_renderer's live switch; R hands it to the renderer (set_render_enabled)
    std::shared_ptr<Scene_root>           indirect_diffuse_scene_root{}; // M2: App_scenes::get_single_scene_root(); R ticks the DDGI / radiance cascades producers on it
};

// What the render phase reports back to the main thread through
// Render_thread::join() or, with frame pipelining, through the next
// Render_thread::kick(). The timings are added into the editor's phase
// timing accumulators by the main thread, so those accumulators are
// touched by the main thread only.
class Frame_result
{
public:
    // The device frame R rendered (Frame_packet::device_frame_id). The main
    // thread publishes it at the join as App_context::
    // last_rendered_frame_index: the frame whose render phase results
    // (Render_frame_results) the main thread reads.
    uint64_t     device_frame_id   {0};
    // Sky_renderer::are_luts_ready() at the end of R: the atmosphere LUTs
    // were generated in this or an earlier frame's command buffer, so a later
    // frame may sample them (the lightmap bake's sky term, M2).
    bool         sky_luts_ready    {false};
    double       render_us         {0.0}; // R render, wall clock
    double       wait_for_swapchain_us{0.0}; // wait_for_swapchain (with begin_swapchain, the swapchain term)
    double       begin_swapchain_us{0.0}; // begin_swapchain (with wait_for_swapchain, the swapchain term)
    double       draw_data_wait_us {0.0}; // the ImGui host nodes' wait for the frame's draw data (Render_thread::wait_for_draw_data)
    // The handoff latencies (dedicated thread; 0 inline): kick() to the
    // render phase's first instruction, and the later of the render phase's
    // end and the start of the main thread's wait to the join's return.
    double       kick_latency_us   {0.0};
    double       join_latency_us   {0.0};
    // Written by the main thread at the join (join(), or the kick() that
    // joined the frame): the main thread's wait for the render phase to
    // finish (equal when it had finished already). The phase timing log's
    // join_wait and the frame time record's join_wait pair.
    std::chrono::steady_clock::time_point join_wait_begin{};
    std::chrono::steady_clock::time_point join_wait_end  {};
    // wait_for_swapchain found no swapchain (Android surface between
    // background and foreground): R submitted the uploads device-only,
    // presented nothing and ended the device frame; the main thread sleeps
    // briefly after the join so the loop does not spin on the surface.
    bool         no_swapchain      {false};
};

// The handoff between the main thread and the render phase
// (doc/editor/render_thread.md section 2.5, doc/editor/editor.md "Frame
// phases and the render thread"). The main thread fills a Frame_packet at
// the end of the prepare phase and calls kick(); the frame is joined
// (its Frame_result returned once its render phase is done) either by
// join(), called by the main thread before the next prepare phase (the
// serial schedule), or by the next kick(), which joins the outstanding
// frame before it hands over the new one (frame pipelining: the main
// thread prepares frame N+1 while the render phase renders frame N). At
// most one frame is outstanding.
//
// inline_main_thread: kick() runs the render phase on the calling thread
// and join() returns the stored result at once. dedicated_thread: the
// render phase runs on a thread owned by this class, which publishes
// itself as the render role (erhe::utility::Thread_roles) before the
// constructor returns. One mutex and two condition variables carry the
// handoff; the packet and result are written only by the side that owns
// the current state, so they need no further locking.
class Render_thread
{
public:
    Render_thread(
        Render_thread_mode                                       mode,
        std::function<void(const Frame_packet&, Frame_result&)> render_phase
    );
    ~Render_thread() noexcept;

    Render_thread(const Render_thread&)            = delete;
    Render_thread& operator=(const Render_thread&) = delete;
    Render_thread(Render_thread&&)                 = delete;
    Render_thread& operator=(Render_thread&&)      = delete;

    // Main thread, end of the prepare phase. Joins the outstanding frame
    // first, waiting for its render phase to finish, and returns its
    // result (nothing when no frame was outstanding: the first frame, or
    // join() joined it already), then hands the packet to the render
    // phase.
    [[nodiscard]] auto kick(const Frame_packet& packet) -> std::optional<Frame_result>;

    // Main thread: waits for the outstanding frame to finish and returns
    // its result (the caller tracks whether a frame was kicked and not
    // joined). The serial schedule calls it at the end of the overlap phase
    // of the next frame; both schedules after the frame loop.
    [[nodiscard]] auto join() -> Frame_result;

    // The ImGui draw data handoff (doc/editor/render_thread.md section 2.5):
    // the main thread publishes once the frame's ImGui phase has copied its
    // draw data (Imgui_windows::end_frame), before or after the kick; the
    // ImGui host rendergraph nodes of that frame wait for it before they
    // read the copy. The wait is slot-exact: it returns once that frame
    // number itself was published (at once when it was, the serial order),
    // never for a later number, and verifies the frame's entry was not
    // overwritten by a later frame. Frame numbers are published in
    // increasing order.
    void publish_draw_data  (uint64_t frame_number);
    void wait_for_draw_data (uint64_t frame_number);

    // Waits for an in-flight frame, then ends and joins the dedicated
    // thread and releases its packet copy. Idempotent; called by ~Editor
    // before the GPU is quiesced.
    void stop();

    [[nodiscard]] auto get_mode() const -> Render_thread_mode;

private:
    enum class State : unsigned int {
        starting, // dedicated thread not yet running its loop
        idle,     // waiting for kick()
        running,  // render phase in progress
        done,     // render phase finished, waiting for join()
        exiting   // stop() requested
    };

    void thread_main();
    // Under m_mutex (dedicated thread) or on the main thread (inline): the
    // finished frame's result with the join fields filled; the state goes
    // done -> idle.
    [[nodiscard]] auto take_result(
        std::chrono::steady_clock::time_point wait_begin,
        std::chrono::steady_clock::time_point wait_end
    ) -> Frame_result;

    Render_thread_mode                                      m_mode;
    std::function<void(const Frame_packet&, Frame_result&)> m_render_phase;
    std::mutex                                              m_mutex;
    std::condition_variable                                 m_kick_cv;
    std::condition_variable                                 m_done_cv;
    std::condition_variable                                 m_draw_data_cv;
    // Published frame numbers by frame_number % size: more entries than
    // frames that can be published and not yet rendered (two: the frame R
    // renders and the one the ImGui phase prepares).
    static constexpr std::size_t                            s_draw_data_slot_count{4};
    std::array<uint64_t, s_draw_data_slot_count>            m_draw_data_published{};
    uint64_t                                                m_draw_data_newest{0}; // the newest frame whose draw data is published
    State                                                   m_state{State::starting};
    Frame_packet                                            m_packet{};
    Frame_result                                            m_result{};
    std::chrono::steady_clock::time_point                   m_kick_time{};
    std::chrono::steady_clock::time_point                   m_done_time{};
    std::thread                                             m_thread;
};

} // namespace editor
