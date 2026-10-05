#pragma once

#include "erhe_profile/profile.hpp"
#include "erhe_scene/trs_transform.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace erhe::scene { class Xformable; using Node = Xformable; }

namespace editor {

class Time;
class App_message_bus;

// Simulation clock source (MCP advance_time). wall_clock is the normal
// editor behavior; paused and manual freeze simulation time except for
// explicitly requested manual advances (request_simulation_advance).
enum class Time_mode : int {
    wall_clock = 0,
    paused     = 1,
    manual     = 2
};

// Source of the editor clock (doc/editor/time.md). wall_clock: the editor
// clock is the wall clock (steady_clock), sampled once per frame. fixed_dt:
// every frame advances the editor clock by a fixed dt, independent of how
// long the frame took, so headless, cloud and CI runs behave the same at any
// frame rate. Orthogonal to Time_mode, which gates the simulation.
enum class Editor_clock_source : int {
    wall_clock = 0,
    fixed_dt   = 1
};

[[nodiscard]] auto c_str(Editor_clock_source source) -> const char*;

class Time_context
{
public:
    float    simulation_dt_s    {0.0};
    int64_t  simulation_dt_ns   {0};
    int64_t  simulation_time_ns {0};
    uint64_t frame_number       {0};
    uint64_t subframe           {0};
};

class Transform_animation_entry
{
public:
    std::shared_ptr<erhe::scene::Node> node;
    erhe::scene::Trs_transform         parent_from_node_before;
    erhe::scene::Trs_transform         parent_from_node_after;
    int64_t                            time_duration_ns;
    int64_t                            start_time_ns;
};

class Time
{
public:
    // ERHE_FIXED_DT_MS=<milliseconds> in the environment selects the
    // fixed_dt editor clock from the first frame (the headless test fixtures
    // and run-books set 16.667).
    Time();

    [[nodiscard]] auto get_simulation_time_ns   () const -> int64_t;
    [[nodiscard]] auto get_frame_number         () const -> uint64_t;
    // Wall-clock frame time average (frame pacing / UI statistics); not
    // affected by the editor clock source.
    [[nodiscard]] auto get_frame_time_average_ms() const -> float;

    // The editor clock: UI, input and animation time. Sampled once per frame
    // by prepare_update(); in wall_clock mode it is the wall clock
    // (steady_clock nanoseconds, plus an offset after a switch from
    // fixed_dt) at the start of the frame, in fixed_dt mode
    // it advances by exactly the fixed dt per frame. Everything that reasons
    // about elapsed user-facing time (ImGui io.DeltaTime and double clicks,
    // input event timestamps, transform and scene animations, double-click /
    // long-press detection in tools) reads this clock; frame pacing, sleeps,
    // transport timeouts, load budgets and profiling stay on the wall clock.
    [[nodiscard]] auto get_editor_time_ns          () const -> int64_t;
    // Editor clock advance of the current frame (0 on the first frame).
    [[nodiscard]] auto get_editor_frame_duration_ns() const -> int64_t;
    // Maps an input event timestamp, stamped by its producer on the wall
    // clock (steady_clock nanoseconds), onto the editor clock: the wall
    // clock plus the wall_clock-mode offset (0 unless the run switched
    // from fixed_dt), or the current editor frame time in fixed_dt mode
    // (the events dispatched in one frame all happened during the
    // previous one).
    [[nodiscard]] auto map_input_timestamp_ns(int64_t wall_timestamp_ns) const -> int64_t;

    // Takes effect at the next prepare_update(). The editor clock stays
    // monotonic across switches: switching to fixed_dt continues from the
    // current value, switching to wall_clock offsets the wall clock so the
    // switching frame advances by its wall duration.
    void set_editor_clock_source(Editor_clock_source source, int64_t fixed_dt_ns);
    // The source of the current frame, and the one the next frame uses.
    [[nodiscard]] auto get_current_editor_clock_source() const -> Editor_clock_source;
    [[nodiscard]] auto get_pending_editor_clock_source() const -> Editor_clock_source;
    [[nodiscard]] auto get_fixed_dt_ns                () const -> int64_t;

    // advance_simulation == false pauses the fixed-step simulation (physics,
    // fly-camera and headset fixed updates) without losing wall-clock time:
    // no fixed steps are produced this frame and the accumulator is frozen, so
    // resuming does not replay a catch-up burst.
    //
    // simulation_advance_ns >= 0 advances the simulation by exactly that
    // duration instead of the sampled wall-clock frame delta (FR4 routing,
    // frame pacing step P2.4: the caller passes the delta between successive
    // predicted display times, so simulated state is sampled at the time each
    // frame is shown rather than the time it is produced). Negative keeps the
    // wall-clock path. The 25 ms dilation cap applies to both sources. In
    // fixed_dt mode the simulation advances by the fixed dt instead (no cap,
    // simulation_advance_ns ignored).
    void prepare_update     (bool advance_simulation = true, int64_t simulation_advance_ns = -1);
    void for_each_fixed_step(std::function<void(const Time_context&)> callback);

    // Manual simulation-time control (MCP advance_time): a queued manual
    // advance replaces the frame's simulation delta - wall clock / predicted
    // display delta, the 25 ms dilation cap and the hidden-window pause all
    // yield to it, so batch settling runs deterministically at
    // max_step_per_frame_ns of simulation time per rendered frame until the
    // queue drains. With no pending advance, paused and manual modes produce
    // zero fixed steps per frame.
    void set_time_mode(Time_mode mode);
    [[nodiscard]] auto get_time_mode() const -> Time_mode;
    void request_simulation_advance(int64_t advance_ns, int64_t max_step_per_frame_ns = 0);
    [[nodiscard]] auto get_pending_simulation_advance_ns() const -> int64_t;

    void update_transform_animations(App_message_bus& app_message_bus);
    void finish_all_transform_animations(App_message_bus& app_message_bus);
    void begin_transform_animation(
        std::shared_ptr<erhe::scene::Node> node,
        erhe::scene::Trs_transform         parent_from_node_before,
        erhe::scene::Trs_transform         parent_from_node_after,
        float                              time_duration
    );

private:
    int64_t  m_wall_last_frame_start_time_ns     {0};
    int64_t  m_editor_time_ns                    {0};
    int64_t  m_editor_frame_duration_ns          {0};
    int64_t  m_wall_to_editor_offset_ns          {0}; // wall_clock mode: editor = wall + offset
    Editor_clock_source m_editor_clock_source    {Editor_clock_source::wall_clock};
    Editor_clock_source m_next_editor_clock_source{Editor_clock_source::wall_clock};
    int64_t  m_fixed_dt_ns                       {16'666'667};
    int64_t  m_simulation_time_accumulator       {0};
    int64_t  m_simulation_dt_ns                  {0};
    int64_t  m_simulation_time_ns                {0};
    uint64_t m_frame_number                      {0};
    Time_mode m_time_mode                        {Time_mode::wall_clock};
    int64_t  m_pending_manual_advance_ns         {0};
    int64_t  m_manual_max_step_per_frame_ns      {250'000'000};

    ERHE_PROFILE_MUTEX(std::mutex,         m_mutex);
    std::vector<Time_context>              m_this_frame_fixed_steps;
    std::vector<Transform_animation_entry> m_transform_animations;
    std::vector<int64_t>                   m_frame_start_times;         // ring buffer of recent frame start times
    std::size_t                            m_frame_start_time_index{0}; // oldest entry / next slot to overwrite once full
    float                                  m_frame_time_average_ms{0.0f};
};

}
