#pragma once

// Deterministic simulated plant for the frame pacer parity suite
// (implementation plan step P1.2). Port of the plant in
// scripts/frame_pacing_sim.py: virtual clock, two-stage pipelined pipeline
// (F frames in flight, fence at k-F, serialized GPU), FIFO presentation
// engine quantized to a true vsync grid the pacer does not know, and
// causally-correct event delivery (nothing is visible to a decision made
// before its timestamp).

#include "erhe_frame_pacing/frame_pacer.hpp"
#include "erhe_frame_pacing/slop_servo_pacer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <random>
#include <vector>

namespace erhe::frame_pacing::test {

class Plant_parameters
{
public:
    Frame_pacer_tunables tunables{};
    int                  frames_in_flight{2};    // F (plant property)
    double               true_grid_phase {0.0031};
    double               true_deadline_offset{0.0015};
    // Claim C12: real grid period (<= 0: use tunables.refresh_period).
    // Real displays deviate from the queried period (measured 11 ppm on
    // the development machine).
    double               true_grid_period{0.0};
};

class Scenario
{
public:
    std::size_t                             frame_count{0};
    std::function<double(std::int64_t)>     cpu_duration; // service time [s]
    std::function<double(std::int64_t)>     gpu_duration; // service time [s]
    Plant_parameters                        plant{};
    // Claim C6: frames in [forced_first, forced_last] run with latency
    // estimate and margin forced to zero.
    bool                                    has_forced_range{false};
    std::int64_t                            forced_first{0};
    std::int64_t                            forced_last {0};
    // Claim C7: swapchain recreation before this frame (<0: none).
    std::int64_t                            recreate_at_frame{-1};
    // Claim C9: GPU measurements delivered via the lazy fence-read path
    // (frame k's data first visible at frame k+F's CPU start).
    bool                                    lazy_gpu_delivery{false};
    // Claim C11: observer mode - the app ignores the pacer's release times
    // and wait ids entirely (nothing is enforced).
    bool                                    unpaced{false};
    // Claim C12: frames in [blackout_first, blackout_last] never deliver
    // present feedback (hidden window / feedback loss).
    bool                                    has_feedback_blackout{false};
    std::int64_t                            blackout_first{0};
    std::int64_t                            blackout_last {0};
    // Claim C13: fifo_latest_ready presentation - when two images are ready
    // for the same vsync, the older is DROPPED (never displayed, no
    // feedback) instead of queued.
    bool                                    latest_ready{false};
    // Claim C13: enforce only the FR5 present-wait clamp, ignore release
    // times - the P2.2 integration state.
    bool                                    clamp_only{false};
    // Claim C15: the presentation engine IGNORES the requested target
    // present time (measured driver behavior, see
    // doc/frame_pacing_present_timing_driver_report.md): an image displays
    // at the earliest vsync at which it is available, never held back for
    // its target. (latest_ready is inert by construction.)
    bool                                    inert_target{false};
    // Claim C15 mitigation: the app delays the present REQUEST until one
    // period before the target (target - T + guard), so the earliest
    // feasible vsync IS the target even on an inert-target engine. Image
    // availability = max(gpu end, request time).
    bool                                    holdback{false};
    // Claim C17: scanout-side display skips - with this probability a
    // committed display slips one slot at scanout (availability unchanged,
    // present op on cadence, first-pixel-out +1 slot). Measured live: ~5%
    // of vblanks at both 23.976 and 120 Hz, uncorrelated with app timing.
    double                                  skip_probability{0.0};
    unsigned int                            skip_seed{9999};
    // Claim C17: deterministic variant - every slot whose index is a
    // multiple of this skips (the live skips are vblank-count-periodic,
    // ~every 21st).
    std::int64_t                            skip_period{0};
    // Claim C17: the present-wait clamp (satisfied times) resolves at the
    // UNSKIPPED slot - live, the present queue op executes on cadence and
    // only first-pixel-out slips, so vkWaitForPresentKHR may unblock a
    // slot before the actual display.
    bool                                    skip_clamp_on_cadence{false};
    // Claim C18: the holdback wait runs INLINE on the producer thread (the
    // erhe integration), so the next frame's decision cannot happen before
    // the previous frame's present request went out (found live: 120 Hz
    // N=3 4-slot cadence lock).
    bool                                    holdback_serialized{false};
    // Claim C18: fixed serial cost between the previous frame's present
    // request and the next decision (present + fence + observer work).
    double                                  tick_overhead{0.0};
    // P4.1: plain-FIFO swapchain image count. > 0 adds acquire
    // backpressure: frame k's CPU start blocks until frame k-(images-1)
    // has DISPLAYED - the queue-full blocking the slop servo senses.
    // (0 = unmodeled, pre-P4.1 plants.)
    int                                     fifo_images{0};
    // Claims C19..C21: release comes from the tier S slop-servo sleep
    // instead of the Frame_pacer decision; the presentation target is
    // ignored (plain FIFO displays at the earliest feasible vsync).
    bool                                    slop_servo{false};
    Slop_servo_tunables                     servo_tunables{};
};

class Frame_record
{
public:
    std::int64_t frame_id   {0};
    int          vsyncs     {1};
    double       decision_time{0.0};
    double       release    {0.0};
    double       target_time{0.0};
    double       cpu_start  {0.0};
    double       gpu_end    {0.0};
    double       display_time{0.0};
    std::int64_t display_slot{0};
    bool         self_miss  {false};
    bool         slipped    {false};
    int          pending    {0};
    double       w_statistic{0.0};
    double       margin     {0.0};
    double       slop       {0.0};  // P4.1: involuntary blocking this frame
    double       sleep      {0.0};  // P4.1: servo sleep applied this frame
};

class Plant_event
{
public:
    enum class Kind : int { cpu = 0, gpu = 1, present = 2 };

