#pragma once

#include "erhe_scene_renderer/pvs_disocclusion.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace erhe::scene_renderer::test {

// The synthetic layered buffers of the disocclusion pass tests
// (doc/plans/pvs.md "Verification"), shared by the CPU reference tests
// (test_pvs_disocclusion.cpp) and the GPU tests that compare the passes
// with the reference bit for bit (test_pvs_disocclusion_gpu.cpp). Every
// case is 8 x 8 tiles of K = 2 (16 x 16 pixels), N = 8 layers, z in [1,
// 16], a 90 degree face whose row 0 is the top row (tan_dy < 0), a 1 m cell
// box centered on the sample point, and 64 entries.
class Pvs_test_case
{
public:
    std::string                 name;
    Pvs_disocclusion_parameters parameters;
    // The three regions (depth, id_min, id_max), layer-major.
    std::vector<uint32_t>       layered_words;
    // Per column (entry id) its bounds in the face's view space (the test
    // faces map world to view with the identity): min, max. A sample of id
    // c grows column c's bounds by the sample's point; a column without
    // samples lies far behind the face.
    std::vector<glm::vec4>      column_bounds;
    std::vector<uint8_t>        column_used;
};

constexpr uint32_t c_pvs_test_resolution  = 16;
constexpr uint32_t c_pvs_test_tile_size   = 2;
constexpr uint32_t c_pvs_test_tiles       = 8;
constexpr uint32_t c_pvs_test_layer_count = 8;
constexpr uint32_t c_pvs_test_columns     = 64;

[[nodiscard]] auto make_pvs_test_parameters() -> Pvs_disocclusion_parameters;

// An empty layered buffer of the test size.
[[nodiscard]] auto make_pvs_test_case(const std::string& name) -> Pvs_test_case;

// Writes a sample (the layer's front depth, id_min = id_max = id).
void set_pvs_test_sample(Pvs_test_case& test_case, uint32_t pixel_x, uint32_t pixel_y, uint32_t layer, uint32_t id);
// Covers every pixel of tile (x, y, layer).
void fill_pvs_test_tile (Pvs_test_case& test_case, uint32_t tile_x, uint32_t tile_y, uint32_t layer, uint32_t id);
// Covers every pixel of the layer outside the tiles [x0, x1) x [y0, y1).
void fill_pvs_test_layer_except(Pvs_test_case& test_case, uint32_t layer, uint32_t id, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);

// The paper's Figure 2: a closed layer 1 with an open window tile W1 at
// (2, 4), a closed layer 2 with an open window tile W2 in row 4 inside
// W1's cone on layer 2 (reachable through W1); at layer 3 tile B (in both
// cones) holds entry c_figure_2_b and tile A (outside W1's cone) entry
// c_figure_2_a. Layer 0 is empty.
constexpr uint32_t c_figure_2_b = 5;
constexpr uint32_t c_figure_2_a = 9;
[[nodiscard]] auto make_figure_2_case() -> Pvs_test_case;
// No sample anywhere.
[[nodiscard]] auto make_empty_case() -> Pvs_test_case;
// Layer 0 fully covered (entry 1), entry 7 behind it at layer 3.
[[nodiscard]] auto make_closed_layer_0_case() -> Pvs_test_case;
// Layers 1 and 2 closed (entries 1, 2) except tile (3, 3), whose samples
// are split between them (left column entry 3 at layer 1, right column
// entry 4 at layer 2: a surface crossing the boundary); entry 8 behind it
// at layer 4.
[[nodiscard]] auto make_intersection_case() -> Pvs_test_case;
// Layer 1 closed (entry 1) except the 3 x 3 tiles [3, 6) x [3, 6); tile
// (4, 4) has no closed neighbour (degenerate); entry 6 behind it at layer
// 3.
[[nodiscard]] auto make_degenerate_case() -> Pvs_test_case;
// A grazing band: from pixel column 6 on a corrugated sheet whose depth
// grows by a constant step per pixel column (and alternates per pixel row);
// tile columns (3, y) take first hits in layers 2, 3 and 4 (a gap-free,
// continuous run: contiguous-run closure closes them at 2 .. 4); the
// columns x <= 2 are open space; entry 9 at (4, 3, 7) behind the sheet.
[[nodiscard]] auto make_grazing_band_case() -> Pvs_test_case;
// Gated spawning: layer 3 closed (entry 3) except tile (4, 4), an open
// chunk with closed neighbours; entry 5 at (4, 4, 5) behind it. In the
// closed case layer 2 is a full sheet (entry 2) in front of it: no
// disoccluded window leads to (4, 4, 3), which spawns nothing. In the open
// case the sheet has a hole at (4, 4): the chunk is reached and spawns.
[[nodiscard]] auto make_gating_closed_case() -> Pvs_test_case;
[[nodiscard]] auto make_gating_open_case() -> Pvs_test_case;

