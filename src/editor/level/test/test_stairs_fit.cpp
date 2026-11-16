#include "level/stairs_fit.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

namespace {

using editor::Stairs_fit;
using editor::Stairs_fit_refusal;
using editor::Stairs_height_sample;
using editor::Stairs_riser;
using editor::Stairs_rules;
using editor::Stairs_zone;

constexpr float c_tolerance = 1.0e-4f;

// A polyline profile sampled every 0.05 m; knots: (distance, height) with rising distances.
auto make_profile(const std::vector<Stairs_height_sample>& knots) -> std::vector<Stairs_height_sample>
{
    std::vector<Stairs_height_sample> samples;
    for (std::size_t i = 0; (i + 1) < knots.size(); ++i) {
        const float length = knots[i + 1].distance - knots[i].distance;
        const int   count  = std::max(1, static_cast<int>(std::lround(length / 0.05f)));
        for (int k = (i == 0) ? 0 : 1; k <= count; ++k) {
            const float t = static_cast<float>(k) / static_cast<float>(count);
            samples.push_back(
                Stairs_height_sample{
                    .distance = knots[i].distance + (length * t),
                    .height   = knots[i].height + ((knots[i + 1].height - knots[i].height) * t)
                }
            );
        }
    }
    return samples;
}

// What every fit holds: risers in order a tread apart, steps within the rise limit, the surface within one rise.
void expect_rules_hold(const Stairs_fit& fit, const Stairs_rules& rules)
{
    for (std::size_t i = 0, end = fit.risers.size(); i < end; ++i) {
        const Stairs_riser& riser = fit.risers[i];
        EXPECT_LE(std::abs(riser.level_after - riser.level_before), rules.max_rise * (1.0f + 2.0e-6f)) << "riser " << i;
        EXPECT_GE(riser.distance, -c_tolerance) << "riser " << i;
        EXPECT_LE(riser.distance, fit.length + c_tolerance) << "riser " << i;
        if (i > 0) {
            EXPECT_GE(riser.distance - fit.risers[i - 1].distance, rules.min_tread - (3.0f * c_tolerance)) << "riser " << i;
            EXPECT_NEAR(riser.level_before, fit.risers[i - 1].level_after, c_tolerance) << "riser " << i;
        }
    }
    EXPECT_LE(fit.max_deviation, rules.max_rise + editor::c_stairs_deviation_slack);
    EXPECT_EQ(fit.step_count, static_cast<int>(fit.risers.size()));
    int flight_steps = 0;
    for (const editor::Stairs_flight& flight : fit.flights) {
        flight_steps += flight.step_count;
        EXPECT_LE(std::abs(flight.rise), rules.max_rise + c_tolerance);
        EXPECT_NEAR(flight.tread, rules.min_tread, c_tolerance);
    }
    EXPECT_EQ(flight_steps, fit.step_count);
}

TEST(Stairs_fit, the_flight_slope_makes_one_flight)
{
    const Stairs_rules rules{.max_rise = 0.2f, .min_tread = 0.3f};
    Stairs_fit fit;
    const Stairs_fit_refusal refusal = editor::fit_stairs(make_profile({{0.0f, 0.0f}, {3.0f, 2.0f}}), rules, {}, fit);
    ASSERT_EQ(refusal.message, "");
    expect_rules_hold(fit, rules);
    ASSERT_EQ(fit.step_count, 10);
    ASSERT_EQ(fit.flights.size(), 1u);
    EXPECT_EQ(fit.flights[0].step_count, 10);
    EXPECT_NEAR(fit.flights[0].rise, 0.2f, c_tolerance);
    EXPECT_NEAR(fit.flights[0].begin, 0.0f, 2.0f * c_tolerance);
    EXPECT_NEAR(fit.flights[0].end, 3.0f, 2.0f * c_tolerance);
    EXPECT_TRUE(fit.landings.empty());
    for (std::size_t i = 0; i < fit.risers.size(); ++i) {
        EXPECT_NEAR(fit.risers[i].distance, 0.3f * static_cast<float>(i), 2.0f * c_tolerance);
    }
    EXPECT_NEAR(fit.total_rise, 2.0f, 1.0e-6f);
}

TEST(Stairs_fit, a_descent_at_the_flight_slope_makes_one_flight)
{
    const Stairs_rules rules{.max_rise = 0.2f, .min_tread = 0.3f};
    Stairs_fit fit;
    const Stairs_fit_refusal refusal = editor::fit_stairs(make_profile({{0.0f, 2.0f}, {3.0f, 0.0f}}), rules, {}, fit);
    ASSERT_EQ(refusal.message, "");
    expect_rules_hold(fit, rules);
    ASSERT_EQ(fit.flights.size(), 1u);
    EXPECT_EQ(fit.flights[0].step_count, 10);
    EXPECT_NEAR(fit.flights[0].rise, -0.2f, c_tolerance);
    EXPECT_TRUE(fit.landings.empty());
    // The top tread is at the start and the last riser at the end.
    EXPECT_NEAR(fit.risers.front().distance, 0.3f, 2.0f * c_tolerance);
    EXPECT_NEAR(fit.risers.back().distance, 3.0f, 2.0f * c_tolerance);
    EXPECT_NEAR(fit.total_rise, -2.0f, 1.0e-6f);
}

TEST(Stairs_fit, half_the_flight_slope_alternates_flights_and_landings)
{
    const Stairs_rules rules{.max_rise = 0.2f, .min_tread = 0.3f};
    Stairs_fit fit;
    const Stairs_fit_refusal refusal = editor::fit_stairs(make_profile({{0.0f, 0.0f}, {6.0f, 2.0f}}), rules, {}, fit);
    ASSERT_EQ(refusal.message, "");
    expect_rules_hold(fit, rules);
    EXPECT_EQ(fit.step_count, 10);
    // Flights of three steps cross the curve from one rise below it to one rise above it.
    ASSERT_EQ(fit.flights.size(), 4u);
    EXPECT_EQ(fit.flights[0].step_count, 3);
    EXPECT_EQ(fit.flights[1].step_count, 3);
    EXPECT_EQ(fit.flights[2].step_count, 3);
    EXPECT_EQ(fit.flights[3].step_count, 1);
    ASSERT_EQ(fit.landings.size(), 4u);
    EXPECT_NEAR(fit.landings[0].begin, 0.0f, c_tolerance);
    EXPECT_NEAR(fit.landings[0].length, 0.6f, 3.0f * c_tolerance);
    EXPECT_NEAR(fit.landings[1].length, 0.9f, 3.0f * c_tolerance);
    EXPECT_NEAR(fit.landings[1].level, 0.6f, c_tolerance);
    EXPECT_NEAR(fit.end_level, 2.0f, 1.0e-6f);
    EXPECT_GT(fit.max_deviation, 0.1f);
}

TEST(Stairs_fit, a_level_curve_is_one_landing)
{
    const Stairs_rules rules{};
    Stairs_fit fit;
    const Stairs_fit_refusal refusal = editor::fit_stairs(make_profile({{0.0f, 1.5f}, {4.0f, 1.5f}}), rules, {}, fit);
    ASSERT_EQ(refusal.message, "");
    EXPECT_TRUE(fit.risers.empty());
    EXPECT_TRUE(fit.flights.empty());
    ASSERT_EQ(fit.landings.size(), 1u);
    EXPECT_NEAR(fit.landings[0].length, 4.0f, c_tolerance);
    EXPECT_NEAR(fit.landings[0].level, 1.5f, 1.0e-6f);
    EXPECT_EQ(fit.get_tread_count(), 1u);
    EXPECT_NEAR(fit.max_deviation, 0.0f, 1.0e-6f);
}

TEST(Stairs_fit, a_bump_below_one_rise_stays_a_landing)
{
    const Stairs_rules rules{};
    Stairs_fit fit;
    const Stairs_fit_refusal refusal = editor::fit_stairs(make_profile({{0.0f, 0.0f}, {2.0f, 0.15f}, {4.0f, 0.0f}}), rules, {}, fit);
    ASSERT_EQ(refusal.message, "");
    EXPECT_TRUE(fit.risers.empty());
    EXPECT_NEAR(fit.max_deviation, 0.15f, c_tolerance);
}

TEST(Stairs_fit, up_then_down_turns_at_the_top)
{
    const Stairs_rules rules{.max_rise = 0.2f, .min_tread = 0.3f};
    Stairs_fit fit;
    const Stairs_fit_refusal refusal = editor::fit_stairs(make_profile({{0.0f, 0.0f}, {3.0f, 1.0f}, {6.0f, 0.0f}}), rules, {}, fit);
    ASSERT_EQ(refusal.message, "");
    expect_rules_hold(fit, rules);
    ASSERT_EQ(fit.step_count, 10);
    float top = 0.0f;
    std::size_t ascending = 0;
    for (const Stairs_riser& riser : fit.risers) {
        top = std::max(top, riser.level_after);
        if (riser.level_after > riser.level_before) {
            ++ascending;
        }
    }
    EXPECT_EQ(ascending, 5u);
    EXPECT_NEAR(top, 1.0f, 1.0e-6f);
    // Ascending risers come first, and the top tread is at least a tread deep.
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_GT(fit.risers[i].level_after, fit.risers[i].level_before);
        EXPECT_LT(fit.risers[i + 5].level_after, fit.risers[i + 5].level_before);
    }
    EXPECT_GE(fit.risers[5].distance - fit.risers[4].distance, 0.3f - c_tolerance);
    EXPECT_NEAR(fit.total_rise, 0.0f, 1.0e-6f);
    EXPECT_NEAR(fit.end_level, 0.0f, 1.0e-6f);
}

