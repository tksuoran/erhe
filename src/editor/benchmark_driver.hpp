#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

// The head pose a benchmark frame renders from (stage space).
class Benchmark_pose
{
public:
    glm::vec3 position   {0.0f, 0.0f, 0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
};

// One keyframe of a keyframes camera path.
class Benchmark_keyframe
{
public:
    int64_t   frame{0};
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 yaw_pitch_roll_deg{0.0f, 0.0f, 0.0f};
};

enum class Benchmark_camera_path_type : int
{
    orbit     = 0,
    keyframes = 1
};

// The camera path of a benchmark file, evaluated by frame number (never by
// wall time), so two runs render the same poses.
class Benchmark_camera_path
{
public:
    [[nodiscard]] auto evaluate(int64_t frame) const -> Benchmark_pose;

    Benchmark_camera_path_type      type{Benchmark_camera_path_type::orbit};
    // orbit
    glm::vec3                       center       {0.0f, 0.0f, 0.0f};
    float                           radius       {2.0f};
    float                           height       {1.6f};
    int64_t                         period_frames{720};
    glm::vec3                       look_at      {0.0f, 1.0f, 0.0f};
    // keyframes (sorted by frame, frame of the first is 0)
    std::vector<Benchmark_keyframe> keyframes;
};

// Per-frame inputs of Benchmark_driver::on_frame_end(), all from the main
// thread at the end of frame().
class Benchmark_frame_inputs
{
public:
    double full_tick_us        {0.0};
    double m1_us               {0.0};
    double m2_us               {0.0};
    double m3_us               {0.0};
    double r_us                {0.0};  // R joined during this tick
    // Wall clock at the end of frame() (the same point of every frame).
    double wall_clock_s        {0.0};
    // Every frame: the frame could render (OpenXR: waited, should_render and
    // the actions updated; always true without OpenXR), and the requested
    // display refresh rate was refused by the runtime.
    bool   frame_rendered                {true};
    // Non-null and non-empty: the display refresh rate request failed (the
    // reason is logged and the run fails at once).
    const std::string* refresh_rate_failure{nullptr};
    // Only read while needs_settle_inputs(): the caller computes them then.
    bool   scene_settled       {false};
    bool   refresh_rate_settled{false}; // the achieved rate is within 0.5 Hz of the requested one (true when none is requested)
    double display_refresh_hz  {0.0};   // achieved refresh rate (XR runtime, or the window's display)
    int    applied_cpu_performance_level{-2}; // XR_EXT_performance_settings levels applied (-1 unset, -2 never applied / no XR)
    int    applied_gpu_performance_level{-2};
    int    thermal_step_down_count      {0};
};

// The directory the result file is written to: logs/ relative to the
// working directory, or on Android <external files dir>/logs (readable with
// adb without run-as on a release APK).
[[nodiscard]] auto get_benchmark_output_directory() -> std::string;

// Run identity fields the editor fills before the measured window
// (results JSON "identity").
class Benchmark_run_identity
{
public:
    std::string commands_path;
    std::string cpu_performance_level;
    std::string gpu_performance_level;
};

// Timedemo-style benchmark run (doc/plans/quest_performance.md "Benchmark
// mode"). Main thread only. States: warmup (warmup_frames frames, the
// simulation clock frozen, the camera path running from frame 0), settle
// (waits until the scene is settled, bounded by settle_timeout_frames),
// measure (measure_frames frames, the simulation clock advancing exactly
// frame_dt per frame and the camera path restarted at frame 0), then done:
// the result line is logged, logs/benchmark_<name>.json written, and
// is_done() asks the editor to exit. A settle timeout logs
// "Benchmark failed <name>: scene did not settle" (or "display refresh
// rate did not settle") and writes no file.
// Every per-frame container is reserved before the measured window.
class Benchmark_driver
{
public:
    // Loads and validates a benchmark file; logs and returns nullptr on error.
    [[nodiscard]] static auto load(const std::string& path) -> std::unique_ptr<Benchmark_driver>;

    [[nodiscard]] auto get_name                () const -> const std::string&;
    [[nodiscard]] auto get_display_refresh_rate_hz() const -> std::optional<float>;
    // The simulation advance of the frame being started: 0 before the
    // measured window, frame_dt during it.
    [[nodiscard]] auto get_frame_advance_ns    () const -> int64_t;
    // The camera pose of the frame being started.
    [[nodiscard]] auto get_head_pose           () const -> Benchmark_pose;
    [[nodiscard]] auto is_waiting_for_settle   () const -> bool;
    // True while settling and on the last measured frame: the frames whose
    // settle inputs and display refresh rate the driver reads.
    [[nodiscard]] auto needs_settle_inputs     () const -> bool;
    [[nodiscard]] auto is_measuring            () const -> bool;
    [[nodiscard]] auto is_done                 () const -> bool;

    void set_run_identity     (const Benchmark_run_identity& identity);
    // M1: the XR predicted display period of the frame being started.
    void set_xr_display_period(uint64_t predicted_display_period_ns);
    // M2: one XR_META_performance_metrics counter value of this frame. The
    // counter list is fixed when the measured window starts; later indices
    // are ignored.
    void set_counter          (std::size_t index, std::string_view name, double value);
    // End of frame(): advances the state and records the measured frame.
    void on_frame_end         (const Benchmark_frame_inputs& inputs);

private:
    enum class State : int { warmup, settle, measure, done };

    class Counter_series
    {
    public:
        std::string         name;
        std::vector<double> values;
        double              pending{0.0};
    };

    void begin_measure(const Benchmark_frame_inputs& inputs);
    void fail         (const std::string& reason);
    void finish       (const Benchmark_frame_inputs& inputs);
    void hash_bytes   (const void* data, std::size_t size);

    std::string             m_path;
    std::string             m_name;
    uint64_t                m_file_hash            {0};
    uint64_t                m_commands_file_hash   {0};  // FNV-1a of the startup commands (or --scene) file, 0 when unreadable
    int64_t                 m_warmup_frames        {120};
    int64_t                 m_measure_frames       {600};
    int64_t                 m_settle_timeout_frames{1800};
    int64_t                 m_frame_dt_ns          {13'888'889};
    std::optional<float>    m_display_refresh_rate_hz;
    Benchmark_camera_path   m_camera_path;
    Benchmark_run_identity  m_identity;

    State                   m_state          {State::warmup};
    int64_t                 m_state_frame    {0};  // frames ended in the current state
    int64_t                 m_settle_frames  {0};  // frames the settle state took
    int64_t                 m_path_frame     {0};  // camera path frame of the frame being started
    uint64_t                m_input_hash     {0};
    double                  m_previous_wall_clock_s{0.0};
    double                  m_measure_begin_wall_clock_s{0.0};
    double                  m_settle_begin_wall_clock_s{0.0};
    uint64_t                m_xr_display_period_ns{0};
    double                  m_display_refresh_hz{0.0};     // at the start of the measured window
    double                  m_display_refresh_hz_end{0.0}; // at its end
    int                     m_thermal_status_begin{-1};
    int                     m_applied_cpu_level_begin{-2};
    int                     m_applied_gpu_level_begin{-2};
    int                     m_thermal_step_downs_begin{0};

    std::vector<double>         m_wall_us;
    std::vector<double>         m_full_tick_us;
    std::vector<double>         m_m1_us;
    std::vector<double>         m_m2_us;
    std::vector<double>         m_m3_us;
    std::vector<double>         m_r_us;
    std::vector<Counter_series> m_counters;
};

} // namespace editor
