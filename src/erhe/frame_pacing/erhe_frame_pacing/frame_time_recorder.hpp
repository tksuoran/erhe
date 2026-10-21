#pragma once

// Frame time record ring (implementation plan step P0.2).
//
// Holds the per-frame event timestamps required by doc/frame_pacing/requirements.md
// (Data requirements), ring-indexed by frame id. This is the single data
// path shared by the frame pacer's estimators, profiling UI, and analysis
// tools (doc/frame_pacing/inputs.md section 4).
//
// All timestamps are seconds in one monotonic clock domain (QPC on
// Windows, mach_absolute_time on Apple - the clock of CACurrentMediaTime and
// Metal's host times); now() below provides that clock. Zero means "not
// recorded".
//
// Thread-safe: the editor's main thread begins a frame's record and runs the
// pacer while its render thread stamps the previous frame (submit, present,
// CPU slot end), so every access takes one mutex. Writers name the field by
// member pointer; readers get copies. No reference to a record escapes the
// lock.

#include <chrono>
#include <cstdint>
#include <mutex>
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
    double present_wait_begin   {0.0}; // FR5 present-wait clamp starts (after the frame start, before the swapchain wait)
    double present_wait_end     {0.0}; // FR5 present-wait clamp done
    double join_wait_begin      {0.0}; // the frame's main thread starts waiting for the previous frame's render phase
    double join_wait_end        {0.0}; // that wait done
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
    // GPU idle inside the frame span: the gap between the frame's first
    // submit and its presenting submit (Vulkan: the render thread records
    // the frame command buffer after the upload command buffer was
    // submitted); 0 = none or not measured. gpu_service_time() excludes it.
    double gpu_idle_time        {0.0};
    double gpu_clock_ratio      {0.0}; // the GPU's average active clock over the frame / its maximum clock; 0 = not measured (Metal: IOReport)
    double cpu_clock_ratio      {0.0}; // the P-cores' average active clock over the frame / their maximum clock; 0 = not measured
    double achieved_present_time{0.0}; // presentation feedback, when available
    // Presentation feedback at the QUEUE_OPERATIONS_END stage (when the
    // present operation executed on the queue), when the surface supports
    // querying it; 0 otherwise. Diagnostic: separates queue-side stalls
    // from scanout-side slips (achieved_present_time is the scanout-near
    // stage).
    double achieved_queue_ops_time{0.0};

    // Normative c_k for the pacer (doc/frame_pacing/inputs.md section 3.2):
    // CPU slot span minus the involuntary waits recorded inside it. The
    // pacer wait, the present wait and the join wait are subtracted for the portion
    // overlapping the slot.
    [[nodiscard]] auto cpu_service_time() const -> double;

    // Normative g_k for the pacer (doc/frame_pacing/inputs.md section 3.3):
    // the GPU frame span minus the recorded idle gap.
    [[nodiscard]] auto gpu_service_time() const -> double;

    // Whole-slot span, for profiling display.
    [[nodiscard]] auto cpu_slot_span() const -> double;

    // The involuntary waits on the frame's path from its release to its
    // submit (doc/frame_pacing/inputs.md section 3.3, claim C28): the FR5
    // present-wait clamp, the join wait, the swapchain acquire and the fence
    // waits. They measure how early the frame was released, so the chain
    // latency L_k excludes them. The present-request holdback and blocking
    // inside the present call come after the submit and are not counted.
    [[nodiscard]] auto release_wait_time(double release) const -> double;
};

class Frame_time_recorder
{
public:
    explicit Frame_time_recorder(std::size_t capacity = 120);

    // Monotonic reference-clock timestamp in seconds (QPC-backed on Windows).
    [[nodiscard]] static auto now() -> double;

    // Opens (or re-opens) the record for frame_id, clearing stale content,
    // and stamps its CPU slot begin.
    void begin_frame(std::int64_t frame_id, double cpu_slot_begin);

    // True when the record for frame_id is open (begun and not evicted).
    [[nodiscard]] auto contains(std::int64_t frame_id) const -> bool;

    // Field writers. Each is a no-op when the record for frame_id is not
    // open. set_once() writes only a field that is still 0; add()
    // accumulates. The two-field set() writes both under one lock, so a
    // reader never sees one half of a (begin, end) pair.
    void set     (std::int64_t frame_id, double Frame_time_record::* field, double value);
    void set     (std::int64_t frame_id, double Frame_time_record::* field_a, double value_a, double Frame_time_record::* field_b, double value_b);
    void set_once(std::int64_t frame_id, double Frame_time_record::* field, double value);
    void add     (std::int64_t frame_id, double Frame_time_record::* field, double value);

    // Copies the record for frame_id into out_record; false (out_record
    // untouched) when the record is not open.
    [[nodiscard]] auto get(std::int64_t frame_id, Frame_time_record& out_record) const -> bool;

    [[nodiscard]] auto capacity() const -> std::size_t { return m_records.size(); }

    // Highest frame id passed to begin_frame(); -1 before the first frame.
    [[nodiscard]] auto get_latest_frame_id() const -> std::int64_t;

    // Display refresh duration (seconds) from the swapchain timing query; 0
    // while unknown. Kept here, under the same lock, because the swapchain
    // that sets it may be recreated on the render thread while the pacer
    // on the main thread reads it.
    void               set_display_refresh_duration(double seconds);
    [[nodiscard]] auto get_display_refresh_duration() const -> double;

private:
    [[nodiscard]] auto find_locked(std::int64_t frame_id) -> Frame_time_record*;

    mutable std::mutex             m_mutex;
    std::vector<Frame_time_record> m_records;        // sized once in the constructor
    std::int64_t                   m_latest_frame_id{-1};
    double                         m_display_refresh_duration{0.0};
};

} // namespace erhe::frame_pacing
