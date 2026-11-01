#include "erhe_scene_renderer/cull_lanes.hpp"

#include "erhe_scene/lod.hpp"
#include "erhe_scene/pvs_database.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>

namespace erhe::scene_renderer::cull_lanes {

// The flags are written with a conditional expression of two constants:
// MSVC vectorizes that, but not a cast of a negated comparison.

// unbounded[i] = 1 for an entry without bounds (negative extent sentinel,
// Draw_list_bounds::get_unbounded_flag()).
void mark_unbounded_lanes(const Draw_list_bounds& bounds, const std::size_t count, uint32_t* const unbounded)
{
    const float* const ex = bounds.extent_x.data();
    for (std::size_t i = 0; i < count; ++i) {
        unbounded[i] = Draw_list_bounds::get_unbounded_flag(ex[i]);
    }
}

// One axis of the range sphere test: the gap along the axis from the
// sphere center coordinate to the box's interval (zero where the center
// coordinate lies within it) into gap[i], its square added to distance2[i].
// |c - s| - e is written as the larger of the two signed gaps (the same
// bits) so the loop holds only selects.
namespace {

// The gap of one box on one axis (sphere_axis_lanes(); the scalar form
// shadow_lod_lanes() evaluates per entry).
[[nodiscard]] inline auto axis_gap(const float center, const float extent, const float sphere_center) -> float
{
    const float offset     = center - sphere_center;
    const float below      = (-offset) - extent;
    const float above      = offset - extent;
    const float signed_gap = (below > above) ? below : above;
    return (signed_gap > 0.0f) ? signed_gap : 0.0f;
}

} // anonymous namespace

void sphere_axis_lanes(const float* const center, const float* const extent, const float sphere_center, const std::size_t count, float* const gap, float* const distance2)
{
    for (std::size_t i = 0; i < count; ++i) {
        const float d = axis_gap(center[i], extent[i], sphere_center);
        gap[i]        = d;
        distance2[i] += d * d;
    }
}

// Range sphere: inside[i] = 0 where the squared distance from the sphere
// center to the box exceeds the squared radius (the box lies wholly outside
// the sphere), 1 otherwise. One loop per axis: MSVC does not vectorize the
// three axes in one loop.
void sphere_lanes(const Cull_sphere& sphere, const Draw_list_bounds& bounds, const std::size_t count, const Sphere_gaps& gaps, float* const distance2, uint32_t* const inside)
{
    std::fill(distance2, distance2 + count, 0.0f);
    sphere_axis_lanes(bounds.center_x.data(), bounds.extent_x.data(), sphere.center.x, count, gaps.x, distance2);
    sphere_axis_lanes(bounds.center_y.data(), bounds.extent_y.data(), sphere.center.y, count, gaps.y, distance2);
    sphere_axis_lanes(bounds.center_z.data(), bounds.extent_z.data(), sphere.center.z, count, gaps.z, distance2);
    const float r2 = sphere.radius * sphere.radius;
    for (std::size_t i = 0; i < count; ++i) {
        inside[i] = (distance2[i] > r2) ? 0u : 1u;
    }
}

// One plane: d = n . c + w + |n| . e (the expressions and the evaluation
// order of erhe::math::aabb_in_convex_volume()), inside[i] cleared where
// d < 0.
void plane_lanes(const glm::vec4& plane, const Draw_list_bounds& bounds, const std::size_t count, uint32_t* const inside)
{
    const float nx = plane.x;
    const float ny = plane.y;
    const float nz = plane.z;
    const float w  = plane.w;
    const float ax = std::abs(nx);
    const float ay = std::abs(ny);
    const float az = std::abs(nz);
    const float* const cx = bounds.center_x.data();
    const float* const cy = bounds.center_y.data();
    const float* const cz = bounds.center_z.data();
    const float* const ex = bounds.extent_x.data();
    const float* const ey = bounds.extent_y.data();
    const float* const ez = bounds.extent_z.data();
    for (std::size_t i = 0; i < count; ++i) {
        const float center_distance  = (((nx * cx[i]) + (ny * cy[i])) + (nz * cz[i])) + w;
        const float projected_extent = ((ax * ex[i]) + (ay * ey[i])) + (az * ez[i]);
        inside[i] &= ((center_distance + projected_extent) < 0.0f) ? 0u : 1u;
    }
}

// One point light cube face (doc/plans/culling.md phase 3): the face looking
// along +axis (direction 1) or -axis (direction -1) sees the octant where
// the coordinate along the axis relative to the light is at least the
// absolute coordinates along the other two axes. A box reaches into it
// where its farthest extent along the face direction, direction * (c - l)
// + e, is at least the larger of its distances from the light along the
// other two axes (the sphere test's gaps, gap_a and gap_b). That is exact
// for the octant, so a box it rejects covers no texel of the face.
// inside[i] = sphere_inside[i] narrowed by that test.
void cube_face_lanes(
    const float* const    center,
    const float* const    extent,
    const float           light,
    const float           direction,
    const float* const    gap_a,
    const float* const    gap_b,
    const uint32_t* const sphere_inside,
    const std::size_t     count,
    uint32_t* const       inside
)
{
    for (std::size_t i = 0; i < count; ++i) {
        const float reach   = (direction * (center[i] - light)) + extent[i];
        const float lateral = (gap_a[i] > gap_b[i]) ? gap_a[i] : gap_b[i];
        inside[i] = sphere_inside[i] & ((reach >= lateral) ? 1u : 0u);
    }
}

// The view's bit of every entry inside the view or without bounds; returns
// the number of those entries (the view's survivors in this list).
auto write_view_bit_lanes(const uint32_t* const inside, const uint32_t* const unbounded, const std::size_t count, uint64_t* const masks, const uint32_t bit_index) -> uint32_t
{
    uint32_t survivors = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const uint32_t kept = inside[i] | unbounded[i];
        masks[i] |= static_cast<uint64_t>(kept) << bit_index;
        survivors += kept;
    }
    return survivors;
}

