#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace erhe::scene_renderer {

// The disocclusion passes of the potentially visible set bake
// (doc/plans/pvs.md "Bake" 5; doc/erhe/draw_list_renderer.md Q6 "PVS"): the
// parameter block one face's passes read, and the CPU reference of the whole
// pass chain, which the GPU passes (Pvs_disocclusion_pipelines,
// res/shaders/pvs_*.comp) match bit for bit.
//
// Terms. The face's layered buffer (Pvs_layered_interface) holds per pixel
// and depth layer the closest sample: depth (cleared 0xffffffff), id_min
// (cleared 0xffffffff) and id_max (cleared 0), word layer *
// resolution^2 + y * resolution + x of each region. A tile is K x K pixels
// (K = tile_size) of one layer; tile (x, y, layer) has index (layer *
// tiles + y) * tiles + x, tiles = resolution / K per side. A tile is closed
// when all its K * K samples are covered, open otherwise (an empty tile
// too). Layer masks are 64 bits, two uint words, bit j = layer j.
//
// The passes, in order:
// - tiles: per tile column (x, y) the coverage of every layer and the
//   pixels' first-hit layers (the smallest layer with a sample, N when
//   none). A tile (x, y, L) is closed when all its samples are covered at
//   layer L, or when every pixel of the column has a first hit and the set
//   of first-hit layers is a gap-free run [L_a .. L_b] containing L (a
//   surface crossing layer boundaries, the grazing band: contiguous-run
//   closure, rule 1) and the tile's sheet is continuous: no 4-adjacent
//   pixel pair of the tile has a gap (the test of rule 3 below, on the
//   first hits); a run with a silhouette inside leaves the tile to the
//   coverage rule. Open otherwise; the last layer is never open. The run's
//   layers L_a .. L_b - 1 are set in the column mask at (x, y): the
//   surface's own samples behind L_a are seen through the run's front.
//   Every column's run (first | last << 8; first = N when it has none) is
//   kept for classify.
// - per layer L = 0 .. N - 1, front to back (gated spawning):
//   - classify: an open tile at layer L < N - 1 is a window only when it is
//     itself disoccluded (L == 0, or bits 0 .. L - 1 all set in its tile
//     mask | the column mask at (x, y)): no ray from the cell reaches a
//     window behind a surface it cannot see past. A window reads the closed
//     bits of its four same-layer neighbours (a screen edge counts as the
//     tile itself); none closed (degenerate) sets bit L of the column mask
//     at (x, y), else the tile and its sloped sides (bit 0 -x, bit 1 +x,
//     bit 2 -y, bit 3 +y) are appended to layer L's cone list. A closed
//     neighbour is a sloped side (the paper's rule). With rule 3 on
//     (opt-in: Pvs_face_geometry::silhouette_tests, columns.z) only across
//     a silhouette (pvs_side_has_gap()), each pixel's depth being its
//     first sample at or behind layer S (the neighbour's run's first layer
//     when L is in its run, else L): (i) on some boundary row the window's
//     edge pixel is deeper than the neighbour's edge pixel, or (ii) some
//     4-adjacent pixel pair inside the window differs (either way, along
//     either axis), by more than pvs_gap_slope_tolerance times the local
//     per-pixel step, max(|a - a'|, |b - b'|) with a' the pixel before a
//     and b' the one after b on the same axis (a pixel with no sample is
//     infinitely deep; a missing a' or b' adds no step), plus
//     pvs_gap_depth_epsilon times the larger depth. doc/plans/pvs.md
//     "Open" holds why it is off. No sloped side is the degenerate case.
//   - cones: per cone of layer L and per layer J > L the hit rectangle of
//     the cone on layer J (pvs_cone_rectangle()); every tile in it at layer
//     J gets bit L in its tile mask, before layer L + 1 is classified.
// - disoccluded map: chunk (x, y, L) is disoccluded when L == 0 or bits 0
//   .. L - 1 are all set in (tile mask | column mask at (x, y)); level 0 of
//   the map holds that flag per chunk, level k + 1 the "any" of 2 x 2
//   texels of level k, per layer, down to one texel.
// - bounds vote: a column (a baked entry, its world bounds) is visible on
//   the face when its bounds, in the face's view space, reach into the cube
//   [-z_near, z_near]^3 around the sample point (the near region: no face
//   rasterizes it), or when its bounds beyond the near plane overlap a
//   disoccluded chunk of the face's 90 degree core: the projected corners
//   give the tile rectangle (clamped to the core), the depths the layer
//   range (widened by one layer on each side: the raster's layer function
//   rounds on the GPU), and the map is read at the level where the
//   rectangle spans at most 2 x 2 texels. The core is the face's cube-map
//   square (|view x / depth| <= 1 and |view y / depth| <= 1, the pixels
//   whose extent reaches into it): the six cores cover every direction, so
//   every point of the bounds outside the near cube is tested by the face
//   whose core holds it; the rim holds the windows and cones that reach
//   the core's chunks. The cell ORs the faces' votes. The disoccluded set
//   is
//   conservative even where only the closest sample of a layer survives a
//   pixel (open / closed comes from the surface that is there, the cones
//   are geometric), so voting the bounds keeps an entry hidden behind
//   another one in the same layer.
//
// Floats: the parameter block carries every quotient the passes need
// (computed here on the CPU), so the passes multiply, add, subtract and
// floor only; the shaders declare those intermediates precise and this
// file is compiled without floating-point contraction, so both evaluate the
// same operations in the same order to the same bits.

