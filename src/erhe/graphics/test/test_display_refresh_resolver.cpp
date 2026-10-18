// Display_refresh_resolver: the decision between the window display mode,
// VK_EXT_present_timing's refreshDuration / refreshInterval and the init
// calibration (free run, then plain-FIFO lock) from GPU frame end times
// (doc/frame_pacing/inputs.md 3.1).

#include "erhe_graphics/display_refresh_resolver.hpp"
#include "erhe_graphics/graphics_log.hpp"

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <cmath>
#include <limits>

namespace {

using erhe::graphics::Display_refresh_resolver;
using erhe::graphics::Display_refresh_report;
using erhe::graphics::Display_refresh_source;
using erhe::graphics::Display_refresh_mode;
using erhe::graphics::Display_refresh_calibration_phase;
using erhe::graphics::Display_refresh_present_kind;
using erhe::graphics::Display_refresh_policy;

constexpr double   period_120_hz = 1.0 / 120.0;
constexpr double   period_240_hz = 1.0 / 240.0;
constexpr uint64_t ns_120_hz     = 8333333;
constexpr uint64_t ns_240_hz     = 4166666;

class Resolver_fixture
{
public:
    Resolver_fixture()
        : resolver{erhe::graphics::log_display_refresh} // created once by the test main's initialize_logging()
    {
        resolver.set_apply_callback([this](const double period) { applied.push_back(period); });
    }

    // Presents n frames in the mode the resolver's phase asks for, each
    // frame's GPU end "interval" seconds after the previous one.
    void present(const int n, const double interval)
    {
        for (int i = 0; i < n; ++i) {
            const Display_refresh_calibration_phase phase = resolver.get_calibration_phase();
            const Display_refresh_present_kind kind =
                (phase == Display_refresh_calibration_phase::free_run) ? Display_refresh_present_kind::free_running :
                (phase == Display_refresh_calibration_phase::locked)   ? Display_refresh_present_kind::fifo :
                                                                         Display_refresh_present_kind::other;
            resolver.note_present(frame_id, kind);
            gpu_end += interval;
            resolver.add_gpu_frame_end(frame_id, gpu_end);
            ++frame_id;
        }
    }

    // A whole calibration: production frames in the free run, then the
    // locked cadence.
    void calibrate(const double production, const double locked)
    {
        resolver.set_calibration_modes(true, true);
        present(static_cast<int>(Display_refresh_resolver::s_free_run_skip + Display_refresh_resolver::s_free_run_deltas) + 1, production);
        EXPECT_EQ(resolver.get_calibration_phase(), Display_refresh_calibration_phase::locked);
        present(static_cast<int>(Display_refresh_resolver::s_locked_settle + Display_refresh_resolver::s_locked_deltas) + 1, locked);
        EXPECT_FALSE(resolver.is_calibrating());
    }

    Display_refresh_resolver resolver;
    std::vector<double>      applied;
    std::int64_t             frame_id{0};
    double                   gpu_end{1.0};
};

auto near(const double a, const double b) -> bool
{
    return std::abs(a - b) <= 1e-6;
}

} // namespace

TEST(Display_refresh_resolver, window_mode_alone_is_used)
{
    Resolver_fixture f;
    f.resolver.set_window_period(period_120_hz, "Dell", 2);
    const Display_refresh_report r = f.resolver.get_report();
    EXPECT_TRUE(near(r.resolved_period_seconds, period_120_hz));
    EXPECT_EQ(r.resolved_source, Display_refresh_source::window_mode);
    ASSERT_EQ(f.applied.size(), 1u);
}

TEST(Display_refresh_resolver, agreeing_sources_take_the_vulkan_value)
{
    Resolver_fixture f;
    f.resolver.set_window_period(period_120_hz, "Dell", 2);
    f.resolver.set_vulkan_timing(ns_120_hz, ns_120_hz);
    const Display_refresh_report r = f.resolver.get_report();
    EXPECT_TRUE(near(r.resolved_period_seconds, static_cast<double>(ns_120_hz) * 1e-9));
    EXPECT_EQ(r.resolved_source, Display_refresh_source::vulkan_timing);
    EXPECT_EQ(r.vulkan_mode, Display_refresh_mode::fixed);
}

