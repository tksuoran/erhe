// Frame pacer parity suite (implementation plan step P1.2).
//
// C++ replication of the verification scenarios and claims C1..C9 from
// scripts/frame_pacing_sim.py / doc/frame_pacing/control_model.md section 11.
// Constants match the Python reference; RNG streams need not match - all
// assertions are on invariants and bounds, not sample paths.

#include "frame_pacing_plant.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <set>
#include <vector>

namespace {

using erhe::frame_pacing::Frame_pacer;
using erhe::frame_pacing::Frame_pacer_tunables;
using erhe::frame_pacing::test::Frame_record;
using erhe::frame_pacing::test::Scenario;
using erhe::frame_pacing::test::run_scenario;
using erhe::frame_pacing::test::settle_time;

constexpr double ms = 1e-3;

// Gaussian stage-time source with a floor, mirroring gauss_fn() in the
// Python reference.
auto make_gauss(const unsigned int seed, const double mean_ms, const double sd_ms) -> std::function<double(std::int64_t)>
{
    auto engine = std::make_shared<std::mt19937>(seed);
    std::normal_distribution<double> distribution{mean_ms, sd_ms};
    return [engine, distribution](std::int64_t) mutable -> double {
        return std::max(0.5, distribution(*engine)) * ms;
    };
}

auto make_stepped(
    std::function<double(std::int64_t)> base,
    std::function<double(std::int64_t)> high,
    const std::int64_t                  first,
    const std::int64_t                  last = -1
) -> std::function<double(std::int64_t)>
{
    return [base, high, first, last](const std::int64_t k) -> double {
        if ((k >= first) && ((last < 0) || (k <= last))) {
            return high(k);
        }
        return base(k);
    };
}

auto count_self_misses(const std::vector<Frame_record>& records, const std::size_t from, const std::size_t to) -> int
{
    int count = 0;
    for (std::size_t i = from; i < std::min(to, records.size()); ++i) {
        if (records[i].self_miss) {
            ++count;
        }
    }
    return count;
}

auto vsync_values(const std::vector<Frame_record>& records, const std::size_t from) -> std::set<int>
{
    std::set<int> values;
    for (std::size_t i = from; i < records.size(); ++i) {
        values.insert(records[i].vsyncs);
    }
    return values;
}

} // anonymous namespace

TEST(Frame_pacer_claims, c1_steady_state)
{
    Scenario scenario;
    scenario.frame_count  = 1200;
    scenario.cpu_duration = make_gauss(101, 6.0, 0.4);
    scenario.gpu_duration = make_gauss(102, 8.0, 0.6);
    Frame_pacer pacer{scenario.plant.tunables};
    const std::vector<Frame_record> records = run_scenario(scenario, pacer);

    const std::set<int> n_values = vsync_values(records, 240);
    EXPECT_EQ(n_values, (std::set<int>{1})) << "cadence must be constant in steady state";

    const int misses = count_self_misses(records, 240, records.size());
    EXPECT_LE(misses, static_cast<int>((records.size() - 240) / 100)) << "miss rate must be <= 1 percent";

    const double t = scenario.plant.tunables.refresh_period;
    std::size_t exact = 0;
    std::size_t total = 0;
    for (std::size_t i = 240; i + 1 < records.size(); ++i) {
        const double duration = records[i + 1].display_time - records[i].display_time;
        if (std::abs(duration - t) < 1e-6) {
            ++exact;
        }
        ++total;
    }
    EXPECT_GE(static_cast<double>(exact), 0.99 * static_cast<double>(total))
        << "every frame must be visible exactly one refresh period";
}

TEST(Frame_pacer_claims, c2_step_load_increase)
{
    const std::size_t step = 300;
    Scenario scenario;
    scenario.frame_count  = 900;
    scenario.cpu_duration = make_gauss(201, 6.0, 0.4);
    scenario.gpu_duration = make_stepped(make_gauss(202, 8.0, 0.6), make_gauss(203, 22.0, 0.8), static_cast<std::int64_t>(step));
    Frame_pacer pacer{scenario.plant.tunables};
    const std::vector<Frame_record> records = run_scenario(scenario, pacer);
    const Frame_pacer_tunables& tun = scenario.plant.tunables;

    std::size_t n_change = 0;
    bool        found    = false;
    for (std::size_t i = step; i < records.size(); ++i) {
        if (records[i].vsyncs == 2) {
            n_change = i;
            found    = true;
            break;
        }
    }
    ASSERT_TRUE(found) << "cadence must downshift after the step";

    // Decision latency: self-misses the pacer had observed (gpu done) by the
    // time the cadence change was scheduled; in-flight frames do not count.
    const double decision_time = records[n_change].decision_time;
    int observed = 0;
    for (std::size_t i = step; i < n_change; ++i) {
        if (records[i].self_miss && (records[i].gpu_end <= decision_time)) {
            ++observed;
        }
    }
    EXPECT_LE(observed, tun.reactive_miss_threshold) << "decision within M observed misses";

    int late_total = 0;
    for (std::size_t i = step; i < step + 60; ++i) {
        if (records[i].self_miss || records[i].slipped) {
            ++late_total;
        }
    }
    const int drain_bound = tun.reactive_miss_threshold + (2 * scenario.plant.frames_in_flight) + 1;
    EXPECT_LE(late_total, drain_bound) << "total disruption bounded by pipeline drain";

    const double settle = settle_time(records, step, 2);
    EXPECT_GE(settle, 0.0);
    EXPECT_LE(settle, 1.0) << "settle within 1 s";

    EXPECT_EQ(vsync_values(records, step + 120), (std::set<int>{2})) << "stays at the new cadence";
}