// One face as the bake renders it: a perspective view from the sample
// point. Pixel px (0 at the left edge of the image) is at view x / depth =
// tan_x0 + px * tan_dx, row py (row 0 of the layered buffer) at view y /
// depth = tan_y0 + py * tan_dy (tan_dy is negative where row 0 is the top
// row). cell_min / cell_max: the cell box in the face's view space (x
// right, y up, z = depth along the view direction) relative to the sample
// point; the cell is behind every layer plane (cell_max.z < z_near).
// view_axes and sample_point map world space to the face's view space
// (faces look along world axes): view coordinate i = (world[axis_i] -
// sample_point[axis_i]), negated when bit i of view_axes.w is set, axis_i =
// view_axes[i].
// Rule 3 (silhouette sides of the cones), opt-in: off, every closed
// 4-neighbour of a window is a sloped side (the paper's rule); on, only a
// neighbour across a silhouette (pvs_side_has_gap()). doc/plans/pvs.md
// "Open" holds why it is off.
enum class Pvs_silhouette_tests : unsigned int {
    off = 0,
    on
};

class Pvs_face_geometry
{
public:
    uint32_t  resolution {0};
    uint32_t  tile_size  {0};
    uint32_t  layer_count{0};
    float     z_near     {0.0f};
    float     z_far      {0.0f};
    float     tan_x0     {0.0f};
    float     tan_dx     {0.0f};
    float     tan_y0     {0.0f};
    float     tan_dy     {0.0f};
    glm::vec3 cell_min   {0.0f};
    glm::vec3 cell_max   {0.0f};
    glm::uvec4 view_axes {0u, 1u, 2u, 0u};
    glm::vec3 sample_point{0.0f};
    Pvs_silhouette_tests silhouette_tests{Pvs_silhouette_tests::off};
};

// The std430 storage block pvs_disocclusion_parameters of the passes:
//
//     uvec4 sizes;      // resolution, tile size K, layer count N, tiles per side
//     uvec4 counts;     // tiles per layer, tiles (all layers), column words,
//                       // words per layer (resolution^2)
//     uvec4 core;       // the core's pixels: x first, x end, y first, y end
//     uvec4 pass;       // the layer / map level of a per-layer dispatch, 0, 0, 0
//     uvec4 columns;    // column count, map levels, rule 3 on (1) / off (0), 0
//     uvec4 view_axes;  // world axis of view x, y, z; negation bits
//     vec4  sample_point; // world space, w = 0
//     vec4  tangents;   // tan_x0, tan_dx, tan_y0, tan_dy
//     vec4  tile_scale; // 1 / (tan_dx * K), 1 / (tan_dy * K), 0, 0
//     vec4  cell_min;   // the cell box in face view space, w = 0
//     vec4  cell_max;
//     vec4  layers[];   // N + 1: front depth z_k of layer k (z_N = z_far),
//                       // 1 / z_k, 1 / (z_k - cell_min.z), 1 / (z_k - cell_max.z)
class Pvs_disocclusion_parameters
{
public:
    static constexpr std::size_t header_vec4_count{11};
    // Layer masks are two uint words.
    static constexpr uint32_t    max_layer_count  {64};

