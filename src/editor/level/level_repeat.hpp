#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace erhe::math { class Curve_path; class Curve_path_table; }

// Scene-free pattern expansion of kit layout nodes (doc/editor/level_tools.md "Pattern expansion", D3.7).

namespace editor {

enum class Repeat_pattern_kind : unsigned int {
    line = 0,
    grid,
    ring,
    path,
    scatter,
    segments, // one piece per curve segment, scaled along x to span its chord
    stairs    // one generated staircase along the curve (doc/editor/level_tools.md "Stairs layouts")
};

[[nodiscard]] auto c_str(Repeat_pattern_kind kind) -> const char*;

// Path, segments and stairs run along a curve: their layout node is a Curve prim.
[[nodiscard]] auto is_curve_layout_kind(Repeat_pattern_kind kind) -> bool;

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
    glm::vec3   scale   {1.0f}; // segments scale x; the other kinds keep 1
    std::size_t item    {0}; // the item a scatter picked for the element; the other kinds leave 0
};

// One item a scatter picks from: its weight (0: never picked) and its footprint radius about the piece's vertical axis.
class Repeat_scatter_item
{
public:
    uint32_t weight{1};
    float    radius{0.0f};
};

// One item a segments run spans a chord with: its bounds along its own x axis.
class Repeat_segment_item
{
public:
    float min_x{0.0f};
    float max_x{0.0f};
};

// The shortest chord a segments run spans (meters).
inline constexpr float c_min_segment_length = 1.0e-4f;

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
    const erhe::math::Curve_path_table* path{nullptr};         // path, segments: the curve in the start frame, not owned
    float                   path_step       {0.0f};            // path: distance from one element to the next
    Repeat_path_orientation path_orientation{Repeat_path_orientation::upright}; // path, segments
    std::span<const Repeat_segment_item> segment_items; // segments: element i takes item i modulo the count, not owned
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

// The pose of one segment's piece from point p0 to point p1 (start frame) spanning the item's x bounds; returns the
// refusal naming the segment, empty when made.
[[nodiscard]] auto make_segment_pose(
    std::size_t                 segment,
    const glm::vec3&            p0,
    const glm::vec3&            p1,
    const Repeat_segment_item&  item,
    Repeat_path_orientation     orientation,
    Repeat_pose&                out_pose
) -> std::string;

// An end point slides along its neighbour only when the two meet at least this sine apart (15 degrees): at most
// 1 / 0.2588 = 3.9 times the offset; a flatter neighbour counts as parallel and the end point moves along the normal.
inline constexpr float c_segment_slide_min_sine = 0.2588190f;

// Where a segment move puts the segment's two control points (doc/editor/level_tools.md "Segment handles").
class Segment_move
{
public:
    std::size_t first_point {0};
    std::size_t second_point{0};
    glm::vec3   first       {0.0f};
    glm::vec3   second      {0.0f};
};

// The unit normal of a segment of path (frame with y up): the right of its plan direction, horizontal. Returns the
// refusal, empty when made.
[[nodiscard]] auto get_segment_normal(const erhe::math::Curve_path& path, std::size_t segment, glm::vec3& out_normal) -> std::string;

// The segment moved offset along its normal: each end point slides along its neighbouring segment's plan line, or
// along the normal where it has no neighbour or the neighbour is parallel; heights are kept. Returns the refusal.
[[nodiscard]] auto move_curve_segment(const erhe::math::Curve_path& path, std::size_t segment, float offset, Segment_move& out_move) -> std::string;

// Writes the move's two positions into path (handles kept as offsets).
void apply_segment_move(const Segment_move& move, erhe::math::Curve_path& path);

// The poses of the run, element 0 at the start; cleared and filled. Returns the refusal (empty when expanded),
// which leaves out_poses empty.
[[nodiscard]] auto expand_repeat(const Repeat_pattern& pattern, const Repeat_pose& start, std::vector<Repeat_pose>& out_poses) -> std::string;

} // namespace editor
