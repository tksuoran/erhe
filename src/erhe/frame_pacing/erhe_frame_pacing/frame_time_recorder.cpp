#include "erhe_frame_pacing/frame_time_recorder.hpp"

#include <algorithm>

namespace erhe::frame_pacing {

auto Frame_time_record::cpu_slot_span() const -> double
{
    if ((cpu_slot_begin <= 0.0) || (cpu_slot_end <= cpu_slot_begin)) {
        return 0.0;
    }
    return cpu_slot_end - cpu_slot_begin;
}

auto Frame_time_record::cpu_service_time() const -> double
{
    const double span = cpu_slot_span();
    if (span <= 0.0) {
        return 0.0;
    }
    double waits = fence_wait_duration;
    if (acquire_end > acquire_begin) {
        waits += acquire_end - acquire_begin;
    }
    if (present_return_time > present_request_time) {
        // FIFO backpressure blocking inside vkQueuePresentKHR is an
        // involuntary wait, not CPU service time.
        waits += present_return_time - present_request_time;
    }
    if (present_holdback_end > present_holdback_begin) {
        // The C15 present-request holdback is a pacing wait, not service.
        waits += present_holdback_end - present_holdback_begin;
    }
    if (pacer_wait_end > pacer_wait_begin) {
        // Only the portion of the pacer wait overlapping the CPU slot counts.
        const double overlap_begin = std::max(pacer_wait_begin, cpu_slot_begin);
        const double overlap_end   = std::min(pacer_wait_end, cpu_slot_end);
        if (overlap_end > overlap_begin) {
            waits += overlap_end - overlap_begin;
        }
    }
    return std::max(0.0, span - waits);
}

Frame_time_recorder::Frame_time_recorder(const std::size_t capacity)
{
    m_records.resize(std::max<std::size_t>(1, capacity));
}

auto Frame_time_recorder::now() -> double
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

auto Frame_time_recorder::begin_frame(const std::int64_t frame_id) -> Frame_time_record&
{
    Frame_time_record& record = m_records[static_cast<std::size_t>(frame_id) % m_records.size()];
    record = Frame_time_record{};
    record.frame_id = frame_id;
    if (frame_id > m_latest_frame_id) {
        m_latest_frame_id = frame_id;
    }
    return record;
}

auto Frame_time_recorder::find(const std::int64_t frame_id) -> Frame_time_record*
{
    if (frame_id < 0) {
        return nullptr;
    }
    Frame_time_record& record = m_records[static_cast<std::size_t>(frame_id) % m_records.size()];
    return (record.frame_id == frame_id) ? &record : nullptr;
}

auto Frame_time_recorder::find(const std::int64_t frame_id) const -> const Frame_time_record*
{
    if (frame_id < 0) {
        return nullptr;
    }
    const Frame_time_record& record = m_records[static_cast<std::size_t>(frame_id) % m_records.size()];
    return (record.frame_id == frame_id) ? &record : nullptr;
}

} // namespace erhe::frame_pacing