void set_view_bit_lanes(uint64_t* const masks, const std::size_t count, const uint64_t bit)
{
    for (std::size_t i = 0; i < count; ++i) {
        masks[i] |= bit;
    }
}

auto get_pvs_inverse_cell_size(const erhe::scene::Pvs_database& database) -> float
{
    return 1.0f / database.get_header().cell_size;
}

auto pvs_point_cell(
    const glm::vec3&  point,
    const glm::vec3&  grid_origin,
    const float       inverse_cell_size,
    const glm::uvec3& cell_counts,
    glm::uvec3&       out_cell
) -> bool
{
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        // cull_classify.comp pvs_point_cell(): the same precise operations.
        const float offset = point[axis] - grid_origin[axis];
        const float scaled = offset * inverse_cell_size;
        const float cell   = std::floor(scaled);
        if (!(cell >= 0.0f) || !(cell < static_cast<float>(cell_counts[axis]))) {
            return false;
        }
        out_cell[axis] = static_cast<uint32_t>(cell);
    }
    return true;
}

void make_pvs_query_cells(
    const erhe::scene::Pvs_database& database,
    const Pvs_resident_pages&        resident,
    const glm::vec3&                 query_min,
    const glm::vec3&                 query_max,
    Pvs_query_cells&                 out
)
{
    out = Pvs_query_cells{};
    const erhe::scene::Pvs_header& header            = database.get_header();
    const float                    inverse_cell_size = get_pvs_inverse_cell_size(database);
    const uint32_t                 words_per_cell    = database.get_words_per_cell();
    std::array<uint32_t, 8> cells{};
    uint32_t                cell_count = 0;
    for (uint32_t corner = 0; corner < 8; ++corner) {
        const glm::vec3 point{
            ((corner & 1u) != 0u) ? query_max.x : query_min.x,
            ((corner & 2u) != 0u) ? query_max.y : query_min.y,
            ((corner & 4u) != 0u) ? query_max.z : query_min.z
        };
        glm::uvec3 cell{0u};
        if (!pvs_point_cell(point, header.grid_origin, inverse_cell_size, header.cell_counts, cell)) {
            return; // keep_all
        }
        const uint32_t cell_index = database.cell_index(cell);
        bool seen = false;
        for (uint32_t i = 0; i < cell_count; ++i) {
            seen = seen || (cells[i] == cell_index);
        }
        if (!seen) {
            cells[cell_count++] = cell_index;
        }
    }
    for (uint32_t i = 0; i < cell_count; ++i) {
        const uint32_t page = database.page_of_cell(cells[i]);
        const uint32_t slot = (page < resident.page_table.size()) ? resident.page_table[page] : 0xffffffffu;
        if (slot == 0xffffffffu) {
            out.cell_count = 0;
            return; // keep_all: a page without a slot (not stored, or not resident)
        }
        const std::size_t first_word = (static_cast<std::size_t>(slot) * resident.words_per_page) + (static_cast<std::size_t>(database.local_cell(cells[i])) * words_per_cell);
        ERHE_VERIFY((first_word + words_per_cell) <= resident.slot_words.size());
        out.words[i] = resident.slot_words.data() + first_word;
        out.pages[i] = page;
        out.slots[i] = slot;
    }
    out.cell_count = cell_count;
    out.keep_all   = false;
}

