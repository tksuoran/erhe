#pragma once

// Display refresh period resolution for frame pacing.
//
// The frame pacer needs the vblank period of the display the window is on.
// No single source is trustworthy on its own:
//
//   - The window's current display mode (SDL) knows which display the window
//     is on, but reports the mode's nominal rate.
//   - VK_EXT_present_timing's swapchain timing properties (refreshDuration,
//     refreshInterval) describe "the presentation engine", which the
//     specification does not tie to the window's display: with two displays
//     the value may be the other display's (measured 2026-10-18: a window on
//     a 120 Hz display got the 240 Hz builtin panel's 4.17 ms). They also
//     carry the fixed / variable refresh indication and change at run time,
//     signalled by the timing properties counter.
//   - The init calibration measures the display itself, trusting no query:
//     under plain FIFO with a bounded image count the frame's GPU work waits
//     for the swapchain image, which the display releases once per vblank,
//     so once the queue is full consecutive GPU frame end timestamps are one
//     refresh period apart (the "locked" phase). To prove the display and
//     not the application set that cadence, a "free run" phase first presents
//     without backpressure (IMMEDIATE or MAILBOX) and measures the production
//     interval; the locked period is accepted only when production took at
//     most half of it. A queried value the calibration contradicts is
//     rejected.
//
// The resolver keeps every source, logs each one and the decision, and hands
// the resolved period to the apply callback whenever it changes (the frame
// time recorder, from which the pacing observer reads it and recreates its
// pacer on a change). Thread-safe: the window source arrives on the main
// thread (display change events), the Vulkan source and the present
// bookkeeping on the thread that presents, the GPU frame ends on the thread
// that harvests the frame brackets.

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace spdlog { class logger; }

namespace erhe::graphics {

enum class Display_refresh_source : unsigned int {
    none = 0,
    window_mode,    // the window's display mode (SDL)
    vulkan_timing,  // VK_EXT_present_timing swapchain timing properties
    calibration     // vblank cadence measured at init
};

// What VK_EXT_present_timing says about the refresh cycle.
enum class Display_refresh_mode : unsigned int {
    unknown = 0,  // no query, or refreshInterval 0
    fixed,        // refreshInterval == refreshDuration
    variable,     // refreshInterval == UINT64_MAX; refreshDuration is the minimum
    decoupled     // refreshInterval < refreshDuration: images visible for a multiple of the vblank interval
};

// The init calibration's phases; the swapchain presents in the present mode
// the phase asks for (Display_refresh_resolver::get_calibration_phase).
enum class Display_refresh_calibration_phase : unsigned int {
    idle = 0,  // not calibrating: the swapchain uses its tier's present mode
    free_run,  // present without backpressure (IMMEDIATE / MAILBOX): production interval
    locked     // present in plain FIFO: vblank-locked GPU frame ends
};

// How a present was made, for attributing its frame's GPU end timestamp.
enum class Display_refresh_present_kind : unsigned int {
    other = 0,      // the tier's own mode, or a mode of no use to the calibration
    free_running,   // IMMEDIATE / MAILBOX: no backpressure
    fifo            // plain FIFO: display backpressure
};

[[nodiscard]] auto c_str(Display_refresh_source            value) -> const char*;
[[nodiscard]] auto c_str(Display_refresh_mode              value) -> const char*;
[[nodiscard]] auto c_str(Display_refresh_calibration_phase value) -> const char*;

class Display_refresh_report
{
public:
    double                            window_period_seconds          {0.0}; // 0 = unknown
    double                            vulkan_refresh_duration_seconds{0.0}; // 0 = unknown
    double                            vulkan_refresh_interval_seconds{0.0}; // 0 = unknown or variable (see vulkan_mode)
    Display_refresh_mode              vulkan_mode                    {Display_refresh_mode::unknown};
    Display_refresh_calibration_phase calibration_phase              {Display_refresh_calibration_phase::idle};
    bool                              calibration_done               {false};
    bool                              calibration_verified           {false}; // locked cadence proven to be the display's
    double                            free_run_period_seconds        {0.0};   // production interval; 0 = not measured
    std::size_t                       free_run_delta_count           {0};
    double                            locked_period_seconds          {0.0};   // vblank-locked cadence; 0 = not measured
    std::size_t                       locked_delta_count             {0};     // deltas after the settle
    std::size_t                       locked_cluster_count           {0};     // of them in the densest cluster
    double                            resolved_period_seconds        {0.0};   // 0 = none yet
    Display_refresh_source            resolved_source                {Display_refresh_source::none};
};

class Display_refresh_resolver
{
public:
    using Apply_callback = std::function<void(double period_seconds)>;

    explicit Display_refresh_resolver(std::shared_ptr<spdlog::logger> logger);

    void set_apply_callback(Apply_callback callback);

    // The window's display mode period (0 = unknown). A change of value
    // after the init calibration drops its result: the window moved to
    // another display, and the cadence measured described the old one.
    void set_window_period(double period_seconds, const char* display_name);

