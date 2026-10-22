#pragma once

// Frame pacing observer mode (implementation plan step P2.1).
//
// Drives a Frame_pacer with the real engine inputs from the frame time
// records - service times, calibrated GPU spans, achieved present times -
// and enforces nothing ITSELF: no release gating, no target present times,
// no waits. The host may enforce parts of the exposed schedule decision as
// actuation steps land (P2.2: the present-wait clamp via
// get_last_decision().wait_id). The observer's purpose is to validate
// input fidelity and pacer behavior against reality before actuation
// (doc/frame_pacing/implementation_plan.md P2.1):
//
// - believed misses should correlate with observed presentation slips,
// - the throughput statistic W should track real load,
// - cadence must not churn on steady scenes,
// - FR4 predictions should track achieved present times.
//
// The observer emits human/AI-readable log lines (cadence changes and a
// periodic summary) through a queue the host drains - the library itself
// stays free of logging dependencies.

#include "erhe_frame_pacing/frame_pacer.hpp"
#include "erhe_frame_pacing/frame_time_recorder.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>

namespace erhe::frame_pacing {

// The pacer's deadline offset as the host configures it.
class Deadline_offset_setup
{
public:
    Deadline_offset_source source{Deadline_offset_source::fixed};
    Present_queue_kind     present_queue{Present_queue_kind::dropping};
    // A deadline the host measured (present calibration), for the display
    // whose refresh period is calibrated_period: the initial offset and
    // floor, with the estimate ceiling at maximum. Used only while the
    // refresh period matches (a display change invalidates it); 0.0: none.
    double calibrated_period{0.0};
    double initial          {0.0};
    double maximum          {0.0};
    // GPU pre-warm before an upshift (claim C26): the fill the pacer starts
    // learning from; 0.0 = the host has no pre-warm.
    double upshift_prewarm  {0.0};

    [[nodiscard]] auto operator==(const Deadline_offset_setup& other) const -> bool = default;
};

class Frame_pacing_observer
{
public:
    // Call once per frame after the engine has opened the frame's time
    // record (Device::wait_frame). refresh_period is the display refresh in
    // seconds from the swapchain timing query; pass 0.0 while unknown -
    // the observer stays dormant until a refresh period arrives.
    void tick(Frame_time_recorder& recorder, double now, double refresh_period);

    // Drains one pending log line; returns false when none are queued.
    [[nodiscard]] auto consume_log_line(std::string& out_line) -> bool;

    [[nodiscard]] auto get_pacer() const -> const Frame_pacer* { return m_pacer.get(); }

    // Schedule decision from the most recent tick(). Default-constructed
    // (wait_id < 0, so no clamp) until the pacer has run. The host enforces
    // wait_id (FR5 present-wait clamp, step P2.2); everything else remains
    // observe-only until P2.3/P2.4.
    [[nodiscard]] auto get_last_decision() const -> const Schedule_decision& { return m_last_decision; }

    // Application-commanded cadence floor (FR1 set_min_vsyncs, behavior doc
    // scenario 9). May be called from any thread; the request is applied to
    // the pacer at the next tick() and survives pacer recreation.
    void               request_min_vsyncs(int min_vsyncs);

    // Where the pacer's deadline offset comes from (Frame_pacer_tunables);
    // the host knows its presentation path. Main thread; a change recreates
    // the pacer at the next tick().
    void set_deadline_offset(const Deadline_offset_setup& setup);

    // The display the host presents to (claim C30): variable_refresh for an
    // Adaptive-Sync display driven directly, with its maximum refresh
    // interval in seconds (> 0) and its update granularity in seconds (0 =
    // continuous; claim C32); the refresh period passed to tick() is then
    // its minimum refresh interval. fixed_grid ignores both.
    // Main thread, on a display or mode change (not per frame); a change
    // recreates the pacer at the next tick(), like a refresh period change.
    void set_display(Present_display_kind display, double max_refresh_interval, double refresh_granularity);
    [[nodiscard]] auto get_min_vsyncs   () const -> int { return m_min_vsyncs_request.load(std::memory_order_relaxed); }

private:
    void deliver_pending(Frame_time_recorder& recorder, std::int64_t current_frame_id);
    void emit_summary   (std::int64_t current_frame_id);

    // Per-frame delivery bookkeeping, ring-indexed by frame id.
    class Delivery
    {
    public:
        std::int64_t frame_id         {-1};
        bool         cpu_delivered    {false};
        bool         gpu_delivered    {false};
        bool         feedback_delivered{false};
        double       predicted_display{0.0};
    };

    static constexpr std::size_t  s_delivery_ring = 64;
    static constexpr std::int64_t s_scan_window   = 48; // < ring size

    std::unique_ptr<Frame_pacer> m_pacer;
    double                       m_refresh_period{0.0};
    Deadline_offset_setup        m_deadline_offset_setup{};
    Deadline_offset_setup        m_pacer_deadline_offset_setup{};
    Present_display_kind         m_display                    {Present_display_kind::fixed_grid};
    double                       m_max_refresh_interval       {0.0};
    double                       m_refresh_granularity        {0.0};
    Present_display_kind         m_pacer_display              {Present_display_kind::fixed_grid};
    double                       m_pacer_max_refresh_interval {0.0};
    double                       m_pacer_refresh_granularity  {0.0};
    Schedule_decision            m_last_decision{};
    std::atomic<int>             m_min_vsyncs_request{1};
    int                          m_min_vsyncs_applied{1};
    std::array<Delivery, s_delivery_ring> m_delivery{};
    std::deque<std::string>      m_log_lines;

    // Accumulators between summaries.
    std::int64_t m_last_summary_frame {-1};
    // Present-wait clamp spans (P2.2), read back from the records: how long
    // the host actually blocked, and on how many frames the clamp bound.
    double       m_pacer_wait_sum     {0.0};
    double       m_pacer_wait_max     {0.0};
    int          m_pacer_wait_count   {0};
    int          m_pacer_wait_blocked_count{0};
    int          m_miss_count         {0};
    int          m_near_miss_count    {0};
    int          m_feedback_count     {0};
    int          m_gpu_sample_count   {0};
    double       m_prediction_error_sum{0.0};
    double       m_prediction_error_max{0.0};
    int          m_prediction_error_count{0};
    double       m_last_w_statistic   {0.0};
    int          m_previous_vsyncs    {1};
};

} // namespace erhe::frame_pacing