TEST(Display_refresh_resolver, disagreeing_sources_take_the_window_until_calibrated)
{
    // The measured case: a window on a 120 Hz display, the swapchain query
    // answering with the other display's 240 Hz.
    Resolver_fixture f;
    f.resolver.set_window_period(period_120_hz, "Dell", 2);
    f.resolver.set_vulkan_timing(ns_240_hz, ns_240_hz);
    Display_refresh_report r = f.resolver.get_report();
    EXPECT_TRUE(near(r.resolved_period_seconds, period_120_hz));
    EXPECT_EQ(r.resolved_source, Display_refresh_source::window_mode);
    EXPECT_FALSE(r.calibration_done);

    // Production 2 ms, the display locks the GPU to 8.33 ms: the window
    // mode is confirmed, the query rejected.
    f.calibrate(0.002, period_120_hz);
    r = f.resolver.get_report();
    EXPECT_TRUE(r.calibration_done);
    EXPECT_TRUE(r.calibration_verified);
    EXPECT_TRUE(near(r.locked_period_seconds, period_120_hz));
    EXPECT_TRUE(near(r.free_run_period_seconds, 0.002));
    EXPECT_TRUE(near(r.resolved_period_seconds, period_120_hz));
    EXPECT_EQ(r.resolved_source, Display_refresh_source::window_mode);
}

TEST(Display_refresh_resolver, calibration_overrides_both_sources_when_neither_matches)
{
    Resolver_fixture f;
    f.resolver.set_window_period(1.0 / 60.0, "panel", 2);
    f.resolver.set_vulkan_timing(ns_240_hz, ns_240_hz);
    f.calibrate(0.002, period_120_hz);
    const Display_refresh_report r = f.resolver.get_report();
    EXPECT_TRUE(r.calibration_verified);
    EXPECT_EQ(r.resolved_source, Display_refresh_source::calibration);
    EXPECT_TRUE(near(r.resolved_period_seconds, period_120_hz));
    EXPECT_TRUE(near(f.applied.back(), period_120_hz));
}

TEST(Display_refresh_resolver, production_near_the_cadence_proves_nothing)
{
    // GPU-bound startup: the free run produces a frame every 15 ms and the
    // locked phase shows 16.7 ms. The application could have set that
    // cadence, so the calibration stays unverified and the queried decision
    // stands.
    Resolver_fixture f;
    f.resolver.set_window_period(period_120_hz, "Dell", 2);
    f.resolver.set_vulkan_timing(ns_240_hz, ns_240_hz);
    f.calibrate(0.015, 2.0 * period_120_hz);
    const Display_refresh_report r = f.resolver.get_report();
    EXPECT_TRUE(r.calibration_done);
    EXPECT_FALSE(r.calibration_verified);
    EXPECT_TRUE(near(r.locked_period_seconds, 2.0 * period_120_hz));
    EXPECT_EQ(r.resolved_source, Display_refresh_source::window_mode);
    EXPECT_TRUE(near(r.resolved_period_seconds, period_120_hz));
}

TEST(Display_refresh_resolver, jittered_cadence_confirms_the_nearby_source)
{
    // Locked deltas 3 % off the nominal: within the tolerance, so the exact
    // window mode is kept rather than the measurement.
    Resolver_fixture f;
    f.resolver.set_window_period(period_120_hz, "Dell", 2);
    f.calibrate(0.002, period_120_hz * 1.03);
    const Display_refresh_report r = f.resolver.get_report();
    EXPECT_TRUE(r.calibration_verified);
    EXPECT_TRUE(near(r.resolved_period_seconds, period_120_hz));
    EXPECT_EQ(r.resolved_source, Display_refresh_source::window_mode);
}