auto pvs_lanes(
    const Pvs_query_cells& cells,
    const uint32_t* const  columns,
    const uint32_t* const  unbounded,
    const std::size_t      count,
    uint32_t* const        inside,
    uint32_t* const        culled,
    uint32_t&              culled_count
) -> uint32_t
{
    std::fill(culled, culled + count, uint32_t{0});
    if (cells.keep_all) {
        return 0;
    }
    uint32_t tested = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const uint32_t column = columns[i];
        if ((inside[i] == 0u) || (unbounded[i] != 0u) || (column == erhe::scene::Pvs_database::untracked_column)) {
            continue;
        }
        ++tested;
        const uint32_t word = column / 32u;
        const uint32_t bit  = uint32_t{1} << (column % 32u);
        bool visible = false;
        for (uint32_t cell = 0; cell < cells.cell_count; ++cell) {
            visible = visible || ((cells.words[cell][word] & bit) != 0u);
        }
        if (!visible) {
            inside[i] = 0u;
            culled[i] = 1u;
            ++culled_count;
        }
    }
    return tested;
}

auto get_lod_template_level(const uint32_t lod_word) -> uint32_t
{
    const Lod_word_fields fields = decode_lod_word(lod_word);
    if ((fields.level_count == 0) || (fields.mode != static_cast<uint32_t>(erhe::scene::Lod_mode::forced))) {
        return 0;
    }
    // make_lod_word() stored max_level >= min_level.
    const uint32_t level = std::clamp(fields.forced_level, fields.min_level, fields.max_level);
    return std::min(level, fields.level_count - 1u);
}

auto get_lod_coarsen_threshold2(const float threshold_pixels, const float hysteresis_band) -> float
{
    // band 0: 1.0f - 0.0f is exactly 1, so the product is threshold_pixels.
    const float coarsen_pixels = threshold_pixels * (1.0f - hysteresis_band);
    return coarsen_pixels * coarsen_pixels;
}

void match_lod_previous_columns(
    const Lod_history_frame& previous,
    const Lod_history_frame& current,
    const std::span<uint32_t> out
)
{
    ERHE_VERIFY(out.size() == current.column_keys.size());
    std::fill(out.begin(), out.end(), lod_column_none);
    // Restarts (section 6): a frame that is not the one right after the
    // previous, another entry layout, a classification switch.
    if (
        ((previous.frame_index + 1) != current.frame_index) ||
        (previous.entry_layout_serial != current.entry_layout_serial) ||
        (previous.classification != current.classification)
    ) {
        return;
    }
    for (std::size_t column = 0, end = current.column_keys.size(); column < end; ++column) {
        const uint64_t key = current.column_keys[column];
        if (key == 0) {
            continue;
        }
        for (std::size_t previous_column = 0, previous_end = previous.column_keys.size(); previous_column < previous_end; ++previous_column) {
            if (previous.column_keys[previous_column] == key) {
                out[column] = static_cast<uint32_t>(previous_column);
                break;
            }
        }
    }
}

namespace {

// The selected level plus the entry's and the global bias, clamped to the
// entry's min / max and to the chain (select_lod_level()).
[[nodiscard]] auto apply_lod_bias_and_clamps(const Lod_word_fields& fields, const uint32_t selected, const int32_t global_bias) -> int32_t
{
    int32_t level = static_cast<int32_t>(selected) + fields.bias + global_bias;
    level = std::max(level, static_cast<int32_t>(fields.min_level));
    level = std::min(level, static_cast<int32_t>(fields.max_level));
    level = std::clamp(level, 0, static_cast<int32_t>(fields.level_count) - 1);
    return level;
}

} // anonymous namespace

