// Present calibration at device init (doc/plans/frame_pacing_metal.md
// "Present calibration at device init"): clear-only drawables on the
// window's CAMetalLayer, presented one at a time before the application's
// first frame, measure the two compositor properties tier M depends on and
// which differ by display mode:
//  - the request window: which presentDrawable:atTime: request times, for an
//    image complete well ahead of its target vsync V, display at V (on a
//    fixed-rate display V - 0.75T ... V; on a ProMotion panel a period
//    earlier);
//  - the deadline: how long before V an image must be complete to display
//    at V when requested inside that window.
// Probes run as a steady stream, one every second vsync, so a probe a
// refresh late or early does not compete with its neighbour for a vsync.
//
// Runs synchronously on the init thread; presented and completed handlers
// arrive on Metal's own threads and are collected under a mutex.

#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/metal/metal_gpu_prewarm.hpp"
#include "erhe_graphics/metal/metal_surface.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/present_calibration.hpp"
#include "erhe_graphics/scoped_transient_object_pool.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_frame_pacing/frame_time_recorder.hpp"

#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace erhe::graphics {

namespace {

constexpr std::size_t s_grid_presents         = 30;    // consecutive displayed plain presents (also outlasts the window-open animation)
constexpr int         s_request_steps         = 11;    // request offsets 0 ... 2.5 T in steps of T/4
constexpr int         s_request_passes        = 3;
constexpr double      s_request_lead_periods  = 3.5;   // request-window samples complete at least this early
constexpr int         s_deadline_steps        = 21;    // completion margins T/2 ... 3 T in steps of T/8
constexpr int         s_deadline_refine_steps = 3;     // refine pass: this many T/8 steps either side of the split
constexpr double      s_guard                 = 0.001; // seconds, added to the measured deadline
constexpr double      s_feedback_timeout      = 0.25;  // seconds past the expected display
constexpr double      s_wall_clock_bound      = 10.0;  // seconds for the whole calibration
constexpr std::uint32_t s_prewarm_rate_iterations = 20000;
constexpr double      s_window_shown_bound    = 2.0;   // seconds for the window to get displayed

auto now() -> double
{
    return erhe::frame_pacing::Frame_time_recorder::now();
}

// Sleeps to within a millisecond of the time, then spins: commit times must
// be accurate to well under the T/8 margin step.
void wait_until(const double time)
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

class Calibration_feedback
{
public:
    class Entry
    {
    public:
        double presented_time{0.0};
        double gpu_end       {0.0};
        bool   presented     {false}; // the presented handler ran (presented_time 0.0: not displayed)
    };

    auto add() -> std::size_t
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_entries.emplace_back();
        return m_entries.size() - 1;
    }
    void set_presented(const std::size_t index, const double presented_time)
    {
        {
            const std::lock_guard<std::mutex> lock{m_mutex};
            m_entries[index].presented_time = presented_time;
            m_entries[index].presented      = true;
        }
        m_condition.notify_all();
    }
    void set_gpu_end(const std::size_t index, const double gpu_end)
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_entries[index].gpu_end = gpu_end;
    }
    // The entry once presented, or as it stands at the timeout.
    auto wait(const std::size_t index, const double timeout_seconds) -> Entry
    {
        std::unique_lock<std::mutex> lock{m_mutex};
        m_condition.wait_for(
            lock,
            std::chrono::duration<double>(std::max(0.0, timeout_seconds)),
            [this, index]() { return m_entries[index].presented; }
        );
        return m_entries[index];
    }

private:
    std::mutex              m_mutex;
    std::condition_variable m_condition;
    std::vector<Entry>      m_entries; // handlers index into it under m_mutex
};

} // namespace

