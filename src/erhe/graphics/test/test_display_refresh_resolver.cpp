// Display_refresh_resolver: the decision between the window display mode,
// VK_EXT_present_timing's refreshDuration / refreshInterval and the init
// calibration from displayed times (doc/frame_pacing/inputs.md 3.1).

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
using erhe::graphics::Display_time_sample_source;

constexpr double period_120_hz = 1.0 / 120.0;
constexpr double period_240_hz = 1.0 / 240.0;
constexpr uint64_t ns_120_hz = 8333333;
constexpr uint64_t ns_240_hz = 4166666;

class Resolver_fixture
{
public:
    Resolver_fixture()
        : resolver{erhe::graphics::log_display_refresh} // created once by the test main's initialize_logging()
    {
        resolver.set_apply_callback([this](const double period) { applied.push_back(period); });
    }

    // Feeds n consecutive presents displayed period seconds apart; work is
    // each frame's own work time for the fast-frame gate.
    void feed(const Display_time_sample_source source, const int n, const double period, const double work)
    {
        resolver.set_frame_work_lookup([work](const std::int64_t) { return work; });
        for (int i = 0; i < n; ++i) {
            resolver.note_present();
            resolver.add_display_time_sample(source, frame_id, 1.0 + static_cast<double>(frame_id) * period);
            ++frame_id;
        }
    }

    Display_refresh_resolver resolver;
    std::vector<double>      applied;
    std::int64_t             frame_id{0};
};

auto near(const double a, const double b) -> bool
{
    return std::abs(a - b) <= 1e-6;
}

} // namespace

TEST(Display_refresh_resolver, window_mode_alone_is_used)
{
    Resolver_fixture f;
    f.resolver.set_window_period(period_120_hz, "Dell");
    const Display_refresh_report r = f.resolver.get_report();
    EXPECT_TRUE(near(r.resolved_period_seconds, period_120_hz));
    EXPECT_EQ(r.resolved_source, Display_refresh_source::window_mode);
    ASSERT_EQ(f.applied.size(), 1u);
}

TEST(Display_refresh_resolver, agreeing_sources_take_the_vulkan_value)
{
    Resolver_fixture f;
    f.resolver.set_window_period(period_120_hz, "Dell");
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
    f.resolver.set_window_period(period_120_hz, "Dell");
    f.resolver.set_vulkan_timing(ns_240_hz, ns_240_hz);
    Display_refresh_report r = f.resolver.get_report();
    EXPECT_TRUE(near(r.resolved_period_seconds, period_120_hz));
    EXPECT_EQ(r.resolved_source, Display_refresh_source::window_mode);
    EXPECT_FALSE(r.calibration_done);

    // The display runs at 120 Hz with fast frames: the calibration confirms
    // the window mode and rejects the query.
    f.feed(Display_time_sample_source::present_feedback, 40, period_120_hz, 0.002);
    r = f.resolver.get_report();
    EXPECT_TRUE(r.calibration_done);
    EXPECT_EQ(r.calibration_source, Display_time_sample_source::present_feedback);
    EXPECT_TRUE(near(r.calibration_period_seconds, period_120_hz));
    EXPECT_TRUE(near(r.resolved_period_seconds, period_120_hz));
    EXPECT_EQ(r.resolved_source, Display_refresh_source::window_mode);
}

TEST(Display_refresh_resolver, calibration_overrides_both_sources_when_neither_matches)
{
    Resolver_fixture f;
    f.resolver.set_window_period(1.0 / 60.0, "panel");
    f.resolver.set_vulkan_timing(ns_240_hz, ns_240_hz);
    f.feed(Display_time_sample_source::present_wait, 40, period_120_hz, 0.002);
    const Display_refresh_report r = f.resolver.get_report();
    EXPECT_TRUE(r.calibration_done);
    EXPECT_EQ(r.resolved_source, Display_refresh_source::calibration);
    EXPECT_TRUE(near(r.resolved_period_seconds, period_120_hz));
    EXPECT_TRUE(near(f.applied.back(), period_120_hz));
}

TEST(Display_refresh_resolver, jittered_measurement_confirms_the_nearby_source)
{
    // Measured 8.79 ms on a 120 Hz display (vkWaitForPresentKHR wake jitter):
    // within the calibration tolerance of the window mode, so the exact
    // mode is kept; the measurement does not replace it.
    Resolver_fixture f;
    f.resolver.set_window_period(period_120_hz, "Dell");
    f.feed(Display_time_sample_source::present_wait, 40, 0.008793, 0.002);
    const Display_refresh_report r = f.resolver.get_report();
    EXPECT_TRUE(r.calibration_done);
    EXPECT_TRUE(near(r.calibration_period_seconds, 0.008793));
    EXPECT_TRUE(near(r.resolved_period_seconds, period_120_hz));
    EXPECT_EQ(r.resolved_source, Display_refresh_source::window_mode);
}

TEST(Display_refresh_resolver, slow_frames_do_not_prove_a_cadence)
{
    // Frames that take longer than half the observed interval could be the
    // limiter themselves: their deltas are rejected and the calibration ends
    // without an estimate after the present bound, leaving the queried
    // sources' decision in place.
    Resolver_fixture f;
    f.resolver.set_window_period(period_120_hz, "Dell");
    f.resolver.set_vulkan_timing(ns_240_hz, ns_240_hz);
    f.feed(Display_time_sample_source::present_feedback, static_cast<int>(Display_refresh_resolver::s_max_presents), 2.0 * period_120_hz, 0.012);
    const Display_refresh_report r = f.resolver.get_report();
    EXPECT_TRUE(r.calibration_done);
    EXPECT_EQ(r.calibration_source, Display_time_sample_source::none);
    EXPECT_GT(r.calibration_rejected_count, 0u);
    EXPECT_EQ(r.resolved_source, Display_refresh_source::window_mode);
}

TEST(Display_refresh_resolver, better_source_silences_lesser_ones)
{
    Resolver_fixture f;
    EXPECT_TRUE(f.resolver.wants_sample(Display_time_sample_source::present_wait));
    EXPECT_TRUE(f.resolver.wants_sample(Display_time_sample_source::present_return));
    f.feed(Display_time_sample_source::present_feedback, 1, period_120_hz, 0.001);
    EXPECT_FALSE(f.resolver.wants_sample(Display_time_sample_source::present_wait));
    EXPECT_FALSE(f.resolver.wants_sample(Display_time_sample_source::present_return));
    EXPECT_TRUE(f.resolver.wants_sample(Display_time_sample_source::present_feedback));
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

TEST(Display_refresh_resolver, window_display_change_restarts_calibration)
{
    Resolver_fixture f;
    f.resolver.set_window_period(period_120_hz, "Dell");
    f.feed(Display_time_sample_source::present_feedback, 40, period_120_hz, 0.002);
    EXPECT_FALSE(f.resolver.is_calibrating());
    f.resolver.set_window_period(period_240_hz, "panel");
    EXPECT_TRUE(f.resolver.is_calibrating());
    const Display_refresh_report r = f.resolver.get_report();
    EXPECT_TRUE(near(r.resolved_period_seconds, period_240_hz));
    EXPECT_EQ(r.resolved_source, Display_refresh_source::window_mode);
}
