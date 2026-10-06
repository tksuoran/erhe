#include "time.hpp"

#include "app_message_bus.hpp"
#include "editor_log.hpp"
#include "scene/node_transform_commit.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace editor {

namespace {

[[nodiscard]] auto wall_now_ns() -> int64_t
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ERHE_FIXED_DT_MS=<milliseconds>: the fixed_dt editor clock from the first
// frame. Not set, empty or not a positive number: the wall clock.
[[nodiscard]] auto read_fixed_dt_environment(int64_t& out_fixed_dt_ns) -> bool
{
    const char* const value = std::getenv("ERHE_FIXED_DT_MS");
    if ((value == nullptr) || (value[0] == '\0')) {
        return false;
    }
    char* end = nullptr;
    const double milliseconds = std::strtod(value, &end);
    if ((end == value) || !std::isfinite(milliseconds) || (milliseconds <= 0.0) || (milliseconds > 1000.0)) {
        log_startup->warn("ERHE_FIXED_DT_MS = '{}' is not a duration in (0, 1000] milliseconds; the editor clock stays on the wall clock", value);
        return false;
    }
    out_fixed_dt_ns = static_cast<int64_t>(std::llround(milliseconds * 1.0e6));
    return true;
}

} // anonymous namespace

auto c_str(const Editor_clock_source source) -> const char*
{
    switch (source) {
        case Editor_clock_source::wall_clock: return "wall_clock";
        case Editor_clock_source::fixed_dt:   return "fixed_dt";
        default:                              return "?";
    }
}

Time::Time()
{
    int64_t fixed_dt_ns = 0;
    if (read_fixed_dt_environment(fixed_dt_ns)) {
        m_fixed_dt_ns              = fixed_dt_ns;
        m_editor_clock_source      = Editor_clock_source::fixed_dt;
        m_next_editor_clock_source = Editor_clock_source::fixed_dt;
        log_startup->info("ERHE_FIXED_DT_MS: editor clock fixed_dt, {} ns per frame", fixed_dt_ns);
    }
}

auto Time::get_editor_time_ns() const -> int64_t
{
    return m_editor_time_ns;
}

auto Time::get_editor_frame_duration_ns() const -> int64_t
{
    return m_editor_frame_duration_ns;
}

auto Time::map_input_timestamp_ns(const int64_t wall_timestamp_ns) const -> int64_t
{
    return (m_editor_clock_source == Editor_clock_source::fixed_dt)
        ? m_editor_time_ns
        : (wall_timestamp_ns + m_wall_to_editor_offset_ns);
}

auto Time::get_current_editor_clock_source() const -> Editor_clock_source
{
    return m_editor_clock_source;
}

void Time::set_editor_clock_source(const Editor_clock_source source, const int64_t fixed_dt_ns)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    m_next_editor_clock_source = source;
    if (fixed_dt_ns > 0) {
        m_fixed_dt_ns = fixed_dt_ns;
    }
}

auto Time::get_pending_editor_clock_source() const -> Editor_clock_source
{
    return m_next_editor_clock_source;
}

auto Time::get_fixed_dt_ns() const -> int64_t
{
    return m_fixed_dt_ns;
}

auto Time::get_frame_time_average_ms() const -> float
{
    return m_frame_time_average_ms;
}

