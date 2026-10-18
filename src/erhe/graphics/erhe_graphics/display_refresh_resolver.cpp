#include "erhe_graphics/display_refresh_resolver.hpp"

#include <spdlog/spdlog.h>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace erhe::graphics {

auto c_str(const Display_refresh_source value) -> const char*
{
    switch (value) {
        case Display_refresh_source::none:          return "none";
        case Display_refresh_source::window_mode:   return "window display mode";
        case Display_refresh_source::vulkan_timing: return "VK_EXT_present_timing";
        case Display_refresh_source::calibration:   return "init calibration";
        default:                                    return "?";
    }
}

auto c_str(const Display_refresh_mode value) -> const char*
{
    switch (value) {
        case Display_refresh_mode::unknown:   return "unknown";
        case Display_refresh_mode::fixed:     return "fixed refresh rate";
        case Display_refresh_mode::variable:  return "variable refresh rate";
        case Display_refresh_mode::decoupled: return "decoupled vblank";
        default:                              return "?";
    }
}

auto c_str(const Display_time_sample_source value) -> const char*
{
    switch (value) {
        case Display_time_sample_source::none:             return "none";
        case Display_time_sample_source::present_feedback: return "present timing feedback";
        case Display_time_sample_source::present_wait:     return "vkWaitForPresentKHR";
        case Display_time_sample_source::present_return:   return "vkQueuePresentKHR return";
        default:                                           return "?";
    }
}

void Display_refresh_resolver::Sample_series::clear()
{
    has_last      = false;
    last_time     = 0.0;
    last_frame_id = -1;
    deltas.clear();
}

Display_refresh_resolver::Display_refresh_resolver(std::shared_ptr<spdlog::logger> logger)
    : m_logger{std::move(logger)}
{
    // Init-time allocation only; the series never grow past the calibration
    // bound.
    m_feedback_series      .deltas.reserve(s_max_presents);
    m_present_wait_series  .deltas.reserve(s_max_presents);
    m_present_return_series.deltas.reserve(s_max_presents);
}

void Display_refresh_resolver::set_apply_callback(Apply_callback callback)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_apply_callback = std::move(callback);
}

void Display_refresh_resolver::set_frame_work_lookup(Frame_work_lookup lookup)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_frame_work_lookup = std::move(lookup);
}

auto Display_refresh_resolver::agree(const double a, const double b) -> bool
{
    if ((a <= 0.0) || (b <= 0.0)) {
        return false;
    }
    return std::abs(a - b) <= (s_agreement_fraction * std::max(a, b));
}

auto Display_refresh_resolver::confirms(const double measured, const double queried) -> bool
{
    if ((measured <= 0.0) || (queried <= 0.0)) {
        return false;
    }
    return std::abs(measured - queried) <= (s_calibration_tolerance * queried);
}

auto Display_refresh_resolver::series_for(const Display_time_sample_source source) -> Sample_series&
{
    switch (source) {
        case Display_time_sample_source::present_feedback: return m_feedback_series;
        case Display_time_sample_source::present_wait:     return m_present_wait_series;
        default:                                           return m_present_return_series;
    }
}

auto Display_refresh_resolver::series_for(const Display_time_sample_source source) const -> const Sample_series&
{
    switch (source) {
        case Display_time_sample_source::present_feedback: return m_feedback_series;
        case Display_time_sample_source::present_wait:     return m_present_wait_series;
        default:                                           return m_present_return_series;
    }
}