void Device_impl::run_present_calibration()
{
    Present_calibration result{};
    result.done = true;

    const auto finish = [this, &result](const char* const reason) {
        if (reason != nullptr) {
            result.valid  = false;
            result.status = reason;
            log_display_refresh->warn("present calibration: {}; tier M requests at T/4 before the target and estimates the deadline from 2.5 ms", reason);
        }
        m_present_request_offset = result.valid ? result.request_offset : 0.0;
        m_device.set_present_calibration(result);
    };

    // 0. GPU pre-warm kernel rate (claim C26), first: it does not depend on
    // the compositor, so a calibration that stops early still enables the
    // pre-warm. The fill that raises the clock is learned online by the
    // frame pacer: with clear-only frames the governor stays at its lowest
    // state even under 8 ms of ALU work per frame, while on top of the
    // editor's real frames 2.3 ms raised it to ~0.97 - the governor responds
    // to the real workload.
    if (m_gpu_prewarm != nullptr) {
        for (int i = 0; i < 3; ++i) {
            static_cast<void>(m_gpu_prewarm->run_blocking(s_prewarm_rate_iterations));
        }
        result.prewarm_seconds_per_iteration = m_gpu_prewarm->get_seconds_per_iteration();
    }

    CA::MetalLayer* const layer          = (m_surface != nullptr) ? m_surface->get_impl().get_metal_layer() : nullptr;
    const double          nominal_period = m_device.get_display_refresh_duration_seconds();
    if ((layer == nullptr) || (nominal_period <= 0.0)) {
        finish("no CAMetalLayer or no display refresh period");
        return;
    }

    std::shared_ptr<Calibration_feedback> feedback = std::make_shared<Calibration_feedback>();
    const double start_time = now();

    // Presents one clear-only drawable: at request_time (0.0: plain
    // present), committed at commit_time (0.0: now). Returns the feedback
    // index, or -1 when no drawable came.
    const auto present_one = [this, layer, &feedback](const double request_time, const double commit_time) -> std::ptrdiff_t {
        Scoped_transient_object_pool object_pool{};
        CA::MetalDrawable* const drawable = layer->nextDrawable();
        if (drawable == nullptr) {
            return -1;
        }
        MTL::CommandBuffer* const command_buffer = m_mtl_command_queue->commandBuffer();
        if (command_buffer == nullptr) {
            return -1;
        }
        command_buffer->setLabel(NS::String::string("present calibration", NS::UTF8StringEncoding));
        MTL::RenderPassDescriptor* const render_pass = MTL::RenderPassDescriptor::renderPassDescriptor();
        MTL::RenderPassColorAttachmentDescriptor* const color = render_pass->colorAttachments()->object(0);
        color->setTexture(drawable->texture());
        color->setLoadAction(MTL::LoadActionClear);
        color->setClearColor(MTL::ClearColor{0.0, 0.0, 0.0, 1.0});
        color->setStoreAction(MTL::StoreActionStore);
        MTL::RenderCommandEncoder* const encoder = command_buffer->renderCommandEncoder(render_pass);
        encoder->endEncoding();

        const std::size_t index = feedback->add();
        std::shared_ptr<Calibration_feedback> presented_feedback = feedback;
        drawable->addPresentedHandler(
            [presented_feedback, index](MTL::Drawable* presented) {
                presented_feedback->set_presented(index, presented->presentedTime());
            }
        );
        std::shared_ptr<Calibration_feedback> completed_feedback = feedback;
        command_buffer->addCompletedHandler(
            [completed_feedback, index](MTL::CommandBuffer* completed) {
                completed_feedback->set_gpu_end(index, completed->GPUEndTime());
            }
        );
        if (request_time > 0.0) {
            command_buffer->presentDrawableAtTime(drawable, request_time);
        } else {
            command_buffer->presentDrawable(drawable);
        }
        if (commit_time > 0.0) {
            wait_until(commit_time);
        }
        command_buffer->commit();
        return static_cast<std::ptrdiff_t>(index);
    };

    // 1. Grid: back-to-back plain presents; consecutive displays are whole
    // periods apart. A window ordered on screen during init is not yet
    // composited, so its first presents are not displayed: present until
    // s_grid_presents consecutive ones are, within s_window_shown_bound.
    std::vector<double> displayed;
    {
        std::size_t checked = 0;
        while (displayed.size() < s_grid_presents) {
            if ((now() - start_time) > s_window_shown_bound) {
                finish("no presentation feedback (window occluded or not yet shown?)");
                return;
            }
            m_surface->get_impl().service_main_run_loop();
            const std::ptrdiff_t index = present_one(0.0, 0.0);
            if (index < 0) {
                finish("nextDrawable or commandBuffer failed");
                return;
            }
            // Judge the presents whose feedback is due (3 in the drawable pool).
            while ((checked + 3) <= static_cast<std::size_t>(index)) {
                const Calibration_feedback::Entry entry = feedback->wait(checked, 1.0);
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
        finish("no presentation feedback (window occluded or not yet shown?)");
        return;
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
    // displayed late again at margins from ~1.9 T on (Dell at 120 Hz), which
    // is not deadline evidence. Probe i targets V_i, requests at
    // V_i - request_offset and commits at V_i - commit_margin. A steadily
    // fed compositor is what tier M sees; a probe waiting for its predecessor's display measured a compositor
    // returning from idle (late displays at any margin). The feedback is
    // collected once the batch has been displayed. False on a failure the
    // calibration cannot continue past.
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
        std::vector<std::pair<std::size_t, double>> issued; // (feedback index, target)
        for (std::size_t i = 0; i < probes.size(); ++i) {
            const Probe&   probe  = probes[i];
            const double   target = first_target + (spacing * static_cast<double>(i));
            const std::ptrdiff_t index = present_one(target - probe.request_offset, target - probe.commit_margin);
            if (index < 0) {
                return false;
            }
            issued.emplace_back(static_cast<std::size_t>(index), target);
        }
        for (std::size_t i = 0; i < probes.size(); ++i) {
            const Probe& probe = probes[i];
            const auto [index, target] = issued[i];
            const Calibration_feedback::Entry entry = feedback->wait(index, (target + (4.0 * period) + s_feedback_timeout) - now());
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
            finish("a probe failed or the wall clock bound was exceeded");
            return;
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
        finish("no request offset displayed on target");
        return;
    }
    result.request_min    = 0.25 * static_cast<double>(best_first) * period;
    result.request_max    = 0.25 * static_cast<double>(best_first + best_count - 1) * period;
    result.request_offset = 0.5 * (result.request_min + result.request_max);

    // 3. Deadline: request inside the window, sweep the completion margin;
    // a coarse pass, then a refine pass around the split.
    const auto deadline_probe = [&result, period](const double margin) -> Probe {
        return Probe{.phase = Present_calibration_phase::deadline, .request_offset = result.request_offset, .commit_margin = margin};
    };
    {
        std::vector<Probe> probes;
        for (int step = 0; step < s_deadline_steps; ++step) {
            probes.push_back(deadline_probe((0.5 + (0.125 * static_cast<double>(step))) * period));
        }
        if (!run_probes(probes)) {
            finish("a probe failed or the wall clock bound was exceeded");
            return;
        }
    }
    // The split: the margin above which samples display on target, chosen
    // to misclassify the fewest samples (late above it, on target below).
    const auto fit_split = [&result, period]() -> double {
        std::vector<std::pair<double, bool>> by_margin; // (margin, late)
        for (const Present_calibration_sample& s : result.samples) {
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
    const double coarse_split = fit_split();
    if (coarse_split < 0.0) {
        finish("no deadline sample was displayed");
        return;
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
            finish("a probe failed or the wall clock bound was exceeded");
            return;
        }
    }
    const double split = fit_split();
    if (split >= (3.0 * period)) {
        finish("every deadline sample up to 3 periods displayed late");
        return;
    }
    // Nothing late: the deadline lies below the smallest margin tried, which
    // stands in for it (an overestimate costs latency only).
    double smallest_margin = 3.0 * period;
    for (const Present_calibration_sample& s : result.samples) {
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
        "deadline {:.3f} ms ({:.2f} T); GPU pre-warm kernel {:.1f} ns per iteration; {} samples, {} early in the deadline phase; {:.0f} ms",
        period * 1000.0,
        result.request_min / period, result.request_max / period,
        result.request_offset * 1000.0, result.request_offset / period,
        result.deadline_offset * 1000.0, result.deadline_offset / period,
        result.prewarm_seconds_per_iteration * 1.0e9,
        result.samples.size(), early_in_deadline,
        (now() - start_time) * 1000.0
    );
    log_display_refresh->info("present calibration: {}", result.status);
    finish(nullptr);
}

} // namespace erhe::graphics