    double       time{0.0};
    std::int64_t sequence{0};
    Kind         kind{Kind::cpu};
    std::int64_t frame_id{0};
    double       gpu_end {0.0};
    double       cpu_dur {0.0};
    double       gpu_dur {0.0};
    double       latency {0.0};
    double       present_time{0.0};
};

class Plant_event_order
{
public:
    auto operator()(const Plant_event& lhs, const Plant_event& rhs) const -> bool
    {
        if (lhs.time != rhs.time) {
            return lhs.time > rhs.time; // min-heap by time
        }
        return lhs.sequence > rhs.sequence;
    }
};

inline auto run_scenario(const Scenario& scenario, Frame_pacer& pacer) -> std::vector<Frame_record>
{
    const Frame_pacer_tunables& tun     = scenario.plant.tunables;
    const double                t       = tun.refresh_period;
    const int                   fif     = scenario.plant.frames_in_flight;
    const double                phase   = scenario.plant.true_grid_phase;
    const double                dpe     = scenario.plant.true_deadline_offset;

    std::priority_queue<Plant_event, std::vector<Plant_event>, Plant_event_order> events;
    std::int64_t sequence = 0;

    const std::size_t n = scenario.frame_count;
    std::vector<double>      cpu_end     (n, 0.0);
    std::vector<double>      gpu_end     (n, 0.0);
    std::vector<double>      display_time(n, 0.0);
    std::vector<Plant_event> pending_gpu (n);      // lazy_gpu_delivery staging
    std::vector<bool>        has_pending (n, false);
    std::vector<Frame_record> records;
    records.reserve(n);

    const auto deliver = [&](const double upto) {
        while (!events.empty() && (events.top().time <= upto)) {
            const Plant_event event = events.top();
            events.pop();
            switch (event.kind) {
                case Plant_event::Kind::cpu: {
                    pacer.on_cpu_done(event.frame_id, event.cpu_dur);
                    break;
                }
                case Plant_event::Kind::gpu: {
                    pacer.on_gpu_done(event.frame_id, event.gpu_end, event.cpu_dur, event.gpu_dur, event.latency);
                    break;
                }
                case Plant_event::Kind::present: {
                    pacer.on_present_feedback(event.frame_id, event.present_time);
                    break;
                }
            }
        }
    };

    const auto push = [&](Plant_event event) {
        event.sequence = sequence++;
        events.push(event);
    };

    std::int64_t prev_display_slot = -1000000000;
    double       decision_time     = 0.0;
    Slop_servo_pacer servo{scenario.servo_tunables, t};

    // latest_ready presentation state: one image can sit ready-but-unshown;
    // a newer image ready for the same slot supersedes (drops) it.
    // satisfied_time[i] = time a present with id >= i reached the display
    // (vkWaitForPresentKHR semantics: any newer id unblocks older waits).
    bool         has_pending_display = false;
    std::int64_t pending_display_id  = 0;
    std::int64_t pending_display_slot= 0;
    std::mt19937 skip_engine{scenario.skip_seed};
    std::uniform_real_distribution<double> skip_uniform{0.0, 1.0};
    const auto maybe_skip = [&](const std::int64_t slot) -> std::int64_t {
        // C17: scanout-side skip - the present op stays on cadence but
        // first-pixel-out slips one slot. Applied at display commit only
        // (dropped images never reach scanout).
        if ((scenario.skip_period > 0) && ((slot % scenario.skip_period) == 0)) {
            return slot + 1;
        }
        if ((scenario.skip_probability > 0.0) && (skip_uniform(skip_engine) < scenario.skip_probability)) {
            return slot + 1;
        }
        return slot;
    };
    std::vector<double> satisfied_time(n, 0.0);
    std::int64_t satisfied_upto = -1;
    const auto resolve_satisfied = [&](const std::int64_t upto_id, const double time) {
        for (std::int64_t idx = satisfied_upto + 1; idx <= upto_id; ++idx) {
            satisfied_time[static_cast<std::size_t>(idx)] = time;
        }
        satisfied_upto = std::max(satisfied_upto, upto_id);
    };

    for (std::int64_t k = 0; k < static_cast<std::int64_t>(n); ++k) {
        if (scenario.recreate_at_frame >= 0 && k == scenario.recreate_at_frame) {
            pacer.notify_swapchain_recreated();
        }
        deliver(decision_time);
        const bool forced =
            scenario.has_forced_range &&
            (k >= scenario.forced_first) &&
            (k <= scenario.forced_last);
        pacer.set_forced_override(forced, 0.0, 0.0);

        const Schedule_decision decision = pacer.schedule(k, decision_time);

        double release_effective = decision_time; // observer mode: enforce nothing
        if (scenario.slop_servo) {
            // Tier S: the servo sleep is the only release control; the
            // Frame_pacer's decision (still computed above) is ignored.
            release_effective = decision_time + servo.get_sleep();
        } else if (!scenario.unpaced) {
            double wait_time = 0.0;
            if ((decision.wait_id >= 0) && (decision.wait_id < k)) {
                wait_time = scenario.latest_ready
                    ? satisfied_time[static_cast<std::size_t>(decision.wait_id)]
                    : display_time[static_cast<std::size_t>(decision.wait_id)];
            }
            release_effective = scenario.clamp_only
                ? std::max(decision_time, wait_time) // P2.2: clamp enforced, release not
                : std::max({decision.release_time, decision_time, wait_time});
        }

        const double cpu_dur = scenario.cpu_duration(k);
        const double gpu_dur = scenario.gpu_duration(k);

        double cpu_start = release_effective;
        if (k >= fif) {
            cpu_start = std::max(cpu_start, gpu_end[static_cast<std::size_t>(k - fif)]);
        }
        if ((scenario.fifo_images > 0) && (k >= scenario.fifo_images - 1)) {
            // Plain-FIFO backpressure (P4.1): with M swapchain images, the
            // acquire blocks until the image used M-1 frames ago has left
            // the display queue. This is the blocking a slop servo senses.
            cpu_start = std::max(cpu_start, display_time[static_cast<std::size_t>(k - (scenario.fifo_images - 1))]);
        }
        const double slop = cpu_start - release_effective; // involuntary blocking
        if (scenario.lazy_gpu_delivery && (k >= fif) && has_pending[static_cast<std::size_t>(k - fif)]) {
            // Lazy read path: frame k-F's GPU results become visible at the
            // fence wait during frame k's CPU start.
            Plant_event event = pending_gpu[static_cast<std::size_t>(k - fif)];
            event.time = cpu_start;
            push(event);
            has_pending[static_cast<std::size_t>(k - fif)] = false;
        }
        const double ce = cpu_start + cpu_dur;
        cpu_end[static_cast<std::size_t>(k)] = ce;
        {
            Plant_event event;
            event.time     = ce;
            event.kind     = Plant_event::Kind::cpu;
            event.frame_id = k;
            event.cpu_dur  = cpu_dur;
            push(event);
        }

        double gpu_start = ce;
        if (k >= 1) {
            gpu_start = std::max(gpu_start, gpu_end[static_cast<std::size_t>(k - 1)]);
        }
        const double ge = gpu_start + gpu_dur;
        gpu_end[static_cast<std::size_t>(k)] = ge;
        {
            // cpu_dur / gpu_dur are pure stage service times (waits excluded);
            // normative, see doc/frame_pacing_inputs.md sections 3.2/3.3.
            Plant_event event;
            event.time     = ge;
            event.kind     = Plant_event::Kind::gpu;
            event.frame_id = k;
            event.gpu_end  = ge;
            event.cpu_dur  = cpu_dur;
            event.gpu_dur  = gpu_dur;
            event.latency  = ge - release_effective;
            if (scenario.lazy_gpu_delivery) {
                pending_gpu[static_cast<std::size_t>(k)] = event;
                has_pending[static_cast<std::size_t>(k)] = true;
            } else {
                push(event);
            }
        }

        // Presentation engine, quantized to the true vsync grid (period tp,
        // which the pacer does NOT know exactly - C12) with deadline offset
        // dpe. FIFO by default; latest_ready (C13) drops a ready-but-unshown
        // image when a newer one is ready for the same vsync.
        const double tp = (scenario.plant.true_grid_period > 0.0) ? scenario.plant.true_grid_period : t;
        // An image is AVAILABLE to the engine once its GPU work is done AND
        // its present request has been made (at CPU end; the C15 holdback
        // mitigation delays the request to one period before the target).
        double request_time = ce;
        if (scenario.holdback) {
            request_time = std::max(ce, decision.hold_until);
        }
        const double available = std::max(ge, request_time);
        const bool feedback_blacked_out =
            scenario.has_feedback_blackout &&
            (k >= scenario.blackout_first) &&
            (k <= scenario.blackout_last);
        const auto push_present = [&](const std::int64_t frame_id, const double time) {
            if (!feedback_blacked_out) {
                Plant_event event;
                event.time         = time;
                event.kind         = Plant_event::Kind::present;
                event.frame_id     = frame_id;
                event.present_time = time;
                push(event);
            }
        };
        if (scenario.latest_ready) {
            const std::int64_t earliest =
                static_cast<std::int64_t>(std::ceil((available + dpe - phase) / tp));
            if (has_pending_display) {
                const std::int64_t candidate = std::max(prev_display_slot + 1, earliest);
                if (candidate <= pending_display_slot) {
                    // k is ready by the pending image's slot: supersede it.
                    // Waits on the dropped id unblock when the display
                    // passes that slot (a newer id presents there).
                    resolve_satisfied(pending_display_id, phase + static_cast<double>(pending_display_slot) * tp);
                } else {
                    // Commit the pending image (C17: scanout may skip; the
                    // clamp may resolve at the unskipped on-cadence slot).
                    const std::int64_t shown_slot = maybe_skip(pending_display_slot);
                    const double pending_dt = phase + static_cast<double>(shown_slot) * tp;
                    display_time[static_cast<std::size_t>(pending_display_id)] = pending_dt;
                    prev_display_slot = shown_slot;
                    const double clamp_dt = scenario.skip_clamp_on_cadence
                        ? (phase + static_cast<double>(pending_display_slot) * tp)
                        : pending_dt;
                    resolve_satisfied(pending_display_id, clamp_dt);
                    push_present(pending_display_id, pending_dt);
                }
            }
            has_pending_display  = true;
            pending_display_id   = k;
            pending_display_slot = std::max(prev_display_slot + 1, earliest);
        } else {
            std::int64_t display_slot = prev_display_slot + 1;
            // Slop-servo mode (P4.1): plain FIFO has no target at all -
            // same earliest-feasible display as an inert-target engine.
            if (!scenario.inert_target && !scenario.slop_servo) {
                display_slot = std::max(
                    display_slot,
                    static_cast<std::int64_t>(std::ceil((decision.target_time - (0.25 * t) - phase) / tp))
                );
            }
            while ((phase + (static_cast<double>(display_slot) * tp) - dpe) < available) {
                ++display_slot;
            }
            display_slot = maybe_skip(display_slot); // C17: scanout may skip
            const double dt = phase + (static_cast<double>(display_slot) * tp);
            display_time[static_cast<std::size_t>(k)] = dt;
            prev_display_slot = display_slot;
            push_present(k, dt);
        }

        int pending = 1;
        for (std::int64_t j = std::max<std::int64_t>(0, k - 8); j < k; ++j) {
            if (display_time[static_cast<std::size_t>(j)] > ce) {
                ++pending;
            }
        }

        // latest_ready: the frame's display (if any) commits later; 0 now,
        // backfilled after the loop. Dropped frames stay at 0.
        const double rec_dt = display_time[static_cast<std::size_t>(k)];

        Frame_record record;
        record.frame_id      = k;
        record.vsyncs        = decision.vsyncs_per_frame;
        record.decision_time = decision_time;
        record.release       = release_effective;
        record.target_time   = decision.target_time;
        record.cpu_start     = cpu_start;
        record.gpu_end       = ge;
        record.display_time  = rec_dt;
        record.display_slot  = prev_display_slot;
        record.slipped       = rec_dt > (decision.target_time + (0.5 * t));
        record.pending       = pending;
        record.slop          = slop;
        record.sleep         = scenario.slop_servo ? servo.get_sleep() : 0.0;
        records.push_back(record);

        const double previous_decision_time = decision_time;
        decision_time = ce;
        if (scenario.holdback_serialized) {
            // The C15 holdback runs inline on the producer thread: the next
            // tick starts only after this frame's present request went out.
            decision_time = std::max(decision_time, request_time) + scenario.tick_overhead;
        }
        if (scenario.slop_servo) {
            // Loop duration = decision-to-decision: sleep + work + blocking.
            servo.update(slop, decision_time - previous_decision_time);
        }
    }

    if (scenario.latest_ready && has_pending_display) {
        const double tp_last = (scenario.plant.true_grid_period > 0.0) ? scenario.plant.true_grid_period : t;
        const double pending_dt = phase + static_cast<double>(pending_display_slot) * tp_last;
        display_time[static_cast<std::size_t>(pending_display_id)] = pending_dt;
        resolve_satisfied(pending_display_id, pending_dt);
        Plant_event event;
        event.time         = pending_dt;
        event.kind         = Plant_event::Kind::present;
        event.frame_id     = pending_display_id;
        event.present_time = pending_dt;
        push(event);
    }
    for (std::size_t k = 0; k < n; ++k) {
        if (has_pending[k]) {
            Plant_event event = pending_gpu[k];
            event.time = event.gpu_end;
            push(event);
            has_pending[k] = false;
        }
    }
    deliver(std::numeric_limits<double>::infinity());

    for (Frame_record& record : records) {
        const Frame_stats stats = pacer.get_frame_stats(record.frame_id);
        if (stats.valid) {
            record.self_miss   = stats.self_miss;
            record.w_statistic = stats.w_statistic;
            record.margin      = stats.margin;
        }
        if (scenario.latest_ready) {
            // Backfill display times committed after the frame's own turn.
            record.display_time = display_time[static_cast<std::size_t>(record.frame_id)];
            record.slipped      = record.display_time > (record.target_time + (0.5 * t));
        }
    }
    return records;
}

// Virtual time from frame from_frame until vsyncs == expected and no
// self-miss for quiet_time of virtual time; negative if never reached.
inline auto settle_time(
    const std::vector<Frame_record>& records,
    const std::size_t                from_frame,
    const int                        expected_vsyncs,
    const double                     quiet_time = 0.5
) -> double
{
    const double start = records[from_frame].release;
    bool   in_quiet    = false;
    double quiet_start = 0.0;
    for (std::size_t i = from_frame; i < records.size(); ++i) {
        const Frame_record& record = records[i];
        const bool ok = (record.vsyncs == expected_vsyncs) && !record.self_miss;
        if (ok) {
            if (!in_quiet) {
                in_quiet    = true;
                quiet_start = record.gpu_end;
            }
            if ((record.gpu_end - quiet_start) >= quiet_time) {
                return quiet_start - start;
            }
        } else {
            in_quiet = false;
        }
    }
    return -1.0;
}

} // namespace erhe::frame_pacing::test