void Display_refresh_resolver::set_window_period(const double period_seconds, const char* display_name)
{
    Apply_callback callback;
    double         period_to_apply = 0.0;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        const bool changed = !agree(period_seconds, m_window_period) && (period_seconds != m_window_period);
        m_window_period       = (period_seconds > 0.0) ? period_seconds : 0.0;
        m_window_display_name = (display_name != nullptr) ? display_name : "";
        if (changed && !m_calibrating && (m_calibration_source != Display_time_sample_source::none)) {
            // The window is on another display now: the measured cadence
            // described the old one.
            m_logger->info("display refresh: window display changed, restarting init calibration");
        }
        if (changed) {
            m_calibrating          = true;
            m_presents_seen        = 0;
            m_feedback_series      .clear();
            m_present_wait_series  .clear();
            m_present_return_series.clear();
            m_calibration_source   = Display_time_sample_source::none;
            m_calibration_period   = 0.0;
            m_calibration_samples  = 0;
            m_calibration_rejected = 0;
        }
        const double before = m_resolved_period;
        resolve_locked();
        log_sources_locked("window display mode");
        if (m_resolved_period != before) {
            callback        = m_apply_callback;
            period_to_apply = m_resolved_period;
        }
    }
    if (callback && (period_to_apply > 0.0)) {
        callback(period_to_apply);
    }
}

void Display_refresh_resolver::set_vulkan_timing(const uint64_t refresh_duration_ns, const uint64_t refresh_interval_ns)
{
    Apply_callback callback;
    double         period_to_apply = 0.0;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_vulkan_refresh_duration_ns = refresh_duration_ns;
        m_vulkan_refresh_interval_ns = refresh_interval_ns;
        m_vulkan_known               = (refresh_duration_ns != 0);
        const double before = m_resolved_period;
        resolve_locked();
        log_sources_locked("swapchain timing properties");
        if (m_resolved_period != before) {
            callback        = m_apply_callback;
            period_to_apply = m_resolved_period;
        }
    }
    if (callback && (period_to_apply > 0.0)) {
        callback(period_to_apply);
    }
}

auto Display_refresh_resolver::vulkan_vblank_period_locked() const -> double
{
    if (!m_vulkan_known) {
        return 0.0;
    }
    const double duration = static_cast<double>(m_vulkan_refresh_duration_ns) * 1e-9;
    if ((m_vulkan_refresh_interval_ns == 0) || (m_vulkan_refresh_interval_ns == std::numeric_limits<uint64_t>::max())) {
        // Unknown dynamics, or variable refresh rate: refreshDuration is the
        // (minimum) cycle.
        return duration;
    }
    // Fixed: interval == duration. Decoupled: the vblank interval is the
    // quantum image durations are multiples of (specification note on
    // VkSwapchainTimingPropertiesEXT), which is what the pacer's grid counts.
    return static_cast<double>(m_vulkan_refresh_interval_ns) * 1e-9;
}

auto Display_refresh_resolver::is_calibrating() const -> bool
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return m_calibrating;
}

auto Display_refresh_resolver::wants_sample(const Display_time_sample_source source) const -> bool
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    if (!m_calibrating) {
        return false;
    }
    // One source at a time, best first: once a better source has delivered a
    // sample the lesser ones stop (their blocking waits cost frames).
    switch (source) {
        case Display_time_sample_source::present_feedback: return true;
        case Display_time_sample_source::present_wait:     return !m_feedback_series.has_last;
        case Display_time_sample_source::present_return:   return !m_feedback_series.has_last && !m_present_wait_series.has_last;
        default:                                           return false;
    }
}

void Display_refresh_resolver::add_display_time_sample(const Display_time_sample_source source, const std::int64_t frame_id, const double time_seconds)
{
    Apply_callback callback;
    double         period_to_apply = 0.0;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        if (!m_calibrating || (source == Display_time_sample_source::none) || (time_seconds <= 0.0)) {
            return;
        }
        Sample_series& series = series_for(source);
        if (series.has_last && (frame_id > series.last_frame_id) && (time_seconds > series.last_time)) {
            const double delta = time_seconds - series.last_time;
            // Only consecutive presents measure the cadence; a gap of
            // several frames (feedback lost for one id) is skipped. The
            // frame-work gate runs when the calibration ends: the frame's
            // GPU bracket is not readable yet when its present is sampled.
            if ((frame_id == series.last_frame_id + 1) && (delta < 0.5)) {
                series.deltas.push_back(Delta_sample{.delta = delta, .frame_id = frame_id});
            }
        }
        series.has_last      = true;
        series.last_time     = time_seconds;
        series.last_frame_id = frame_id;

        const bool enough = series.deltas.size() >= s_required_deltas;
        if (enough) {
            const double before = m_resolved_period;
            finish_calibration_locked();
            if (m_resolved_period != before) {
                callback        = m_apply_callback;
                period_to_apply = m_resolved_period;
            }
        }
    }
    if (callback && (period_to_apply > 0.0)) {
        callback(period_to_apply);
    }
}