// Writes a sample at an arbitrary depth (in the layer whose front depth
// range holds it), id_min = id_max = id.
void set_pvs_test_depth(Pvs_test_case& test_case, uint32_t pixel_x, uint32_t pixel_y, float depth, uint32_t id);
// Rule 3, the silhouette test of a window's closed neighbours (every rule 3
// case turns it on: Pvs_disocclusion_parameters::columns.z). Each case
// puts entry c_rule_3_behind behind the surface (layers 6 .. 7 at tile
// column (3, 3) and (4, 3)).
constexpr uint32_t c_rule_3_behind = 20;
// (a) A plane whose depth grows linearly in x over the whole face (every
// pixel covered, its layer stepping across tiles: a stair-step of closed
// tiles): no closed neighbour of a window in front of it is a silhouette,
// so nothing behind the plane is disoccluded.
[[nodiscard]] auto make_rule_3_stair_case() -> Pvs_test_case;
// (b) A box face at layer 2 over the tile columns x <= 3, a far wall at
// layer 7 over x >= 4: the window (4, y, 2) has a silhouette toward (3, y)
// and its cone reaches behind the box.
[[nodiscard]] auto make_rule_3_box_edge_case() -> Pvs_test_case;
// (c) A crease: depth grows away from the boundary between tile columns 3
// and 4 on both sides (continuous depth, a tangent discontinuity): no
// silhouette, nothing behind it disoccluded.
[[nodiscard]] auto make_rule_3_crease_case() -> Pvs_test_case;
// (d) One gap row: tile column 3 a sheet at layer 2; tile column 4 has its
// even pixel rows on the same sheet and its odd rows on a far wall at
// layer 7: the window (4, y, 2) keeps its sloped side toward (3, y).
[[nodiscard]] auto make_rule_3_one_row_case() -> Pvs_test_case;
// (e) The silhouette inside the window: tile column 3 a sheet at layer 2
// that continues one pixel into tile column 4 (pixel column 8), whose
// other pixel column shows a far wall at layer 7: the edge pixels are
// continuous, the step inside the window is the silhouette, and the
// window (4, y, 2) keeps its sloped side toward (3, y).
[[nodiscard]] auto make_rule_3_inner_edge_case() -> Pvs_test_case;
// (f) The stair-step plane of (a) with a step of 1.5 times the per-pixel
// slope between pixel columns 9 and 10: above pvs_gap_slope_tolerance, a
// silhouette; the window of tile column 5 at layer 3 (the plane reaches
// layer 4 there) keeps a sloped side toward tile column 4.
[[nodiscard]] auto make_rule_3_small_step_case() -> Pvs_test_case;
// (g) A ledge inside the window, parallel to the shared edge (tile size 4
// for this case: 4 x 4 tiles). The neighbour (1, 1) and the window (2, 1)
// share a flat sheet in pixel rows 4 .. 5 (layer 2's front) and a sloped
// sheet in rows 6 .. 7 just in front of layer 3 that crosses into layer 3
// inside the window (so the window is open at layer 2); every boundary row
// is continuous and every step along a row is the slope, but rows 5 and 6
// differ by a ledge: the window keeps its sloped side toward (1, 1).
[[nodiscard]] auto make_rule_3_ledge_case() -> Pvs_test_case;
// (h) Rule 1 continuity: tile (4, 3)'s left pixel column at layer 2's
// front, its right column at layer 4's front (a gap-free set of first hits
// would not be: layers 2 and 4; so both columns of the tile have first
// hits 2 and 3: the right column at layer 3's back), a step far above the
// slope: the run does not close the tile.
[[nodiscard]] auto make_rule_1_step_case() -> Pvs_test_case;
// (i) The depth epsilon: tile (4, 3) holds a sheet at 10 m whose depth is
// constant along each pixel column (zero local slope) with a one-ULP
// difference between pixel rows: continuous, the tile keeps its run. With
// Pvs_epsilon_step::one_centimeter the right pixel column is 1 cm deeper than
// the left one: a gap, no run.
enum class Pvs_epsilon_step : unsigned int {
    none = 0,
    one_centimeter
};
[[nodiscard]] auto make_epsilon_case(Pvs_epsilon_step step) -> Pvs_test_case;

// Grows a column's bounds by a view-space box.
void add_pvs_test_bounds(Pvs_test_case& test_case, uint32_t column, const glm::vec3& box_min, const glm::vec3& box_max);
// The bounds vote: layer 0 closed (entry 1) except tiles (5, 5) and (0, 3);
// column 10 reaches into the near cube; column 11 spans the whole face at
// layer 2 (only the chunks behind the holes are disoccluded: found through
// the coarse levels of the map); column 12 reaches past the face's left
// edge into tile (0, 3); column 13 lies wholly left of the face; column 14
// runs from layer 7 past z_far behind (5, 5); column 15 at (2, 2) of layer
// 3, outside every cone.
[[nodiscard]] auto make_bounds_vote_case() -> Pvs_test_case;
// A larger random buffer (64 x 64 pixels, K = 4, N = 16, a 120 degree
// face, an off-center cell, 2048 entries): partial, full and empty tiles
// in every layer, so the cone rectangles' float arithmetic runs over many
// windows and layers (the GPU test compares it bit for bit).
[[nodiscard]] auto make_random_case(uint32_t seed, uint32_t layer_count = 16) -> Pvs_test_case;
// The production layer count (64): runs crossing layers 31 .. 33 (pixel
// rows 2 .. 3, columns 0 .. 5, a continuous plane), a sheet at layer 31
// with a hole at tile (5, 5) showing a sheet at layer 32, entries behind at
// layer 34: the high mask words (bits 32 .. 63) in every pass.
[[nodiscard]] auto make_word_boundary_case() -> Pvs_test_case;

[[nodiscard]] auto pvs_test_tile_index(uint32_t x, uint32_t y, uint32_t layer) -> uint32_t;
[[nodiscard]] auto has_pvs_test_entry(const Pvs_disocclusion_result& result, uint32_t entry) -> bool;

} // namespace erhe::scene_renderer::test