TEST(Display_refresh_resolver, outliers_do_not_anchor_the_cluster)
{
    Resolver_fixture f;
    f.resolver.set_window_period(period_120_hz, "Dell", 2);
    f.resolver.set_calibration_modes(true, true);
    f.present(static_cast<int>(Display_refresh_resolver::s_free_run_skip + Display_refresh_resolver::s_free_run_deltas) + 1, 0.002);
    // Settle, then mostly one-period deltas with a few doubled ones.
    f.present(static_cast<int>(Display_refresh_resolver::s_locked_settle) + 1, period_120_hz);
    for (int i = 0; i < 32; ++i) {
        f.present(1, ((i % 8) == 3) ? (2.0 * period_120_hz) : period_120_hz);
    }
    const Display_refresh_report r = f.resolver.get_report();
    EXPECT_TRUE(r.calibration_done);
    EXPECT_TRUE(r.calibration_verified);
    EXPECT_TRUE(near(r.locked_period_seconds, period_120_hz));
    EXPECT_EQ(r.locked_cluster_count, 28u);
}

TEST(Display_refresh_resolver, no_fifo_ends_the_calibration_at_once)
{
    Resolver_fixture f;
    f.resolver.set_window_period(period_120_hz, "Dell", 2);
    f.resolver.set_calibration_modes(true, false);
    EXPECT_FALSE(f.resolver.is_calibrating());
    const Display_refresh_report r = f.resolver.get_report();
    EXPECT_TRUE(r.calibration_done);
    EXPECT_FALSE(r.calibration_verified);
    EXPECT_EQ(r.resolved_source, Display_refresh_source::window_mode);
}

TEST(Display_refresh_resolver, no_free_run_mode_leaves_the_result_unverified)
{
    Resolver_fixture f;
    f.resolver.set_window_period(period_120_hz, "Dell", 2);
    f.resolver.set_calibration_modes(false, true);
    EXPECT_EQ(f.resolver.get_calibration_phase(), Display_refresh_calibration_phase::locked);
    f.present(static_cast<int>(Display_refresh_resolver::s_locked_settle + Display_refresh_resolver::s_locked_deltas) + 1, period_120_hz);
    const Display_refresh_report r = f.resolver.get_report();
    EXPECT_TRUE(r.calibration_done);
    EXPECT_FALSE(r.calibration_verified);
    EXPECT_TRUE(near(r.locked_period_seconds, period_120_hz));
    EXPECT_EQ(r.resolved_source, Display_refresh_source::window_mode);
}

TEST(Display_refresh_resolver, present_bound_ends_a_calibration_that_never_locks)
{
    Resolver_fixture f;
    f.resolver.set_window_period(period_120_hz, "Dell", 2);
    f.resolver.set_calibration_modes(true, true);
    // Frames presented, but no GPU frame end ever harvested (no bracket).
    for (std::size_t i = 0; i < Display_refresh_resolver::s_max_presents; ++i) {
        f.resolver.note_present(f.frame_id++, Display_refresh_present_kind::free_running);
    }
    EXPECT_FALSE(f.resolver.is_calibrating());
    EXPECT_FALSE(f.resolver.get_report().calibration_verified);
}

TEST(Display_refresh_resolver, variable_and_decoupled_modes)
{
    Resolver_fixture f;
    f.resolver.set_vulkan_timing(ns_120_hz, std::numeric_limits<uint64_t>::max());
    Display_refresh_report r = f.resolver.get_report();
    EXPECT_EQ(r.vulkan_mode, Display_refresh_mode::variable);
    EXPECT_TRUE(near(r.resolved_period_seconds, static_cast<double>(ns_120_hz) * 1e-9));

    // Images visible for 2 vblank intervals: the grid counts the vblank.
    f.resolver.set_vulkan_timing(2 * ns_240_hz, ns_240_hz);
    r = f.resolver.get_report();
    EXPECT_EQ(r.vulkan_mode, Display_refresh_mode::decoupled);
    EXPECT_TRUE(near(r.resolved_period_seconds, static_cast<double>(ns_240_hz) * 1e-9));
}