    glm::uvec4             sizes     {0u};
    glm::uvec4             counts    {0u};
    glm::uvec4             core      {0u};
    glm::uvec4             pass      {0u};
    glm::uvec4             columns   {0u};
    glm::uvec4             view_axes {0u};
    glm::vec4              sample_point{0.0f};
    glm::vec4              tangents  {0.0f};
    glm::vec4              tile_scale{0.0f};
    glm::vec4              cell_min  {0.0f};
    glm::vec4              cell_max  {0.0f};
    std::vector<glm::vec4> layers;

    [[nodiscard]] auto get_resolution      () const -> uint32_t { return sizes.x; }
    [[nodiscard]] auto get_tile_size       () const -> uint32_t { return sizes.y; }
    [[nodiscard]] auto get_layer_count     () const -> uint32_t { return sizes.z; }
    [[nodiscard]] auto get_tiles           () const -> uint32_t { return sizes.w; }
    [[nodiscard]] auto get_tiles_per_layer () const -> uint32_t { return counts.x; }
    [[nodiscard]] auto get_tile_count      () const -> uint32_t { return counts.y; }
    [[nodiscard]] auto get_column_word_count() const -> uint32_t { return counts.z; }
    [[nodiscard]] auto get_column_count    () const -> uint32_t { return columns.x; }
    [[nodiscard]] auto get_map_level_count () const -> uint32_t { return columns.y; }
    [[nodiscard]] auto get_words_per_layer () const -> uint32_t { return counts.w; }
    // The block's bytes and their std430 image.
    [[nodiscard]] auto get_byte_count      () const -> std::size_t;
    void write(std::span<std::byte> destination) const;
};

// The parameter block of a face over column_count columns (the vote's
// column words: (column_count + 31) / 32). Verifies the geometry:
// resolution a multiple of tile_size, layer_count in [1, 64], 0 < z_near <
// z_far, the cell behind z_near.
[[nodiscard]] auto make_pvs_disocclusion_parameters(const Pvs_face_geometry& geometry, uint32_t column_count) -> Pvs_disocclusion_parameters;
// The same into caller-owned storage (its layers vector keeps its capacity):
// the bake's per-face path.
void make_pvs_disocclusion_parameters(const Pvs_face_geometry& geometry, uint32_t column_count, Pvs_disocclusion_parameters& out);

// The disoccluded map: the texels per side of level `level` (tiles per
// side halved, rounded up), the word offset of a level (per level, layer
// count * size^2 words, layer-major), and the total word count.
[[nodiscard]] auto pvs_map_level_size  (uint32_t tiles, uint32_t level) -> uint32_t;
[[nodiscard]] auto pvs_map_level_offset(const Pvs_disocclusion_parameters& parameters, uint32_t level) -> uint32_t;
[[nodiscard]] auto pvs_map_word_count  (const Pvs_disocclusion_parameters& parameters) -> uint32_t;

// The bounds vote of one column on the face (see "bounds vote" above):
// world bounds min / max, the map of the face.
[[nodiscard]] auto pvs_bounds_vote(
    const Pvs_disocclusion_parameters& parameters,
    const glm::vec3&                   bounds_min,
    const glm::vec3&                   bounds_max,
    std::span<const uint32_t>          map
) -> bool;

// The hit rectangle of the cone of open tile (tx, ty) at layer L with closed
// sides `sides` on layer J > L (the cones pass): over the eight corners c of
// the cell box and the four corners w of the tile's window on layer L's
// front plane, the points where the ray from c through w crosses the front
// planes of layers J and J + 1 (the angular extent of the cone grows
// monotonically with depth, so the two planes bound every depth of layer
// J), in tile coordinates; lo = floor(min), hi = floor(max) + 1, clamped on
// each side whose neighbour is open to the tile's own column, then to the
// tile grid. Returns (lo.x, lo.y, hi.x, hi.y), hi exclusive (empty when hi
// <= lo on an axis).
[[nodiscard]] auto pvs_cone_rectangle(
    const Pvs_disocclusion_parameters& parameters,
    uint32_t                           tile_x,
    uint32_t                           tile_y,
    uint32_t                           layer,
    uint32_t                           sides,
    uint32_t                           target_layer
) -> glm::uvec4;

