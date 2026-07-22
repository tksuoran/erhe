#include "erhe_frame_pacing/frame_pacing_observer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace erhe::frame_pacing {

namespace {

constexpr std::int64_t s_summary_interval_frames = 600;

[[nodiscard]] auto format_line(const char* const format, ...) -> std::string
{
    char buffer[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return std::string{buffer};
}

} // anonymous namespace

void Frame_pacing_observer::tick(Frame_time_recorder& recorder, const double now, const double refresh_period)
{
    if (refresh_period <= 0.0) {
        return; // display refresh not known yet
    }
    // (Re)create the pacer when the refresh period first arrives or changes
    // by more than 1 percent (display / mode change - behaves like a
    // swapchain recreation for the pacer).
    if ((m_pacer == nullptr) || (std::abs(refresh_period - m_refresh_period) > (0.01 * m_refresh_period))) {
        Frame_pacer_tunables tunables;
        tunables.refresh_period = refresh_period;
        m_pacer          = std::make_unique<Frame_pacer>(tunables);
        m_refresh_period = refresh_period;
        m_min_vsyncs_applied = 1; // fresh pacer starts at the default floor
        m_log_lines.push_back(
            format_line("frame pacing (observer): pacer created, refresh_period=%.3f ms", refresh_period * 1000.0)
        );
    }

    // Apply a pending application-commanded cadence floor (FR1, scenario 9).
    {
        const int requested_min_vsyncs = m_min_vsyncs_request.load(std::memory_order_relaxed);
        if (requested_min_vsyncs != m_min_vsyncs_applied) {
            m_pacer->set_min_vsyncs(requested_min_vsyncs);
            m_min_vsyncs_applied = requested_min_vsyncs;
            m_log_lines.push_back(format_line("frame pacing: set_min_vsyncs(%d)", requested_min_vsyncs));
        }
    }

    const std::int64_t current_frame_id = recorder.get_latest_frame_id();
    if (current_frame_id < 0) {
        return;
    }

    deliver_pending(recorder, current_frame_id);

    // Observer mode: compute the decision, record the prediction, enforce
    // nothing. The release time / target slot / wait id are what the pacer
    // WOULD have used (FR2/FR3/FR5); FR4 prediction accuracy is checked
    // against achieved present feedback in deliver_pending().
    const Schedule_decision decision = m_pacer->schedule(current_frame_id, now);
    m_last_decision = decision; // host may enforce wait_id (FR5 clamp, P2.2)
    Delivery& entry = m_delivery[static_cast<std::size_t>(current_frame_id) % s_delivery_ring];
    entry = Delivery{};
    entry.frame_id          = current_frame_id;
    entry.predicted_display = decision.predicted_display;

    const int vsyncs = m_pacer->get_vsyncs_per_frame();
    if (vsyncs != m_previous_vsyncs) {
        m_log_lines.push_back(
            format_line(
                "frame pacing (observer): cadence change %d -> %d at frame %lld (W=%.3f ms margin=%.3f ms)",
                m_previous_vsyncs, vsyncs, static_cast<long long>(current_frame_id),
                m_last_w_statistic * 1000.0, m_pacer->get_margin() * 1000.0
            )
        );
        m_previous_vsyncs = vsyncs;
    }

    if (m_last_summary_frame < 0) {
        m_last_summary_frame = current_frame_id;
    } else if ((current_frame_id - m_last_summary_frame) >= s_summary_interval_frames) {
        emit_summary(current_frame_id);
    }
}

void Frame_pacing_observer::deliver_pending(Frame_time_recorder& recorder, const std::int64_t current_frame_id)
{
    const std::int64_t first = std::max<std::int64_t>(0, current_frame_id - s_scan_window);
    for (std::int64_t frame_id = first; frame_id < current_frame_id; ++frame_id) {
        Delivery& entry = m_delivery[static_cast<std::size_t>(frame_id) % s_delivery_ring];
        if (entry.frame_id != frame_id) {
            continue; // never scheduled through the observer (startup) or evicted
        }
        const Frame_time_record* const record = recorder.find(frame_id);
        if (record == nullptr) {
            entry.cpu_delivered      = true;
            entry.gpu_delivered      = true;
            entry.feedback_delivered = true;
            continue;
        }
        if (!entry.cpu_delivered && (record->cpu_slot_end > 0.0)) {
            m_pacer->on_cpu_done(frame_id, record->cpu_service_time());
            entry.cpu_delivered = true;
            if (record->pacer_wait_end > record->pacer_wait_begin) {
                const double wait_span = record->pacer_wait_end - record->pacer_wait_begin;
                m_pacer_wait_sum = m_pacer_wait_sum + wait_span;
                m_pacer_wait_max = std::max(m_pacer_wait_max, wait_span);
                ++m_pacer_wait_count;
                if (wait_span > 0.0002) { // clamp actually blocked (not a no-op return)
                    ++m_pacer_wait_blocked_count;
                }
            }
        }
        if (!entry.gpu_delivered && (record->gpu_frame_end > record->gpu_frame_begin)) {
            // Latency measured from the actual start of per-frame work: the
            // pacer wait end when a wait ran, else the CPU slot begin
            // (observer mode: no wait is enforced).
            const double release = (record->pacer_wait_end > 0.0) ? record->pacer_wait_end : record->cpu_slot_begin;
            m_pacer->on_gpu_done(
                frame_id,
                record->gpu_frame_end,
                record->cpu_service_time(),
                record->gpu_frame_end - record->gpu_frame_begin,
                record->gpu_frame_end - release
            );
            entry.gpu_delivered = true;
            ++m_gpu_sample_count;
            const Frame_stats stats = m_pacer->get_frame_stats(frame_id);
            if (stats.valid) {
                m_last_w_statistic = stats.w_statistic;
                if (stats.self_miss) {
                    ++m_miss_count;
                } else if (stats.slack < 0.001) {
                    ++m_near_miss_count;
                }
            }
        }
        if (!entry.feedback_delivered && (record->achieved_present_time > 0.0)) {
            m_pacer->on_present_feedback(frame_id, record->achieved_present_time);
            entry.feedback_delivered = true;
            ++m_feedback_count;
            if (entry.predicted_display > 0.0) {
                const double error = std::abs(record->achieved_present_time - entry.predicted_display);
                m_prediction_error_sum += error;
                m_prediction_error_max  = std::max(m_prediction_error_max, error);
                ++m_prediction_error_count;
            }
        }
    }
}

void Frame_pacing_observer::emit_summary(const std::int64_t current_frame_id)
{
    const double mean_prediction_error_ms = (m_prediction_error_count > 0)
        ? (m_prediction_error_sum / static_cast<double>(m_prediction_error_count)) * 1000.0
        : 0.0;
    const double mean_pacer_wait_ms = (m_pacer_wait_count > 0)
        ? (m_pacer_wait_sum / static_cast<double>(m_pacer_wait_count)) * 1000.0
        : 0.0;
    m_log_lines.push_back(
        format_line(
            "frame pacing (observer): frame %lld N=%d W=%.3f ms margin=%.3f ms misses=%d near=%d gpu_samples=%d feedback=%d pred_err mean=%.3f ms max=%.3f ms clamp_wait mean=%.3f ms max=%.3f ms blocked=%d/%d",
            static_cast<long long>(current_frame_id),
            m_pacer->get_vsyncs_per_frame(),
            m_last_w_statistic * 1000.0,
            m_pacer->get_margin() * 1000.0,
            m_miss_count,
            m_near_miss_count,
            m_gpu_sample_count,
            m_feedback_count,
            mean_prediction_error_ms,
            m_prediction_error_max * 1000.0,
            mean_pacer_wait_ms,
            m_pacer_wait_max * 1000.0,
            m_pacer_wait_blocked_count,
            m_pacer_wait_count
        )
    );
    m_last_summary_frame     = current_frame_id;
    m_pacer_wait_sum         = 0.0;
    m_pacer_wait_max         = 0.0;
    m_pacer_wait_count       = 0;
    m_pacer_wait_blocked_count = 0;
    m_miss_count             = 0;
    m_near_miss_count        = 0;
    m_feedback_count         = 0;
    m_gpu_sample_count       = 0;
    m_prediction_error_sum   = 0.0;
    m_prediction_error_max   = 0.0;
    m_prediction_error_count = 0;
}

auto Frame_pacing_observer::consume_log_line(std::string& out_line) -> bool
{
    if (m_log_lines.empty()) {
        return false;
    }
    out_line = std::move(m_log_lines.front());
    m_log_lines.pop_front();
    return true;
}

void Frame_pacing_observer::request_min_vsyncs(const int min_vsyncs)
{
    const int clamped = std::max(1, std::min(min_vsyncs, 8)); // Frame_pacer_tunables max_vsyncs default
    m_min_vsyncs_request.store(clamped, std::memory_order_relaxed);
}

} // namespace erhe::frame_pacing
