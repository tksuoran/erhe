// Present calibration at device init, the backend-neutral part
// (doc/erhe/metal_backend.md "Frame pacing", doc/erhe/vulkan_backend.md
// "Surface and swapchain"): clear-only probes, presented before the
// application's first frame, measure the two properties of a presentation
// path that honors target present times which differ by display mode:
//  - the request window: which request times, for an image complete well
//    ahead of its target vsync V, display at V (on a fixed-rate display
//    V - 0.75T ... V; on a ProMotion panel a period earlier);
//  - the deadline: how long before V an image must be complete to display
//    at V when requested inside that window.
// Probes run as a steady stream, one every second vsync, so a probe a
// refresh late or early does not compete with its neighbour for a vsync.

#include "erhe_graphics/present_calibration_runner.hpp"
#include "erhe_graphics/present_calibration.hpp"
#include "erhe_frame_pacing/frame_time_recorder.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <utility>
#include <vector>

namespace erhe::graphics {

namespace {

// A change to any of these constants or to the phases below changes what the
// calibration measures: bump c_present_calibration_version
// (present_calibration_cache.hpp) so cached entries are not reused.
constexpr int    s_request_steps         = 11;    // request offsets 0 ... 2.5 T in steps of T/4
constexpr int    s_request_passes        = 3;
constexpr double s_request_lead_periods  = 3.5;   // request-window samples complete at least this early
constexpr int    s_deadline_steps        = 21;    // completion margins T/2 ... 3 T in steps of T/8
constexpr int    s_deadline_refine_steps = 3;     // refine pass: this many T/8 steps either side of the split
constexpr int    s_deadline_coarse_attempts = 3;  // coarse passes when a pass displays late as a whole
constexpr double s_guard                 = 0.001; // seconds, added to the measured deadline
constexpr double s_feedback_timeout      = 0.25;  // seconds past the expected display
constexpr double s_wall_clock_bound      = 10.0;  // seconds for the whole calibration
constexpr double s_window_shown_bound    = 2.0;   // seconds for the window to get displayed
constexpr std::size_t s_grid_presents    = 30;    // consecutive displayed plain presents (also outlasts the window-open animation)

auto now() -> double
{
    return erhe::frame_pacing::Frame_time_recorder::now();
}

} // namespace

IPresent_calibration_presenter::~IPresent_calibration_presenter() noexcept = default;

void present_calibration_wait_until(const double time)
{
    for (;;) {
        const double remaining = time - now();
        if (remaining <= 0.0) {
            return;
        }
        if (remaining > 0.0015) {
            std::this_thread::sleep_for(std::chrono::duration<double>(remaining - 0.001));
        }
    }
}

auto run_present_calibration(
    IPresent_calibration_presenter& presenter,
    const double                    nominal_period,
    Present_calibration&            result
) -> const char*
{
    const auto fail = [&result](const char* const reason) -> const char* {
        result.valid  = false;
        result.status = reason;
        return reason;
    };
    if (nominal_period <= 0.0) {
        return fail("no display refresh period");
    }
    const double      start_time  = now();
    const std::size_t queue_depth = std::max<std::size_t>(1, presenter.get_queue_depth());

    // 1. Grid: back-to-back plain presents; consecutive displays are whole
    // periods apart. A window ordered on screen during init is not yet
    // composited, so its first presents are not displayed: present until
    // s_grid_presents consecutive ones are, within s_window_shown_bound.
    std::vector<double> displayed;
    {
        std::size_t checked = 0;
        while (displayed.size() < s_grid_presents) {
            if ((now() - start_time) > s_window_shown_bound) {
                return fail("no presentation feedback (window occluded or not yet shown?)");
            }
            presenter.service_window();
            const std::ptrdiff_t index = presenter.present(0.0, 0.0);
            if (index < 0) {
                return fail("a grid present failed");
            }
            // Judge the presents whose feedback is due (queue_depth in the pool).
            while ((checked + queue_depth) <= static_cast<std::size_t>(index)) {
                const Present_calibration_probe_feedback entry = presenter.wait(checked, 1.0);
                if (entry.presented_time > 0.0) {
                    displayed.push_back(entry.presented_time);
                } else {
                    displayed.clear();
                }
                ++checked;
            }
        }
        result.samples.push_back(Present_calibration_sample{.phase = Present_calibration_phase::grid, .parameter = static_cast<double>(checked)});
        for (const double presented_time : displayed) {
            result.samples.push_back(Present_calibration_sample{.phase = Present_calibration_phase::grid, .presented_time = presented_time});
        }
    }
    std::vector<double> period_samples;
    for (std::size_t i = 1; i < displayed.size(); ++i) {
        const double dt = displayed[i] - displayed[i - 1];
        const double dn = std::round(dt / nominal_period);
        if ((dn >= 1.0) && (dn <= 4.0)) {
            period_samples.push_back(dt / dn);
        }
    }
    if (period_samples.size() < (s_grid_presents / 2)) {
        return fail("no presentation feedback (window occluded or not yet shown?)");
    }
    std::sort(period_samples.begin(), period_samples.end());
    const double period = period_samples[period_samples.size() / 2];
    result.period = period;
    double grid_reference = displayed.back();

    const auto next_vsync_after = [&grid_reference, period](const double time) -> double {
        return grid_reference + (std::ceil((time - grid_reference) / period) * period);
    };

    // A batch of probes presented as a steady stream, one every second vsync
    // (a probe a refresh late or early does not take its neighbour's slot),
    // or further apart where the commit margin would otherwise commit a
    // probe before its predecessor's target: two probes queued together
    // displayed late again at margins from ~1.9 T on (Metal, Dell at 120 Hz),
    // which is not deadline evidence. Probe i targets V_i, requests at
    // V_i - request_offset and commits at V_i - commit_margin. A steadily
    // fed compositor is what the pacer sees; a probe waiting for its
    // predecessor's display measured a compositor returning from idle (late
    // displays at any margin). The feedback is collected once the batch has
    // been displayed. False on a failure the calibration cannot continue past.
    class Probe
    {
    public:
        Present_calibration_phase phase;
        double request_offset;
        double commit_margin;
    };
    const auto run_probes = [&](const std::vector<Probe>& probes) -> bool {
        double lead = 0.0;
        for (const Probe& probe : probes) {
            lead = std::max(lead, probe.commit_margin);
        }
        const double spacing      = std::max(2.0, std::floor(lead / period) + 1.0) * period;
        const double first_target = next_vsync_after(now() + lead + (0.5 * period));
        std::vector<std::pair<std::size_t, double>> issued; // (probe index, target)
        for (std::size_t i = 0; i < probes.size(); ++i) {
            const Probe&         probe  = probes[i];
            const double         target = first_target + (spacing * static_cast<double>(i));
            const std::ptrdiff_t index  = presenter.present(target - probe.request_offset, target - probe.commit_margin);
            if (index < 0) {
                return false;
            }
            issued.emplace_back(static_cast<std::size_t>(index), target);
        }
        for (std::size_t i = 0; i < probes.size(); ++i) {
            const Probe& probe = probes[i];
            const auto [index, target] = issued[i];
            const Present_calibration_probe_feedback entry = presenter.wait(index, (target + (4.0 * period) + s_feedback_timeout) - now());
            Present_calibration_sample record{
                .phase             = probe.phase,
                .target_time       = target,
                .parameter         = (probe.phase == Present_calibration_phase::deadline) ? (target - entry.gpu_end) : probe.request_offset,
                .completion_margin = target - entry.gpu_end,
                .presented_time    = entry.presented_time,
                .slot_offset       = 0
            };
            if (entry.presented_time > 0.0) {
                record.slot_offset = static_cast<int>(std::lround((entry.presented_time - target) / period));
            }
            result.samples.push_back(record);
        }
        // Re-anchor the grid on the newest display (displays lie on it).
        for (auto it = result.samples.rbegin(); it != result.samples.rend(); ++it) {
            if (it->presented_time > 0.0) {
                grid_reference = it->presented_time;
                break;
            }
        }
        return (now() - start_time) <= s_wall_clock_bound;
    };

    // 2. Request window: complete early, sweep the request offset.
    {
        std::vector<Probe> probes;
        for (int pass = 0; pass < s_request_passes; ++pass) {
            for (int step = 0; step < s_request_steps; ++step) {
                probes.push_back(Probe{
                    .phase          = Present_calibration_phase::request_window,
                    .request_offset = 0.25 * static_cast<double>(step) * period,
                    .commit_margin  = s_request_lead_periods * period
                });
            }
        }
        if (!run_probes(probes)) {
            return fail("a probe failed or the wall clock bound was exceeded");
        }
    }
    // The longest run of request offsets whose samples displayed on target in
    // the majority of passes: the compositor displays stretches of probes a
    // refresh late at any offset (whole passes shifted by one on the Dell at
    // 120 Hz, at the start of the stream and in its middle), so an offset is
    // judged by most of its passes, not by every one.
    int best_first = -1;
    int best_count = 0;
    {
        int run_first = -1;
        for (int step = 0; step <= s_request_steps; ++step) {
            int samples        = 0;
            int samples_target = 0;
            for (const Present_calibration_sample& s : result.samples) {
                if ((s.phase != Present_calibration_phase::request_window) || (step >= s_request_steps)) {
                    continue;
                }
                if (std::abs(s.parameter - (0.25 * static_cast<double>(step) * period)) < (0.01 * period)) {
                    ++samples;
                    if ((s.presented_time > 0.0) && (s.slot_offset == 0) && (s.completion_margin >= (0.5 * s_request_lead_periods * period))) {
                        ++samples_target;
                    }
                }
            }
            const bool on_target = (2 * samples_target) > samples;
            if (on_target && (run_first < 0)) {
                run_first = step;
            }
            if (!on_target && (run_first >= 0)) {
                if ((step - run_first) > best_count) {
                    best_first = run_first;
                    best_count = step - run_first;
                }
                run_first = -1;
            }
        }
    }
    if (best_count == 0) {
        return fail("no request offset displayed on target");
    }
    result.request_min    = 0.25 * static_cast<double>(best_first) * period;
    result.request_max    = 0.25 * static_cast<double>(best_first + best_count - 1) * period;
    result.request_offset = 0.5 * (result.request_min + result.request_max);

    // 3. Deadline: request inside the window, sweep the completion margin;
    // a coarse pass, then a refine pass around the split.
    const auto deadline_probe = [&result](const double margin) -> Probe {
        return Probe{.phase = Present_calibration_phase::deadline, .request_offset = result.request_offset, .commit_margin = margin};
    };
    // The deadline samples the fit uses start here: a coarse pass the
    // compositor shifted as a whole stays in the samples (reported) but not
    // in the fit.
    std::size_t deadline_begin = result.samples.size();
    // The split: the margin above which samples display on target, chosen
    // to misclassify the fewest samples (late above it, on target below).
    const auto fit_split = [&result, &deadline_begin, period]() -> double {
        std::vector<std::pair<double, bool>> by_margin; // (margin, late)
        for (std::size_t i = deadline_begin; i < result.samples.size(); ++i) {
            const Present_calibration_sample& s = result.samples[i];
            if ((s.phase == Present_calibration_phase::deadline) && (s.presented_time > 0.0)) {
                by_margin.emplace_back(s.parameter, s.slot_offset > 0);
            }
        }
        std::sort(by_margin.begin(), by_margin.end());
        // Split before index i: samples [0, i) are below it.
        std::size_t best_split = 0;
        std::size_t best_error = by_margin.size() + 1;
        for (std::size_t i = 0; i <= by_margin.size(); ++i) {
            std::size_t error = 0;
            for (std::size_t j = 0; j < by_margin.size(); ++j) {
                const bool below = (j < i);
                if (below != by_margin[j].second) {
                    ++error;
                }
            }
            if (error < best_error) { // first (smallest) split on ties
                best_error = error;
                best_split = i;
            }
        }
        if (by_margin.empty()) {
            return -1.0;
        }
        if (best_split == 0) {
            return 0.0; // nothing late: the deadline lies below every margin tried
        }
        if (best_split == by_margin.size()) {
            return 4.0 * period; // everything late
        }
        return 0.5 * (by_margin[best_split - 1].first + by_margin[best_split].first);
    };
    // A coarse pass in which every displayed probe is late, up to 3 T of
    // margin, is most likely a shifted pass: the request window phase's
    // probes displayed on target at this request offset with 3.5 T of margin
    // (a real deadline in (3 T, 3.5 T] would fit both, and is measured again
    // the same way), and the compositor displays whole stretches of probes a
    // refresh late (seen in the request window phase
    // too, which is why that phase judges by the majority of its passes;
    // MoltenVK on a ProMotion panel, 1 launch in 10). Such a pass is
    // measured again, up to s_deadline_coarse_attempts times.
    int    discarded_passes = 0;
    double coarse_split     = -1.0;
    for (int attempt = 0; attempt < s_deadline_coarse_attempts; ++attempt) {
        deadline_begin = result.samples.size();
        std::vector<Probe> probes;
        for (int step = 0; step < s_deadline_steps; ++step) {
            probes.push_back(deadline_probe((0.5 + (0.125 * static_cast<double>(step))) * period));
        }
        if (!run_probes(probes)) {
            return fail("a probe failed or the wall clock bound was exceeded");
        }
        coarse_split = fit_split();
        if (coarse_split < (3.0 * period)) {
            break;
        }
        ++discarded_passes;
    }
    if (coarse_split < 0.0) {
        return fail("no deadline sample was displayed");
    }
    if ((coarse_split > 0.0) && (coarse_split < (3.0 * period))) {
        std::vector<Probe> probes;
        for (int pass = 0; pass < 2; ++pass) {
            for (int step = -s_deadline_refine_steps; step <= s_deadline_refine_steps; ++step) {
                const double margin = coarse_split + (0.125 * static_cast<double>(step) * period);
                if (margin > (0.25 * period)) {
                    probes.push_back(deadline_probe(margin));
                }
            }
        }
        if (!run_probes(probes)) {
            return fail("a probe failed or the wall clock bound was exceeded");
        }
    }
    const double split = fit_split();
    if (split >= (3.0 * period)) {
        return fail("every deadline sample up to 3 periods displayed late");
    }
    // Nothing late: the deadline lies below the smallest margin tried, which
    // stands in for it (an overestimate costs latency only).
    double smallest_margin = 3.0 * period;
    for (std::size_t i = deadline_begin; i < result.samples.size(); ++i) {
        const Present_calibration_sample& s = result.samples[i];
        if (s.phase == Present_calibration_phase::deadline) {
            smallest_margin = std::min(smallest_margin, s.parameter);
        }
    }
    result.deadline_offset = ((split > 0.0) ? split : smallest_margin) + s_guard;
    result.valid           = true;

    std::size_t early_in_deadline = 0;
    for (const Present_calibration_sample& s : result.samples) {
        if ((s.phase == Present_calibration_phase::deadline) && (s.presented_time > 0.0) && (s.slot_offset < 0)) {
            ++early_in_deadline;
        }
    }
    result.status = fmt::format(
        "period {:.3f} ms; request window {:.2f} ... {:.2f} T -> request at {:.3f} ms ({:.2f} T) before the target; "
        "deadline {:.3f} ms ({:.2f} T); {} samples, {} early in the deadline phase, {} coarse deadline passes late as a whole; {:.0f} ms",
        period * 1000.0,
        result.request_min / period, result.request_max / period,
        result.request_offset * 1000.0, result.request_offset / period,
        result.deadline_offset * 1000.0, result.deadline_offset / period,
        result.samples.size(), early_in_deadline, discarded_passes,
        (now() - start_time) * 1000.0
    );
    return nullptr;
}

} // namespace erhe::graphics