// What the pass chain wrote: the closed flag per tile (0 or 1), column masks
// (2 words per (x, y)), tile masks (2 words per tile), the disoccluded map,
// the column words (bit c: column c visible); and, for the tests, the cones
// (tile index, sides) the reference found, layer by layer in tile order.
class Pvs_disocclusion_result
{
public:
    std::vector<uint32_t>   closed;
    std::vector<uint32_t>   runs; // per tile column: run first | last << 8
    std::vector<uint32_t>   column_masks;
    std::vector<uint32_t>   tile_masks;
    std::vector<uint32_t>   map;
    std::vector<uint32_t>   column_words;
    std::vector<glm::uvec2> cones;
};

// The CPU reference of the whole chain over one face's layered buffer
// words (the depth region, layer-major, words per layer * layer count
// words; the id regions may follow and are not read) and the columns'
// world bounds (two vec4 per column: min, max; w unused). column_words
// accumulates: it is resized to the parameters' column word count,
// zero-filled only when its size differs.
void run_pvs_disocclusion_reference(
    const Pvs_disocclusion_parameters& parameters,
    std::span<const uint32_t>          layered_words,
    std::span<const glm::vec4>         column_bounds,
    Pvs_disocclusion_result&           result
);

// Diagnostics of rule 3 for one side (the bake trace): the pixel pair of
// the boundary rows (i) and of the window's inside (ii) closest to a gap,
// as margin = step - limit (positive: a gap), the pixels, their depths and
// the outward neighbours'.
class Pvs_gap_pair
{
public:
    float      margin{-1.0e30f};
    glm::ivec4 pixels{0};        // a x, a y, b x, b y
    glm::vec4  depths{0.0f};     // a, a', b, b' (0: missing)
    uint32_t   start {0};        // the layer S the depths start at
};
class Pvs_side_gap_details
{
public:
    bool         edge_gap {false};
    bool         inner_gap{false};
    Pvs_gap_pair edge;
    Pvs_gap_pair inner;
};
[[nodiscard]] auto pvs_side_gap_details(
    const Pvs_disocclusion_parameters& parameters,
    std::span<const uint32_t>          depth,
    std::span<const uint32_t>          runs,
    uint32_t                           tile_x,
    uint32_t                           tile_y,
    uint32_t                           layer,
    uint32_t                           side
) -> Pvs_side_gap_details;

// Rule 3's tolerance: a step from one pixel to the next of up to this many
// times the local per-pixel depth step is the surface continuing (a plane's
// step is 1 / (1 - r) times its neighbour's for a per-pixel depth ratio r,
// 1.14 at r = 0.12); a larger step is a silhouette. The shaders get it as
// the define ERHE_PVS_GAP_SLOPE_TOLERANCE (Pvs_disocclusion_pipelines).
constexpr float pvs_gap_slope_tolerance = 1.25f;
// And a relative floor: a step up to this fraction of the depth is never a
// gap (1 mm at 10 m), so a sheet whose local slope is zero (a wall's depth
// along a face's pixel column) is not split by the interpolation's ULP
// noise. The shaders get it as ERHE_PVS_GAP_DEPTH_EPSILON.
constexpr float pvs_gap_depth_epsilon = 1.0e-4f;

// Rule 3 of classify (see above): whether the closed neighbour on `side`
// (0 -x, 1 +x, 2 -y, 3 +y) of window (tile_x, tile_y, layer) shares a
// silhouette edge with it, from the depth region of the layered buffer and
// the columns' runs. erhe_pvs_side_has_gap() is its GLSL twin.
[[nodiscard]] auto pvs_side_has_gap(
    const Pvs_disocclusion_parameters& parameters,
    std::span<const uint32_t>          depth,
    std::span<const uint32_t>          runs,
    uint32_t                           tile_x,
    uint32_t                           tile_y,
    uint32_t                           layer,
    uint32_t                           side
) -> bool;

// The disoccluded map (every level) from the result's tile and column
// masks, into result.map: the reference's map step (pvs_map.comp).
void build_pvs_disoccluded_map(const Pvs_disocclusion_parameters& parameters, Pvs_disocclusion_result& result);

// The vote's decision for tile (x, y, layer) from the result's masks.
[[nodiscard]] auto is_pvs_tile_disoccluded(
    const Pvs_disocclusion_parameters& parameters,
    const Pvs_disocclusion_result&     result,
    uint32_t                           tile_x,
    uint32_t                           tile_y,
    uint32_t                           layer
) -> bool;

} // namespace erhe::scene_renderer
