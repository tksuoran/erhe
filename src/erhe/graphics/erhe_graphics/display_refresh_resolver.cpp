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
        case Display_refresh_source::override_rate: return "config override";
        default:                                    return "?";
    }
}

auto c_str(const Display_refresh_policy value) -> const char*
{
    switch (value) {
        case Display_refresh_policy::automatic:     return "auto";
        case Display_refresh_policy::window_mode:   return "window";
        case Display_refresh_policy::vulkan_timing: return "vulkan";
        case Display_refresh_policy::calibration:   return "calibration";
        case Display_refresh_policy::override_rate: return "override";
        default:                                    return "?";
    }
}

auto parse_display_refresh_policy(const std::string& text, Display_refresh_policy& out_policy) -> bool
{
    if (text == "auto")        { out_policy = Display_refresh_policy::automatic;     return true; }
    if (text == "window")      { out_policy = Display_refresh_policy::window_mode;   return true; }
    if (text == "vulkan")      { out_policy = Display_refresh_policy::vulkan_timing; return true; }
    if (text == "calibration") { out_policy = Display_refresh_policy::calibration;   return true; }
    if (text == "override")    { out_policy = Display_refresh_policy::override_rate; return true; }
    return false;
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

auto c_str(const Display_refresh_calibration_phase value) -> const char*
{
    switch (value) {
        case Display_refresh_calibration_phase::idle:     return "idle";
        case Display_refresh_calibration_phase::free_run: return "free run";
        case Display_refresh_calibration_phase::locked:   return "locked";
        default:                                          return "?";
    }
}

void Display_refresh_resolver::Delta_series::clear()
{
    has_last      = false;
    last_time     = 0.0;
    last_frame_id = -1;
    deltas.clear();
}

Display_refresh_resolver::Display_refresh_resolver(std::shared_ptr<spdlog::logger> logger)
    : m_logger{std::move(logger)}
{
    // Init-time allocation only; nothing here grows past the calibration
    // bounds.
    m_present_ring.resize(s_present_ring);
    m_free_run_series.deltas.reserve(s_max_presents);
    m_locked_series  .deltas.reserve(s_max_presents);
}

void Display_refresh_resolver::set_apply_callback(Apply_callback callback)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_apply_callback = std::move(callback);
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

auto Display_refresh_resolver::median(std::vector<double> values) -> double
{
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

void Display_refresh_resolver::densest_cluster(std::vector<double> values, double& out_median, std::size_t& out_count)
{
    out_median = 0.0;
    out_count  = 0;
    if (values.empty()) {
        return;
    }
    // The center with the most values within s_cluster_fraction of it; the
    // smaller center on a tie. An outlier never anchors the cluster.
    std::sort(values.begin(), values.end());
    std::size_t best_begin = 0;
    std::size_t best_end   = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const double center = values[i];
        std::size_t  begin  = i;
        while ((begin > 0) && (values[begin - 1] >= (center * (1.0 - s_cluster_fraction)))) {
            --begin;
        }
        std::size_t end = i;
        while ((end < values.size()) && (values[end] <= (center * (1.0 + s_cluster_fraction)))) {
            ++end;
        }
        if ((end - begin) > (best_end - best_begin)) {
            best_begin = begin;
            best_end   = end;
        }
    }
    out_count  = best_end - best_begin;
    out_median = values[best_begin + (out_count / 2)];
}

void Display_refresh_resolver::restart_calibration_locked()
{
    m_calibrating           = true;
    m_presents_seen         = 0;
    m_free_run_series.clear();
    m_locked_series  .clear();
    for (Present_entry& entry : m_present_ring) {
        entry = Present_entry{};
    }
    m_calibration_verified  = false;
    m_free_run_period       = 0.0;
    m_locked_period         = 0.0;
    m_locked_cluster_count  = 0;
    m_locked_delta_count    = 0;
    if (!m_modes_known) {
        m_phase = Display_refresh_calibration_phase::idle; // the calibration swapchain reports its modes when it exists
    } else if (!m_can_lock) {
        finish_calibration_locked();
    } else {
        m_phase = m_can_free_run ? Display_refresh_calibration_phase::free_run : Display_refresh_calibration_phase::locked;
    }
}

void Display_refresh_resolver::set_policy(const Display_refresh_policy policy, const double override_period_seconds)
{
    Apply_callback callback;
    double         period_to_apply = 0.0;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_policy          = policy;
        m_override_period = (override_period_seconds > 0.0) ? override_period_seconds : 0.0;
        const double before = m_resolved_period;
        resolve_locked();
        log_sources_locked("config");
        if (m_resolved_period != before) {
            callback        = m_apply_callback;
            period_to_apply = m_resolved_period;
        }
    }
    if (callback && (period_to_apply > 0.0)) {
        callback(period_to_apply);
    }
}