    // VK_EXT_present_timing swapchain timing properties, at swapchain
    // creation and whenever the timing properties counter changes.
    void set_vulkan_timing(uint64_t refresh_duration_ns, uint64_t refresh_interval_ns);

    // Init calibration, driven by the swapchain:
    //  - set_calibration_modes: which present modes the calibration
    //    swapchain can present in. Without a free-running mode the
    //    production interval stays unmeasured and the result unverified;
    //    without plain FIFO the calibration ends at once.
    //  - get_calibration_phase: the mode the next present should use.
    //  - note_present: a present was made, in the given kind of mode.
    //  - add_gpu_frame_end: a frame's GPU bracket end (reference clock
    //    seconds), from the bracket harvest; attributed to the frame's
    //    present kind.
    void set_calibration_modes(bool can_free_run, bool can_lock);
    // Ends a running calibration with what it has (an unverified result
    // unless the phases completed): the calibration swapchain failed or hit
    // its time bound.
    void abort_calibration(const char* reason);
    [[nodiscard]] auto get_calibration_phase() const -> Display_refresh_calibration_phase;
    void note_present(std::int64_t frame_id, Display_refresh_present_kind kind);
    void add_gpu_frame_end(std::int64_t frame_id, double gpu_end_seconds);
    [[nodiscard]] auto is_calibrating() const -> bool;

    [[nodiscard]] auto get_report() const -> Display_refresh_report;

    // Decision parameters, exposed for the unit test.
    static constexpr double      s_agreement_fraction    = 0.02;  // two queried periods agree within this fraction
    static constexpr double      s_calibration_tolerance = 0.05;  // the locked cadence confirms a queried period within this fraction
    static constexpr std::size_t s_free_run_skip         = 2;     // first free-run deltas dropped (mode switch in flight)
    static constexpr std::size_t s_free_run_deltas       = 16;    // production deltas that end the free run
    static constexpr std::size_t s_locked_settle         = 8;     // first FIFO deltas dropped (the queue fills)
    static constexpr std::size_t s_locked_deltas         = 32;    // vblank deltas that end the locked phase
    static constexpr double      s_cluster_fraction      = 0.125; // a delta belongs to a cluster within this fraction of its center
    static constexpr double      s_min_cluster_share     = 0.75;  // the densest cluster must hold this share of the locked deltas
    static constexpr double      s_production_fraction   = 0.5;   // production at most this fraction of the locked period proves the display set it
    static constexpr std::size_t s_max_presents          = 240;   // presents after which the calibration ends regardless
    static constexpr std::size_t s_present_ring          = 256;   // presents remembered for attributing GPU frame ends

private:
    class Delta_series
    {
    public:
        bool                has_last{false};
        double              last_time{0.0};
        std::int64_t        last_frame_id{-1};
        std::vector<double> deltas; // consecutive-frame GPU end deltas, in arrival order
        void clear();
    };

    class Present_entry
    {
    public:
        std::int64_t                 frame_id{-1};
        Display_refresh_present_kind kind    {Display_refresh_present_kind::other};
    };

    void               restart_calibration_locked();
    void               finish_calibration_locked();
    void               resolve_locked();
    void               log_sources_locked(const char* reason) const;
    [[nodiscard]] auto vulkan_vblank_period_locked() const -> double;
    [[nodiscard]] static auto agree   (double a, double b) -> bool;
    [[nodiscard]] static auto confirms(double measured, double queried) -> bool;
    [[nodiscard]] static auto median  (std::vector<double> values) -> double;
    // The densest cluster of the values: its median and its size.
    static void densest_cluster(std::vector<double> values, double& out_median, std::size_t& out_count);

    std::shared_ptr<spdlog::logger> m_logger;
    mutable std::mutex              m_mutex;
    Apply_callback                  m_apply_callback;

    double      m_window_period{0.0};
    std::string m_window_display_name;
    uint64_t    m_vulkan_refresh_duration_ns{0};
    uint64_t    m_vulkan_refresh_interval_ns{0};
    bool        m_vulkan_known{false};

    Display_refresh_calibration_phase m_phase          {Display_refresh_calibration_phase::idle};
    bool                              m_calibrating    {true};  // until finish_calibration_locked
    bool                              m_modes_known    {false}; // set_calibration_modes called
    bool                              m_can_free_run   {false};
    bool                              m_can_lock       {false};
    std::size_t                       m_presents_seen  {0};
    std::vector<Present_entry>        m_present_ring;
    Delta_series                      m_free_run_series;
    Delta_series                      m_locked_series;
    bool                              m_calibration_verified{false};
    double                            m_free_run_period     {0.0};
    double                            m_locked_period       {0.0};
    std::size_t                       m_locked_cluster_count{0};
    std::size_t                       m_locked_delta_count  {0};

    double                 m_resolved_period{0.0};
    Display_refresh_source m_resolved_source{Display_refresh_source::none};
};

} // namespace erhe::graphics
