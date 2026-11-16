#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace erhe::math { class Curve_path_table; }

// Scene-free pattern expansion of kit layout nodes (doc/editor/level_tools.md "Pattern expansion", D3.7).

namespace editor {

enum class Repeat_pattern_kind : unsigned int {
    line = 0,
    grid,
    ring,
    path,
    scatter
};

[[nodiscard]] auto c_str(Repeat_pattern_kind kind) -> const char*;

enum class Repeat_ring_facing : unsigned int {
    outward = 0,   // each element turns with its angle about the ring's center
    start_rotation // each element keeps the start rotation
};

// How a piece on a path turns with the curve (doc/editor/level_tools.md "Pattern expansion").
enum class Repeat_path_orientation : unsigned int {
    upright = 0, // the x axis takes the tangent's heading about the start frame's up axis
    tangent      // the x axis takes the tangent, the up axis the up-keeping frame's up
};

[[nodiscard]] auto c_str(Repeat_path_orientation orientation) -> const char*;

// The region a scatter fills, centred on the start in its xz plane.
enum class Repeat_scatter_region : unsigned int {
    rectangle = 0, // scatter_size_x by scatter_size_z
    disc           // scatter_radius
};

[[nodiscard]] auto c_str(Repeat_scatter_region region) -> const char*;

// Candidate points a scatter draws per piece asked for before it stops with what fits.
inline constexpr std::size_t c_scatter_attempts_per_piece = 64;

inline constexpr std::size_t c_max_repeat_count = 1024;

// The largest weight of one scatter item (doc/editor/level_tools.md "Scatter layouts").
inline constexpr uint32_t c_max_scatter_weight = 1000;

class Repeat_pose
{
public:
    glm::vec3   position{0.0f};
    glm::quat   rotation{1.0f, 0.0f, 0.0f, 0.0f};
    std::size_t item    {0}; // the item a scatter picked for the element; the other kinds leave 0
};

// One item a scatter picks from: its weight (0: never picked) and its footprint radius about the piece's vertical axis.
class Repeat_scatter_item
{
public:
    uint32_t weight{1};
    float    radius{0.0f};
};

// The fields of the kind in use count; lengths in meters, axes in the start frame unless stated.
class Repeat_pattern
{
public:
    Repeat_pattern_kind    kind        {Repeat_pattern_kind::line};
    int                    count       {3};                    // line, ring, path
    glm::vec3              spacing     {1.0f, 0.0f, 0.0f};     // line: the step from one element to the next
    int                    count_x     {3};                    // grid
    int                    count_z     {3};                    // grid
    float                  spacing_x   {1.0f};                 // grid
    float                  spacing_z   {1.0f};                 // grid
    float                  radius      {4.0f};                 // ring
    float                  arc_degrees {360.0f};               // ring: 360 is a full ring
    Repeat_ring_facing     facing      {Repeat_ring_facing::outward};
    const erhe::math::Curve_path_table* path{nullptr};         // path: the curve in the start frame, not owned
    float                   path_step       {0.0f};            // path: distance from one element to the next
    Repeat_path_orientation path_orientation{Repeat_path_orientation::upright};
    Repeat_scatter_region  scatter_region {Repeat_scatter_region::rectangle}; // scatter: count is the pieces asked for
    float                  scatter_size_x {8.0f};
    float                  scatter_size_z {8.0f};
    float                  scatter_radius {4.0f};
    float                  scatter_spacing{1.0f};   // scatter: the least gap between two piece footprints
    std::span<const Repeat_scatter_item> scatter_items; // scatter: the items picked from, at least one, not owned
    float                  yaw_min_degrees{0.0f};   // scatter: each piece turns about the start's up axis within the range
    float                  yaw_max_degrees{360.0f};
    int                    scatter_seed   {0};
};

// The item of a scatter candidate's pick draw: an exact integer pick over the weights, total_weight their sum (> 0).
[[nodiscard]] auto pick_scatter_item(std::span<const Repeat_scatter_item> items, uint64_t total_weight, uint32_t draw) -> std::size_t;

// The poses of the run, element 0 at the start; cleared and filled. Returns the refusal (empty when expanded),
// which leaves out_poses empty.
[[nodiscard]] auto expand_repeat(const Repeat_pattern& pattern, const Repeat_pose& start, std::vector<Repeat_pose>& out_poses) -> std::string;

} // namespace editor