void Display_refresh_resolver::set_window_period(const double period_seconds, const char* display_name, const std::size_t display_count)
{
    Apply_callback callback;
    double         period_to_apply = 0.0;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        const bool changed = (period_seconds != m_window_period) && !agree(period_seconds, m_window_period);
        m_window_period       = (period_seconds > 0.0) ? period_seconds : 0.0;
        m_window_display_name = (display_name != nullptr) ? display_name : "";
        m_display_count       = display_count;
        if (changed && m_modes_known) {
            // The window is on another display now: the cadence measured at
            // init described the old one. The calibration swapchain ran at
            // device init and is gone, so the result is dropped and the
            // queried sources decide for this display.
            m_logger->info("display refresh: window display changed, dropping the init calibration result");
            m_calibrating          = false;
            m_phase                = Display_refresh_calibration_phase::idle;
            m_calibration_verified = false;
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

void Display_refresh_resolver::set_calibration_modes(const bool can_free_run, const bool can_lock)
{
    Apply_callback callback;
    double         period_to_apply = 0.0;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_modes_known  = true;
        m_can_free_run = can_free_run;
        m_can_lock     = can_lock;
        if (!m_calibrating) {
            return; // the calibration is done
        }
        const double before = m_resolved_period;
        restart_calibration_locked();
        if (m_calibrating) {
            m_logger->info(
                "display refresh: init calibration starts in the {} phase (free-running mode {}, plain FIFO {})",
                c_str(m_phase), can_free_run ? "available" : "unavailable", can_lock ? "available" : "unavailable"
            );
        }
        if (m_resolved_period != before) {
            callback        = m_apply_callback;
            period_to_apply = m_resolved_period;
        }
    }
    if (callback && (period_to_apply > 0.0)) {
        callback(period_to_apply);
    }
}

void Display_refresh_resolver::abort_calibration(const char* reason)
{
    Apply_callback callback;
    double         period_to_apply = 0.0;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        if (!m_calibrating) {
            return;
        }
        m_logger->warn("display refresh: init calibration aborted: {}", (reason != nullptr) ? reason : "");
        const double before = m_resolved_period;
        finish_calibration_locked();
        if (m_resolved_period != before) {
            callback        = m_apply_callback;
            period_to_apply = m_resolved_period;
        }
    }
    if (callback && (period_to_apply > 0.0)) {
        callback(period_to_apply);
    }
}

auto Display_refresh_resolver::get_calibration_phase() const -> Display_refresh_calibration_phase
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return m_calibrating ? m_phase : Display_refresh_calibration_phase::idle;
}

auto Display_refresh_resolver::is_calibrating() const -> bool
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return m_calibrating;
}