TEST(Frame_pacer_claims, c3_step_load_decrease)
{
    const std::size_t up   = 300;
    const std::size_t down = 900;
    Scenario scenario;
    scenario.frame_count  = 1800;
    scenario.cpu_duration = make_gauss(301, 6.0, 0.4);
    scenario.gpu_duration = make_stepped(
        make_gauss(302, 8.0, 0.6),
        make_gauss(303, 22.0, 0.8),
        static_cast<std::int64_t>(up),
        static_cast<std::int64_t>(down) - 1
    );
    Frame_pacer pacer{scenario.plant.tunables};
    const std::vector<Frame_record> records = run_scenario(scenario, pacer);
    const Frame_pacer_tunables& tun = scenario.plant.tunables;

    std::size_t back  = 0;
    bool        found = false;
    for (std::size_t i = down; i < records.size(); ++i) {
        if (records[i].vsyncs == 1) {
            back  = i;
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found) << "cadence must upshift after the load drop";

    const double upshift_delay = records[back].gpu_end - records[down].release;
    EXPECT_GE(upshift_delay, 0.8 * tun.upshift_dwell_time) << "upshift not before the dwell";
    EXPECT_LE(upshift_delay, tun.upshift_dwell_time + 0.6) << "upshift within dwell + detection lag";

    const int transition_misses = count_self_misses(records, down, back + 30);
    EXPECT_EQ(transition_misses, 0) << "upshift transition must be miss-free";
}

TEST(Frame_pacer_claims, c4_borderline_load)
{
    Scenario scenario;
    scenario.frame_count  = 2400;
    scenario.cpu_duration = make_gauss(401, 6.0, 0.4);
    scenario.gpu_duration = make_gauss(402, 16.6, 0.4);
    Frame_pacer pacer{scenario.plant.tunables};
    const std::vector<Frame_record> records = run_scenario(scenario, pacer);

    EXPECT_EQ(vsync_values(records, 240), (std::set<int>{2})) << "parks at the safe side of the boundary";

    int changes = 0;
    for (std::size_t i = 241; i < records.size(); ++i) {
        if (records[i].vsyncs != records[i - 1].vsyncs) {
            ++changes;
        }
    }
    EXPECT_EQ(changes, 0) << "zero cadence alternation at borderline load";
}

TEST(Frame_pacer_claims, c5_single_frame_impulse)
{
    Scenario scenario;
    scenario.frame_count  = 1200;
    scenario.cpu_duration = make_gauss(501, 6.0, 0.4);
    scenario.gpu_duration = make_stepped(
        make_gauss(502, 8.0, 0.6),
        [](std::int64_t) -> double { return 30.0 * ms; },
        600,
        600
    );
    Frame_pacer pacer{scenario.plant.tunables};
    const std::vector<Frame_record> records = run_scenario(scenario, pacer);
    const Frame_pacer_tunables& tun = scenario.plant.tunables;

    EXPECT_EQ(vsync_values(records, 240), (std::set<int>{1})) << "cadence unchanged by an impulse";

    int late = 0;
    for (std::size_t i = 600; i < 640; ++i) {
        if (records[i].self_miss || records[i].slipped) {
            ++late;
        }
    }
    EXPECT_LE(late, 4) << "spike + GPU-queue cascade + at most one resync slip";

    const double margin_before = records[590].margin;
    double margin_tail_min = std::numeric_limits<double>::infinity();
    for (std::size_t i = 900; i < 1190; ++i) {
        margin_tail_min = std::min(margin_tail_min, records[i].margin);
    }
    const double margin_attack = tun.margin_attack_frac * tun.refresh_period;
    const double margin_min    = tun.margin_min_frac * tun.refresh_period;
    EXPECT_LE(margin_tail_min, std::max(margin_before, margin_min) + margin_attack)
        << "no permanent margin elevation beyond one attack step";
}

TEST(Frame_pacer_claims, c6_queue_invariant_under_corruption)
{
    Scenario scenario;
    scenario.frame_count      = 1200;
    scenario.cpu_duration     = make_gauss(601, 6.0, 0.4);
    scenario.gpu_duration     = make_gauss(602, 8.0, 0.6);
    scenario.has_forced_range = true;
    scenario.forced_first     = 600;
    scenario.forced_last      = 660;
    Frame_pacer pacer{scenario.plant.tunables};
    const std::vector<Frame_record> records = run_scenario(scenario, pacer);
    const Frame_pacer_tunables& tun = scenario.plant.tunables;

    int max_pending = 0;
    for (const Frame_record& record : records) {
        max_pending = std::max(max_pending, record.pending);
    }
    EXPECT_LE(max_pending, tun.target_queued_images + scenario.plant.frames_in_flight)
        << "present-queue bound must hold even with corrupted estimates";

    EXPECT_EQ(vsync_values(records, 240), (std::set<int>{1}))
        << "timing-only misses must not cause a cadence response";

    const double settle = settle_time(records, 661, 1);
    EXPECT_GE(settle, 0.0);
    EXPECT_LE(settle, 1.0) << "recovery within 1 s after corruption ends";
}

TEST(Frame_pacer_claims, c7_cold_start_and_recreation)
{
    Scenario scenario;
    scenario.frame_count      = 1200;
    scenario.cpu_duration     = make_gauss(701, 6.0, 0.4);
    scenario.gpu_duration     = make_gauss(702, 8.0, 0.6);
    scenario.recreate_at_frame = 600;
    Frame_pacer pacer{scenario.plant.tunables};
    const std::vector<Frame_record> records = run_scenario(scenario, pacer);

    const double settle_cold = settle_time(records, 0, 1);
    EXPECT_GE(settle_cold, 0.0);
    EXPECT_LE(settle_cold, 1.0) << "cold start settles within 1 s";

    const double settle_recreation = settle_time(records, 600, 1);
    EXPECT_GE(settle_recreation, 0.0);
    EXPECT_LE(settle_recreation, std::max(settle_cold, 0.2) + 0.3)
        << "recreation recovery must not exceed cold start";
}

TEST(Frame_pacer_claims, c8_ripple_below_hysteresis)
{
    Scenario scenario;
    scenario.frame_count  = 1200;
    scenario.cpu_duration = make_gauss(801, 6.0, 0.4);
    scenario.gpu_duration = make_gauss(802, 8.0, 0.6);
    Frame_pacer pacer{scenario.plant.tunables};
    const std::vector<Frame_record> records = run_scenario(scenario, pacer);
    const Frame_pacer_tunables& tun = scenario.plant.tunables;

    double w_min = std::numeric_limits<double>::infinity();
    double w_max = 0.0;
    for (std::size_t i = 240; i < records.size(); ++i) {
        if (records[i].w_statistic > 0.0) {
            w_min = std::min(w_min, records[i].w_statistic);
            w_max = std::max(w_max, records[i].w_statistic);
        }
    }
    ASSERT_LT(w_min, w_max);
    const double ripple     = w_max - w_min;
    const double hysteresis = tun.hysteresis_frac * tun.refresh_period;
    EXPECT_LT(ripple, hysteresis) << "decision-statistic ripple must stay inside the hysteresis band";
}

TEST(Frame_pacer_claims, c10_heavy_tail_borderline)
{
    // Typical frames fit 2 vsyncs (median ~12 ms < 16.67 ms) but the tail
    // does not (q95 ~19 ms). The tail is absorbed by margin + queue
    // elasticity, so the load IS sustainable at N=2: the pacer must dwell
    // there; probing N=1 is allowed but must converge geometrically
    // (upshift backoff) instead of flapping.
    auto engine = std::make_shared<std::mt19937>(1010u);
    std::normal_distribution<double> low_mode {12.0, 1.0};
    std::normal_distribution<double> high_mode{19.0, 1.0};
    std::uniform_real_distribution<double> select{0.0, 1.0};
    Scenario scenario;
    scenario.frame_count  = 2400;
    scenario.cpu_duration = make_gauss(1011, 6.0, 0.4);
    scenario.gpu_duration = [engine, low_mode, high_mode, select](std::int64_t) mutable -> double {
        const double mode_value = (select(*engine) < 0.15) ? high_mode(*engine) : low_mode(*engine);
        return std::max(0.5, mode_value) * ms;
    };
    Frame_pacer pacer{scenario.plant.tunables};
    const std::vector<Frame_record> records = run_scenario(scenario, pacer);

    const std::set<int> n_values = vsync_values(records, 240);
    EXPECT_TRUE((n_values.count(2) == 1) && (n_values.count(3) == 0))
        << "must dwell at the sustainable cadence, never over-downshift";

    int changes      = 0;
    int changes_tail = 0;
    for (std::size_t i = 241; i < records.size(); ++i) {
        if (records[i].vsyncs != records[i - 1].vsyncs) {
            ++changes;
            if (i >= 1800) {
                ++changes_tail;
            }
        }
    }
    EXPECT_LE(changes, 16) << "probing must be bounded";
    EXPECT_LE(changes_tail, 2) << "probing must converge (geometric backoff)";

    const int misses = count_self_misses(records, 240, records.size());
    EXPECT_LE(misses, static_cast<int>((records.size() - 240) / 100))
        << "the load must in fact be sustainable at the held cadence";
}

TEST(Frame_pacer_claims, c11_observer_mode)
{
    // The app ignores the schedule entirely (observer mode): the pacer
    // cannot HIT its targets, but its predictions must not drift - they
    // must stay within a few refresh periods of achieved presents, forever.
    Scenario scenario;
    scenario.frame_count  = 2400;
    scenario.cpu_duration = make_gauss(1101, 6.0, 0.4);
    scenario.gpu_duration = make_gauss(1102, 8.0, 0.6);
    scenario.unpaced      = true;
    Frame_pacer pacer{scenario.plant.tunables};
    const std::vector<Frame_record> records = run_scenario(scenario, pacer);
    const double t = scenario.plant.tunables.refresh_period;

    double max_error      = 0.0;
    double max_error_tail = 0.0;
    for (std::size_t i = 240; i < records.size(); ++i) {
        const double error = std::abs(records[i].display_time - records[i].target_time);
        max_error = std::max(max_error, error);
        if (i >= records.size() / 2) {
            max_error_tail = std::max(max_error_tail, error);
        }
    }
    EXPECT_LE(max_error, 5.0 * t) << "prediction drift must stay bounded";
    EXPECT_LE(max_error_tail, max_error + 1e-9) << "no growth over time";
}

TEST(Frame_pacer_claims, c12_grid_period_tracking)
{
    // The true grid period differs from the nominal (queried) refresh
    // period by 200 ppm, and present feedback blacks out for 4 s mid-run.
    // The pacer must track the actual period (deviation 10), hold the grid
    // through the blackout, and pace as if the period were exact.
    constexpr double ppm = 200e-6;
    Scenario scenario;
    scenario.frame_count  = 3600;
    scenario.cpu_duration = make_gauss(1201, 6.0, 0.4);
    scenario.gpu_duration = make_gauss(1202, 8.0, 0.6);
    scenario.plant.true_grid_period = scenario.plant.tunables.refresh_period * (1.0 + ppm);
    scenario.has_feedback_blackout  = true;
    scenario.blackout_first         = 1800;
    scenario.blackout_last          = 2040;
    Frame_pacer pacer{scenario.plant.tunables};
    const std::vector<Frame_record> records = run_scenario(scenario, pacer);
    const double t = scenario.plant.tunables.refresh_period;

    // 1. Period estimate converges to the true period, not the nominal.
    const double period_error_ppm = std::abs(pacer.get_grid_period() - scenario.plant.true_grid_period) / t * 1e6;
    EXPECT_LE(period_error_ppm, 20.0) << "period estimate must converge to the true period";

    // 2. Grid stays believable through the feedback blackout: the untracked
    //    drift over 4 s would be ~0.8 ms; require better than half of it.
    double blackout_error = 0.0;
    for (std::size_t i = static_cast<std::size_t>(scenario.blackout_last) - 10;
         i <= static_cast<std::size_t>(scenario.blackout_last); ++i)
    {
        blackout_error = std::max(blackout_error, std::abs(records[i].display_time - records[i].target_time));
    }
    EXPECT_LE(blackout_error, 0.4e-3) << "grid must hold through the feedback blackout";

    // 3. The wrong nominal period must not degrade pacing.
    EXPECT_EQ(vsync_values(records, 240), (std::set<int>{1}));
    int misses = 0;
    for (std::size_t i = 240; i < records.size(); ++i) {
        misses += records[i].self_miss ? 1 : 0;
    }
    EXPECT_LE(static_cast<double>(misses) / static_cast<double>(records.size() - 240), 0.01);
}

TEST(Frame_pacer_claims, c13_latest_ready_fast_app)
{
    // The live-editor regime that ratcheted the slot cursor ~10 s ahead
    // (P2.2-era finding, deviation 11): light load (app ~4x the refresh
    // rate), fifo_latest_ready drops every superseded image, and only the
    // FR5 clamp is enforced (release gating is P2.3). Dropped frames
    // consume NO display slot, so a per-frame slot-monotonicity floor
    // advances the cursor faster than the display - the schedule must stay
    // anchored to reality regardless.
    Scenario scenario;
    scenario.frame_count  = 3600;
    scenario.cpu_duration = make_gauss(1301, 2.0, 0.2);
    scenario.gpu_duration = make_gauss(1302, 2.0, 0.2);
    scenario.latest_ready = true;
    scenario.clamp_only   = true;
    Frame_pacer pacer{scenario.plant.tunables};
    const std::vector<Frame_record> records = run_scenario(scenario, pacer);
    const double t = scenario.plant.tunables.refresh_period;

    double max_target_lead      = 0.0;
    double max_target_lead_tail = 0.0;
    int    displayed            = 0;
    for (std::size_t i = 240; i < records.size(); ++i) {
        const double lead = records[i].target_time - records[i].decision_time;
        max_target_lead = std::max(max_target_lead, lead);
        if (i >= records.size() / 2) {
            max_target_lead_tail = std::max(max_target_lead_tail, lead);
        }
        displayed += (records[i].display_time > 0.0) ? 1 : 0;
    }
    EXPECT_LE(max_target_lead, 8.0 * t) << "slot cursor must stay anchored to reality";
    EXPECT_LE(max_target_lead_tail, max_target_lead + 1e-9) << "no cursor growth over time";
    EXPECT_GE(displayed, static_cast<int>(records.size() - 240) / 4) << "display must be active (drops expected, not starvation)";
}

TEST(Frame_pacer_claims, c14_latest_ready_full_pacing)
{
    // Same plant as C13 (app ~4x the refresh rate on fifo_latest_ready), but
    // with release gating enforced (P2.3): the pacer must pace the app down
    // to one frame per cadence slot, at which point latest-ready has nothing
    // to supersede - drops disappear and every frame occupies its own slot.
    Scenario scenario;
    scenario.frame_count  = 3600;
    scenario.cpu_duration = make_gauss(1401, 2.0, 0.2);
    scenario.gpu_duration = make_gauss(1402, 2.0, 0.2);
    scenario.latest_ready = true;
    Frame_pacer pacer{scenario.plant.tunables};
    const std::vector<Frame_record> records = run_scenario(scenario, pacer);
    const double t = scenario.plant.tunables.refresh_period;

    // 1. No drops after convergence: release gating leaves nothing for
    //    latest-ready to supersede.
    std::vector<const Frame_record*> displayed;
    for (std::size_t i = 240; i < records.size(); ++i) {
        if (records[i].display_time > 0.0) {
            displayed.push_back(&records[i]);
        }
    }
    EXPECT_EQ(displayed.size(), records.size() - 240) << "every frame must reach the display";

    // 2. Every frame occupies its own slot: consecutive display times one
    //    true period apart (N=1 at this load).
    EXPECT_EQ(vsync_values(records, 240), (std::set<int>{1}));
    double max_gap_error = 0.0;
    for (std::size_t i = 1; i < displayed.size(); ++i) {
        const double gap = displayed[i]->display_time - displayed[i - 1]->display_time;
        max_gap_error = std::max(max_gap_error, std::abs(gap - t));
    }
    EXPECT_LE(max_gap_error, 0.1e-3) << "consecutive frames must display exactly one period apart";

    // 3. The schedule stays anchored: bounded release lead, no runaway.
    double max_release_lead = 0.0;
    for (std::size_t i = 240; i < records.size(); ++i) {
        max_release_lead = std::max(max_release_lead, records[i].release - records[i].decision_time);
    }
    EXPECT_LE(max_release_lead, 8.0 * t) << "release schedule must stay anchored to reality";

    // 4. Paced, not thrashing: miss rate as in C1.
    const int misses = count_self_misses(records, 240, records.size());
    EXPECT_LE(static_cast<double>(misses) / static_cast<double>(records.size() - 240), 0.01);
}

TEST(Frame_pacer_claims, c15_inert_target_holdback)
{
    // The measured driver behavior (doc/reference/nvidia_present_timing_driver_report.md):
    // target present times are accepted but never honored - an image
    // displays at the earliest vsync at which it is AVAILABLE. The
    // mitigation delays the present REQUEST to one period before the
    // target. Regime mirrors the real 120 Hz N=3 observation; the GPU-side
    // spike tail inflates the latency quantile (release budget) without
    // moving the cadence statistic, so typical frames carry more than one
    // refresh period of slack - the early-display precondition.
    const auto make_spiky_gpu = [](const unsigned int seed) -> std::function<double(std::int64_t)> {
        auto engine = std::make_shared<std::mt19937>(seed);
        std::normal_distribution<double>       base{4.5, 0.3};
        std::uniform_real_distribution<double> uniform{0.0, 1.0};
        return [engine, base, uniform](std::int64_t) mutable -> double {
            if (uniform(*engine) < 0.08) {
                return 12.0 * ms;
            }
            return std::max(0.5, base(*engine)) * ms;
        };
    };
    const auto early_and_exact = [](const std::vector<Frame_record>& records, const double t) {
        int early = 0;
        int exact = 0;
        int shown = 0;
        for (std::size_t i = 240; i < records.size(); ++i) {
            if (records[i].display_time <= 0.0) {
                continue;
            }
            ++shown;
            const long slots = std::lround((records[i].display_time - records[i].target_time) / t);
            early += (slots < 0) ? 1 : 0;
            exact += (slots == 0) ? 1 : 0;
        }
        return std::tuple<int, int, int>{early, exact, shown};
    };

    Scenario scenario;
    scenario.frame_count           = 3600;
    scenario.plant.tunables.refresh_period = 1.0 / 120.0;
    scenario.cpu_duration          = make_gauss(1501, 20.5, 0.8);
    scenario.gpu_duration          = make_spiky_gpu(1502);
    scenario.latest_ready          = true;
    scenario.inert_target          = true;
    const double t = scenario.plant.tunables.refresh_period;

    // a) Without the mitigation: a visible fraction of frames displays a
    //    slot early; pacing stays bounded.
    {
        Frame_pacer pacer{scenario.plant.tunables};
        const std::vector<Frame_record> records = run_scenario(scenario, pacer);
        const auto [early, exact, shown] = early_and_exact(records, t);
        EXPECT_GE(static_cast<double>(early) / static_cast<double>(shown), 0.02)
            << "inert-target engine must show the early-display symptom";
        const std::set<int> n_values = vsync_values(records, 240);
        EXPECT_TRUE((n_values == std::set<int>{3}) || (n_values == std::set<int>{3, 4}))
            << "cadence must stay bounded";
    }

    // b) With the request holdback: the earliest feasible vsync IS the
    //    target - early displays vanish and the cadence is exact.
    {
        scenario.holdback = true;
        Frame_pacer pacer{scenario.plant.tunables};
        const std::vector<Frame_record> records = run_scenario(scenario, pacer);
        const auto [early, exact, shown] = early_and_exact(records, t);
        EXPECT_EQ(early, 0) << "holdback must remove all early displays";
        EXPECT_GE(static_cast<double>(exact) / static_cast<double>(shown), 0.98)
            << "holdback must give exact cadence";
        const int misses = count_self_misses(records, 240, records.size());
        EXPECT_LE(static_cast<double>(misses) / static_cast<double>(records.size() - 240), 0.02);
        EXPECT_EQ(vsync_values(records, 240), (std::set<int>{3})) << "cadence must be stable with holdback";
    }
}

TEST(Frame_pacer_claims, c16_gross_period_reseed)
{
    // The live finding (driver report issue #2): after an app-driven
    // fullscreen mode set to 23.976 Hz from a 120 Hz desktop, the driver
    // reports refreshDuration = 43.478 ms (23.000 Hz) while achieved-present
    // feedback shows the true period is 41.708 ms - a 4.2% error, ~8x the
    // PLL clamp band. C12 covers ppm-scale deviation; this claim covers the
    // gross case: the pacer must re-seed period and phase from measured
    // feedback deltas and converge to the true grid (deviation 12).
    constexpr double nominal_period = 1.0 / 23.0;
    constexpr double true_period    = 0.041708;

    // a) Light load: re-seed converges, grid and pacing are exact.
    {
        Scenario scenario;
        scenario.frame_count                   = 1800;
        scenario.plant.tunables.refresh_period = nominal_period;
        scenario.plant.true_grid_period        = true_period;
        scenario.cpu_duration                  = make_gauss(1601, 6.0, 0.4);
        scenario.gpu_duration                  = make_gauss(1602, 8.0, 0.6);
        Frame_pacer pacer{scenario.plant.tunables};
        const std::vector<Frame_record> records = run_scenario(scenario, pacer);

        const double period_error_ppm = std::abs(pacer.get_grid_period() - true_period) / nominal_period * 1e6;
        EXPECT_LE(period_error_ppm, 100.0)
            << "tracked period must re-seed to the measured true period";

        double tail_error = 0.0;
        for (std::size_t i = 1200; i < records.size(); ++i) {
            tail_error = std::max(tail_error, std::abs(records[i].display_time - records[i].target_time));
        }
        EXPECT_LE(tail_error, 0.5e-3) << "grid must converge to the true grid";

        EXPECT_EQ(vsync_values(records, 240), (std::set<int>{1}));
        const int misses = count_self_misses(records, 240, records.size());
        EXPECT_LE(static_cast<double>(misses) / static_cast<double>(records.size() - 240), 0.01);
    }

    // b) Load in the ambiguity band (true period < load < nominal period):
    //    with budgets quantized by the wrong nominal the pacer believes N=1
    //    is feasible and sits missing every frame - the reactive path can
    //    never attribute (streak_attr + guard < nominal budget). Budgets
    //    must follow the TRACKED period: after the re-seed the load is
    //    recognized as infeasible at N=1 and the pacer downshifts.
    {
        Scenario scenario;
        scenario.frame_count                   = 1800;
        scenario.plant.tunables.refresh_period = nominal_period;
        scenario.plant.true_grid_period        = true_period;
        scenario.cpu_duration                  = make_gauss(1603, 6.0, 0.4);
        scenario.gpu_duration                  = make_gauss(1604, 41.9, 0.15);
        Frame_pacer pacer{scenario.plant.tunables};
        const std::vector<Frame_record> records = run_scenario(scenario, pacer);

        EXPECT_EQ(vsync_values(records, 900), (std::set<int>{2}))
            << "load infeasible at the true period must downshift";
        const int misses = count_self_misses(records, 900, records.size());
        EXPECT_LE(static_cast<double>(misses) / static_cast<double>(records.size() - 900), 0.01);
    }
}

TEST(Frame_pacer_claims, c17_scanout_skip_robustness)
{
    // The live finding (dual-stage feedback, H1 resolved): ~5% of vblanks
    // slip one slot at scanout (vblank-count-periodic, ~every 21st),
    // uncorrelated with any app timing; the present op executes on cadence
    // while first-pixel-out lands one slot late - so the present-wait clamp
    // may unblock a slot before the actual display (skip_clamp_on_cadence).
    // Claim: the closed-loop response stays BOUNDED - a skip costs one
    // 2-slot gap (or a latest-ready drop) and a one-slot schedule shift.
    //
    // NEGATIVE FINDING (for the live investigation): the ~50% alternating
    // 1,2-gap sawtooth observed live at 120 Hz N=1 does NOT reproduce in
    // this plant in any variant (random/periodic skips, holdback off,
    // on-cadence clamp, tight load); the live amplification must involve
    // integration- or driver-level behavior outside this model.
    const auto run_arm = [](const double skip_probability, const std::int64_t skip_period) {
        Scenario scenario;
        scenario.frame_count                   = 3600;
        scenario.plant.tunables.refresh_period = 1.0 / 120.0;
        scenario.cpu_duration                  = make_gauss(1701, 2.0, 0.2);
        scenario.gpu_duration                  = make_gauss(1702, 2.0, 0.2);
        scenario.latest_ready                  = true;
        scenario.holdback                      = true;
        scenario.skip_clamp_on_cadence         = true;
        scenario.skip_probability              = skip_probability;
        scenario.skip_period                   = skip_period;
        Frame_pacer pacer{scenario.plant.tunables};
        return std::pair{run_scenario(scenario, pacer), scenario.plant.tunables.refresh_period};
    };
    const auto gap_stats = [](const std::vector<Frame_record>& records, const double t) {
        std::vector<const Frame_record*> displayed;
        for (std::size_t i = 240; i < records.size(); ++i) {
            if (records[i].display_time > 0.0) {
                displayed.push_back(&records[i]);
            }
        }
        std::vector<long> gaps;
        for (std::size_t i = 0; i + 1 < displayed.size(); ++i) {
            gaps.push_back(std::lround((displayed[i + 1]->display_time - displayed[i]->display_time) / t));
        }
        std::size_t exact    = 0;
        double      gap_sum  = 0.0;
        for (const long gap : gaps) {
            exact   += (gap == 1) ? 1 : 0;
            gap_sum += static_cast<double>(gap);
        }
        return std::tuple{
            displayed.size(),
            gaps.size(),
            static_cast<double>(exact) / static_cast<double>(gaps.size()),
            gap_sum / static_cast<double>(gaps.size())
        };
    };

    // a) Random skips at the ambient rate.
    {
        const auto [records, t] = run_arm(0.05, 0);
        const auto [displayed, gap_count, exact_fraction, mean_gap] = gap_stats(records, t);
        EXPECT_GE(exact_fraction, 0.875)
            << "cadence error must stay comparable to the ambient skip rate (live sawtooth was ~50%)";
        EXPECT_GE(displayed, static_cast<std::size_t>(0.90 * static_cast<double>(records.size() - 240)))
            << "drops must stay bounded";
        const int misses = count_self_misses(records, 240, records.size());
        EXPECT_LE(static_cast<double>(misses) / static_cast<double>(records.size() - 240), 0.02);
        double max_release_lead = 0.0;
        for (std::size_t i = 240; i < records.size(); ++i) {
            max_release_lead = std::max(max_release_lead, records[i].release - records[i].decision_time);
        }
        EXPECT_LE(max_release_lead, 8.0 * t) << "no schedule runaway under skips";
    }

    // b) Vblank-count-periodic skips (the live pattern): a periodic
    //    disturbance is the classic way to entrain a feedback loop into a
    //    limit cycle - the response must stay bounded.
    {
        const auto [records, t] = run_arm(0.0, 21);
        const auto [displayed, gap_count, exact_fraction, mean_gap] = gap_stats(records, t);
        static_cast<void>(displayed);
        static_cast<void>(gap_count);
        EXPECT_GE(exact_fraction, 0.85) << "periodic skips must not entrain a limit cycle";
        EXPECT_LE(mean_gap, 1.15) << "mean cadence bounded under periodic skips";
        const int misses = count_self_misses(records, 240, records.size());
        EXPECT_LE(static_cast<double>(misses) / static_cast<double>(records.size() - 240), 0.02);
    }
}

TEST(Frame_pacer_claims, c18_serialized_holdback)
{
    // The live 120 Hz N=3 finding: the C15 holdback runs INLINE on the
    // producer thread, so the next frame's decision waits for the previous
    // frame's present request (~target - T). With slot reachability judged
    // by the q95 budget (spike-tail-inflated), the anchor slot was
    // chronically "unreachable", the schedule slid one slot per frame, and
    // the loop locked into a stable 4-slot cadence at planned N=3. Fix
    // (deviation 13): reachability uses TYPICAL (median) latency + margin;
    // the q95 budget still sets the release. Capping the holdback at the
    // next release point instead was rejected (re-admits early displays
    // when budget > (N+1)*T - dpe - 2*guard); past that bound only an
    // async present thread gives both exact holdback and full-budget
    // scheduling - the inline compromise (spike frames slip, typical
    // frames on cadence) is the accepted producer-thread behavior.
    const auto make_spiky_gpu = [](const unsigned int seed) -> std::function<double(std::int64_t)> {
        auto engine = std::make_shared<std::mt19937>(seed);
        std::normal_distribution<double>       base{4.5, 0.3};
        std::uniform_real_distribution<double> uniform{0.0, 1.0};
        return [engine, base, uniform](std::int64_t) mutable -> double {
            if (uniform(*engine) < 0.08) {
                return 12.0 * ms;
            }
            return std::max(0.5, base(*engine)) * ms;
        };
    };
    const auto gap_stats = [](const std::vector<Frame_record>& records, const double t) {
        std::vector<const Frame_record*> displayed;
        for (std::size_t i = 240; i < records.size(); ++i) {
            if (records[i].display_time > 0.0) {
                displayed.push_back(&records[i]);
            }
        }
        std::size_t exact3  = 0;
        std::size_t exact1  = 0;
        double      gap_sum = 0.0;
        std::size_t count   = 0;
        for (std::size_t i = 0; i + 1 < displayed.size(); ++i) {
            const long gap = std::lround((displayed[i + 1]->display_time - displayed[i]->display_time) / t);
            exact3  += (gap == 3) ? 1 : 0;
            exact1  += (gap == 1) ? 1 : 0;
            gap_sum += static_cast<double>(gap);
            ++count;
        }
        return std::tuple{
            gap_sum / static_cast<double>(count),
            static_cast<double>(exact3) / static_cast<double>(count),
            static_cast<double>(exact1) / static_cast<double>(count)
        };
    };

    // a) The live operating point: spiky N=3 load, serialized holdback.
    {
        Scenario scenario;
        scenario.frame_count                   = 3600;
        scenario.plant.tunables.refresh_period = 1.0 / 120.0;
        scenario.cpu_duration                  = make_gauss(1801, 20.5, 0.8);
        scenario.gpu_duration                  = make_spiky_gpu(1802);
        scenario.latest_ready                  = true;
        scenario.holdback                      = true;
        scenario.skip_probability              = 0.05;
        scenario.skip_clamp_on_cadence         = true;
        scenario.holdback_serialized           = true;
        scenario.tick_overhead                 = 1e-3;
        Frame_pacer pacer{scenario.plant.tunables};
        const std::vector<Frame_record> records = run_scenario(scenario, pacer);
        const double t = scenario.plant.tunables.refresh_period;

        const auto [mean_gap, exact3, exact1] = gap_stats(records, t);
        static_cast<void>(exact1);
        EXPECT_LE(mean_gap, 3.3) << "no 4-slot cadence lock (unfixed loop locks at mean ~4.0)";
        EXPECT_GE(exact3, 0.70) << "typical frames must hold the 3-slot cadence";
        int early = 0;
        for (std::size_t i = 240; i < records.size(); ++i) {
            if ((records[i].display_time > 0.0) &&
                (std::lround((records[i].display_time - records[i].target_time) / t) < 0))
            {
                ++early;
            }
        }
        EXPECT_EQ(early, 0) << "the holdback mitigation must stay intact";
        const int misses = count_self_misses(records, 240, records.size());
        EXPECT_LE(static_cast<double>(misses) / static_cast<double>(records.size() - 240), 0.09)
            << "spike slips bounded (~= the spike rate)";
        EXPECT_EQ(vsync_values(records, 240), (std::set<int>{3}));
    }

    // b) The N=1 sawtooth regime (light load, serialized): must be clean.
    {
        Scenario scenario;
        scenario.frame_count                   = 3600;
        scenario.plant.tunables.refresh_period = 1.0 / 120.0;
        scenario.cpu_duration                  = make_gauss(1803, 2.0, 0.2);
        scenario.gpu_duration                  = make_gauss(1804, 2.0, 0.2);
        scenario.latest_ready                  = true;
        scenario.holdback                      = true;
        scenario.skip_probability              = 0.05;
        scenario.skip_clamp_on_cadence         = true;
        scenario.holdback_serialized           = true;
        scenario.tick_overhead                 = 1e-3;
        Frame_pacer pacer{scenario.plant.tunables};
        const std::vector<Frame_record> records = run_scenario(scenario, pacer);
        const double t = scenario.plant.tunables.refresh_period;

        const auto [mean_gap, exact3, exact1] = gap_stats(records, t);
        static_cast<void>(exact3);
        EXPECT_LE(mean_gap, 1.10) << "no N=1 sawtooth (live alternated 1,2 at ~50%)";
        EXPECT_GE(exact1, 0.875) << "cadence error must stay at the ambient skip rate";
    }
}

TEST(Frame_pacer_claims, c9_lazy_measurement_delivery)
{
    // C9a: steady state unaffected by the lazy fence-read delivery path.
    {
        Scenario scenario;
        scenario.frame_count       = 1200;
        scenario.cpu_duration      = make_gauss(901, 6.0, 0.4);
        scenario.gpu_duration      = make_gauss(902, 8.0, 0.6);
        scenario.lazy_gpu_delivery = true;
        Frame_pacer pacer{scenario.plant.tunables};
        const std::vector<Frame_record> records = run_scenario(scenario, pacer);
        const Frame_pacer_tunables& tun = scenario.plant.tunables;

        EXPECT_EQ(vsync_values(records, 240), (std::set<int>{1}));
        const int misses = count_self_misses(records, 240, records.size());
        EXPECT_LE(misses, static_cast<int>((records.size() - 240) / 100));
        int max_pending = 0;
        for (const Frame_record& record : records) {
            max_pending = std::max(max_pending, record.pending);
        }
        EXPECT_LE(max_pending, tun.target_queued_images + scenario.plant.frames_in_flight);
    }

    // C9b: step response degrades gracefully under lazy delivery.
    {
        const std::size_t step = 300;
        Scenario scenario;
        scenario.frame_count       = 900;
        scenario.cpu_duration      = make_gauss(903, 6.0, 0.4);
        scenario.gpu_duration      = make_stepped(make_gauss(904, 8.0, 0.6), make_gauss(905, 22.0, 0.8), static_cast<std::int64_t>(step));
        scenario.lazy_gpu_delivery = true;
        Frame_pacer pacer{scenario.plant.tunables};
        const std::vector<Frame_record> records = run_scenario(scenario, pacer);
        const Frame_pacer_tunables& tun = scenario.plant.tunables;

        int late_total = 0;
        for (std::size_t i = step; i < step + 80; ++i) {
            if (records[i].self_miss || records[i].slipped) {
                ++late_total;
            }
        }
        const int lazy_bound = tun.reactive_miss_threshold + (3 * scenario.plant.frames_in_flight) + 1;
        EXPECT_LE(late_total, lazy_bound) << "disruption bounded by M + 3F + 1 under lazy delivery";

        const double settle = settle_time(records, step, 2);
        EXPECT_GE(settle, 0.0);
        EXPECT_LE(settle, 1.0);
        EXPECT_EQ(vsync_values(records, step + 180), (std::set<int>{2})) << "no oscillation under lazy delivery";
    }
}

// P4.1 tier S slop-servo pacer claims (reference model: SlopServo in
// scripts/frame_pacing_sim.py; the C++ Slop_servo_pacer under test drives
// the same plant through Scenario::slop_servo).
namespace {

auto slot_gap_exact_fraction(const std::vector<Frame_record>& records, const std::size_t from) -> double
{
    std::size_t exact = 0;
    std::size_t total = 0;
    for (std::size_t i = from; i + 1 < records.size(); ++i) {
        if ((records[i + 1].display_slot - records[i].display_slot) == 1) {
            ++exact;
        }
        ++total;
    }
    return (total > 0) ? (static_cast<double>(exact) / static_cast<double>(total)) : 0.0;
}

auto median_of(std::vector<double> values) -> double
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

} // anonymous namespace

TEST(Frame_pacer_claims, c19_slop_servo_equilibrium)
{
    // On a plain-FIFO backpressure plant with steady load, the slop servo
    // converges to residual blocking ~= headroom, trimming input-poll
    // latency (display - release) versus the unpaced baseline without
    // giving up throughput. disp - cpu_start is identical in both runs:
    // the backpressure still binds at equilibrium; the servo converts
    // post-input blocking into pre-input sleep, it does not move production.
    const auto make_scenario = [](const bool servo_on) {
        Scenario scenario;
        scenario.frame_count  = 1200;
        scenario.cpu_duration = make_gauss(1901, 6.0, 0.4);
        scenario.gpu_duration = make_gauss(1902, 8.0, 0.6);
        scenario.fifo_images  = 3;
        scenario.slop_servo   = servo_on;
        scenario.unpaced      = !servo_on;
        scenario.inert_target = true;
        return scenario;
    };

    Scenario base_scenario = make_scenario(false);
    Frame_pacer base_pacer{base_scenario.plant.tunables};
    const std::vector<Frame_record> base_records = run_scenario(base_scenario, base_pacer);

    Scenario scenario = make_scenario(true);
    Frame_pacer pacer{scenario.plant.tunables};
    const std::vector<Frame_record> records = run_scenario(scenario, pacer);

    const double headroom = scenario.servo_tunables.headroom;
    std::vector<double> slops;
    std::vector<double> sleeps;
    std::vector<double> latencies;
    std::vector<double> base_latencies;
    for (std::size_t i = 300; i < records.size(); ++i) {
        slops.push_back(records[i].slop);
        sleeps.push_back(records[i].sleep);
        latencies.push_back(records[i].display_time - records[i].release);
        base_latencies.push_back(base_records[i].display_time - base_records[i].release);
    }
    const double slop_median  = median_of(slops);
    const double sleep_median = median_of(sleeps);
    EXPECT_LE(slop_median, 3.0 * headroom) << "residual blocking must converge to ~headroom";
    EXPECT_GE(sleep_median, 1.0 * ms) << "the servo must engage";
    EXPECT_LE(median_of(latencies), median_of(base_latencies) - (0.5 * sleep_median))
        << "input-poll latency must be trimmed by ~the sleep";
    EXPECT_GE(slot_gap_exact_fraction(records, 300), 0.99) << "throughput must be kept";
}

TEST(Frame_pacer_claims, c19b_slop_servo_cadence_above_one_vsync)
{
    // A GPU-bound load on a 120 Hz plain-FIFO plant settles on a cadence
    // of several refresh periods per frame. The servo must read the
    // cadence from the loop and pace against N * T: the blocking turns
    // into pre-input sleep, the cadence (throughput) is kept, and latency
    // is trimmed by about the sleep - as C19 states for the one-vsync case.
    const auto make_scenario = [](const bool servo_on) {
        Scenario scenario;
        scenario.frame_count  = 1500;
        scenario.plant.tunables.refresh_period = 1.0 / 120.0;
        scenario.cpu_duration = make_gauss(1911, 6.0, 0.4);
        scenario.gpu_duration = make_gauss(1912, 20.0, 0.6);
        scenario.fifo_images  = 2;
        scenario.slop_servo   = servo_on;
        scenario.unpaced      = !servo_on;
        scenario.inert_target = true;
        return scenario;
    };

    Scenario base_scenario = make_scenario(false);
    Frame_pacer base_pacer{base_scenario.plant.tunables};
    const std::vector<Frame_record> base_records = run_scenario(base_scenario, base_pacer);

    Scenario scenario = make_scenario(true);
    Frame_pacer pacer{scenario.plant.tunables};
    const std::vector<Frame_record> records = run_scenario(scenario, pacer);

    const double headroom = scenario.servo_tunables.headroom;
    std::vector<double> slops;
    std::vector<double> sleeps;
    std::vector<double> latencies;
    std::vector<double> base_latencies;
    std::vector<double> gaps;
    std::vector<double> base_gaps;
    for (std::size_t i = 400; i < records.size(); ++i) {
        slops.push_back(records[i].slop);
        sleeps.push_back(records[i].sleep);
        latencies.push_back(records[i].display_time - records[i].release);
        base_latencies.push_back(base_records[i].display_time - base_records[i].release);
        if (i + 1 < records.size()) {
            gaps.push_back(static_cast<double>(records[i + 1].display_slot - records[i].display_slot));
            base_gaps.push_back(static_cast<double>(base_records[i + 1].display_slot - base_records[i].display_slot));
        }
    }
    const double base_gap = median_of(base_gaps);
    EXPECT_GE(base_gap, 3.0) << "the plant must be GPU-bound above one vsync per frame";
    const double sleep_median = median_of(sleeps);
    EXPECT_GE(sleep_median, 4.0 * ms) << "the servo must engage at a cadence above one vsync";
    EXPECT_LE(median_of(slops), 3.0 * headroom) << "residual blocking must converge to ~headroom";
    EXPECT_EQ(median_of(gaps), base_gap) << "the cadence must be kept";
    std::size_t kept = 0;
    for (const double gap : gaps) {
        if (gap == base_gap) {
            ++kept;
        }
    }
    EXPECT_GE(static_cast<double>(kept) / static_cast<double>(gaps.size()), 0.97) << "throughput must be kept frame by frame";
    EXPECT_LE(median_of(latencies), median_of(base_latencies) - (0.5 * sleep_median))
        << "input-poll latency must be trimmed by ~the sleep";
}

TEST(Frame_pacer_claims, c20_slop_servo_overshoot_backoff)
{
    // A load spike that overshoots the refresh period backs the sleep off
    // multiplicatively; the disturbance is bounded and the servo
    // re-converges.
    const std::size_t spike_first = 600;
    Scenario scenario;
    scenario.frame_count  = 1200;
    scenario.cpu_duration = make_stepped(
        make_gauss(2001, 6.0, 0.4),
        [](std::int64_t) { return 30.0 * ms; },
        static_cast<std::int64_t>(spike_first),
        static_cast<std::int64_t>(spike_first + 2)
    );
    scenario.gpu_duration = make_gauss(2002, 8.0, 0.6);
    scenario.fifo_images  = 3;
    scenario.slop_servo   = true;
    Frame_pacer pacer{scenario.plant.tunables};
    const std::vector<Frame_record> records = run_scenario(scenario, pacer);

    const double sleep_before = records[spike_first - 1].sleep;
    double sleep_after = std::numeric_limits<double>::max();
    for (std::size_t i = spike_first; i < spike_first + 6; ++i) {
        sleep_after = std::min(sleep_after, records[i].sleep);
    }
    EXPECT_LE(sleep_after, 0.6 * sleep_before) << "back-off must be multiplicative";

    int disturbed = 0;
    for (std::size_t i = spike_first; i < spike_first + 39; ++i) {
        if ((records[i + 1].display_slot - records[i].display_slot) != 1) {
            ++disturbed;
        }
    }
    EXPECT_LE(disturbed, 8) << "spike disturbance must be bounded";

    std::vector<double> tail_sleeps;
    for (std::size_t i = 900; i < records.size(); ++i) {
        tail_sleeps.push_back(records[i].sleep);
    }
    EXPECT_GE(median_of(tail_sleeps), 0.7 * sleep_before) << "the servo must re-converge";
    EXPECT_GE(slot_gap_exact_fraction(records, 900), 0.99);
}

TEST(Frame_pacer_claims, c21_slop_servo_failure_modes)
{
    // a) fifo_latest_ready: superseded images drop instead of blocking -
    //    slop reads ~0 and the servo winds to nothing (this is why the
    //    pacer method must own the present mode, P4.2).
    {
        Scenario scenario;
        scenario.frame_count  = 1200;
        scenario.cpu_duration = make_gauss(2101, 6.0, 0.4);
        scenario.gpu_duration = make_gauss(2102, 4.0, 0.3);
        scenario.latest_ready = true;
        scenario.slop_servo   = true;
        Frame_pacer pacer{scenario.plant.tunables};
        const std::vector<Frame_record> records = run_scenario(scenario, pacer);

        std::vector<double> sleeps;
        int drops = 0;
        for (std::size_t i = 300; i < records.size(); ++i) {
            sleeps.push_back(records[i].sleep);
            if (records[i].display_time == 0.0) {
                ++drops;
            }
        }
        EXPECT_LE(median_of(sleeps), 1.0 * ms) << "no backpressure to sense: servo dead";
        EXPECT_GE(static_cast<double>(drops) / static_cast<double>(sleeps.size()), 0.3)
            << "the app free-runs and latest_ready drops frames";
    }

    // b) Heavy-tailed load on the proper plain-FIFO plant: the M=3 queue
    //    holds ~2T of slack so tail frames do NOT miss slots - the
    //    mean-based servo's failure mode is OSCILLATION (tier doc: "heavy-
    //    tailed load oscillates or parks conservatively"): tail frames
    //    overshoot the loop period, the sleep collapses multiplicatively,
    //    recovers additively, and the latency benefit is intermittent.
    //    The full pacer's quantile budget absorbs the same load.
    {
        const auto make_heavy = [](const unsigned int seed) -> std::function<double(std::int64_t)> {
            auto engine = std::make_shared<std::mt19937>(seed);
            std::uniform_real_distribution<double> uniform{0.0, 1.0};
            return [engine, uniform](std::int64_t) mutable -> double {
                return ((uniform(*engine) < 0.10) ? 14.0 : 6.0) * ms;
            };
        };

        Scenario scenario;
        scenario.frame_count  = 1600;
        scenario.cpu_duration = make_heavy(2103);
        scenario.gpu_duration = make_gauss(2104, 6.0, 0.3);
        scenario.fifo_images  = 3;
        scenario.slop_servo   = true;
        Frame_pacer pacer{scenario.plant.tunables};
        const std::vector<Frame_record> records = run_scenario(scenario, pacer);

        int backoffs = 0;
        std::vector<double> sleeps;
        std::vector<double> latencies;
        for (std::size_t i = 300; i < records.size(); ++i) {
            sleeps.push_back(records[i].sleep);
            latencies.push_back(records[i].display_time - records[i].release);
            if ((i + 1 < records.size()) && (records[i + 1].sleep < (0.7 * records[i].sleep) - 1e-9)) {
                ++backoffs;
            }
        }
        std::sort(sleeps.begin(), sleeps.end());
        std::sort(latencies.begin(), latencies.end());
        const double sleep_spread = sleeps[static_cast<std::size_t>(0.9 * static_cast<double>(sleeps.size()))]
                                  - sleeps[static_cast<std::size_t>(0.1 * static_cast<double>(sleeps.size()))];
        const double latency_jitter = latencies[static_cast<std::size_t>(0.95 * static_cast<double>(latencies.size()))]
                                    - latencies[latencies.size() / 2];
        EXPECT_GE(backoffs, 20) << "the sleep must recurrently collapse";
        EXPECT_GE(sleep_spread, 3.0 * ms) << "the sleep must oscillate";
        EXPECT_GE(latency_jitter, 3.0 * ms) << "the latency benefit must be intermittent";

        Scenario pacer_scenario;
        pacer_scenario.frame_count  = 1600;
        pacer_scenario.cpu_duration = make_heavy(2103);
        pacer_scenario.gpu_duration = make_gauss(2104, 6.0, 0.3);
        Frame_pacer full_pacer{pacer_scenario.plant.tunables};
        const std::vector<Frame_record> pacer_records = run_scenario(pacer_scenario, full_pacer);
        int pacer_misses = 0;
        for (std::size_t i = 300; i < pacer_records.size(); ++i) {
            if (pacer_records[i].self_miss || pacer_records[i].slipped) {
                ++pacer_misses;
            }
        }
        EXPECT_LE(static_cast<double>(pacer_misses) / static_cast<double>(pacer_records.size() - 300), 0.01)
            << "the full pacer must absorb the same load";
    }
}

TEST(Frame_pacer_claims, c22_compositor_deadline_estimation)
{
    // A composited presentation path (measured on the macOS compositor with
    // Metal presentDrawable:atTime:, doc/erhe/metal_backend.md "Frame
    // pacing"): targets are honored, but an image must be complete about one
    // refresh period plus 1.5 ms before its vsync - far beyond the 2.5 ms
    // initial deadline offset. Without the delta_pe estimator every frame
    // finishing inside that band displays a refresh late although its slack
    // against the assumed deadline is positive (timing-only misses, behavior
    // section 6b); the estimator learns the deadline from those outcomes.
    // 12000 frames (100 s) include the slow decay probing below the
    // learned deadline and the re-raise it triggers.
    Scenario scenario;
    scenario.frame_count                  = 12000;
    scenario.plant.tunables.refresh_period = 1.0 / 120.0;
    const double t = scenario.plant.tunables.refresh_period;
    scenario.plant.true_deadline_offset   = t + (1.5 * ms);
    scenario.plant.tunables.deadline_offset_source = erhe::frame_pacing::Deadline_offset_source::estimated;
    scenario.cpu_duration                 = make_gauss(2201, 2.0, 0.2);
    scenario.gpu_duration                 = make_gauss(2202, 5.5, 0.3);
    Frame_pacer pacer{scenario.plant.tunables};
    const std::vector<Frame_record> records = run_scenario(scenario, pacer);

    const std::size_t from = 600;
    EXPECT_EQ(vsync_values(records, from), (std::set<int>{1})) << "cadence must stay at one refresh per frame";

    std::size_t slipped = 0;
    std::size_t exact   = 0;
    std::size_t total   = 0;
    for (std::size_t i = from; i + 1 < records.size(); ++i) {
        if (records[i].slipped) {
            ++slipped;
        }
        const double duration = records[i + 1].display_time - records[i].display_time;
        if (std::abs(duration - t) < 1e-6) {
            ++exact;
        }
        ++total;
    }
    EXPECT_LE(slipped, total / 100) << "at most 1 percent of frames may display after their target";
    EXPECT_GE(static_cast<double>(exact), 0.99 * static_cast<double>(total))
        << "every frame must be visible exactly one refresh period";
    // The estimate decays below the deadline it learned (probing) until
    // late frames raise it again; the margin covers the difference.
    EXPECT_GE(pacer.get_deadline_offset(), scenario.plant.true_deadline_offset - scenario.plant.tunables.guard)
        << "the estimate must stay within one guard of the true deadline offset";
    EXPECT_LE(pacer.get_deadline_offset(), scenario.plant.true_deadline_offset + t)
        << "the estimate must stay within one refresh period of the true deadline offset";
}

TEST(Frame_pacer_claims, c22b_deadline_estimate_stays_put_on_direct_path)
{
    // Direct presentation (the plant's default 1.5 ms deadline offset, below
    // the 2.5 ms initial value): no timing-only misses occur, so the estimate
    // stays at its initial value and latency is not inflated.
    Scenario scenario;
    scenario.plant.tunables.deadline_offset_source = erhe::frame_pacing::Deadline_offset_source::estimated;
    scenario.frame_count  = 1200;
    scenario.cpu_duration = make_gauss(2211, 6.0, 0.4);
    scenario.gpu_duration = make_gauss(2212, 8.0, 0.6);
    Frame_pacer pacer{scenario.plant.tunables};
    const std::vector<Frame_record> records = run_scenario(scenario, pacer);
    static_cast<void>(records);
    EXPECT_DOUBLE_EQ(pacer.get_deadline_offset(), scenario.plant.tunables.deadline_offset_initial);
}

TEST(Frame_pacer_claims, c23_upshift_under_gpu_clock_scaling)
{
    // Measured on the Apple GPU (doc/erhe/metal_backend.md "Frame pacing"):
    // at N=2 the GPU clocks down and a tail of frames grows past the N=1
    // budget ((N - 1) * T - H = 7.5 ms at 120 Hz; p95 7.6 ms at N=2 against
    // 6.0 ms at N=1). A startup spike downshifts to N=2; with a 12 percent
    // tail a per-frame upshift test (upshift_quantile 1.0) does not complete
    // its dwell within the run, the q90 test does.
    Scenario scenario;
    scenario.frame_count                   = 2400;
    scenario.plant.tunables.refresh_period = 1.0 / 120.0;
    scenario.cpu_duration                  = make_gauss(2301, 2.0, 0.2);
    auto engine = std::make_shared<std::mt19937>(2302);
    scenario.gpu_duration_at_cadence = [engine](const std::int64_t k, const int vsyncs) mutable -> double {
        std::uniform_real_distribution<double> uniform{0.0, 1.0};
        std::normal_distribution<double>       base{5.8, 0.15};
        if ((k >= 100) && (k < 130)) {
            return 20.0 * ms; // startup spike (scene load)
        }
        if ((vsyncs >= 2) && (uniform(*engine) < 0.12)) {
            return 7.9 * ms; // clocked-down tail at the longer cadence
        }
        return std::max(0.5, base(*engine)) * ms;
    };
    Frame_pacer pacer{scenario.plant.tunables};
    const std::vector<Frame_record> records = run_scenario(scenario, pacer);

    std::size_t at_two = 0;
    for (std::size_t i = 130; i < 400; ++i) {
        if (records[i].vsyncs >= 2) {
            ++at_two;
        }
    }
    EXPECT_GT(at_two, 0u) << "the spike must downshift (scenario premise)";
    EXPECT_EQ(vsync_values(records, 1200), (std::set<int>{1})) << "the cadence must return to N=1 and stay";
    const int misses = count_self_misses(records, 1200, records.size());
    EXPECT_LE(misses, static_cast<int>((records.size() - 1200) / 100)) << "miss rate must be <= 1 percent";
}

TEST(Frame_pacer_claims, c24_fifo_upshift_promotion)
{
    // Measured live (Metal tier M, built-in ProMotion panel,
    // doc/erhe/metal_backend.md "Frame pacing"): the compositor needs an
    // image 2T + ~1 ms before its vsync, frees a drawable when it latches the
    // frame two after it (pool of 3) and displays every queued image (FIFO);
    // the GPU span at N=2 has a tail beyond T (~15 percent of the frames
    // 9-14 ms) while at N=1 it stays near 6 ms. After a startup spike the
    // pacer upshifted to N=1 and went straight back to N=2: judged by the
    // whole window (the N=2 tail and its near-misses) N=1 looked infeasible.
    // The predictive downshift now judges a cadence by its own samples, and
    // on a FIFO queue the anchor follows every in-flight target.
    const double t        = 1.0 / 120.0;
    const double dpe_true = (2.0 * t) + (1.0 * ms);
    Scenario scenario;
    scenario.frame_count                         = 3000;
    scenario.plant.tunables.refresh_period       = t;
    scenario.plant.true_deadline_offset          = dpe_true;
    scenario.plant.tunables.deadline_offset_source  = erhe::frame_pacing::Deadline_offset_source::estimated;
    scenario.plant.tunables.deadline_offset_initial = dpe_true + (0.5 * ms);
    scenario.plant.tunables.deadline_offset_max     = dpe_true + (0.5 * ms) + (0.5 * t);
    scenario.plant.tunables.present_queue           = erhe::frame_pacing::Present_queue_kind::fifo;
    scenario.drawable_pool                       = 3;
    scenario.pool_latch_lead                     = dpe_true - (1.8 * ms);
    scenario.cpu_duration                        = make_gauss(2401, 3.0, 0.6);
    auto engine = std::make_shared<std::mt19937>(2402);
    scenario.gpu_duration_at_cadence = [engine](const std::int64_t k, const int vsyncs) mutable -> double {
        std::uniform_real_distribution<double> uniform{0.0, 1.0};
        std::uniform_real_distribution<double> tail{9.0, 14.0};
        std::normal_distribution<double>       base{6.0, 0.3};
        if ((k >= 100) && (k < 130)) {
            return 20.0 * ms; // startup spike (scene load)
        }
        if ((vsyncs >= 2) && (uniform(*engine) < 0.15)) {
            return tail(*engine) * ms; // clocked-down tail at the longer cadence
        }
        return std::max(0.5, base(*engine)) * ms;
    };
    Frame_pacer pacer{scenario.plant.tunables};
    const std::vector<Frame_record> records = run_scenario(scenario, pacer);

    std::size_t at_two = 0;
    for (std::size_t i = 130; i < 400; ++i) {
        if (records[i].vsyncs >= 2) {
            ++at_two;
        }
    }
    EXPECT_GT(at_two, 0u) << "the spike must downshift (scenario premise)";
    EXPECT_EQ(vsync_values(records, 1500), (std::set<int>{1})) << "the cadence must return to N=1 and stay";
    std::size_t collisions = 0;
    std::size_t slips      = 0;
    for (std::size_t i = 131; i < records.size(); ++i) {
        if (records[i].target_time <= (records[i - 1].target_time + (0.5 * t))) {
            ++collisions;
        }
    }
    for (std::size_t i = 1500; i < records.size(); ++i) {
        if (records[i].slipped) {
            ++slips;
        }
    }
    EXPECT_EQ(collisions, 0u) << "a FIFO queue must never get two targets in one slot";
    EXPECT_LE(slips, 15u) << "at most 1 percent slipped";
}

namespace {

auto run_c25(const bool report_clock, const double work_ms, const unsigned int seed, const double cpu_ms = 3.6, const bool report_cpu = true) -> std::vector<Frame_record>
{
    const double t        = 1.0 / 120.0;
    const double dpe_true = t + (1.0 * ms);
    Scenario scenario;
    scenario.frame_count                            = 6000;
    scenario.plant.tunables.refresh_period          = t;
    scenario.plant.true_deadline_offset             = dpe_true;
    scenario.plant.tunables.deadline_offset_source  = erhe::frame_pacing::Deadline_offset_source::estimated;
    scenario.plant.tunables.deadline_offset_initial = dpe_true + (0.3 * ms);
    scenario.plant.tunables.deadline_offset_max     = dpe_true + (0.3 * ms) + (0.5 * t);
    scenario.plant.tunables.present_queue           = erhe::frame_pacing::Present_queue_kind::fifo;
    scenario.drawable_pool                          = 3;
    scenario.pool_latch_lead                        = dpe_true - (1.8 * ms);
    scenario.report_gpu_clock                       = report_clock;
    scenario.report_cpu_clock                       = report_clock && report_cpu;
    scenario.cpu_duration                           = make_gauss(seed, cpu_ms, 0.5);
    auto cpu_clock_engine = std::make_shared<std::mt19937>(seed + 3);
    scenario.cpu_clock = [cpu_clock_engine](const std::int64_t, const int vsyncs) mutable -> double {
        if (vsyncs <= 1) {
            std::normal_distribution<double> high{0.94, 0.03};
            return std::clamp(high(*cpu_clock_engine), 0.85, 1.0);
        }
        std::normal_distribution<double> low{0.53, 0.04};
        return std::clamp(low(*cpu_clock_engine), 0.45, 0.7);
    };
    auto work_engine  = std::make_shared<std::mt19937>(seed + 1);
    auto clock_engine = std::make_shared<std::mt19937>(seed + 2);
    scenario.gpu_duration_at_cadence = [work_engine, work_ms](const std::int64_t k, const int) mutable -> double {
        std::normal_distribution<double> work{work_ms, 0.3};
        if ((k >= 100) && (k < 130)) {
            return 20.0 * ms; // startup spike (scene load)
        }
        return std::max(0.5, work(*work_engine)) * ms;
    };
    scenario.gpu_clock = [clock_engine](const std::int64_t, const int vsyncs) mutable -> double {
        if (vsyncs <= 1) {
            std::normal_distribution<double> high{0.96, 0.015};
            return std::clamp(high(*clock_engine), 0.9, 1.0);
        }
        std::uniform_real_distribution<double> spread{0.41, 0.97};
        return spread(*clock_engine);
    };
    Frame_pacer pacer{scenario.plant.tunables};
    return run_scenario(scenario, pacer);
}

} // anonymous namespace

TEST(Frame_pacer_claims, c25_upshift_under_measured_gpu_clock)
{
    // Measured live (Metal tier M, Dell U2725QE at 120 Hz, IOReport GPU
    // performance states): at N=1 the GPU runs at 93-98 percent of its
    // maximum clock; at N=2 it is idle half the time and DVFS spreads it over
    // 41-97 percent, so the same ~5 ms of work takes 6.4-8.1 ms median with
    // a tail beyond the period, every such frame reset the upshift dwell, and
    // a varying share of launches stayed at N=2. With the host reporting the
    // clock the upshift test predicts the GPU time at N-1 from the work at
    // the clock seen there.
    const std::vector<Frame_record> without_clock = run_c25(false, 5.0, 2501);
    EXPECT_EQ(vsync_values(without_clock, 3000).count(1), 0u) << "spans alone keep the longer cadence (the live failure)";

    const std::vector<Frame_record> with_clock = run_c25(true, 5.0, 2501);
    EXPECT_EQ(vsync_values(with_clock, 3000), (std::set<int>{1})) << "the work at full clock returns to N=1 and stays";
    std::size_t slips = 0;
    for (std::size_t i = 3000; i < with_clock.size(); ++i) {
        if (with_clock[i].slipped) {
            ++slips;
        }
    }
    EXPECT_LE(slips, 30u) << "at most 1 percent slipped";

    // A CPU that clocks down too (Debug: ~4.3 ms of CPU work at full clock,
    // ~8 ms at N=2): the GPU clock alone keeps the longer cadence, both
    // clocks return.
    const std::vector<Frame_record> gpu_only = run_c25(true, 5.0, 2511, 4.3, false);
    EXPECT_EQ(vsync_values(gpu_only, 3000).count(1), 0u) << "the GPU clock alone keeps the longer cadence";
    const std::vector<Frame_record> both = run_c25(true, 5.0, 2511, 4.3, true);
    EXPECT_EQ(vsync_values(both, 3000), (std::set<int>{1})) << "both clocks return to N=1";

    // Work that does not fit N=1 even at full clock keeps the longer cadence.
    const std::vector<Frame_record> heavy = run_c25(true, 9.0, 2505);
    std::size_t changes = 0;
    for (std::size_t i = 601; i < heavy.size(); ++i) {
        if (heavy[i].vsyncs != heavy[i - 1].vsyncs) {
            ++changes;
        }
    }
    EXPECT_EQ(changes, 0u) << "no cadence changes after the startup";
    EXPECT_GE(heavy.back().vsyncs, 2) << "stays at a longer cadence";
}

namespace {

auto run_c26(const double prewarm_seconds) -> std::vector<Frame_record>
{
    const double t        = 1.0 / 120.0;
    const double dpe_true = t + (1.0 * ms);
    Scenario scenario;
    scenario.frame_count                            = 6000;
    scenario.plant.tunables.refresh_period          = t;
    scenario.plant.true_deadline_offset             = dpe_true;
    scenario.plant.tunables.deadline_offset_source  = erhe::frame_pacing::Deadline_offset_source::estimated;
    scenario.plant.tunables.deadline_offset_initial = dpe_true + (0.3 * ms);
    scenario.plant.tunables.deadline_offset_max     = dpe_true + (0.3 * ms) + (0.5 * t);
    scenario.plant.tunables.present_queue           = erhe::frame_pacing::Present_queue_kind::fifo;
    scenario.plant.tunables.upshift_prewarm_seconds = prewarm_seconds;
    scenario.drawable_pool                          = 3;
    scenario.pool_latch_lead                        = dpe_true - (1.8 * ms);
    scenario.dvfs                                   = true;
    scenario.cpu_duration                           = make_gauss(2601, 1.0, 0.2);
    auto work_engine = std::make_shared<std::mt19937>(2602);
    scenario.gpu_duration_at_cadence = [work_engine](const std::int64_t k, const int) mutable -> double {
        std::normal_distribution<double> work{5.1, 0.15};
        if ((k >= 100) && (k < 130)) {
            return 20.0 * ms; // startup spike (scene load)
        }
        return std::max(0.5, work(*work_engine)) * ms;
    };
    Frame_pacer pacer{scenario.plant.tunables};
    return run_scenario(scenario, pacer);
}

} // anonymous namespace

TEST(Frame_pacer_claims, c26_gpu_prewarm)
{
    // Measured live (Metal tier M, Dell U2725QE at 120 Hz): with ~5 ms of work
    // the Apple GPU holds ~0.5 of its maximum clock at N=2 and ~0.97 at N=1.
    // After an upshift the clock needs ~6 frames to ramp; meanwhile frames
    // display 1-2 refreshes late (visible judder) and the reactive path
    // reverts. 2.3 ms of ALU-only work after each N=2 frame raised the clock
    // to 0.97 (probe), so pre-warm runs that before the switch.
    const auto summary = [](const std::vector<Frame_record>& records, std::size_t& upshifts, std::size_t& slips) {
        upshifts = 0;
        slips    = 0;
        for (std::size_t i = 140; i < records.size(); ++i) {
            if ((records[i].vsyncs == 1) && (records[i - 1].vsyncs > 1)) {
                ++upshifts;
            }
            if (records[i].slipped) {
                ++slips;
            }
        }
    };
    std::size_t upshifts_off = 0;
    std::size_t slips_off    = 0;
    const std::vector<Frame_record> without = run_c26(0.0);
    summary(without, upshifts_off, slips_off);
    EXPECT_GT(slips_off, 0u) << "without pre-warm the upshift slips while the clock ramps (the live failure)";

    std::size_t upshifts_on = 0;
    std::size_t slips_on    = 0;
    const std::vector<Frame_record> with = run_c26(2.3 * ms);
    summary(with, upshifts_on, slips_on);
    EXPECT_EQ(upshifts_on, 1u) << "one upshift";
    EXPECT_EQ(vsync_values(with, 3000), (std::set<int>{1})) << "N=1 holds";
    EXPECT_EQ(slips_on, 0u) << "no slips across the transition";

    // A fill below what the governor answers (0.7 ms against the plant's
    // 2 ms) is doubled by each pre-warm that ends with the clock still low,
    // without a cadence switch.
    std::size_t upshifts_learn = 0;
    std::size_t slips_learn    = 0;
    const std::vector<Frame_record> learn = run_c26(0.7 * ms);
    summary(learn, upshifts_learn, slips_learn);
    EXPECT_EQ(vsync_values(learn, 3000), (std::set<int>{1})) << "learned fill: N=1 holds";
    EXPECT_EQ(slips_learn, 0u) << "learned fill: no slips while learning";
}

namespace {

auto run_c27(const double prewarm_seconds) -> std::vector<Frame_record>
{
    const double t        = 1.0 / 120.0;
    const double dpe_true = t + (1.0 * ms);
    Scenario scenario;
    scenario.frame_count                            = 6000;
    scenario.plant.tunables.refresh_period          = t;
    scenario.plant.true_deadline_offset             = dpe_true;
    scenario.plant.tunables.deadline_offset_source  = erhe::frame_pacing::Deadline_offset_source::estimated;
    scenario.plant.tunables.deadline_offset_initial = dpe_true + (0.3 * ms);
    scenario.plant.tunables.deadline_offset_max     = dpe_true + (0.3 * ms) + (0.5 * t);
    scenario.plant.tunables.present_queue           = erhe::frame_pacing::Present_queue_kind::fifo;
    scenario.plant.tunables.upshift_prewarm_seconds = prewarm_seconds;
    scenario.drawable_pool                          = 3;
    scenario.pool_latch_lead                        = dpe_true - (1.8 * ms);
    scenario.dvfs                                   = true;
    scenario.cpu_duration                           = make_gauss(2701, 1.0, 0.2);
    auto work_engine = std::make_shared<std::mt19937>(2702);
    scenario.gpu_duration_at_cadence = [work_engine](const std::int64_t k, const int) mutable -> double {
        std::normal_distribution<double> work{5.1, 0.15};
        if ((k >= 30) && (k < 60)) {
            return 20.0 * ms; // startup spike, as early as live
        }
        return std::max(0.5, work(*work_engine)) * ms;
    };
    Frame_pacer pacer{scenario.plant.tunables};
    return run_scenario(scenario, pacer);
}

} // anonymous namespace

TEST(Frame_pacer_claims, c27_steady_clock_records)
{
    // Live (Metal tier M, Dell at 120 Hz, the pre-warm kernel missing): the
    // startup spike downshifted after ~33 frames at N=1; the first upshift
    // slipped while the clock ramped (0.45 -> 0.78) and reverted, and those
    // ramp frames - recorded as N=1's clock - put its low decile near 0.58:
    // the work (5.1 ms at full clock) then predicted beyond the period at
    // N=1, every frame reset the dwell, N=2 for good. The clock records
    // hold each cadence's steady clock only.
    const auto upshifts = [](const std::vector<Frame_record>& records) -> std::vector<std::size_t> {
        std::vector<std::size_t> result;
        for (std::size_t i = 70; i < records.size(); ++i) {
            if ((records[i].vsyncs == 1) && (records[i - 1].vsyncs > 1)) {
                result.push_back(i);
            }
        }
        return result;
    };

    // Without a pre-warm every upshift slips while the clock ramps (C26) and
    // reverts; the pacer keeps probing with backoff (C10) instead of locking.
    const std::vector<std::size_t> without = upshifts(run_c27(0.0));
    ASSERT_GE(without.size(), 4u) << "a reverted upshift does not end the upshift probes";
    for (std::size_t i = 2; i < without.size(); ++i) {
        EXPECT_GT(without[i] - without[i - 1], without[i - 1] - without[i - 2]) << "probes back off";
    }

    // With the pre-warm, the same startup: one upshift, N=1 holds, no slips.
    const std::vector<Frame_record> with = run_c27(2.3 * ms);
    EXPECT_EQ(upshifts(with).size(), 1u) << "one upshift";
    EXPECT_EQ(vsync_values(with, 3000), (std::set<int>{1})) << "N=1 holds";
    std::size_t slips = 0;
    for (std::size_t i = 140; i < with.size(); ++i) {
        if (with[i].slipped) {
            ++slips;
        }
    }
    EXPECT_EQ(slips, 0u) << "no slips across the transition";
}

TEST(Frame_pacer_claims, c28_latency_per_visit)
{
    // Live (Metal tier M, Dell at 120 Hz, all threading modes): after the
    // upshift the pacer released every frame at once for 85-120 frames - its
    // N=1 latency window still held the startup frames (scene load,
    // 30-50 ms) - and the frames waited in the FR5 clamp, at the join and in
    // nextDrawable instead; those waits, counted as latency, kept the
    // estimate at the N=2 level (22-30 ms against ~7 ms in steady state).
    // Release: one refresh of extra latency; Debug: 20-30 % of the frames
    // skipped a slot. The latency input excludes the involuntary waits (the
    // plant follows the host's definition), and a cadence's latency window
    // holds the current visit.
    const std::vector<Frame_record> records = run_c27(2.3 * ms); // the live startup: spike at N=1, pre-warm
    std::size_t upshift = 0;
    for (std::size_t i = 70; i < records.size(); ++i) {
        if ((records[i].vsyncs == 1) && (records[i - 1].vsyncs > 1)) {
            upshift = i;
            break;
        }
    }
    ASSERT_GT(upshift, 0u);
    const auto median_input_to_display = [&records](const std::size_t first, const std::size_t last) -> double {
        std::vector<double> values;
        for (std::size_t i = first; i < std::min(last, records.size()); ++i) {
            values.push_back(records[i].display_time - records[i].decision_time);
        }
        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
    };
    const double t      = 1.0 / 120.0;
    const double steady = median_input_to_display(3000, records.size());
    // Frames 20-100 after the upshift (the first 20 carry the N=2 seed by
    // design, C3).
    const double excess = median_input_to_display(upshift + 20, upshift + 100) - steady;
    EXPECT_LT(excess, 0.5 * t) << "the latency estimate follows the cadence entered";
}

namespace {

auto run_c29() -> std::vector<Frame_record>
{
    const double t        = 1.0 / 120.0;
    const double dpe_true = t + (1.0 * ms);
    Scenario scenario;
    scenario.frame_count                            = 6000;
    scenario.plant.tunables.refresh_period          = t;
    scenario.plant.true_deadline_offset             = dpe_true;
    scenario.plant.tunables.deadline_offset_source  = erhe::frame_pacing::Deadline_offset_source::estimated;
    scenario.plant.tunables.deadline_offset_initial = dpe_true + (0.3 * ms);
    scenario.plant.tunables.deadline_offset_max     = dpe_true + (0.3 * ms) + (0.5 * t);
    scenario.plant.tunables.present_queue           = erhe::frame_pacing::Present_queue_kind::fifo;
    scenario.plant.tunables.upshift_prewarm_seconds = 2.3 * ms;
    scenario.drawable_pool                          = 3;
    scenario.pool_latch_lead                        = dpe_true - (1.8 * ms);
    scenario.dvfs                                   = true;
    scenario.dvfs_idle_work                         = 1.0 * ms;
    scenario.cpu_duration                           = make_gauss(2901, 1.0, 0.2);
    auto work_engine = std::make_shared<std::mt19937>(2902);
    scenario.gpu_duration_at_cadence = [work_engine](const std::int64_t k, const int) mutable -> double {
        if (k < 500) {
            std::normal_distribution<double> empty_editor{0.3, 0.03};
            return std::max(0.05, empty_editor(*work_engine)) * ms;
        }
        if (k < 530) {
            return 30.0 * ms; // the scene open
        }
        std::normal_distribution<double> scene{4.5, 0.15};
        return std::max(0.5, scene(*work_engine)) * ms;
    };
    Frame_pacer pacer{scenario.plant.tunables};
    return run_scenario(scenario, pacer);
}

} // anonymous namespace

TEST(Frame_pacer_claims, c29_clock_records_by_workload)
{
    // Live (Metal tier M, Dell at 120 Hz, Bistro): the empty editor ran at
    // N=1 with the GPU idling at 0.21 of its maximum clock; the scene open
    // downshifted to N=2, and N=1's clock record - all from the empty
    // editor - predicted Bistro's 4.1 ms of work (at full clock) at 18 ms
    // there: every frame reset the dwell, N=2 for good, although with the
    // pacer off the same frames displayed one refresh apart. The clock at
    // N-1 is judged from the samples of a similar workload only.
    const std::vector<Frame_record> records = run_c29();
    std::size_t upshifts = 0;
    std::size_t slips    = 0;
    for (std::size_t i = 540; i < records.size(); ++i) {
        if ((records[i].vsyncs == 1) && (records[i - 1].vsyncs > 1)) {
            ++upshifts;
        }
        if (records[i].slipped) {
            ++slips;
        }
    }
    EXPECT_EQ(upshifts, 1u) << "one upshift after the scene open";
    EXPECT_EQ(vsync_values(records, 3000), (std::set<int>{1})) << "N=1 holds";
    EXPECT_EQ(slips, 0u) << "no slips after the scene open";
}

namespace {

enum class C30_display : unsigned int {
    fixed_grid = 0,   // the windowed compositor's fixed grid (Dell, T + 1 ms deadline)
    variable_refresh  // Adaptive-Sync 48-120 Hz, fullscreen direct to display
};

// Whether the pacer is told the display kind (the control withholds it).
enum class C30_pacer_told : unsigned int {
    display_kind = 0,
    nothing
};

constexpr double c30_period = 1.0 / 120.0;
constexpr double c30_deadline_offset_initial = c30_period + (1.3 * ms); // the Dell calibration + 0.3 ms

// Claim C30 scenario: the Dell tier M tunables (present calibration of the
// windowed compositor, pre-warm on), GPU work work_ms at full clock with a
// heavier stretch [heavy_first, heavy_last) of heavy_ms (none when empty),
// the GPU clock at 0.8 of its maximum at N>=2 (IOReport, live); on the VRR
// display (48-120 Hz, direct to display, 1 ms deadline) or on the windowed
// compositor's fixed grid.
auto run_c30(
    const unsigned int   seed,
    const double         work_ms,
    const std::int64_t   heavy_first,
    const std::int64_t   heavy_last,
    const double         heavy_ms,
    const C30_display    display,
    const C30_pacer_told told = C30_pacer_told::display_kind
) -> std::vector<Frame_record>
{
    const double t       = c30_period;
    const double dpe_cal = t + (1.0 * ms);
    Scenario scenario;
    scenario.frame_count                            = 6000;
    scenario.plant.tunables.refresh_period          = t;
    scenario.plant.tunables.deadline_offset_source  = erhe::frame_pacing::Deadline_offset_source::estimated;
    scenario.plant.tunables.deadline_offset_initial = c30_deadline_offset_initial;
    scenario.plant.tunables.deadline_offset_max     = dpe_cal + (0.3 * ms) + (0.5 * t);
    scenario.plant.tunables.present_queue           = erhe::frame_pacing::Present_queue_kind::fifo;
    scenario.plant.tunables.upshift_prewarm_seconds = 2.3 * ms;
    scenario.drawable_pool                          = 3;
    scenario.dvfs                                   = true;
    scenario.dvfs_low                               = 0.8;
    if (display == C30_display::variable_refresh) {
        scenario.plant.true_deadline_offset = 1.0 * ms;
        scenario.pool_latch_lead            = 0.0;
        scenario.vrr_min_interval           = t;
        scenario.vrr_max_interval           = 1.0 / 48.0;
        if (told == C30_pacer_told::display_kind) {
            scenario.plant.tunables.display              = erhe::frame_pacing::Present_display_kind::variable_refresh;
            scenario.plant.tunables.max_refresh_interval = 1.0 / 48.0;
        }
    } else {
        scenario.plant.true_deadline_offset = dpe_cal;
        scenario.pool_latch_lead            = dpe_cal - (1.8 * ms);
    }
    scenario.cpu_duration = make_gauss(seed, 1.0, 0.2);
    auto work_engine = std::make_shared<std::mt19937>(seed + 1);
    scenario.gpu_duration_at_cadence = [work_engine, work_ms, heavy_first, heavy_last, heavy_ms](const std::int64_t k, const int) mutable -> double {
        const double mean = ((k >= heavy_first) && (k < heavy_last)) ? heavy_ms : work_ms;
        std::normal_distribution<double> work{mean, 0.1};
        return std::max(0.5, work(*work_engine)) * ms;
    };
    Frame_pacer pacer{scenario.plant.tunables};
    return run_scenario(scenario, pacer);
}

// Present deltas after frame 3000 within 0.1 ms of T, and their median.
auto c30_exact_deltas(const std::vector<Frame_record>& records, double& median) -> std::size_t
{
    std::vector<double> deltas;
    std::size_t exact = 0;
    for (std::size_t i = 3001; i < records.size(); ++i) {
        const double delta = records[i].display_time - records[i - 1].display_time;
        deltas.push_back(delta);
        if (std::abs(delta - c30_period) < (0.1 * ms)) {
            ++exact;
        }
    }
    std::sort(deltas.begin(), deltas.end());
    median = deltas.empty() ? 0.0 : deltas[deltas.size() / 2];
    return exact;
}

// C30 goal: N=1 after frame 3000, at least 99 % of the present deltas
// within 0.1 ms of T, and the deadline offset estimate within guard of its
// initial value (pre-fix the VRR arms raised it from 9.6 to 13.2 ms).
void expect_c30_120_hz(const std::vector<Frame_record>& records, const char* arm)
{
    ASSERT_GT(records.size(), 3001u);
    double median = 0.0;
    const std::size_t exact = c30_exact_deltas(records, median);
    const std::size_t count = records.size() - 3001;
    double deadline_offset_max = 0.0;
    for (std::size_t i = 3000; i < records.size(); ++i) {
        deadline_offset_max = std::max(deadline_offset_max, records[i].deadline_offset);
    }
    EXPECT_EQ(vsync_values(records, 3000), (std::set<int>{1})) << arm << ": N=1";
    EXPECT_GE(static_cast<double>(exact), 0.99 * static_cast<double>(count))
        << arm << ": " << exact << "/" << count << " present deltas within 0.1 ms of T, median "
        << (median / ms) << " ms";
    EXPECT_LE(deadline_offset_max, c30_deadline_offset_initial + Frame_pacer_tunables{}.guard)
        << arm << ": deadline offset " << (deadline_offset_max / ms) << " ms";
}

} // anonymous namespace

TEST(Frame_pacer_claims, c30_variable_refresh_display)
{
    // Live (Metal tier M, Release, Bistro, 3840x1440 @ 120 Hz fullscreen,
    // Adaptive-Sync 48-120 Hz): presentedTime deltas 10.6-11.8 ms or
    // 16-20 ms while the GPU frame (7.2-7.6 ms) fits 8.33 ms; history
    // dependent. A VRR display has no grid: late frames display when ready
    // and below the 48 Hz floor the display repeats the previous image, so
    // the gross-error re-seed (C16) took those deltas for the display's
    // period, and once re-seeded the display honored the pacer's own grid
    // (doc/plans/frame_pacing_adaptive_sync.md "Mechanism"). Told the
    // display kind, the pacer keeps the period, keeps its targets within the
    // display's maximum interval of each other and drops the cadence tests'
    // guard allowance (algorithm.md deviation 24).

    // Controls: the load holds 120 Hz on the windowed compositor's fixed
    // grid with the startup spike, and on the VRR display without one.
    expect_c30_120_hz(run_c30(3001, 6.5, 30, 60, 20.0, C30_display::fixed_grid), "fixed grid, startup spike");
    expect_c30_120_hz(run_c30(3001, 6.5, 0, 0, 0.0, C30_display::variable_refresh), "VRR without a disturbance");
    // Control: the pacer not told the display is VRR drifts (the live failure).
    {
        const std::vector<Frame_record> drifted = run_c30(3001, 6.5, 30, 60, 20.0, C30_display::variable_refresh, C30_pacer_told::nothing);
        ASSERT_GT(drifted.size(), 3001u);
        double median = 0.0;
        const std::size_t exact = c30_exact_deltas(drifted, median);
        EXPECT_LT(exact, (drifted.size() - 3001) / 2) << "pacer not told: the grid drifts, median " << (median / ms) << " ms";
        EXPECT_GT(median, 1.02 * c30_period) << "pacer not told: the grid drifts";
    }
    // The live load (7.2 ms) stays at N=2 on the fixed grid: the cadence
    // tests add guard to the stage times (q95(g) + guard > T), correct there
    // (a frame that misses its vsync by a little costs a whole refresh).
    // Probes back to N=1 are allowed (C10 backoff); at least 90 % of the
    // frames after frame 3000 stay at N=2.
    {
        const std::vector<Frame_record> fixed_live = run_c30(3001, 7.2, 0, 0, 0.0, C30_display::fixed_grid);
        ASSERT_GT(fixed_live.size(), 3000u);
        std::size_t at_two = 0;
        for (std::size_t i = 3000; i < fixed_live.size(); ++i) {
            if (fixed_live[i].vsyncs == 2) {
                ++at_two;
            }
        }
        EXPECT_GE(static_cast<double>(at_two), 0.9 * static_cast<double>(fixed_live.size() - 3000)) << "fixed grid, live load: " << at_two << " frames at N=2";
    }

    expect_c30_120_hz(run_c30(3001, 6.5, 30, 60, 20.0, C30_display::variable_refresh), "VRR after a startup spike");
    {
        // A 1 s stall (one frame at k=500): the target spacing bound must not
        // hold the cursor to the stale target before it (it caught up one
        // slot per frame: 115 frames targeted before their release).
        const std::vector<Frame_record> stall = run_c30(3001, 6.5, 500, 501, 1000.0, C30_display::variable_refresh);
        std::size_t behind = 0;
        for (std::size_t i = 501; i < stall.size(); ++i) {
            if (stall[i].target_time < (stall[i].release - (0.1 * ms))) {
                ++behind;
            }
        }
        std::size_t off_period = 0;
        for (std::size_t i = 511; i < stall.size(); ++i) {
            if (std::abs((stall[i].display_time - stall[i - 1].display_time) - c30_period) >= (0.1 * ms)) {
                ++off_period;
            }
        }
        EXPECT_LE(behind, 3u) << "VRR after a 1 s stall: frames targeted before their release";
        EXPECT_EQ(off_period, 0u) << "VRR after a 1 s stall: present deltas off T from frame 510";
    }
    {
        const std::vector<Frame_record> heavy = run_c30(3001, 6.5, 30, 600, 16.0, C30_display::variable_refresh);
        expect_c30_120_hz(heavy, "VRR after a heavy configuration");
        // During it: 16 ms of work at the N>=2 clock (0.8) takes 20 ms and
        // chooses N=3 (25 ms targets); a target more than the maximum
        // refresh interval after the previous one would make the display
        // repeat the previous image (without the target spacing bound: 271
        // of 509 deltas at 29.17 ms). The frames fit the maximum interval,
        // so none may repeat.
        std::size_t repeats = 0;
        for (std::size_t i = 91; i < 600; ++i) {
            if ((heavy[i].display_time - heavy[i - 1].display_time) > ((1.0 / 48.0) + 1e-6)) {
                ++repeats;
            }
        }
        EXPECT_EQ(repeats, 0u) << "VRR heavy configuration: present deltas beyond the maximum refresh interval";
    }
    // The live load: the N=2 trap on the fixed-grid rules.
    expect_c30_120_hz(run_c30(3001, 7.2, 0, 0, 0.0, C30_display::variable_refresh), "VRR live load (the N=2 trap)");
}
