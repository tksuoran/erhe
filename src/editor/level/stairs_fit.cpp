#include "level/stairs_fit.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

namespace editor {

namespace {

// A riser being placed: its window of plan distances and the stretch of the profile (section) it belongs to.
class Riser_plan
{
public:
    double      level_before{0.0};
    double      level_after {0.0};
    double      window_begin{0.0};
    double      window_end  {0.0};
    double      earliest    {0.0};
    double      latest      {0.0};
    double      distance    {0.0};
    std::size_t section     {0};
};

enum class Profile_side : unsigned int { before = 0, after };

// The profile's height at a distance; at a vertical jump the value on the given side.
[[nodiscard]] auto get_profile_height(const std::span<const Stairs_height_sample> samples, const double distance, const Profile_side side) -> double
{
    const float key = static_cast<float>(distance);
    const std::span<const Stairs_height_sample>::iterator i = (side == Profile_side::after)
        ? std::upper_bound(samples.begin(), samples.end(), key, [](const float value, const Stairs_height_sample& sample) -> bool { return value < sample.distance; })
        : std::lower_bound(samples.begin(), samples.end(), key, [](const Stairs_height_sample& sample, const float value) -> bool { return sample.distance < value; });
    if (i == samples.begin()) {
        return static_cast<double>(samples.front().height);
    }
    if (i == samples.end()) {
        return static_cast<double>(samples.back().height);
    }
    const Stairs_height_sample& a = *(i - 1);
    const Stairs_height_sample& b = *i;
    const double span = static_cast<double>(b.distance) - static_cast<double>(a.distance);
    if (!(span > 0.0)) {
        return static_cast<double>((side == Profile_side::after) ? b.height : a.height);
    }
    const double t = std::clamp((distance - static_cast<double>(a.distance)) / span, 0.0, 1.0);
    return static_cast<double>(a.height) + (t * (static_cast<double>(b.height) - static_cast<double>(a.height)));
}

// The sample indices where the profile turns by at least the threshold, with the first and the last sample.
void find_pivots(const std::span<const Stairs_height_sample> samples, const double threshold, std::vector<std::size_t>& out_pivots)
{
    out_pivots.clear();
    out_pivots.push_back(0);
    int         direction = 0;
    std::size_t highest   = 0;
    std::size_t lowest    = 0;
    for (std::size_t i = 1, end = samples.size(); i < end; ++i) {
        const double height = static_cast<double>(samples[i].height);
        if (height > static_cast<double>(samples[highest].height)) {
            highest = i;
        }
        if (height < static_cast<double>(samples[lowest].height)) {
            lowest = i;
        }
        const bool fell = (static_cast<double>(samples[highest].height) - height) >= threshold;
        const bool rose = (height - static_cast<double>(samples[lowest].height)) >= threshold;
        if (direction == 0) {
            // The first stretch starts at the first sample whichever way it goes.
            if (rose) {
                direction = 1;
                highest   = i;
            } else if (fell) {
                direction = -1;
                lowest    = i;
            }
        } else if ((direction > 0) && fell) {
            out_pivots.push_back(highest);
            direction = -1;
            lowest    = i;
        } else if ((direction < 0) && rose) {
            out_pivots.push_back(lowest);
            direction = 1;
            highest   = i;
        }
    }
    if (out_pivots.back() != (samples.size() - 1)) {
        out_pivots.push_back(samples.size() - 1);
    }
}

// The first distance at or after the given one that is in no zone.
[[nodiscard]] auto leave_zones_forward(const std::span<const Stairs_zone> zones, double distance) -> double
{
    for (const Stairs_zone& zone : zones) {
        if ((distance > (static_cast<double>(zone.begin) + c_stairs_station_epsilon)) && (distance < (static_cast<double>(zone.end) - c_stairs_station_epsilon))) {
            distance = static_cast<double>(zone.end);
        }
    }
    return distance;
}

// The last distance at or before the given one that is in no zone.
[[nodiscard]] auto leave_zones_backward(const std::span<const Stairs_zone> zones, double distance) -> double
{
    for (std::size_t i = zones.size(); i > 0; --i) {
        const Stairs_zone& zone = zones[i - 1];
        if ((distance > (static_cast<double>(zone.begin) + c_stairs_station_epsilon)) && (distance < (static_cast<double>(zone.end) - c_stairs_station_epsilon))) {
            distance = static_cast<double>(zone.begin);
        }
    }
    return distance;
}

[[nodiscard]] auto is_in_zone(const std::span<const Stairs_zone> zones, const double distance) -> bool
{
    return leave_zones_forward(zones, distance) != distance;
}

[[nodiscard]] auto make_refusal(std::string message, const double distance) -> Stairs_fit_refusal
{
    return Stairs_fit_refusal{.message = std::move(message), .distance = static_cast<float>(distance)};
}

} // anonymous namespace

void Stairs_fit::clear()
{
    risers.clear();
    flights.clear();
    landings.clear();
    length        = 0.0f;
    start_level   = 0.0f;
    end_level     = 0.0f;
    total_rise    = 0.0f;
    max_deviation = 0.0f;
    step_count    = 0;
}

auto Stairs_fit::get_tread_count() const -> std::size_t
{
    return risers.size() + 1;
}

auto Stairs_fit::get_tread_level(const std::size_t tread) const -> float
{
    return (tread == 0) ? start_level : risers[tread - 1].level_after;
}

auto Stairs_fit::get_tread_begin(const std::size_t tread) const -> float
{
    return (tread == 0) ? 0.0f : risers[tread - 1].distance;
}

auto Stairs_fit::get_tread_end(const std::size_t tread) const -> float
{
    return (tread < risers.size()) ? risers[tread].distance : length;
}

auto fit_stairs(
    const std::span<const Stairs_height_sample> samples,
    const Stairs_rules&                         rules,
    const std::span<const Stairs_zone>          zones,
    Stairs_fit&                                 out_fit
) -> Stairs_fit_refusal
{
    out_fit.clear();
    if (!std::isfinite(rules.max_rise) || !(rules.max_rise >= 0.01f)) {
        return make_refusal(fmt::format("the step rise limit {:.3f} m must be at least 0.01 m", rules.max_rise), 0.0);
    }
    if (!std::isfinite(rules.min_tread) || !(rules.min_tread >= 0.01f)) {
        return make_refusal(fmt::format("the tread depth {:.3f} m must be at least 0.01 m", rules.min_tread), 0.0);
    }
    if (samples.size() < 2) {
        return make_refusal("the stairs need a curve", 0.0);
    }
    for (std::size_t i = 0, end = samples.size(); i < end; ++i) {
        if (!std::isfinite(samples[i].distance) || !std::isfinite(samples[i].height) || ((i > 0) && (samples[i].distance < samples[i - 1].distance))) {
            return make_refusal("the height profile does not read", static_cast<double>(samples[i].distance));
        }
    }
    const double first  = static_cast<double>(samples.front().distance);
    const double length = static_cast<double>(samples.back().distance);
    const double rise   = static_cast<double>(rules.max_rise);
    const double tread  = static_cast<double>(rules.min_tread);
    const double eps    = static_cast<double>(c_stairs_station_epsilon);
    if (!(first == 0.0) || !(length > eps)) {
        return make_refusal("the curve has no length in plan", 0.0);
    }

    static thread_local std::vector<std::size_t> pivots;
    static thread_local std::vector<Riser_plan>  plans;
    find_pivots(samples, rise, pivots);
    plans.clear();

    // The steps of each stretch between two turns: equal, the least count within the rise limit.
    double level = static_cast<double>(samples.front().height);
    for (std::size_t section = 0; (section + 1) < pivots.size(); ++section) {
        const std::size_t ia     = pivots[section];
        const std::size_t ib     = pivots[section + 1];
        const double      target = static_cast<double>(samples[ib].height);
        const double      change = target - level;
        if (std::abs(change) < (static_cast<double>(c_stairs_min_step_share) * rise)) {
            continue;
        }
        const double sign       = (change > 0.0) ? 1.0 : -1.0;
        // A quotient above an integer by rounding alone takes no further step.
        const int    step_count = std::max(1, static_cast<int>(std::ceil((std::abs(change) / rise) * (1.0 - 1.0e-6))));
        if ((plans.size() + static_cast<std::size_t>(step_count)) > static_cast<std::size_t>(c_max_stairs_steps)) {
            return make_refusal(fmt::format("the curve needs more than {} steps", c_max_stairs_steps), static_cast<double>(samples[ia].distance));
        }
        const double step = change / static_cast<double>(step_count);
        for (int k = 0; k < step_count; ++k) {
            Riser_plan plan;
            plan.section      = section;
            plan.level_before = level + (static_cast<double>(k) * step);
            plan.level_after  = ((k + 1) == step_count) ? target : (level + (static_cast<double>(k + 1) * step));
            // The tread after the riser is within one rise of the curve from the last place the curve is further below it.
            const double after_limit  = (sign * plan.level_after)  - rise;
            const double before_limit = (sign * plan.level_before) + rise;
            plan.window_begin = static_cast<double>(samples[ia].distance);
            for (std::size_t i = ib; i > ia; --i) {
                const double below = sign * static_cast<double>(samples[i - 1].height);
                if (below < after_limit) {
                    const double above = sign * static_cast<double>(samples[i].height);
                    const double d0    = static_cast<double>(samples[i - 1].distance);
                    const double d1    = static_cast<double>(samples[i].distance);
                    plan.window_begin = (above > below) ? (d0 + ((d1 - d0) * std::clamp((after_limit - below) / (above - below), 0.0, 1.0))) : d1;
                    break;
                }
            }
            // The tread before it is within one rise of the curve up to the first place the curve is further above it.
            plan.window_end = static_cast<double>(samples[ib].distance);
            for (std::size_t i = ia + 1; i <= ib; ++i) {
                const double above = sign * static_cast<double>(samples[i].height);
                if (above > before_limit) {
                    const double below = sign * static_cast<double>(samples[i - 1].height);
                    const double d0    = static_cast<double>(samples[i - 1].distance);
                    const double d1    = static_cast<double>(samples[i].distance);
                    plan.window_end = (above > below) ? (d0 + ((d1 - d0) * std::clamp((before_limit - below) / (above - below), 0.0, 1.0))) : d0;
                    break;
                }
            }
            plans.push_back(plan);
        }
        level = target;
    }

    out_fit.length      = static_cast<float>(length);
    out_fit.start_level = samples.front().height;
    out_fit.end_level   = static_cast<float>(level);
    out_fit.total_rise  = out_fit.end_level - out_fit.start_level;
    out_fit.step_count  = static_cast<int>(plans.size());

    // The earliest and the latest place of each riser: in its window, a tread from its neighbours, in no zone, and a
    // tread from an end where the end is the top of its step.
    const std::size_t count = plans.size();
    for (std::size_t j = 0; j < count; ++j) {
        Riser_plan&  plan      = plans[j];
        const bool   ascending = plan.level_after > plan.level_before;
        const double lower     = (j == 0) ? (ascending ? 0.0 : tread) : (plans[j - 1].earliest + tread);
        plan.earliest = leave_zones_forward(zones, std::max(plan.window_begin, lower));
    }
    for (std::size_t j = count; j > 0; --j) {
        Riser_plan&  plan      = plans[j - 1];
        const bool   ascending = plan.level_after > plan.level_before;
        const double upper     = (j == count) ? (ascending ? (length - tread) : length) : (plans[j].latest - tread);
        plan.latest = leave_zones_backward(zones, std::min(plan.window_end, upper));
    }
    for (std::size_t j = 0; j < count; ++j) {
        const Riser_plan& plan = plans[j];
        if (plan.earliest > (plan.latest + eps)) {
            // The risers from the last one free at its window's begin to the first one free at its window's end do not fit.
            std::size_t first_bound = j;
            while ((first_bound > 0) && (plans[first_bound].earliest > (plans[first_bound].window_begin + eps))) {
                --first_bound;
            }
            std::size_t last_bound = j;
            while (((last_bound + 1) < count) && (plans[last_bound].latest < (plans[last_bound].window_end - eps))) {
                ++last_bound;
            }
            const double place = std::clamp(0.5 * (plans[first_bound].window_begin + plans[last_bound].window_end), 0.0, length);
            // Every riser bound by its neighbours from the curve's start to its end: the steps do not fit the plan length.
            const bool   first_ascending = plans.front().level_after > plans.front().level_before;
            const bool   last_ascending  = plans.back().level_after > plans.back().level_before;
            const double needed = (static_cast<double>(count - 1) * tread) + (first_ascending ? 0.0 : tread) + (last_ascending ? tread : 0.0);
            if ((first_bound == 0) && ((last_bound + 1) == count) && (needed > (length + eps))) {
                return make_refusal(
                    fmt::format(
                        "the curve is too short for its steps {:.3f} m along its plan: it needs {} steps with treads of at least {:.3f} m, {:.3f} m of plan, "
                        "and has {:.3f} m",
                        place, count, rules.min_tread, needed, length
                    ),
                    place
                );
            }
            return make_refusal(
                fmt::format(
                    "the curve is too steep for the stairs {:.3f} m along its plan: steps {} to {} of {} cannot stay within one step rise ({:.3f} m) of the "
                    "curve with treads {:.3f} m deep (a flight rises at most {:.3f} m per meter, and a turn from up to down or down to up needs a tread)",
                    place, first_bound + 1, last_bound + 1, count, rules.max_rise, rules.min_tread, rise / tread
                ),
                place
            );
        }
    }

    // A flight starts as late as it may and runs on, a tread per step, while each next riser is in its window.
    for (std::size_t j = 0; j < count; ++j) {
        Riser_plan& plan = plans[j];
        plan.distance = std::max(plan.latest, plan.earliest);
        if (j == 0) {
            continue;
        }
        const Riser_plan& previous  = plans[j - 1];
        const double      candidate = previous.distance + tread;
        if ((previous.section == plan.section) && (candidate >= (plan.window_begin - eps)) && (candidate <= (plan.latest + eps)) && !is_in_zone(zones, candidate)) {
            plan.distance = candidate;
        }
    }

    for (std::size_t j = 0; j < count; ++j) {
        const Riser_plan& plan         = plans[j];
        const bool        ascending    = plan.level_after > plan.level_before;
        const bool        continues    =
            (j > 0) && (plans[j - 1].section == plan.section) && (std::abs((plan.distance - plans[j - 1].distance) - tread) <= (2.0 * eps));
        if (!continues) {
            Stairs_flight flight;
            flight.first_riser = j;
            flight.rise        = static_cast<float>(plan.level_after - plan.level_before);
            flight.tread       = rules.min_tread;
            flight.begin       = static_cast<float>(ascending ? plan.distance : (plan.distance - tread));
            out_fit.flights.push_back(flight);
        }
        Stairs_flight& flight = out_fit.flights.back();
        ++flight.step_count;
        flight.end = static_cast<float>(ascending ? (plan.distance + tread) : plan.distance);
        out_fit.risers.push_back(
            Stairs_riser{
                .distance     = static_cast<float>(std::clamp(plan.distance, 0.0, length)),
                .level_before = static_cast<float>(plan.level_before),
                .level_after  = static_cast<float>(plan.level_after),
                .flight       = out_fit.flights.size() - 1
            }
        );
    }

    // The landings and the largest height difference between the treads and the curve.
    double      max_deviation = 0.0;
    double      worst_place   = 0.0;
    std::size_t sample        = 0;
    for (std::size_t k = 0, end = out_fit.get_tread_count(); k < end; ++k) {
        const double begin       = static_cast<double>(out_fit.get_tread_begin(k));
        const double finish      = static_cast<double>(out_fit.get_tread_end(k));
        const double tread_level = static_cast<double>(out_fit.get_tread_level(k));
        const bool   upper_begin = (k > 0) && (out_fit.risers[k - 1].level_after > out_fit.risers[k - 1].level_before);
        const bool   upper_end   = (k < count) && (out_fit.risers[k].level_after < out_fit.risers[k].level_before);
        const double owned       = (upper_begin ? tread : 0.0) + (upper_end ? tread : 0.0);
        if (((finish - begin) - owned) > (2.0 * eps)) {
            out_fit.landings.push_back(
                Stairs_landing{
                    .begin  = static_cast<float>(begin + (upper_begin ? tread : 0.0)),
                    .length = static_cast<float>((finish - begin) - owned),
                    .level  = static_cast<float>(tread_level)
                }
            );
        }
        if (!((finish - begin) > eps)) {
            continue;
        }
        const auto measure = [&max_deviation, &worst_place, tread_level](const double height, const double place) {
            const double deviation = std::abs(height - tread_level);
            if (deviation > max_deviation) {
                max_deviation = deviation;
                worst_place   = place;
            }
        };
        measure(get_profile_height(samples, begin,  Profile_side::after),  begin);
        measure(get_profile_height(samples, finish, Profile_side::before), finish);
        while ((sample < samples.size()) && (static_cast<double>(samples[sample].distance) <= begin)) {
            ++sample;
        }
        for (std::size_t i = sample; (i < samples.size()) && (static_cast<double>(samples[i].distance) < finish); ++i) {
            measure(static_cast<double>(samples[i].height), static_cast<double>(samples[i].distance));
        }
    }
    out_fit.max_deviation = static_cast<float>(max_deviation);
    // The windows keep this; a profile they do not hold for (turns the pivots do not separate) is refused, never followed loosely.
    if (max_deviation > (rise + static_cast<double>(c_stairs_deviation_slack))) {
        return make_refusal(
            fmt::format(
                "the stairs cannot stay within one step rise ({:.3f} m) of the curve {:.3f} m along its plan (they would leave it by {:.3f} m)",
                rules.max_rise, worst_place, max_deviation
            ),
            worst_place
        );
    }
    return Stairs_fit_refusal{};
}

} // namespace editor
