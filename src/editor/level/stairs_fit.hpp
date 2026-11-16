#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

// Scene-free fit of flights and landings to a height profile (doc/editor/level_tools.md "Stairs layouts", R5.12.13).

namespace editor {

// Stations nearer than this are one station, and a riser may leave its window by this much (meters along the plan).
inline constexpr float c_stairs_station_epsilon = 1.0e-4f;
// A climb below this share of the rise limit makes no step: the tread stays level.
inline constexpr float c_stairs_min_step_share  = 0.05f;
// A riser may stand a station epsilon outside its window, so the surface leaves the curve by at most this more than the rise limit (meters).
inline constexpr float c_stairs_deviation_slack = 2.0e-4f;
inline constexpr int   c_max_stairs_steps       = 512;

// R3.5: a step is at most max_rise high and a tread at least min_tread deep, so a flight's steepest slope is their ratio.
class Stairs_rules
{
public:
    float max_rise {0.2f};
    float min_tread{0.3f};
};

// The curve's height at a distance along its plan; distances do not decrease.
class Stairs_height_sample
{
public:
    float distance{0.0f};
    float height  {0.0f};
};

// A stretch of the plan that holds no riser (the landing of a plan corner).
class Stairs_zone
{
public:
    float begin{0.0f};
    float end  {0.0f};
};

// A riser at a plan distance, between the tread before it and the tread after it.
class Stairs_riser
{
public:
    float       distance    {0.0f};
    float       level_before{0.0f};
    float       level_after {0.0f};
    std::size_t flight      {0};
};

// A run of equal steps one tread apart; each riser has its tread on its upper side.
class Stairs_flight
{
public:
    std::size_t first_riser{0};
    int         step_count {0};
    float       rise       {0.0f}; // per step, negative going down
    float       tread      {0.0f};
    float       begin      {0.0f}; // plan distances, the treads included
    float       end        {0.0f};
};

// The level part of a tread that no flight's step uses.
class Stairs_landing
{
public:
    float begin {0.0f};
    float length{0.0f};
    float level {0.0f};
};

class Stairs_fit
{
public:
    std::vector<Stairs_riser>   risers;
    std::vector<Stairs_flight>  flights;
    std::vector<Stairs_landing> landings;
    float length       {0.0f}; // of the plan
    float start_level  {0.0f};
    float end_level    {0.0f};
    float total_rise   {0.0f}; // end_level - start_level
    float max_deviation{0.0f}; // the largest height difference between the walking surface and the curve
    int   step_count   {0};

    void clear();
    // Tread i lies between riser i - 1 and riser i; there is one more tread than risers.
    [[nodiscard]] auto get_tread_count() const -> std::size_t;
    [[nodiscard]] auto get_tread_level(std::size_t tread) const -> float;
    [[nodiscard]] auto get_tread_begin(std::size_t tread) const -> float;
    [[nodiscard]] auto get_tread_end  (std::size_t tread) const -> float;
};

class Stairs_fit_refusal
{
public:
    std::string message;        // empty when fitted
    float       distance{0.0f}; // the place along the plan
};

// zones: ascending and apart. out_fit is cleared and filled; with a refusal its contents are not a fit.
[[nodiscard]] auto fit_stairs(
    std::span<const Stairs_height_sample> samples,
    const Stairs_rules&                   rules,
    std::span<const Stairs_zone>          zones,
    Stairs_fit&                           out_fit
) -> Stairs_fit_refusal;

} // namespace editor