TEST(Stairs_fit, a_curve_steeper_than_a_flight_is_refused_naming_the_place)
{
    const Stairs_rules rules{.max_rise = 0.2f, .min_tread = 0.3f};
    Stairs_fit fit;
    const Stairs_fit_refusal whole = editor::fit_stairs(make_profile({{0.0f, 0.0f}, {1.0f, 2.0f}}), rules, {}, fit);
    // Steeper than a flight from end to end: its steps do not fit its plan length.
    EXPECT_NE(whole.message.find("too short"), std::string::npos) << whole.message;
    EXPECT_GE(whole.distance, 0.0f);
    EXPECT_LE(whole.distance, 1.0f);

    // Level, a climb of 1 m over 0.3 m, level: refused at the climb.
    const Stairs_fit_refusal local = editor::fit_stairs(make_profile({{0.0f, 0.0f}, {2.0f, 0.0f}, {2.3f, 1.0f}, {5.0f, 1.0f}}), rules, {}, fit);
    EXPECT_NE(local.message.find("too steep"), std::string::npos) << local.message;
    EXPECT_GE(local.distance, 1.9f);
    EXPECT_LE(local.distance, 2.4f);
    EXPECT_NE(local.message.find("m along its plan"), std::string::npos) << local.message;

    // A vertical jump of one rise is one riser at or after it; of two rises a riser on each side; three are refused there.
    const std::vector<Stairs_height_sample> one_step{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.2f}, {2.0f, 0.2f}};
    EXPECT_EQ(editor::fit_stairs(one_step, rules, {}, fit).message, "");
    ASSERT_EQ(fit.risers.size(), 1u);
    EXPECT_GE(fit.risers[0].distance, 1.0f - c_tolerance);
    const std::vector<Stairs_height_sample> two_steps{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.4f}, {2.0f, 0.4f}};
    EXPECT_EQ(editor::fit_stairs(two_steps, rules, {}, fit).message, "");
    ASSERT_EQ(fit.risers.size(), 2u);
    EXPECT_LE(fit.risers[0].distance, 1.0f + c_tolerance);
    EXPECT_GE(fit.risers[1].distance, 1.0f - c_tolerance);
    const std::vector<Stairs_height_sample> three_steps{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.6f}, {2.0f, 0.6f}};
    const Stairs_fit_refusal jump = editor::fit_stairs(three_steps, rules, {}, fit);
    EXPECT_NE(jump.message, "");
    EXPECT_NEAR(jump.distance, 1.0f, 0.05f);

    // Down and up at the flight slope leave no tread at the bottom: refused there.
    const Stairs_fit_refusal valley = editor::fit_stairs(make_profile({{0.0f, 1.0f}, {1.5f, 0.0f}, {3.0f, 1.0f}}), rules, {}, fit);
    EXPECT_NE(valley.message.find("too short"), std::string::npos) << valley.message;
    EXPECT_NEAR(valley.distance, 1.5f, 0.05f);
}