void Display_refresh_resolver::note_present()
{
    Apply_callback callback;
    double         period_to_apply = 0.0;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        if (!m_calibrating) {
            return;
        }
        ++m_presents_seen;
        if (m_presents_seen >= s_max_presents) {
            const double before = m_resolved_period;
            finish_calibration_locked();
            if (m_resolved_period != before) {
                callback        = m_apply_callback;
                period_to_apply = m_resolved_period;
            }
        }
    }
    if (callback && (period_to_apply > 0.0)) {
        callback(period_to_apply);
    }
}

auto Display_refresh_resolver::estimate_period_locked(const Sample_series& series) const -> Estimate
{
    Estimate estimate{};
    // A frame proves the cadence only when its own work could not have been
    // the limiter: work well under the observed delta means the display,
    // not the application, set the interval. Unknown work (no record, no
    // GPU bracket) proves nothing. Without a work lookup every delta counts
    // (the unit test).
    std::vector<double> accepted;
    accepted.reserve(series.deltas.size());
    for (const Delta_sample& sample : series.deltas) {
        if (m_frame_work_lookup) {
            const double work = m_frame_work_lookup(sample.frame_id);
            if ((work <= 0.0) || (work >= (s_fast_frame_fraction * sample.delta))) {
                ++estimate.rejected;
                continue;
            }
        }
        accepted.push_back(sample.delta);
    }
    if (accepted.size() < s_min_cluster) {
        return estimate;
    }
    // Deltas are integer multiples of the vblank period plus jitter. The
    // densest cluster (the center with the most deltas within
    // s_cluster_fraction of it; the smaller center on a tie) is the period
    // the fast frames ran at; its median is the estimate. An outlier never
    // anchors the cluster, which a smallest-delta rule let happen.
    std::sort(accepted.begin(), accepted.end());
    std::size_t best_begin = 0;
    std::size_t best_end   = 0;
    for (std::size_t i = 0; i < accepted.size(); ++i) {
        const double center = accepted[i];
        std::size_t  begin  = i;
        while ((begin > 0) && (accepted[begin - 1] >= (center * (1.0 - s_cluster_fraction)))) {
            --begin;
        }
        std::size_t end = i;
        while ((end < accepted.size()) && (accepted[end] <= (center * (1.0 + s_cluster_fraction)))) {
            ++end;
        }
        if ((end - begin) > (best_end - best_begin)) {
            best_begin = begin;
            best_end   = end;
        }
    }
    const std::size_t count = best_end - best_begin;
    if (count < s_min_cluster) {
        return estimate;
    }
    estimate.period   = accepted[best_begin + (count / 2)];
    estimate.accepted = count;
    return estimate;
}

