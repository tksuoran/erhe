#include "erhe_frame_pacing/frame_time_recorder.hpp"
#include "erhe_profile/profile.hpp"

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
    // Only the portion of the pacer waits overlapping the CPU slot counts.
    const auto add_overlap = [this, &waits](const double wait_begin, const double wait_end) {
        if (wait_end <= wait_begin) {
            return;
        }
        const double overlap_begin = std::max(wait_begin, cpu_slot_begin);
        const double overlap_end   = std::min(wait_end, cpu_slot_end);
        if (overlap_end > overlap_begin) {
            waits += overlap_end - overlap_begin;
        }
    };
    add_overlap(pacer_wait_begin, pacer_wait_end);
    add_overlap(present_wait_begin, present_wait_end);
    add_overlap(join_wait_begin, join_wait_end);
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

void Frame_time_recorder::begin_frame(const std::int64_t frame_id, const double cpu_slot_begin)
{
    ERHE_PROFILE_FUNCTION();

    const std::lock_guard<std::mutex> lock{m_mutex};
    Frame_time_record& record = m_records[static_cast<std::size_t>(frame_id) % m_records.size()];
    record = Frame_time_record{};
    record.frame_id       = frame_id;
    record.cpu_slot_begin = cpu_slot_begin;
    if (frame_id > m_latest_frame_id) {
        m_latest_frame_id = frame_id;
    }
}

auto Frame_time_recorder::find_locked(const std::int64_t frame_id) -> Frame_time_record*
{
    if (frame_id < 0) {
        return nullptr;
    }
    Frame_time_record& record = m_records[static_cast<std::size_t>(frame_id) % m_records.size()];
    return (record.frame_id == frame_id) ? &record : nullptr;
}

auto Frame_time_recorder::contains(const std::int64_t frame_id) const -> bool
{
    if (frame_id < 0) {
        return false;
    }
    const std::lock_guard<std::mutex> lock{m_mutex};
    return m_records[static_cast<std::size_t>(frame_id) % m_records.size()].frame_id == frame_id;
}

void Frame_time_recorder::set(const std::int64_t frame_id, double Frame_time_record::* const field, const double value)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    Frame_time_record* const record = find_locked(frame_id);
    if (record != nullptr) {
        record->*field = value;
    }
}

void Frame_time_recorder::set(
    const std::int64_t                 frame_id,
    double Frame_time_record::* const field_a,
    const double                       value_a,
    double Frame_time_record::* const field_b,
    const double                       value_b
)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    Frame_time_record* const record = find_locked(frame_id);
    if (record != nullptr) {
        record->*field_a = value_a;
        record->*field_b = value_b;
    }
}

void Frame_time_recorder::set_once(const std::int64_t frame_id, double Frame_time_record::* const field, const double value)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    Frame_time_record* const record = find_locked(frame_id);
    if ((record != nullptr) && (record->*field == 0.0)) {
        record->*field = value;
    }
}

void Frame_time_recorder::add(const std::int64_t frame_id, double Frame_time_record::* const field, const double value)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    Frame_time_record* const record = find_locked(frame_id);
    if (record != nullptr) {
        record->*field += value;
    }
}

auto Frame_time_recorder::get(const std::int64_t frame_id, Frame_time_record& out_record) const -> bool
{
    if (frame_id < 0) {
        return false;
    }
    const std::lock_guard<std::mutex> lock{m_mutex};
    const Frame_time_record& record = m_records[static_cast<std::size_t>(frame_id) % m_records.size()];
    if (record.frame_id != frame_id) {
        return false;
    }
    out_record = record;
    return true;
}

auto Frame_time_recorder::get_latest_frame_id() const -> std::int64_t
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return m_latest_frame_id;
}

void Frame_time_recorder::set_display_refresh_duration(const double seconds)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_display_refresh_duration = seconds;
}

auto Frame_time_recorder::get_display_refresh_duration() const -> double
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return m_display_refresh_duration;
}

} // namespace erhe::frame_pacing