TEST(Stairs_fit, the_end_height_is_reached_exactly)
{
    const Stairs_rules rules{.max_rise = 0.2f, .min_tread = 0.3f};
    for (const float height : {0.05f, 0.37f, 1.37f, 2.5f, -1.11f}) { // each at least 5 % of the rise limit: a step
        Stairs_fit fit;
        const Stairs_fit_refusal refusal = editor::fit_stairs(make_profile({{0.0f, 0.25f}, {5.0f, 0.25f + height}}), rules, {}, fit);
        ASSERT_EQ(refusal.message, "") << height;
        expect_rules_hold(fit, rules);
        EXPECT_NEAR(fit.start_level, 0.25f, 1.0e-6f);
        EXPECT_NEAR(fit.end_level, 0.25f + height, 1.0e-6f) << height;
        EXPECT_NEAR(fit.get_tread_level(fit.get_tread_count() - 1), 0.25f + height, 1.0e-6f) << height;
        EXPECT_EQ(fit.step_count, static_cast<int>(std::ceil((std::abs(height) / 0.2f) - 1.0e-4f))) << height;
    }
}

TEST(Stairs_fit, a_curve_too_short_for_its_steps_says_so)
{
    // 1.9 m over 2.9 m is gentler than a flight (0.655 against 0.667), but 10 steps need 10 treads: 3 m.
    const Stairs_rules rules{.max_rise = 0.2f, .min_tread = 0.3f};
    Stairs_fit fit;
    const Stairs_fit_refusal refusal = editor::fit_stairs(make_profile({{0.0f, 0.0f}, {2.9f, 1.9f}}), rules, {}, fit);
    EXPECT_NE(refusal.message.find("too short"), std::string::npos) << refusal.message;
    EXPECT_NE(refusal.message.find("needs 10 steps with treads of at least 0.300 m, 3.000 m of plan, and has 2.900 m"), std::string::npos) << refusal.message;
    EXPECT_EQ(refusal.message.find("too steep"), std::string::npos) << refusal.message;
    EXPECT_GE(refusal.distance, 0.0f);
    EXPECT_LE(refusal.distance, 2.9f);
    // Going down, the top tread is at the start: the same count.
    EXPECT_NE(editor::fit_stairs(make_profile({{0.0f, 1.9f}, {2.9f, 0.0f}}), rules, {}, fit).message.find("too short"), std::string::npos);
    // 3 m is enough.
    EXPECT_EQ(editor::fit_stairs(make_profile({{0.0f, 0.0f}, {3.0f, 1.9f}}), rules, {}, fit).message, "");
}