auto select_lod_level(
    const Lod_lane_view& view,
    const uint32_t       lod_word,
    const float* const   lod_errors,
    const float          max_axis_scale,
    const float          distance2,
    const uint32_t       unbounded,
    const uint32_t       previous_level
) -> uint32_t
{
    const Lod_word_fields fields = decode_lod_word(lod_word);
    if (fields.level_count == 0) {
        return 0;
    }
    if ((fields.mode != static_cast<uint32_t>(erhe::scene::Lod_mode::automatic)) || (unbounded != 0u)) {
        return get_lod_template_level(lod_word);
    }
    // Written so a precise GLSL port in cull_classify.comp evaluates the same
    // expressions in the same order (doc/plans/lod_gpu_selection.md
    // section 2, step 2b).
    const float k     = max_axis_scale * view.projection_scale;
    const float d2    = (view.projection == Lod_projection::orthographic) ? 1.0f : distance2;
    const float rhs   = view.threshold2 * d2;
    const float rhs_c = view.coarsen_threshold2 * d2;
    // Errors grow strictly along a chain, so the passing levels are a
    // prefix; the selected level is its last. coarse: the same for the
    // coarsening threshold (hysteresis, section 6).
    uint32_t selected = 0;
    uint32_t coarse   = 0;
    for (uint32_t level = 1; level < fields.level_count; ++level) {
        const float ek  = lod_errors[level] * k;
        const float lhs = ek * ek;
        if ((lhs <= rhs) && (selected == (level - 1u))) {
            selected = level;
        }
        if ((lhs <= rhs_c) && (coarse == (level - 1u))) {
            coarse = level;
        }
    }
    const int32_t fine_level = apply_lod_bias_and_clamps(fields, selected, view.global_bias);
    if (previous_level == lod_previous_level_none) {
        return static_cast<uint32_t>(fine_level);
    }
    // The previous level kept inside [coarse, selected] (both after the
    // biases and clamps, which are monotonic, so coarse <= selected while
    // coarsen_threshold2 <= threshold2): max, then min, as the GPU does.
    const int32_t coarse_level = apply_lod_bias_and_clamps(fields, coarse, view.global_bias);
    int32_t level = std::max(static_cast<int32_t>(previous_level), coarse_level);
    level = std::min(level, fine_level);
    return static_cast<uint32_t>(level);
}

namespace {

// select_lod_level() of one entry with its chain's errors from lod_table;
// level 0 for an entry without chain.
[[nodiscard]] auto select_entry_lod_level(
    const Lod_lane_view&              view,
    const Draw_list_entry&            entry,
    const std::span<const glm::uvec4> lod_table,
    const float                       max_axis_scale,
    const float                       distance2,
    const uint32_t                    unbounded,
    const uint32_t                    previous_level
) -> uint32_t
{
    if (entry.lod_table_index == lod_table_index_none) {
        return 0;
    }
    const uint32_t level_count = decode_lod_word(entry.lod_word).level_count;
    ERHE_VERIFY(level_count <= 16u);
    ERHE_VERIFY((static_cast<std::size_t>(entry.lod_table_index) + level_count) <= lod_table.size());
    std::array<float, 16> errors{};
    for (uint32_t l = 0; l < level_count; ++l) {
        errors[l] = std::bit_cast<float>(lod_table[entry.lod_table_index + l].z);
    }
    return select_lod_level(view, entry.lod_word, errors.data(), max_axis_scale, distance2, unbounded, previous_level);
}

} // anonymous namespace

