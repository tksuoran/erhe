#pragma once

#include "erhe_scene_renderer/draw_list.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// The culling classification lanes of Draw_list_scene::classify_cull_views()
// (doc/erhe/draw_list_renderer.md Q6), in their own translation unit so the
// deviceless tests can call them; reaches_view() runs them for one view over
// a set of boxes (Shadow_map_cache's per-pass test). Public API of
// erhe::scene_renderer. Each lane is one branch-free loop over raw arrays
// (the entries' SoA bounds and the per-entry flags), the shape the compiler
// vectorizes without intrinsics.
namespace erhe::scene {
    class Pvs_database;
}

namespace erhe::scene_renderer::cull_lanes {

// The per-axis gaps from the range sphere's center to the boxes, which the
// cube face lanes read after the sphere test.
class Sphere_gaps
{
public:
    float* x{nullptr};
    float* y{nullptr};
    float* z{nullptr};
};

void mark_unbounded_lanes(const Draw_list_bounds& bounds, std::size_t count, uint32_t* unbounded);
void sphere_axis_lanes   (const float* center, const float* extent, float sphere_center, std::size_t count, float* gap, float* distance2);
void sphere_lanes        (const Cull_sphere& sphere, const Draw_list_bounds& bounds, std::size_t count, const Sphere_gaps& gaps, float* distance2, uint32_t* inside);
void plane_lanes         (const glm::vec4& plane, const Draw_list_bounds& bounds, std::size_t count, uint32_t* inside);
void cube_face_lanes(
    const float*    center,
    const float*    extent,
    float           light,
    float           direction,
    const float*    gap_a,
    const float*    gap_b,
    const uint32_t* sphere_inside,
    std::size_t     count,
    uint32_t*       inside
);
[[nodiscard]] auto write_view_bit_lanes(const uint32_t* inside, const uint32_t* unbounded, std::size_t count, uint64_t* masks, uint32_t bit_index) -> uint32_t;
void set_view_bit_lanes(uint64_t* masks, std::size_t count, uint64_t bit);

// The potentially visible set rule (doc/plans/pvs.md "Runtime
// consumption"; res/shaders/cull_classify.comp applies the same rule with the
// same arithmetic): the cells of a view's query box are the cells holding the
// box's eight corners. A corner's cell per axis is floor((p - origin) *
// inverse_cell_size) (a precise subtract and multiply, the reciprocal
// computed once on the CPU, get_pvs_inverse_cell_size(), so both paths
// compute the same cells); a corner outside the grid keeps every entry.

// 1 / cell_size of the database's grid, the factor both paths multiply with.
[[nodiscard]] auto get_pvs_inverse_cell_size(const erhe::scene::Pvs_database& database) -> float;

// The cell of a point (x, y, z cell coordinates), or false when the point is
// outside the grid.
[[nodiscard]] auto pvs_point_cell(
    const glm::vec3&  point,
    const glm::vec3&  grid_origin,
    float             inverse_cell_size,
    const glm::uvec3& cell_counts,
    glm::uvec3&       out_cell
) -> bool;

// The resident pages of a frame (doc/plans/pvs.md "Residency and
// streaming"): per page of the database its slot (0xffffffff: not
// resident), and the slots' words (words_per_page each), the CPU copy of
// what the GPU classification reads (Pvs_gpu_database).
class Pvs_resident_pages
{
public:
    std::span<const uint32_t> page_table    {};
    std::span<const uint32_t> slot_words    {};
    uint32_t                  words_per_page{0};
};

// The cells of one view's query box: per corner cell (deduplicated) the
// first word of the cell's PVS in the resident page slots, its page and
// slot. keep_all: some corner is outside the grid or in a page without a
// resident slot, and the view culls nothing by PVS.
class Pvs_query_cells
{
public:
    bool                           keep_all  {true};
    uint32_t                       cell_count{0};
    std::array<const uint32_t*, 8> words     {};
    std::array<uint32_t, 8>        pages     {};
    std::array<uint32_t, 8>        slots     {};
};

// The query cells of the box [query_min, query_max] (non-empty) in database
// with the frame's resident pages.
void make_pvs_query_cells(
    const erhe::scene::Pvs_database& database,
    const Pvs_resident_pages&        resident,
    const glm::vec3&                 query_min,
    const glm::vec3&                 query_max,
    Pvs_query_cells&                 out
);

// The PVS rule over count entries: an entry inside the view (inside[i] = 1),
// with bounds (unbounded[i] = 0) and a tracked column whose bit is clear in
// every query cell is culled: inside[i] cleared, culled[i] = 1 (else 0).
// Returns the number of entries tested (inside, bounded and tracked); adds
// the culled ones to culled_count. With keep_all nothing is culled.
[[nodiscard]] auto pvs_lanes(
    const Pvs_query_cells& cells,
    const uint32_t*        columns,
    const uint32_t*        unbounded,
    std::size_t            count,
    uint32_t*              inside,
    uint32_t*              culled,
    uint32_t&              culled_count
) -> uint32_t;

// Level of detail selection (doc/plans/lod_gpu_selection.md section 2): the
// inputs of one LOD column. threshold2 is the frame's pixel threshold
// squared (computed once per frame, the value the GPU reads as float bits),
// global_bias the frame's bias added to every selected level.
class Lod_lane_view
{
public:
    glm::vec3      origin          {0.0f};
    float          projection_scale{0.0f};
    Lod_projection projection      {Lod_projection::perspective};
    float          threshold2      {1.0f};
    int32_t        global_bias     {0};
};

// The level an entry draws without selection (the template range of its
// command, R1c): the forced level clamped to the record's min / max and to
// the chain under Lod_mode::forced, else 0; 0 for an entry without chain.
[[nodiscard]] auto get_lod_template_level(uint32_t lod_word) -> uint32_t;

// The selected level of one entry (section 2): k = max_axis_scale *
// projection_scale; level L passes when (error_L * k) * (error_L * k) <=
// threshold2 * distance2, with distance2 the squared distance from the
// view origin to the entry's box (1 for an orthographic view); the selected
// level is the last of the passing prefix of the chain, plus the entry's
// and the global bias, clamped to the entry's min / max and to the chain.
// Multiplies, adds and compares only, in this order, with no contraction
// (the translation unit is built with -ffp-contract=off), so a precise port
// in res/shaders/cull_classify.comp can match it bit for bit. An entry that
// is not under Lod_mode::automatic, or has no bounds (unbounded != 0),
// takes get_lod_template_level(); an entry without chain level 0.
// lod_errors are the chain's errors, level 0 first (lod_errors[0] unread).
[[nodiscard]] auto select_lod_level(
    const Lod_lane_view& view,
    uint32_t             lod_word,
    const float*         lod_errors,
    float                max_axis_scale,
    float                distance2,
    uint32_t             unbounded
) -> uint32_t;

// The LOD column's level of every entry of a list into the column's nibble
// of the level words (words[i] |= level << nibble_shift; the caller clears
// the words): select_lod_level() with the entries' lod_word and chain rows
// (lod_table, Draw_list_scene::get_lod_table(): row lod_table_index + L
// holds level L's error in z as float bits) and the boxes' squared distance
// from the view origin, written to distance2 with the per-axis gaps of
// sphere_axis_lanes() in gap (count floats each, owned by the caller).
// Levels do not depend on visibility: every entry gets one.
void lod_lanes(
    const Lod_lane_view&             view,
    const Draw_list_bounds&          bounds,
    std::span<const Draw_list_entry> entries,
    std::span<const glm::uvec4>      lod_table,
    std::size_t                      count,
    float*                           gap,
    float*                           distance2,
    uint32_t*                        words,
    uint32_t                         nibble_shift
);

// The lanes' per-box arrays of reaches_view(), owned by the caller (capacity
// kept across calls).
class View_reach_scratch
{
public:
    std::vector<uint32_t> unbounded;
    std::vector<uint32_t> inside;
    std::vector<uint32_t> sphere_inside;
    std::vector<float>    distance2;
    std::vector<float>    gap_x;
    std::vector<float>    gap_y;
    std::vector<float>    gap_z;
};

// Whether any box of bounds is kept by view: the tests and lanes
// Draw_list_scene::classify_cull_views() classifies an entry with, so a box
// this rejects is culled from the view. A box without bounds is kept by
// every view, and a planes view without planes keeps every box.
[[nodiscard]] auto reaches_view(const Cull_view_tests& view, const Draw_list_bounds& bounds, View_reach_scratch& scratch) -> bool;

} // namespace erhe::scene_renderer::cull_lanes