void Display_refresh_resolver::note_present(const std::int64_t frame_id, const Display_refresh_present_kind kind)
{
    Apply_callback callback;
    double         period_to_apply = 0.0;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        if (!m_calibrating || (frame_id < 0)) {
            return;
        }
        Present_entry& entry = m_present_ring[static_cast<std::size_t>(frame_id) % s_present_ring];
        entry.frame_id = frame_id;
        entry.kind     = kind;
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

void Display_refresh_resolver::add_gpu_frame_end(const std::int64_t frame_id, const double gpu_end_seconds)
{
    Apply_callback callback;
    double         period_to_apply = 0.0;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        if (!m_calibrating || (frame_id < 0) || (gpu_end_seconds <= 0.0)) {
            return;
        }
        const Present_entry& entry = m_present_ring[static_cast<std::size_t>(frame_id) % s_present_ring];
        if (entry.frame_id != frame_id) {
            return; // not presented in this calibration
        }
        Delta_series* series = nullptr;
        switch (entry.kind) {
            case Display_refresh_present_kind::free_running: series = &m_free_run_series; break;
            case Display_refresh_present_kind::fifo:         series = &m_locked_series;   break;
            default:                                         return;
        }
        // Only consecutive presents of the same kind measure a cadence.
        if (series->has_last && (frame_id == (series->last_frame_id + 1)) && (gpu_end_seconds > series->last_time)) {
            const double delta = gpu_end_seconds - series->last_time;
            if (delta < 0.5) {
                series->deltas.push_back(delta);
            }
        }
        series->has_last      = true;
        series->last_time     = gpu_end_seconds;
        series->last_frame_id = frame_id;

        if ((m_phase == Display_refresh_calibration_phase::free_run) && (m_free_run_series.deltas.size() >= (s_free_run_skip + s_free_run_deltas))) {
            m_phase = Display_refresh_calibration_phase::locked;
            m_logger->info("display refresh: init calibration free run done ({} deltas), entering the locked (plain FIFO) phase", m_free_run_series.deltas.size());
        }
        if ((m_phase == Display_refresh_calibration_phase::locked) && (m_locked_series.deltas.size() >= (s_locked_settle + s_locked_deltas))) {
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

void Display_refresh_resolver::finish_calibration_locked()
{
    m_calibrating = false;
    m_phase       = Display_refresh_calibration_phase::idle;

    // Production interval: the free run after the mode switch settled.
    std::vector<double> free_run_deltas;
    if (m_free_run_series.deltas.size() > s_free_run_skip) {
        free_run_deltas.assign(m_free_run_series.deltas.begin() + static_cast<std::ptrdiff_t>(s_free_run_skip), m_free_run_series.deltas.end());
    }
    m_free_run_period = median(free_run_deltas);

    // Locked cadence: the FIFO deltas after the queue filled.
    std::vector<double> locked_deltas;
    if (m_locked_series.deltas.size() > s_locked_settle) {
        locked_deltas.assign(m_locked_series.deltas.begin() + static_cast<std::ptrdiff_t>(s_locked_settle), m_locked_series.deltas.end());
    }
    m_locked_delta_count = locked_deltas.size();
    densest_cluster(locked_deltas, m_locked_period, m_locked_cluster_count);

    const bool dense =
        (m_locked_delta_count >= (s_locked_deltas / 2)) &&
        (static_cast<double>(m_locked_cluster_count) >= (s_min_cluster_share * static_cast<double>(m_locked_delta_count)));
    const bool display_bound = (m_free_run_period > 0.0) && (m_locked_period > 0.0) && (m_free_run_period <= (s_production_fraction * m_locked_period));
    m_calibration_verified = dense && display_bound;

    if (!m_calibration_verified) {
        m_logger->warn(
            "display refresh: init calibration unverified after {} presents: free run {} deltas (production {:.3f} ms), locked {} deltas, densest cluster {} at {:.3f} ms{}{}; the queried sources stand",
            m_presents_seen,
            free_run_deltas.size(), m_free_run_period * 1000.0,
            m_locked_delta_count, m_locked_cluster_count, m_locked_period * 1000.0,
            dense ? "" : " (not dense enough)",
            ((m_locked_period > 0.0) && (m_free_run_period > 0.0) && !display_bound) ? " (production too close to the cadence: the application, not the display, may have set it)" : ""
        );
    }
    resolve_locked();
    log_sources_locked("init calibration");
}

void Display_refresh_resolver::resolve_locked()
{
    const double window   = m_window_period;
    const double vulkan   = vulkan_vblank_period_locked();
    const double measured = m_calibration_verified ? m_locked_period : 0.0;

    // Any two known sources disagreeing beyond tolerance is worth a warning
    // whatever the policy picks (log_sources_locked).
    m_sources_disagree =
        ((window > 0.0) && (vulkan > 0.0) && !agree(window, vulkan)) ||
        ((measured > 0.0) && (window > 0.0) && !confirms(measured, window)) ||
        ((measured > 0.0) && (vulkan > 0.0) && !confirms(measured, vulkan));

    // A forced source, when it is there.
    switch (m_policy) {
        case Display_refresh_policy::override_rate: {
            if (m_override_period > 0.0) {
                m_resolved_period = m_override_period;
                m_resolved_source = Display_refresh_source::override_rate;
                return;
            }
            break;
        }
        case Display_refresh_policy::window_mode: {
            if (window > 0.0) {
                m_resolved_period = window;
                m_resolved_source = Display_refresh_source::window_mode;
                return;
            }
            break;
        }
        case Display_refresh_policy::vulkan_timing: {
            if (vulkan > 0.0) {
                m_resolved_period = vulkan;
                m_resolved_source = Display_refresh_source::vulkan_timing;
                return;
            }
            break;
        }
        case Display_refresh_policy::calibration: {
            if (measured > 0.0) {
                m_resolved_period = measured;
                m_resolved_source = Display_refresh_source::calibration;
                return;
            }
            // Pending: the automatic decision serves until the calibration
            // ends; unverified: it stays, with the warning below.
            break;
        }
        default: {
            break;
        }
    }
    if ((m_policy != Display_refresh_policy::automatic) && !m_calibrating && !m_policy_fallback_logged) {
        m_policy_fallback_logged = true;
        m_logger->warn(
            "display refresh: configured source '{}' is not available ({}); using the automatic decision",
            c_str(m_policy),
            (m_policy == Display_refresh_policy::override_rate) ? "display_refresh_override_hz is not positive" :
            (m_policy == Display_refresh_policy::calibration)   ? "the init calibration did not verify a cadence" :
                                                                  "the source reported nothing"
        );
    }

    if (measured > 0.0) {
        // The measured cadence vetoes queried values it contradicts and
        // confirms the one it matches, which is then taken (an exact
        // nominal). It stands on its own only when it contradicts every
        // queried value.
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
        "display refresh ({}): policy {}{}; window mode {:.3f} ms ({:.2f} Hz{}{}{}{}); VK_EXT_present_timing refreshDuration {:.3f} ms ({:.2f} Hz) refreshInterval {:.3f} ms, {}; init calibration {}: production {:.3f} ms, locked {:.3f} ms ({:.2f} Hz; {} of {} deltas in the cluster); resolved {:.3f} ms ({:.2f} Hz) from {}",
        reason,
        c_str(m_policy),
        (m_policy == Display_refresh_policy::override_rate) ? fmt::format(" ({:.3f} ms, {:.2f} Hz)", m_override_period * 1000.0, hz(m_override_period)) : std::string{},
        m_window_period * 1000.0, hz(m_window_period),
        m_window_display_name.empty() ? "" : ", display ", m_window_display_name,
        (m_display_count > 1) ? fmt::format(", {} displays", m_display_count) : std::string{},
        (m_display_count == 1) ? ", 1 display" : "",
        vulkan_duration * 1000.0, hz(vulkan_duration),
        vulkan_interval * 1000.0, c_str(mode),
        m_calibrating ? "pending" : (m_calibration_verified ? "verified" : "unverified"),
        m_free_run_period * 1000.0,
        m_locked_period * 1000.0, hz(m_locked_period), m_locked_cluster_count, m_locked_delta_count,
        m_resolved_period * 1000.0, hz(m_resolved_period), c_str(m_resolved_source)
    );
    if (m_sources_disagree) {
        const double vulkan   = vulkan_vblank_period_locked();
        const double measured = m_calibration_verified ? m_locked_period : 0.0;
        std::string known;
        if (m_window_period > 0.0) { known += fmt::format(" window mode {:.3f} ms;", m_window_period * 1000.0); }
        if (vulkan > 0.0)          { known += fmt::format(" VK_EXT_present_timing {:.3f} ms;", vulkan * 1000.0); }
        if (measured > 0.0)        { known += fmt::format(" measured vblank cadence {:.3f} ms;", measured * 1000.0); }
        m_logger->warn(
            "display refresh: the sources disagree:{} using {:.3f} ms from {}{}",
            known, m_resolved_period * 1000.0, c_str(m_resolved_source),
            m_calibrating ? " until the init calibration decides" : ""
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
    report.policy                          = m_policy;
    report.override_period_seconds         = m_override_period;
    report.display_count                   = m_display_count;
    report.sources_disagree                = m_sources_disagree;
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
    report.calibration_phase       = m_calibrating ? m_phase : Display_refresh_calibration_phase::idle;
    report.calibration_done        = !m_calibrating;
    report.calibration_verified    = m_calibration_verified;
    report.free_run_period_seconds = m_free_run_period;
    report.free_run_delta_count    = (m_free_run_series.deltas.size() > s_free_run_skip) ? (m_free_run_series.deltas.size() - s_free_run_skip) : 0;
    report.locked_period_seconds   = m_locked_period;
    report.locked_delta_count      = m_locked_delta_count;
    report.locked_cluster_count    = m_locked_cluster_count;
    report.resolved_period_seconds = m_resolved_period;
    report.resolved_source         = m_resolved_source;
    return report;
}

} // namespace erhe::graphics
