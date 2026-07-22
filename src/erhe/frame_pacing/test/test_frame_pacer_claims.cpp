// Frame pacer parity suite (implementation plan step P1.2).
//
// C++ replication of the verification scenarios and claims C1..C9 from
// scripts/frame_pacing_sim.py / doc/frame_pacing_control_model.md section 11.
// Constants match the Python reference; RNG streams need not match - all
// assertions are on invariants and bounds, not sample paths.

#include "frame_pacing_plant.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <random>
#include <set>

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
    // The measured driver behavior (doc/frame_pacing_present_timing_driver_report.md):
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
