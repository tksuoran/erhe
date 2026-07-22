#pragma once

// Frame time record ring (implementation plan step P0.2).
//
// Holds the per-frame event timestamps required by doc/frame_pacing.md
// (Data requirements), ring-indexed by frame id. This is the single data
// path shared by the frame pacer's estimators, profiling UI, and analysis
// tools (doc/frame_pacing_inputs.md section 4).
//
// All timestamps are seconds in one monotonic clock domain (QPC on
// Windows); now() below provides that clock. Zero means "not recorded".
// The recorder is intended to be driven from the render/main thread; it is
// not thread-safe.

#include <chrono>
#include <cstdint>
#include <vector>

namespace erhe::frame_pacing {

class Frame_time_record
{
public:
    std::int64_t frame_id{-1};

    double cpu_slot_begin       {0.0}; // work on the frame in flight starts
    double cpu_slot_end         {0.0}; // work on the frame in flight ends
    double pacer_wait_begin     {0.0}; // pacer starts waiting for release time
    double pacer_wait_end       {0.0}; // wait done, app starts
    double fence_wait_duration  {0.0}; // accumulated vkWaitForFences inside the slot
    double acquire_begin        {0.0}; // just before vkAcquireNextImageKHR
    double acquire_end          {0.0}; // right after vkAcquireNextImageKHR
    double submit_time          {0.0}; // vkQueueSubmit call
    double present_holdback_begin{0.0}; // present-request holdback wait starts (C15 mitigation)
    double present_holdback_end  {0.0}; // holdback wait done, request follows
    double present_request_time {0.0}; // just before vkQueuePresentKHR
    double present_return_time  {0.0}; // right after vkQueuePresentKHR (FIFO backpressure can block inside)
    double gpu_frame_begin      {0.0}; // first render pass begin (calibrated)
    double gpu_frame_end        {0.0}; // last render pass end (calibrated)
    double achieved_present_time{0.0}; // presentation feedback, when available
    // Presentation feedback at the QUEUE_OPERATIONS_END stage (when the
    // present operation executed on the queue), when the surface supports
    // querying it; 0 otherwise. Diagnostic: separates queue-side stalls
    // from scanout-side slips (achieved_present_time is the scanout-near
    // stage).
    double achieved_queue_ops_time{0.0};

    // Normative c_k for the pacer (doc/frame_pacing_inputs.md section 3.2):
    // CPU slot span minus the involuntary waits recorded inside it. The
    // pacer wait is expected to precede the slot and is subtracted only for
    // the portion overlapping the slot.
    [[nodiscard]] auto cpu_service_time() const -> double;

    // Whole-slot span, for profiling display.
    [[nodiscard]] auto cpu_slot_span() const -> double;
};

class Frame_time_recorder
{
public:
    explicit Frame_time_recorder(std::size_t capacity = 120);

    // Monotonic reference-clock timestamp in seconds (QPC-backed on Windows).
    [[nodiscard]] static auto now() -> double;

    // Opens (or re-opens) the record for frame_id, clearing stale content.
    auto begin_frame(std::int64_t frame_id) -> Frame_time_record&;

    // Record for frame_id, or nullptr when never recorded / evicted.
    [[nodiscard]] auto find(std::int64_t frame_id) -> Frame_time_record*;
    [[nodiscard]] auto find(std::int64_t frame_id) const -> const Frame_time_record*;

    [[nodiscard]] auto capacity() const -> std::size_t { return m_records.size(); }

    // Highest frame id passed to begin_frame(); -1 before the first frame.
    [[nodiscard]] auto get_latest_frame_id() const -> std::int64_t { return m_latest_frame_id; }

private:
    std::vector<Frame_time_record> m_records;
    std::int64_t                   m_latest_frame_id{-1};
};

} // namespace erhe::frame_pacing