TEST(Stairs_fit, a_climb_far_below_a_step_stays_level)
{
    const Stairs_rules rules{.max_rise = 0.2f, .min_tread = 0.3f};
    Stairs_fit fit;
    // 0.15 mm and 9 mm (below 5 % of the rise limit, 10 mm) make no riser; 11 mm makes one step.
    for (const float height : {0.00015f, 0.009f, -0.009f}) {
        ASSERT_EQ(editor::fit_stairs(make_profile({{0.0f, 1.0f}, {4.0f, 1.0f + height}}), rules, {}, fit).message, "") << height;
        EXPECT_TRUE(fit.risers.empty()) << height;
        EXPECT_NEAR(fit.end_level, 1.0f, 1.0e-6f) << height;
        EXPECT_NEAR(fit.max_deviation, std::abs(height), 1.0e-5f) << height;
    }
    ASSERT_EQ(editor::fit_stairs(make_profile({{0.0f, 1.0f}, {4.0f, 1.011f}}), rules, {}, fit).message, "");
    ASSERT_EQ(fit.risers.size(), 1u);
    EXPECT_NEAR(fit.end_level, 1.011f, 1.0e-6f);
    // After a climb, an end 5 mm above the last step's level adds no sliver riser.
    ASSERT_EQ(editor::fit_stairs(make_profile({{0.0f, 0.0f}, {3.0f, 1.0f}, {4.0f, 0.75f}, {6.0f, 0.755f}}), rules, {}, fit).message, "");
    for (const Stairs_riser& riser : fit.risers) {
        EXPECT_GE(std::abs(riser.level_after - riser.level_before), 0.01f);
    }
}