void Display_refresh_resolver::finish_calibration_locked()
{
    m_calibrating = false;
    // Best source that produced an estimate.
    const Display_time_sample_source order[] = {
        Display_time_sample_source::present_feedback,
        Display_time_sample_source::present_wait,
        Display_time_sample_source::present_return
    };
    m_calibration_source   = Display_time_sample_source::none;
    m_calibration_period   = 0.0;
    m_calibration_samples  = 0;
    m_calibration_rejected = 0;
    Estimate estimates[3]{};
    for (std::size_t i = 0; i < 3; ++i) {
        estimates[i] = estimate_period_locked(series_for(order[i]));
        if ((m_calibration_source == Display_time_sample_source::none) && (estimates[i].period > 0.0)) {
            m_calibration_source   = order[i];
            m_calibration_period   = estimates[i].period;
            m_calibration_samples  = estimates[i].accepted;
            m_calibration_rejected = estimates[i].rejected;
        }
    }
    if (m_calibration_source == Display_time_sample_source::none) {
        // No estimate: the report still shows what was thrown away, and
        // the log the raw deltas, so a run can be read afterwards.
        m_calibration_rejected = std::max({estimates[0].rejected, estimates[1].rejected, estimates[2].rejected});
        const auto describe = [](const Sample_series& series) -> std::string {
            if (series.deltas.empty()) {
                return std::string{"none"};
            }
            std::vector<double> sorted;
            sorted.reserve(series.deltas.size());
            for (const Delta_sample& sample : series.deltas) {
                sorted.push_back(sample.delta);
            }
            std::sort(sorted.begin(), sorted.end());
            return fmt::format(
                "{} deltas, min {:.2f} median {:.2f} max {:.2f} ms",
                sorted.size(), sorted.front() * 1000.0, sorted[sorted.size() / 2] * 1000.0, sorted.back() * 1000.0
            );
        };
        m_logger->warn(
            "display refresh: init calibration ended without an estimate after {} presents; feedback: {} ({} rejected as slow or unknown work); present-wait: {} ({} rejected); present-return: {} ({} rejected); the queried sources stand unverified",
            m_presents_seen,
            describe(m_feedback_series),       estimates[0].rejected,
            describe(m_present_wait_series),   estimates[1].rejected,
            describe(m_present_return_series), estimates[2].rejected
        );
    }
    resolve_locked();
    log_sources_locked("init calibration");
}

void Display_refresh_resolver::resolve_locked()
{
    const double window = m_window_period;
    const double vulkan = vulkan_vblank_period_locked();
    const double measured = m_calibration_period;

    if (measured > 0.0) {
        // The measured cadence vetoes queried values it contradicts and
        // confirms the one it matches, which is then taken (an exact
        // nominal, finer than a median of jittered samples). It stands on
        // its own only when it contradicts every queried value.
        if (confirms(measured, vulkan)) {
            m_resolved_period = vulkan;
            m_resolved_source = Display_refresh_source::vulkan_timing;
        } else if (confirms(measured, window)) {
            m_resolved_period = window;
            m_resolved_source = Display_refresh_source::window_mode;
        } else {
            m_resolved_period = measured;
            m_resolved_source = Display_refresh_source::calibration;
        }
        return;
    }
    if ((window > 0.0) && (vulkan > 0.0)) {
        if (agree(window, vulkan)) {
            m_resolved_period = vulkan;
            m_resolved_source = Display_refresh_source::vulkan_timing;
        } else {
            // The window knows which display it is on; the swapchain query
            // describes "the presentation engine", which the specification
            // does not tie to that display.
            m_resolved_period = window;
            m_resolved_source = Display_refresh_source::window_mode;
        }
        return;
    }
    if (vulkan > 0.0) {
        m_resolved_period = vulkan;
        m_resolved_source = Display_refresh_source::vulkan_timing;
        return;
    }
    if (window > 0.0) {
        m_resolved_period = window;
        m_resolved_source = Display_refresh_source::window_mode;
        return;
    }
    m_resolved_period = 0.0;
    m_resolved_source = Display_refresh_source::none;
}

