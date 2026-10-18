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
//   - The displayed times of the first presents (presentation feedback, else
//     vkWaitForPresentKHR completions, else vkQueuePresentKHR returns under
//     plain-FIFO backpressure) show the cadence the display actually ran:
//     the init calibration below. It trusts no other source: a candidate it
//     does not confirm is rejected.
//
// The resolver keeps every source, logs each one and the decision, and hands
// the resolved period to the apply callback whenever it changes (the frame
// time recorder, from which the pacing observer reads it and recreates its
// pacer on a change). Thread-safe: the window source arrives on the main
// thread (display change events), the Vulkan source and the calibration
// samples on the thread that presents.

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
    calibration     // displayed-time cadence measured at init
};

// What VK_EXT_present_timing says about the refresh cycle.
enum class Display_refresh_mode : unsigned int {
    unknown = 0,  // no query, or refreshInterval 0
    fixed,        // refreshInterval == refreshDuration
    variable,     // refreshInterval == UINT64_MAX; refreshDuration is the minimum
    decoupled     // refreshInterval < refreshDuration: images visible for a multiple of the vblank interval
};

enum class Display_time_sample_source : unsigned int {
    none = 0,
    present_feedback,  // VK_EXT_present_timing past presentation timing, scanout-near stage
    present_wait,      // vkWaitForPresentKHR completion
    present_return     // vkQueuePresentKHR return under plain-FIFO backpressure
};

[[nodiscard]] auto c_str(Display_refresh_source     value) -> const char*;
[[nodiscard]] auto c_str(Display_refresh_mode       value) -> const char*;
[[nodiscard]] auto c_str(Display_time_sample_source value) -> const char*;

class Display_refresh_report
{
public:
    double                     window_period_seconds          {0.0}; // 0 = unknown
    double                     vulkan_refresh_duration_seconds{0.0}; // 0 = unknown
    double                     vulkan_refresh_interval_seconds{0.0}; // 0 = unknown or variable (see vulkan_mode)
    Display_refresh_mode       vulkan_mode                    {Display_refresh_mode::unknown};
    bool                       calibration_done               {false};
    Display_time_sample_source calibration_source             {Display_time_sample_source::none};
    double                     calibration_period_seconds     {0.0}; // 0 = no estimate
    std::size_t                calibration_sample_count       {0};   // accepted deltas behind the estimate
    std::size_t                calibration_rejected_count     {0};   // deltas of frames too slow to prove the cadence
    double                     resolved_period_seconds        {0.0}; // 0 = none yet
    Display_refresh_source     resolved_source                {Display_refresh_source::none};
};

class Display_refresh_resolver
{
public:
    using Apply_callback       = std::function<void(double period_seconds)>;
    // Returns the frame's own work time in seconds (the longer of its CPU
    // service time and its GPU span), 0 when unknown.
    using Frame_work_lookup    = std::function<double(std::int64_t frame_id)>;

    explicit Display_refresh_resolver(std::shared_ptr<spdlog::logger> logger);

    void set_apply_callback   (Apply_callback callback);
    void set_frame_work_lookup(Frame_work_lookup lookup);

    // The window's display mode period (0 = unknown). A change of value
    // restarts the init calibration: the window moved to another display.
    void set_window_period(double period_seconds, const char* display_name);

    // VK_EXT_present_timing swapchain timing properties, at swapchain
    // creation and whenever the timing properties counter changes.
    void set_vulkan_timing(uint64_t refresh_duration_ns, uint64_t refresh_interval_ns);

    // Init calibration. Samples are the times (reference clock seconds) at
    // which consecutive presents were displayed, from the best source the
    // platform offers; the calibration uses one source (the best that
    // delivers) and ends after enough accepted deltas or after
    // s_max_presents presents, whichever comes first.
    [[nodiscard]] auto is_calibrating() const -> bool;
    [[nodiscard]] auto wants_sample(Display_time_sample_source source) const -> bool;
    void add_display_time_sample(Display_time_sample_source source, std::int64_t frame_id, double time_seconds);
    void note_present(); // one present happened (bounds the calibration)

    [[nodiscard]] auto get_report() const -> Display_refresh_report;

    // Decision parameters, exposed for the unit test.
    static constexpr double      s_agreement_fraction  = 0.02; // two queried periods agree within this fraction
    static constexpr double      s_calibration_tolerance = 0.10; // a measured cadence confirms a queried period within this fraction (median jitter; real display mismatches are 1.2x and more)
    static constexpr std::size_t s_required_deltas     = 32;   // consecutive-present deltas that end the calibration
    static constexpr std::size_t s_min_cluster         = 8;    // accepted deltas in the densest cluster for an estimate
    static constexpr double      s_cluster_fraction    = 0.125; // a delta belongs to a cluster within this fraction of its center
    static constexpr std::size_t s_max_presents        = 120;  // presents after which the calibration ends regardless
    static constexpr double      s_fast_frame_fraction = 0.5;  // frame work below this fraction of its delta proves the cadence

private:
    class Delta_sample
    {
    public:
        double       delta   {0.0};
        std::int64_t frame_id{-1}; // the later of the two presents
    };

    class Sample_series
    {
    public:
        bool                      has_last{false};
        double                    last_time{0.0};
        std::int64_t              last_frame_id{-1};
        std::vector<Delta_sample> deltas;          // consecutive-present deltas, gated at the end
        void clear();
    };

    class Estimate
    {
    public:
        double      period  {0.0}; // 0 = none
        std::size_t accepted{0};   // deltas behind it (fast frames in the densest cluster)
        std::size_t rejected{0};   // deltas of frames too slow, or of unknown work
    };

    void               resolve_locked();
    void               finish_calibration_locked();
    void               log_sources_locked(const char* reason) const;
    // Gates the series' deltas on the frames' work (the frame bracket of
    // every sampled frame is readable by the time the calibration ends),
    // then takes the median of the densest cluster of the accepted deltas.
    [[nodiscard]] auto estimate_period_locked(const Sample_series& series) const -> Estimate;
    [[nodiscard]] auto vulkan_vblank_period_locked() const -> double;
    [[nodiscard]] static auto agree(double a, double b) -> bool;
    [[nodiscard]] static auto confirms(double measured, double queried) -> bool;
    [[nodiscard]] auto series_for(Display_time_sample_source source) -> Sample_series&;
    [[nodiscard]] auto series_for(Display_time_sample_source source) const -> const Sample_series&;

    std::shared_ptr<spdlog::logger> m_logger;
    mutable std::mutex              m_mutex;
    Apply_callback                  m_apply_callback;
    Frame_work_lookup               m_frame_work_lookup;

    double                     m_window_period          {0.0};
    std::string                m_window_display_name;
    uint64_t                   m_vulkan_refresh_duration_ns{0};
    uint64_t                   m_vulkan_refresh_interval_ns{0};
    bool                       m_vulkan_known           {false};

    bool                       m_calibrating            {true};
    std::size_t                m_presents_seen          {0};
    Sample_series              m_feedback_series;
    Sample_series              m_present_wait_series;
    Sample_series              m_present_return_series;
    Display_time_sample_source m_calibration_source     {Display_time_sample_source::none};
    double                     m_calibration_period     {0.0};
    std::size_t                m_calibration_samples    {0};
    std::size_t                m_calibration_rejected   {0};

    double                     m_resolved_period        {0.0};
    Display_refresh_source     m_resolved_source        {Display_refresh_source::none};
};

} // namespace erhe::graphics