TEST(Display_refresh_resolver, window_display_change_drops_the_calibration_result)
{
    Resolver_fixture f;
    f.resolver.set_window_period(period_120_hz, "Dell", 2);
    f.resolver.set_vulkan_timing(ns_240_hz, ns_240_hz);
    f.calibrate(0.002, period_120_hz);
    EXPECT_FALSE(f.resolver.is_calibrating());
    EXPECT_TRUE(f.resolver.get_report().calibration_verified);
    // The window moves to the 240 Hz panel: the measured 120 Hz cadence no
    // longer applies; the window mode and the query now agree.
    f.resolver.set_window_period(period_240_hz, "panel", 2);
    EXPECT_FALSE(f.resolver.is_calibrating());
    const Display_refresh_report r = f.resolver.get_report();
    EXPECT_FALSE(r.calibration_verified);
    EXPECT_TRUE(near(r.resolved_period_seconds, static_cast<double>(ns_240_hz) * 1e-9));
    EXPECT_EQ(r.resolved_source, Display_refresh_source::vulkan_timing);
}

TEST(Display_refresh_resolver, override_policy_wins_over_everything)
{
    Resolver_fixture f;
    f.resolver.set_policy(Display_refresh_policy::override_rate, 1.0 / 90.0);
    f.resolver.set_window_period(period_120_hz, "Dell", 2);
    f.resolver.set_vulkan_timing(ns_240_hz, ns_240_hz);
    f.calibrate(0.002, period_120_hz);
    const Display_refresh_report r = f.resolver.get_report();
    EXPECT_TRUE(near(r.resolved_period_seconds, 1.0 / 90.0));
    EXPECT_EQ(r.resolved_source, Display_refresh_source::override_rate);
    EXPECT_TRUE(r.sources_disagree);
    EXPECT_EQ(r.display_count, 2u);
}

TEST(Display_refresh_resolver, forced_source_is_used_when_available)
{
    Resolver_fixture f;
    f.resolver.set_policy(Display_refresh_policy::vulkan_timing, 0.0);
    f.resolver.set_window_period(period_120_hz, "Dell", 2);
    f.resolver.set_vulkan_timing(ns_240_hz, ns_240_hz);
    Display_refresh_report r = f.resolver.get_report();
    EXPECT_EQ(r.resolved_source, Display_refresh_source::vulkan_timing);
    // The calibration contradicts the forced query; the policy still holds.
    f.calibrate(0.002, period_120_hz);
    r = f.resolver.get_report();
    EXPECT_EQ(r.resolved_source, Display_refresh_source::vulkan_timing);
    EXPECT_TRUE(near(r.resolved_period_seconds, static_cast<double>(ns_240_hz) * 1e-9));
    EXPECT_TRUE(r.sources_disagree);
}

TEST(Display_refresh_resolver, unavailable_forced_source_falls_back_to_automatic)
{
    Resolver_fixture f;
    f.resolver.set_policy(Display_refresh_policy::calibration, 0.0);
    f.resolver.set_window_period(period_120_hz, "Dell", 1);
    // GPU-bound startup: the calibration cannot verify, so the forced
    // source is unavailable and the automatic decision (window mode) serves.
    f.calibrate(0.015, 2.0 * period_120_hz);
    const Display_refresh_report r = f.resolver.get_report();
    EXPECT_FALSE(r.calibration_verified);
    EXPECT_EQ(r.resolved_source, Display_refresh_source::window_mode);
    EXPECT_TRUE(near(r.resolved_period_seconds, period_120_hz));

    Display_refresh_policy parsed = Display_refresh_policy::automatic;
    EXPECT_TRUE(erhe::graphics::parse_display_refresh_policy("override", parsed));
    EXPECT_EQ(parsed, Display_refresh_policy::override_rate);
    EXPECT_FALSE(erhe::graphics::parse_display_refresh_policy("bogus", parsed));
}