void lod_lanes(
    const Lod_lane_view&                   view,
    const Draw_list_bounds&                bounds,
    const std::span<const Draw_list_entry> entries,
    const std::span<const glm::uvec4>      lod_table,
    const std::size_t                      count,
    float* const                           gap,
    float* const                           distance2,
    uint32_t* const                        words,
    const uint32_t                         nibble_shift,
    const uint32_t* const                  previous_words,
    const uint32_t                         previous_nibble_shift
)
{
    ERHE_VERIFY(entries.size() >= count);
    ERHE_VERIFY(bounds.max_axis_scale.size() >= count);
    ERHE_VERIFY(nibble_shift <= 28u);
    ERHE_VERIFY(previous_nibble_shift <= 28u);
    // The squared distance from the view origin to each box: the range
    // sphere test's per-axis gaps (sphere_axis_lanes()), with the origin as
    // the sphere center. An orthographic view reads none (d2 = 1).
    const bool orthographic = (view.projection == Lod_projection::orthographic);
    if (!orthographic) {
        std::fill(distance2, distance2 + count, 0.0f);
        sphere_axis_lanes(bounds.center_x.data(), bounds.extent_x.data(), view.origin.x, count, gap, distance2);
        sphere_axis_lanes(bounds.center_y.data(), bounds.extent_y.data(), view.origin.y, count, gap, distance2);
        sphere_axis_lanes(bounds.center_z.data(), bounds.extent_z.data(), view.origin.z, count, gap, distance2);
    }
    const float* const extent_x       = bounds.extent_x.data();
    const float* const max_axis_scale = bounds.max_axis_scale.data();
    for (std::size_t i = 0; i < count; ++i) {
        const uint32_t previous_level = (previous_words != nullptr)
            ? ((previous_words[i] >> previous_nibble_shift) & 0xfu)
            : lod_previous_level_none;
        const uint32_t level = select_entry_lod_level(
            view, entries[i], lod_table, max_axis_scale[i], orthographic ? 1.0f : distance2[i], Draw_list_bounds::get_unbounded_flag(extent_x[i]), previous_level
        );
        words[i] |= (level & 0xfu) << nibble_shift;
    }
}

void frustum_inside_lanes(
    const std::span<const glm::vec4> planes,
    const Draw_list_bounds&          bounds,
    const uint32_t* const            unbounded,
    const std::size_t                count,
    uint32_t* const                  inside
)
{
    std::fill(inside, inside + count, uint32_t{1});
    for (const glm::vec4& plane : planes) {
        plane_lanes(plane, bounds, count, inside);
    }
    for (std::size_t i = 0; i < count; ++i) {
        inside[i] |= (unbounded[i] != 0u) ? 1u : 0u;
    }
}

void shadow_lod_lanes(
    const Lod_lane_view&                                      view,
    const Draw_list_bounds&                                   bounds,
    const std::span<const Draw_list_entry>                    entries,
    const std::span<const glm::uvec4>                         lod_table,
    const uint32_t* const                                     kept,
    const std::array<const uint32_t*, max_lod_color_columns>& color_inside,
    const uint32_t                                            color_column_mask,
    const uint32_t* const                                     color_words,
    const std::size_t                                         count,
    uint32_t* const                                           words,
    const uint32_t                                            nibble_shift,
    const uint32_t* const                                     previous_words,
    const uint32_t                                            previous_nibble_shift
)
{
    ERHE_VERIFY(entries.size() >= count);
    ERHE_VERIFY(bounds.max_axis_scale.size() >= count);
    ERHE_VERIFY(nibble_shift <= 28u);
    ERHE_VERIFY(previous_nibble_shift <= 28u);
    ERHE_VERIFY(color_column_mask < (1u << max_lod_color_columns));
    for (uint32_t column = 0; column < max_lod_color_columns; ++column) {
        ERHE_VERIFY((((color_column_mask >> column) & 1u) == 0u) || (color_inside[column] != nullptr));
    }
    const bool         orthographic   = (view.projection == Lod_projection::orthographic);
    const float* const center_x       = bounds.center_x.data();
    const float* const center_y       = bounds.center_y.data();
    const float* const center_z       = bounds.center_z.data();
    const float* const extent_x       = bounds.extent_x.data();
    const float* const extent_y       = bounds.extent_y.data();
    const float* const extent_z       = bounds.extent_z.data();
    const float* const max_axis_scale = bounds.max_axis_scale.data();
    for (std::size_t i = 0; i < count; ++i) {
        // Not drawn in the pass: level 0 (decision 4).
        if (kept[i] == 0u) {
            continue;
        }
        // The finest level of the sampling columns whose frustum holds the
        // entry; 0xf (above every level) while none does.
        uint32_t level = 0xfu;
        for (uint32_t column = 0; column < max_lod_color_columns; ++column) {
            if ((((color_column_mask >> column) & 1u) != 0u) && (color_inside[column][i] != 0u)) {
                level = std::min(level, (color_words[i] >> (4u * column)) & 0xfu);
            }
        }
        if (level == 0xfu) {
            // The own selection: the squared distance as lod_lanes() sums
            // the gaps of sphere_axis_lanes() (x, then y, then z).
            float distance2 = 1.0f;
            if (!orthographic) {
                const float gap_x = axis_gap(center_x[i], extent_x[i], view.origin.x);
                const float gap_y = axis_gap(center_y[i], extent_y[i], view.origin.y);
                const float gap_z = axis_gap(center_z[i], extent_z[i], view.origin.z);
                distance2 = ((gap_x * gap_x) + (gap_y * gap_y)) + (gap_z * gap_z);
            }
            const uint32_t previous_level = (previous_words != nullptr)
                ? ((previous_words[i] >> previous_nibble_shift) & 0xfu)
                : lod_previous_level_none;
            level = select_entry_lod_level(view, entries[i], lod_table, max_axis_scale[i], distance2, Draw_list_bounds::get_unbounded_flag(extent_x[i]), previous_level);
        }
        words[i] |= (level & 0xfu) << nibble_shift;
    }
}