void Time::prepare_update(bool advance_simulation, int64_t simulation_advance_ns)
{
    ERHE_PROFILE_FUNCTION();

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    ++m_frame_number;
    const bool    first_frame                 = (m_wall_last_frame_start_time_ns == 0);
    const int64_t wall_frame_start_time_ns    = wall_now_ns();
    const int64_t wall_frame_duration_ns      = first_frame ? 0 : (wall_frame_start_time_ns - m_wall_last_frame_start_time_ns);

    constexpr std::size_t frame_time_window_size = 200;
    if (!m_frame_start_times.empty()) {
        const std::size_t oldest_index = (m_frame_start_times.size() < frame_time_window_size) ? 0 : m_frame_start_time_index;
        const int64_t average_frametime_duration_ns = wall_frame_start_time_ns - m_frame_start_times[oldest_index];
        const int64_t average_frametime_ns          = average_frametime_duration_ns / static_cast<int64_t>(m_frame_start_times.size());
        const double  average_frametime_ms          = static_cast<double>(average_frametime_ns) / 1'000'000.0;
        m_frame_time_average_ms = static_cast<float>(average_frametime_ms);
    }
    if (m_frame_start_times.size() < frame_time_window_size) {
        m_frame_start_times.reserve(frame_time_window_size); // no-op after the first frame
        m_frame_start_times.push_back(wall_frame_start_time_ns);
    } else {
        m_frame_start_times[m_frame_start_time_index] = wall_frame_start_time_ns;
        m_frame_start_time_index = (m_frame_start_time_index + 1) % frame_time_window_size;
    }
    m_wall_last_frame_start_time_ns = wall_frame_start_time_ns;

    // The editor clock, monotonic across source switches. A switch requested
    // since the last frame takes effect here. In wall_clock mode the clock is
    // the wall clock plus an offset: 0 unless the run switched from fixed_dt,
    // in which case the offset makes this frame advance the clock by the wall
    // frame duration from where fixed_dt left it.
    const Editor_clock_source previous_source = m_editor_clock_source;
    m_editor_clock_source = m_next_editor_clock_source;
    if (m_editor_clock_source == Editor_clock_source::fixed_dt) {
        m_editor_frame_duration_ns = first_frame ? 0 : m_fixed_dt_ns;
        // Started in fixed_dt mode (ERHE_FIXED_DT_MS), the clock starts at 0,
        // so time-derived visuals (the selection highlight pulse) are the
        // same on every run.
        m_editor_time_ns           = first_frame ? 0 : (m_editor_time_ns + m_fixed_dt_ns);
    } else {
        if (!first_frame && (previous_source != Editor_clock_source::wall_clock)) {
            m_wall_to_editor_offset_ns = (m_editor_time_ns + wall_frame_duration_ns) - wall_frame_start_time_ns;
        }
        m_editor_frame_duration_ns = wall_frame_duration_ns;
        m_editor_time_ns           = wall_frame_start_time_ns + m_wall_to_editor_offset_ns;
    }

    int64_t simulation_frame_duration_ns = 0;
    if (m_editor_clock_source == Editor_clock_source::fixed_dt) {
        // Exactly dt of simulation per frame: no dilation cap (the frame
        // takes as long as it takes, the simulation does not notice), and
        // the predicted display delta does not apply.
        simulation_frame_duration_ns = m_editor_frame_duration_ns;
    } else {
        simulation_frame_duration_ns = (simulation_advance_ns >= 0) ? simulation_advance_ns : wall_frame_duration_ns;
        // Cap frame duration to 25ms. This causes time dilation
        if (simulation_frame_duration_ns > 25'000'000) {
            simulation_frame_duration_ns = 25'000'000;
        }
    }

    // When the simulation is paused (e.g. the window is not visible), do not
    // advance simulation time at all. The accumulator stays frozen, the fixed
    // step loop below produces zero steps, and no wall-clock time piles up to
    // be replayed as a catch-up burst when the simulation resumes. The frame
    // number and the editor clock still advance (above), so the first active
    // frame after resume measures a normal small dt.
    if (!advance_simulation) {
        simulation_frame_duration_ns = 0;
    }

    // Manual time control (MCP advance_time): a pending manual advance
    // replaces this frame's simulation delta outright - the wall-clock /
    // display delta, the 25 ms dilation cap and the hidden-window pause
    // above all yield to it (an explicit request must run even when the
    // window is hidden, and exceeding the cap is the point of batch
    // settling). With nothing pending, paused and manual modes freeze
    // simulation time the same way the hidden pause does.
    if (m_pending_manual_advance_ns > 0) {
        const int64_t step = std::min(m_pending_manual_advance_ns, m_manual_max_step_per_frame_ns);
        simulation_frame_duration_ns = step;
        m_pending_manual_advance_ns -= step;
    } else if (m_time_mode != Time_mode::wall_clock) {
        simulation_frame_duration_ns = 0;
    }

    m_simulation_time_accumulator += simulation_frame_duration_ns;
    m_simulation_dt_ns = 1'000'000'000 / 240;
    const float simulation_dt_s = 1.0f / 240.0f;
    m_this_frame_fixed_steps.clear();
    uint64_t subframe = 0;
    while (m_simulation_time_accumulator >= m_simulation_dt_ns) {
        m_this_frame_fixed_steps.push_back(
            Time_context{
                .simulation_dt_s     = simulation_dt_s,
                .simulation_dt_ns    = m_simulation_dt_ns,
                .simulation_time_ns  = m_simulation_time_ns,
                .frame_number        = m_frame_number,
                .subframe            = subframe
            }
        );
        m_simulation_time_accumulator -= m_simulation_dt_ns;
        m_simulation_time_ns += m_simulation_dt_ns;
        ++subframe;
    }
}