TEST(Stairs_fit, no_step_is_higher_than_the_rise_limit)
{
    const Stairs_rules rules{.max_rise = 0.2f, .min_tread = 0.3f};
    Stairs_fit fit;
    // 0.02 mm above ten rises takes an eleventh step; the float 2.0 over the float 0.2 takes ten.
    for (const float height : {2.0f, 2.00002f, 2.001f, 0.2f, 0.20001f, 3.0f, 0.6f}) {
        ASSERT_EQ(editor::fit_stairs(make_profile({{0.0f, 0.0f}, {12.0f, height}}), rules, {}, fit).message, "") << height;
        for (const Stairs_riser& riser : fit.risers) {
            EXPECT_LE(riser.level_after - riser.level_before, 0.2f * (1.0f + 2.0e-6f)) << height;
        }
        EXPECT_LE(fit.max_deviation, 0.2f + editor::c_stairs_deviation_slack) << height;
    }
    ASSERT_EQ(editor::fit_stairs(make_profile({{0.0f, 0.0f}, {12.0f, 2.0f}}), rules, {}, fit).message, "");
    EXPECT_EQ(fit.step_count, 10);
    ASSERT_EQ(editor::fit_stairs(make_profile({{0.0f, 0.0f}, {12.0f, 2.00002f}}), rules, {}, fit).message, "");
    EXPECT_EQ(fit.step_count, 11);
}

TEST(Stairs_fit, no_riser_stands_in_a_zone)
{
    const Stairs_rules rules{.max_rise = 0.2f, .min_tread = 0.3f};
    const std::vector<Stairs_zone> zones{{.begin = 2.0f, .end = 3.1f}};
    Stairs_fit fit;
    const Stairs_fit_refusal refusal = editor::fit_stairs(make_profile({{0.0f, 0.0f}, {9.0f, 2.0f}}), rules, zones, fit);
    ASSERT_EQ(refusal.message, "");
    expect_rules_hold(fit, rules);
    for (const Stairs_riser& riser : fit.risers) {
        EXPECT_FALSE((riser.distance > 2.0f + c_tolerance) && (riser.distance < 3.1f - c_tolerance)) << riser.distance;
    }
    // A zone the curve climbs two steps in leaves no place for the riser between them.
    const std::vector<Stairs_zone> wide{{.begin = 1.0f, .end = 4.0f}};
    EXPECT_NE(editor::fit_stairs(make_profile({{0.0f, 0.0f}, {5.0f, 2.0f}}), rules, wide, fit).message, "");
}

TEST(Stairs_fit, values_that_do_not_read_are_refused)
{
    Stairs_fit fit;
    const std::vector<Stairs_height_sample> profile = make_profile({{0.0f, 0.0f}, {3.0f, 1.0f}});
    EXPECT_NE(editor::fit_stairs(profile, Stairs_rules{.max_rise = 0.0f, .min_tread = 0.3f}, {}, fit).message, "");
    EXPECT_NE(editor::fit_stairs(profile, Stairs_rules{.max_rise = 0.2f, .min_tread = 0.0f}, {}, fit).message, "");
    EXPECT_NE(editor::fit_stairs({}, Stairs_rules{}, {}, fit).message, "");
    // More steps than the limit.
    EXPECT_NE(editor::fit_stairs(make_profile({{0.0f, 0.0f}, {400.0f, 120.0f}}), Stairs_rules{}, {}, fit).message, "");
}

} // anonymous namespace