auto count_lod_level_changes(
    const uint32_t* const words,
    const uint32_t        nibble_shift,
    const uint32_t* const previous_words,
    const uint32_t        previous_nibble_shift,
    const uint32_t* const inside,
    const std::size_t     count
) -> uint32_t
{
    ERHE_VERIFY(nibble_shift <= 28u);
    ERHE_VERIFY(previous_nibble_shift <= 28u);
    uint32_t changed = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const uint32_t level          = (words[i] >> nibble_shift) & 0xfu;
        const uint32_t previous_level = (previous_words[i] >> previous_nibble_shift) & 0xfu;
        changed += ((inside[i] != 0u) && (level != previous_level)) ? 1u : 0u;
    }
    return changed;
}

auto reaches_view(const Cull_view_tests& view, const Draw_list_bounds& bounds, View_reach_scratch& scratch) -> bool
{
    const std::size_t count = bounds.extent_x.size();
    if (count == 0) {
        return false;
    }
    scratch.unbounded.resize(count);
    scratch.inside.resize(count);
    uint32_t* const unbounded = scratch.unbounded.data();
    uint32_t* const inside    = scratch.inside.data();
    mark_unbounded_lanes(bounds, count, unbounded);
    if (view.kind == Cull_view_kind::cube_face) {
        scratch.sphere_inside.resize(count);
        scratch.distance2.resize(count);
        scratch.gap_x.resize(count);
        scratch.gap_y.resize(count);
        scratch.gap_z.resize(count);
        const Sphere_gaps gaps{.x = scratch.gap_x.data(), .y = scratch.gap_y.data(), .z = scratch.gap_z.data()};
        sphere_lanes(view.sphere, bounds, count, gaps, scratch.distance2.data(), scratch.sphere_inside.data());
        // Face f looks along axis f / 2, toward + for even f (as
        // Draw_list_scene::classify_cull_views()).
        const uint32_t axis       = view.cube_face / 2;
        const float    direction  = ((view.cube_face % 2) == 0) ? 1.0f : -1.0f;
        const float*   axis_gaps[3]{gaps.x, gaps.y, gaps.z};
        const float*   centers[3]  {bounds.center_x.data(), bounds.center_y.data(), bounds.center_z.data()};
        const float*   extents[3]  {bounds.extent_x.data(), bounds.extent_y.data(), bounds.extent_z.data()};
        cube_face_lanes(
            centers[axis],
            extents[axis],
            view.sphere.center[static_cast<glm::length_t>(axis)],
            direction,
            axis_gaps[(axis + 1) % 3],
            axis_gaps[(axis + 2) % 3],
            scratch.sphere_inside.data(),
            count,
            inside
        );
    } else {
        std::fill(inside, inside + count, uint32_t{1});
        for (uint32_t plane_index = 0; plane_index < view.plane_count; ++plane_index) {
            plane_lanes(view.planes[plane_index], bounds, count, inside);
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        if ((inside[i] | unbounded[i]) != 0u) {
            return true;
        }
    }
    return false;
}

} // namespace erhe::scene_renderer::cull_lanes