void Time::for_each_fixed_step(std::function<void(const Time_context&)> callback)
{
    for (const Time_context& time_context : m_this_frame_fixed_steps) {
        callback(time_context);
    }
}

void Time::set_time_mode(Time_mode mode)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    m_time_mode = mode;
}

auto Time::get_time_mode() const -> Time_mode
{
    return m_time_mode;
}

void Time::request_simulation_advance(int64_t advance_ns, int64_t max_step_per_frame_ns)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    if (advance_ns > 0) {
        m_pending_manual_advance_ns += advance_ns;
    }
    if (max_step_per_frame_ns > 0) {
        m_manual_max_step_per_frame_ns = max_step_per_frame_ns;
    }
}

auto Time::get_pending_simulation_advance_ns() const -> int64_t
{
    return m_pending_manual_advance_ns;
}

auto Time::get_frame_number() const -> uint64_t
{
    return m_frame_number;
}

auto Time::get_simulation_time_ns() const -> int64_t
{
    return m_simulation_time_ns;
}


void Time::begin_transform_animation(
    std::shared_ptr<erhe::scene::Node> node,
    erhe::scene::Trs_transform         parent_from_node_before,
    erhe::scene::Trs_transform         parent_from_node_after,
    const float                        time_duration
)
{
    int64_t time_duration_ns = static_cast<int64_t>(std::round(static_cast<double>(time_duration) * 1e9));
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    m_transform_animations.emplace_back(
        node, parent_from_node_before, parent_from_node_after, time_duration_ns, m_editor_time_ns
    );
}

void Time::finish_all_transform_animations(App_message_bus& app_message_bus)
{
    for (Transform_animation_entry& entry : m_transform_animations) {
        entry.node->set_parent_from_node(entry.parent_from_node_after);
        announce_committed_node_transform(app_message_bus, *entry.node);
    }
}

void Time::update_transform_animations(App_message_bus& app_message_bus)
{
    // Finish completed animations
    m_transform_animations.erase(
        std::remove_if(
            m_transform_animations.begin(),
            m_transform_animations.end(),
            [this, &app_message_bus](const Transform_animation_entry& entry) {
                const int64_t time_position = m_editor_time_ns - entry.start_time_ns;
                if (time_position >= entry.time_duration_ns) {
                    entry.node->set_parent_from_node(entry.parent_from_node_after);
                    // The animated operation's pose is reached: committed.
                    announce_committed_node_transform(app_message_bus, *entry.node);
                    return true;
                }
                return false;
            }
        ),
        m_transform_animations.end()
    );

    for (Transform_animation_entry& entry : m_transform_animations) {
        const int64_t time_position = m_editor_time_ns - entry.start_time_ns;
        ERHE_VERIFY(time_position < entry.time_duration_ns);
        const float t = static_cast<float>(
            static_cast<double>(time_position) / static_cast<double>(entry.time_duration_ns)
        );
        ERHE_VERIFY(t >= 0.0f);
        const float t_ = glm::smoothstep(0.0f, 1.0f, t);
        const erhe::scene::Trs_transform transform = erhe::scene::interpolate(
            entry.parent_from_node_before,
            entry.parent_from_node_after,
            t_
        );
        entry.node->set_parent_from_node(transform);
        // A frame of the animation is a live move, not a commit.
        app_message_bus.node_touched.send_message(Node_touched_message{.node = entry.node.get()});
    }
}


}  // namespace editor