void Display_refresh_resolver::log_sources_locked(const char* reason) const
{
    const auto hz = [](const double period) -> double { return (period > 0.0) ? (1.0 / period) : 0.0; };
    const double vulkan_duration = static_cast<double>(m_vulkan_refresh_duration_ns) * 1e-9;
    const double vulkan_interval =
        ((m_vulkan_refresh_interval_ns == 0) || (m_vulkan_refresh_interval_ns == std::numeric_limits<uint64_t>::max()))
            ? 0.0
            : static_cast<double>(m_vulkan_refresh_interval_ns) * 1e-9;
    Display_refresh_mode mode = Display_refresh_mode::unknown;
    if (m_vulkan_known && (m_vulkan_refresh_interval_ns != 0)) {
        mode =
            (m_vulkan_refresh_interval_ns == std::numeric_limits<uint64_t>::max()) ? Display_refresh_mode::variable :
            (m_vulkan_refresh_interval_ns == m_vulkan_refresh_duration_ns)         ? Display_refresh_mode::fixed :
                                                                                     Display_refresh_mode::decoupled;
    }
    m_logger->info(
        "display refresh ({}): window mode {:.3f} ms ({:.2f} Hz{}{}); VK_EXT_present_timing refreshDuration {:.3f} ms ({:.2f} Hz) refreshInterval {:.3f} ms, {}; init calibration {}{:.3f} ms ({:.2f} Hz) from {} ({} deltas, {} rejected); resolved {:.3f} ms ({:.2f} Hz) from {}",
        reason,
        m_window_period * 1000.0, hz(m_window_period),
        m_window_display_name.empty() ? "" : ", display ", m_window_display_name,
        vulkan_duration * 1000.0, hz(vulkan_duration),
        vulkan_interval * 1000.0, c_str(mode),
        m_calibrating ? "pending, " : "",
        m_calibration_period * 1000.0, hz(m_calibration_period), c_str(m_calibration_source),
        m_calibration_samples, m_calibration_rejected,
        m_resolved_period * 1000.0, hz(m_resolved_period), c_str(m_resolved_source)
    );
    if ((m_resolved_source != Display_refresh_source::none) && (m_calibration_period > 0.0)) {
        const double vulkan = vulkan_vblank_period_locked();
        if ((vulkan > 0.0) && !confirms(m_calibration_period, vulkan)) {
            m_logger->warn(
                "display refresh: VK_EXT_present_timing reports {:.3f} ms but the display ran at {:.3f} ms per present at init; the query is not used",
                vulkan * 1000.0, m_calibration_period * 1000.0
            );
        }
        if ((m_window_period > 0.0) && !confirms(m_calibration_period, m_window_period)) {
            m_logger->warn(
                "display refresh: the window display mode says {:.3f} ms but the display ran at {:.3f} ms per present at init; the mode is not used",
                m_window_period * 1000.0, m_calibration_period * 1000.0
            );
        }
    } else if ((m_window_period > 0.0) && (vulkan_vblank_period_locked() > 0.0) && !agree(m_window_period, vulkan_vblank_period_locked())) {
        m_logger->warn(
            "display refresh: VK_EXT_present_timing reports {:.3f} ms, the window display mode {:.3f} ms; the window mode is used until the init calibration decides",
            vulkan_vblank_period_locked() * 1000.0, m_window_period * 1000.0
        );
    }
    if (mode == Display_refresh_mode::variable) {
        m_logger->warn("display refresh: the presentation engine runs variable refresh rate; the pacer's fixed grid uses the minimum cycle");
    }
}

auto Display_refresh_resolver::get_report() const -> Display_refresh_report
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    Display_refresh_report report{};
    report.window_period_seconds           = m_window_period;
    report.vulkan_refresh_duration_seconds = static_cast<double>(m_vulkan_refresh_duration_ns) * 1e-9;
    report.vulkan_refresh_interval_seconds =
        ((m_vulkan_refresh_interval_ns == 0) || (m_vulkan_refresh_interval_ns == std::numeric_limits<uint64_t>::max()))
            ? 0.0
            : static_cast<double>(m_vulkan_refresh_interval_ns) * 1e-9;
    if (m_vulkan_known && (m_vulkan_refresh_interval_ns != 0)) {
        report.vulkan_mode =
            (m_vulkan_refresh_interval_ns == std::numeric_limits<uint64_t>::max()) ? Display_refresh_mode::variable :
            (m_vulkan_refresh_interval_ns == m_vulkan_refresh_duration_ns)         ? Display_refresh_mode::fixed :
                                                                                     Display_refresh_mode::decoupled;
    }
    report.calibration_done           = !m_calibrating;
    report.calibration_source         = m_calibration_source;
    report.calibration_period_seconds = m_calibration_period;
    report.calibration_sample_count   = m_calibration_samples;
    report.calibration_rejected_count = m_calibration_rejected;
    report.resolved_period_seconds    = m_resolved_period;
    report.resolved_source            = m_resolved_source;
    return report;
}

} // namespace erhe::graphics
