// Compiled with floating-point contraction off (CMakeLists.txt): the CPU
// reference evaluates the passes' float operations exactly as the shaders'
// precise intermediates do (pvs_disocclusion.hpp "Floats").

#include "erhe_scene_renderer/pvs_disocclusion.hpp"
#include "erhe_scene_renderer/pvs_layers.hpp"

#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace erhe::scene_renderer {

namespace {

constexpr uint32_t c_cleared_depth  = 0xffffffffu;

[[nodiscard]] auto tile_index(const uint32_t tiles, const uint32_t x, const uint32_t y, const uint32_t layer) -> uint32_t
{
    return (((layer * tiles) + y) * tiles) + x;
}

// The word of sample `sample` (0 .. K * K - 1, row-major in the tile) of
// tile (x, y, layer) in one region of the layered buffer.
[[nodiscard]] auto sample_word(const Pvs_disocclusion_parameters& parameters, const uint32_t x, const uint32_t y, const uint32_t layer, const uint32_t sample) -> uint32_t
{
    const uint32_t k          = parameters.get_tile_size();
    const uint32_t resolution = parameters.get_resolution();
    const uint32_t px         = (x * k) + (sample % k);
    const uint32_t py         = (y * k) + (sample / k);
    return (layer * parameters.get_words_per_layer()) + (py * resolution) + px;
}

// Bits 0 .. layer - 1 of a two-word layer mask.
[[nodiscard]] auto below_mask_low(const uint32_t layer) -> uint32_t
{
    return (layer >= 32u) ? 0xffffffffu : ((1u << layer) - 1u);
}

[[nodiscard]] auto below_mask_high(const uint32_t layer) -> uint32_t
{
    return (layer <= 32u) ? 0u : ((1u << (layer - 32u)) - 1u);
}

// The pixels [first, end) whose extent [t0 + p * dt, t0 + (p + 1) * dt]
// reaches into the core's [-1, 1] tangent range (with a margin of 1e-6:
// a pixel on the boundary belongs to both faces).
[[nodiscard]] auto core_pixel_range(const float t0, const float dt, const uint32_t resolution) -> glm::uvec2
{
    constexpr double margin = 1.0e-6;
    uint32_t first = resolution;
    uint32_t end   = 0;
    for (uint32_t pixel = 0; pixel < resolution; ++pixel) {
        const double a  = static_cast<double>(t0) + (static_cast<double>(pixel)      * static_cast<double>(dt));
        const double b  = static_cast<double>(t0) + (static_cast<double>(pixel + 1u) * static_cast<double>(dt));
        const double lo = std::min(a, b);
        const double hi = std::max(a, b);
        if ((hi >= (-1.0 - margin)) && (lo <= (1.0 + margin))) {
            first = std::min(first, pixel);
            end   = std::max(end, pixel + 1u);
        }
    }
    if (first >= end) {
        return glm::uvec2{0u, 0u};
    }
    return glm::uvec2{first, end};
}

} // anonymous namespace

auto Pvs_disocclusion_parameters::get_byte_count() const -> std::size_t
{
    return (header_vec4_count + layers.size()) * sizeof(glm::vec4);
}

void Pvs_disocclusion_parameters::write(const std::span<std::byte> destination) const
{
    ERHE_VERIFY(destination.size_bytes() >= get_byte_count());
    std::byte* out = destination.data();
    std::memcpy(out + (0 * sizeof(glm::vec4)), &sizes,      sizeof(glm::uvec4));
    std::memcpy(out + (1 * sizeof(glm::vec4)), &counts,     sizeof(glm::uvec4));
    std::memcpy(out + (2 * sizeof(glm::vec4)), &core,       sizeof(glm::uvec4));
    std::memcpy(out + ( 3 * sizeof(glm::vec4)), &pass,         sizeof(glm::uvec4));
    std::memcpy(out + ( 4 * sizeof(glm::vec4)), &columns,      sizeof(glm::uvec4));
    std::memcpy(out + ( 5 * sizeof(glm::vec4)), &view_axes,    sizeof(glm::uvec4));
    std::memcpy(out + ( 6 * sizeof(glm::vec4)), &sample_point, sizeof(glm::vec4));
    std::memcpy(out + ( 7 * sizeof(glm::vec4)), &tangents,     sizeof(glm::vec4));
    std::memcpy(out + ( 8 * sizeof(glm::vec4)), &tile_scale,   sizeof(glm::vec4));
    std::memcpy(out + ( 9 * sizeof(glm::vec4)), &cell_min,     sizeof(glm::vec4));
    std::memcpy(out + (10 * sizeof(glm::vec4)), &cell_max,     sizeof(glm::vec4));
    if (!layers.empty()) {
        std::memcpy(out + (header_vec4_count * sizeof(glm::vec4)), layers.data(), layers.size() * sizeof(glm::vec4));
    }
}

auto make_pvs_disocclusion_parameters(const Pvs_face_geometry& geometry, const uint32_t column_count) -> Pvs_disocclusion_parameters
{
    Pvs_disocclusion_parameters parameters{};
    make_pvs_disocclusion_parameters(geometry, column_count, parameters);
    return parameters;
}

void make_pvs_disocclusion_parameters(const Pvs_face_geometry& geometry, const uint32_t column_count, Pvs_disocclusion_parameters& parameters)
{
    const uint32_t resolution  = geometry.resolution;
    const uint32_t k           = geometry.tile_size;
    const uint32_t layer_count = geometry.layer_count;
    ERHE_VERIFY((k >= 1) && (resolution >= k) && ((resolution % k) == 0));
    ERHE_VERIFY((layer_count >= 1) && (layer_count <= Pvs_disocclusion_parameters::max_layer_count));
    ERHE_VERIFY((geometry.z_near > 0.0f) && (geometry.z_near < geometry.z_far) && std::isfinite(geometry.z_far));
    ERHE_VERIFY(geometry.cell_max.z < geometry.z_near);
    ERHE_VERIFY((geometry.tan_dx != 0.0f) && (geometry.tan_dy != 0.0f));

    const uint32_t tiles           = resolution / k;
    const uint32_t tiles_per_layer = tiles * tiles;
    // Every word index of the passes is a uint32.
    ERHE_VERIFY((uint64_t{tiles_per_layer} * uint64_t{layer_count} * 2u) < (uint64_t{1} << 32));

    parameters.sizes      = glm::uvec4{resolution, k, layer_count, tiles};
    parameters.pass       = glm::uvec4{0u};
    const uint32_t column_word_count = std::max(1u, (column_count + 31u) / 32u);
    parameters.counts     = glm::uvec4{tiles_per_layer, tiles_per_layer * layer_count, column_word_count, resolution * resolution};
    uint32_t map_levels = 1;
    while (pvs_map_level_size(tiles, map_levels - 1u) > 1u) {
        ++map_levels;
    }
    parameters.columns      = glm::uvec4{column_count, map_levels, (geometry.silhouette_tests == Pvs_silhouette_tests::on) ? 1u : 0u, 0u};
    ERHE_VERIFY((geometry.view_axes.x < 3u) && (geometry.view_axes.y < 3u) && (geometry.view_axes.z < 3u));
    parameters.view_axes    = geometry.view_axes;
    parameters.sample_point = glm::vec4{geometry.sample_point, 0.0f};
    const glm::uvec2 core_x = core_pixel_range(geometry.tan_x0, geometry.tan_dx, resolution);
    const glm::uvec2 core_y = core_pixel_range(geometry.tan_y0, geometry.tan_dy, resolution);
    parameters.core       = glm::uvec4{core_x.x, core_x.y, core_y.x, core_y.y};
    parameters.tangents   = glm::vec4{geometry.tan_x0, geometry.tan_dx, geometry.tan_y0, geometry.tan_dy};
    const float tile_dx   = geometry.tan_dx * static_cast<float>(k);
    const float tile_dy   = geometry.tan_dy * static_cast<float>(k);
    parameters.tile_scale = glm::vec4{1.0f / tile_dx, 1.0f / tile_dy, 0.0f, 0.0f};
    parameters.cell_min   = glm::vec4{geometry.cell_min, 0.0f};
    parameters.cell_max   = glm::vec4{geometry.cell_max, 0.0f};
    parameters.layers.resize(static_cast<std::size_t>(layer_count) + 1);
    for (uint32_t layer = 0; layer <= layer_count; ++layer) {
        const float z = pvs_layer_front_depth(layer, geometry.z_near, geometry.z_far, layer_count);
        parameters.layers[layer] = glm::vec4{
            z,
            1.0f / z,
            1.0f / (z - geometry.cell_min.z),
            1.0f / (z - geometry.cell_max.z)
        };
    }
}

auto pvs_cone_rectangle(
    const Pvs_disocclusion_parameters& parameters,
    const uint32_t                     tile_x,
    const uint32_t                     tile_y,
    const uint32_t                     layer,
    const uint32_t                     sides,
    const uint32_t                     target_layer
) -> glm::uvec4
{
    // res/shaders/erhe_pvs_disocclusion.glsl erhe_pvs_cone_rectangle(),
    // operation for operation.
    const uint32_t  k          = parameters.get_tile_size();
    const uint32_t  tiles      = parameters.get_tiles();
    const glm::vec4 tangents   = parameters.tangents;
    const glm::vec4 tile_scale = parameters.tile_scale;
    const glm::vec4 cell_min   = parameters.cell_min;
    const glm::vec4 cell_max   = parameters.cell_max;
    const glm::vec4 window     = parameters.layers[layer];

    const float u_x0 = tangents.x + (static_cast<float>(tile_x * k)         * tangents.y);
    const float u_x1 = tangents.x + (static_cast<float>((tile_x + 1u) * k)  * tangents.y);
    const float u_y0 = tangents.z + (static_cast<float>(tile_y * k)         * tangents.w);
    const float u_y1 = tangents.z + (static_cast<float>((tile_y + 1u) * k)  * tangents.w);
    const float w_x0 = window.x * u_x0;
    const float w_x1 = window.x * u_x1;
    const float w_y0 = window.x * u_y0;
    const float w_y1 = window.x * u_y1;

    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_y = 0.0f;
    float max_y = 0.0f;
    bool  first = true;
    for (uint32_t plane = 0; plane < 2u; ++plane) {
        const glm::vec4 target = parameters.layers[target_layer + plane];
        for (uint32_t depth_corner = 0; depth_corner < 2u; ++depth_corner) {
            const float cd      = (depth_corner == 0u) ? cell_min.z : cell_max.z;
            const float inv_gap = (depth_corner == 0u) ? window.z   : window.w;
            const float t       = (target.x - cd) * inv_gap;
            for (uint32_t corner = 0; corner < 2u; ++corner) {
                const float cx = (corner == 0u) ? cell_min.x : cell_max.x;
                const float cy = (corner == 0u) ? cell_min.y : cell_max.y;
                for (uint32_t window_corner = 0; window_corner < 2u; ++window_corner) {
                    const float wx = (window_corner == 0u) ? w_x0 : w_x1;
                    const float wy = (window_corner == 0u) ? w_y0 : w_y1;
                    const float x  = cx + (t * (wx - cx));
                    const float y  = cy + (t * (wy - cy));
                    const float fx = ((x * target.y) - tangents.x) * tile_scale.x;
                    const float fy = ((y * target.y) - tangents.z) * tile_scale.y;
                    if (first) {
                        min_x = fx;
                        max_x = fx;
                        min_y = fy;
                        max_y = fy;
                        first = false;
                    } else {
                        min_x = std::min(min_x, fx);
                        max_x = std::max(max_x, fx);
                        min_y = std::min(min_y, fy);
                        max_y = std::max(max_y, fy);
                    }
                }
            }
        }
    }
    const float tiles_f = static_cast<float>(tiles);
    uint32_t lo_x = static_cast<uint32_t>(std::clamp(std::floor(min_x),        0.0f, tiles_f));
    uint32_t lo_y = static_cast<uint32_t>(std::clamp(std::floor(min_y),        0.0f, tiles_f));
    uint32_t hi_x = static_cast<uint32_t>(std::clamp(std::floor(max_x) + 1.0f, 0.0f, tiles_f));
    uint32_t hi_y = static_cast<uint32_t>(std::clamp(std::floor(max_y) + 1.0f, 0.0f, tiles_f));
    // A side whose neighbour is open: the neighbour's own cone or column
    // covers what lies beyond it, so the cone keeps to the tile's column.
    if ((sides & 1u) == 0u) { lo_x = std::max(lo_x, tile_x); }
    if ((sides & 2u) == 0u) { hi_x = std::min(hi_x, tile_x + 1u); }
    if ((sides & 4u) == 0u) { lo_y = std::max(lo_y, tile_y); }
    if ((sides & 8u) == 0u) { hi_y = std::min(hi_y, tile_y + 1u); }
    return glm::uvec4{lo_x, lo_y, hi_x, hi_y};
}

auto pvs_map_level_size(const uint32_t tiles, const uint32_t level) -> uint32_t
{
    return (tiles + (1u << level) - 1u) >> level;
}

auto pvs_map_level_offset(const Pvs_disocclusion_parameters& parameters, const uint32_t level) -> uint32_t
{
    uint32_t offset = 0;
    for (uint32_t i = 0; i < level; ++i) {
        const uint32_t size = pvs_map_level_size(parameters.get_tiles(), i);
        offset += parameters.get_layer_count() * size * size;
    }
    return offset;
}

auto pvs_map_word_count(const Pvs_disocclusion_parameters& parameters) -> uint32_t
{
    return pvs_map_level_offset(parameters, parameters.get_map_level_count());
}

namespace {

// The boundaries b in [0, tiles] of the tile grid a point (v, z) of the
// view plane is at or past (its tile index + 1; 0 left of the face, tiles +
// 1 right of it): boundary b is at view v / z = t0 + (b * K) * dt; past
// means v >= that * z for dt > 0, v <= that * z for dt < 0. Monotone in b:
// a binary search. res/shaders/erhe_pvs_disocclusion.glsl
// erhe_pvs_boundary_count(), operation for operation.
[[nodiscard]] auto boundary_count(const float v, const float z, const float t0, const float dt, const uint32_t k, const uint32_t tiles) -> uint32_t
{
    uint32_t low  = 0;
    uint32_t high = tiles + 1u;
    while (low < high) {
        const uint32_t middle   = (low + high) >> 1u;
        const float    tangent  = t0 + (static_cast<float>(middle * k) * dt);
        const float    boundary = tangent * z;
        const bool     past     = (dt > 0.0f) ? (v >= boundary) : (v <= boundary);
        if (past) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return low;
}

// The layer whose front depth range holds z (z >= the front of layer 0):
// the last k in [0, N - 1] with z >= z_k. erhe_pvs_layer_of_front().
[[nodiscard]] auto layer_of_front(const Pvs_disocclusion_parameters& parameters, const float z) -> uint32_t
{
    uint32_t low  = 1;
    uint32_t high = parameters.get_layer_count();
    while (low < high) {
        const uint32_t middle = (low + high) >> 1u;
        if (z >= parameters.layers[middle].x) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return low - 1u;
}

} // anonymous namespace

auto pvs_bounds_vote(
    const Pvs_disocclusion_parameters& parameters,
    const glm::vec3&                   bounds_min,
    const glm::vec3&                   bounds_max,
    const std::span<const uint32_t>    map
) -> bool
{
    // erhe_pvs_bounds_vote() (res/shaders/pvs_vote.comp), operation for
    // operation.
    const glm::uvec4 axes  = parameters.view_axes;
    const glm::vec4  point = parameters.sample_point;
    glm::vec3 view_min{0.0f};
    glm::vec3 view_max{0.0f};
    for (uint32_t i = 0; i < 3u; ++i) {
        const uint32_t axis = axes[i];
        const float    low  = bounds_min[axis] - point[axis];
        const float    high = bounds_max[axis] - point[axis];
        if (((axes.w >> i) & 1u) != 0u) {
            view_min[i] = -high;
            view_max[i] = -low;
        } else {
            view_min[i] = low;
            view_max[i] = high;
        }
    }
    const float z_near = parameters.layers[0].x;
    // The near region: the bounds reach into the cube of half size z_near.
    if (
        (view_max.x >= -z_near) && (view_min.x <= z_near) &&
        (view_max.y >= -z_near) && (view_min.y <= z_near) &&
        (view_max.z >= -z_near) && (view_min.z <= z_near)
    ) {
        return true;
    }
    if (view_max.z <= z_near) {
        return false; // in front of the near plane: another face's
    }
    const float    z0    = std::max(view_min.z, z_near);
    const float    z1    = view_max.z;
    const uint32_t k     = parameters.get_tile_size();
    const uint32_t tiles = parameters.get_tiles();
    const glm::vec4 tangents = parameters.tangents;
    uint32_t count_x_min = tiles + 1u;
    uint32_t count_x_max = 0u;
    uint32_t count_y_min = tiles + 1u;
    uint32_t count_y_max = 0u;
    for (uint32_t corner = 0; corner < 4u; ++corner) {
        const float z  = ((corner & 1u) != 0u) ? z1 : z0;
        const float vx = ((corner & 2u) != 0u) ? view_max.x : view_min.x;
        const float vy = ((corner & 2u) != 0u) ? view_max.y : view_min.y;
        const uint32_t count_x = boundary_count(vx, z, tangents.x, tangents.y, k, tiles);
        const uint32_t count_y = boundary_count(vy, z, tangents.z, tangents.w, k, tiles);
        count_x_min = std::min(count_x_min, count_x);
        count_x_max = std::max(count_x_max, count_x);
        count_y_min = std::min(count_y_min, count_y);
        count_y_max = std::max(count_y_max, count_y);
    }
    if ((count_x_max == 0u) || (count_x_min == (tiles + 1u)) || (count_y_max == 0u) || (count_y_min == (tiles + 1u))) {
        return false; // outside the face
    }
    const glm::uvec4 core = parameters.core;
    if ((core.y == 0u) || (core.w == 0u)) {
        return false;
    }
    const uint32_t x0 = std::max(std::max(count_x_min, 1u) - 1u, core.x / k);
    const uint32_t x1 = std::min(std::min(count_x_max, tiles) - 1u, (core.y - 1u) / k);
    const uint32_t y0 = std::max(std::max(count_y_min, 1u) - 1u, core.z / k);
    const uint32_t y1 = std::min(std::min(count_y_max, tiles) - 1u, (core.w - 1u) / k);
    if ((x0 > x1) || (y0 > y1)) {
        return false;
    }
    const uint32_t layer_count = parameters.get_layer_count();
    const uint32_t layer_0     = std::max(layer_of_front(parameters, z0), 1u) - 1u;
    const uint32_t layer_1     = std::min(layer_of_front(parameters, z1) + 1u, layer_count - 1u);
    uint32_t level = 0;
    while ((((x1 >> level) - (x0 >> level)) > 1u) || (((y1 >> level) - (y0 >> level)) > 1u)) {
        ++level;
    }
    const uint32_t size   = pvs_map_level_size(tiles, level);
    const uint32_t offset = pvs_map_level_offset(parameters, level);
    for (uint32_t layer = layer_0; layer <= layer_1; ++layer) {
        for (uint32_t y = (y0 >> level); y <= (y1 >> level); ++y) {
            for (uint32_t x = (x0 >> level); x <= (x1 >> level); ++x) {
                if (map[offset + (((layer * size) + y) * size) + x] != 0u) {
                    return true;
                }
            }
        }
    }
    return false;
}

namespace {

// The pixel's first sample at or behind layer `start` (false: none, an
// infinitely deep pixel, or outside the face). erhe_pvs_read_behind().
[[nodiscard]] auto read_behind(
    const Pvs_disocclusion_parameters& parameters,
    const std::span<const uint32_t>    depth,
    const int32_t                      px,
    const int32_t                      py,
    const uint32_t                     start,
    float&                             out
) -> bool
{
    const int32_t resolution = static_cast<int32_t>(parameters.get_resolution());
    if ((px < 0) || (py < 0) || (px >= resolution) || (py >= resolution)) {
        return false;
    }
    const uint32_t pixel = static_cast<uint32_t>((py * resolution) + px);
    for (uint32_t layer = start, end = parameters.get_layer_count(); layer < end; ++layer) {
        const uint32_t word = depth[(layer * parameters.get_words_per_layer()) + pixel];
        if (word != c_cleared_depth) {
            out = std::bit_cast<float>(word);
            return true;
        }
    }
    return false;
}

// Whether the step between two adjacent pixels a and b is a gap (the
// shared test of rules 1 and 3; erhe_pvs_pair_has_gap()): larger than
// pvs_gap_slope_tolerance times the local per-pixel step plus
// pvs_gap_depth_epsilon times the larger depth, max(|a - a'|,
// |b - b'|) with a' the pixel before a and b' the one after b on the same
// axis (a missing one adds no step). A missing pixel is infinitely deep.
// Directional: only b deeper than a counts, and a missing a is no gap.
[[nodiscard]] auto pair_has_gap(
    const bool  a_ok,  const float a,
    const bool  b_ok,  const float b,
    const bool  a1_ok, const float a1,
    const bool  b1_ok, const float b1,
    const bool  directional
) -> bool
{
    if (!a_ok && !b_ok) {
        return false;
    }
    if (!a_ok) {
        return !directional;
    }
    if (!b_ok) {
        return true;
    }
    const float a_step = a1_ok ? std::abs(a - a1) : 0.0f;
    const float b_step = b1_ok ? std::abs(b - b1) : 0.0f;
    const float step   = std::max(a_step, b_step);
    const float limit  = (pvs_gap_slope_tolerance * step) + (pvs_gap_depth_epsilon * std::max(a, b));
    if (b > (a + limit)) {
        return true;
    }
    return !directional && (a > (b + limit));
}

// Whether any 4-adjacent pixel pair inside tile column (tile_x, tile_y)
// has a gap, each pixel's first sample at or behind layer `start`
// (erhe_pvs_tile_has_inner_gap()).
[[nodiscard]] auto tile_has_inner_gap(
    const Pvs_disocclusion_parameters& parameters,
    const std::span<const uint32_t>    depth,
    const uint32_t                     tile_x,
    const uint32_t                     tile_y,
    const uint32_t                     start
) -> bool
{
    const int32_t k  = static_cast<int32_t>(parameters.get_tile_size());
    const int32_t x0 = static_cast<int32_t>(tile_x) * k;
    const int32_t y0 = static_cast<int32_t>(tile_y) * k;
    for (int32_t axis = 0; axis < 2; ++axis) {
        const int32_t dx = (axis == 0) ? 1 : 0;
        const int32_t dy = (axis == 0) ? 0 : 1;
        for (int32_t j = 0; j < k; ++j) {
            for (int32_t i = 0; (i + 1) < k; ++i) {
                const int32_t ax = x0 + ((axis == 0) ? i : j);
                const int32_t ay = y0 + ((axis == 0) ? j : i);
                float a  = 0.0f;
                float b  = 0.0f;
                float a1 = 0.0f;
                float b1 = 0.0f;
                const bool a_ok  = read_behind(parameters, depth, ax,            ay,            start, a);
                const bool b_ok  = read_behind(parameters, depth, ax + dx,       ay + dy,       start, b);
                const bool a1_ok = read_behind(parameters, depth, ax - dx,       ay - dy,       start, a1);
                const bool b1_ok = read_behind(parameters, depth, ax + (2 * dx), ay + (2 * dy), start, b1);
                if (pair_has_gap(a_ok, a, b_ok, b, a1_ok, a1, b1_ok, b1, false)) {
                    return true;
                }
            }
        }
    }
    return false;
}

} // anonymous namespace

auto pvs_side_has_gap(
    const Pvs_disocclusion_parameters& parameters,
    const std::span<const uint32_t>    depth,
    const std::span<const uint32_t>    runs,
    const uint32_t                     tile_x,
    const uint32_t                     tile_y,
    const uint32_t                     layer,
    const uint32_t                     side
) -> bool
{
    // erhe_pvs_side_has_gap() (erhe_pvs_disocclusion.glsl), operation for
    // operation.
    const int32_t  k     = static_cast<int32_t>(parameters.get_tile_size());
    const uint32_t tiles = parameters.get_tiles();
    // The neighbour's closing surface starts at its run's first layer when
    // the layer is in its run, else at the layer.
    const uint32_t neighbour_x = (side == 0u) ? (tile_x - 1u) : ((side == 1u) ? (tile_x + 1u) : tile_x);
    const uint32_t neighbour_y = (side == 2u) ? (tile_y - 1u) : ((side == 3u) ? (tile_y + 1u) : tile_y);
    const uint32_t run         = runs[(neighbour_y * tiles) + neighbour_x];
    const uint32_t run_first   = run & 0xffu;
    const uint32_t run_last    = (run >> 8u) & 0xffu;
    const uint32_t start       = ((layer >= run_first) && (layer <= run_last)) ? run_first : layer;
    // (i) The shared edge: per boundary row the window's edge pixel w
    // deeper than the neighbour's edge pixel n (n one step outward from
    // the window, n' one more, w' one into the window).
    const int32_t inward      = ((side & 1u) == 0u) ? 1 : -1;
    const int32_t tile_across = (side < 2u) ? static_cast<int32_t>(tile_x) : static_cast<int32_t>(tile_y);
    const int32_t tile_along  = (side < 2u) ? static_cast<int32_t>(tile_y) : static_cast<int32_t>(tile_x);
    const int32_t edge        = ((side & 1u) == 0u) ? (tile_across * k) : ((tile_across * k) + (k - 1));
    for (int32_t i = 0; i < k; ++i) {
        const int32_t along = (tile_along * k) + i;
        const auto read = [&](const int32_t across, float& out) -> bool {
            return (side < 2u)
                ? read_behind(parameters, depth, across, along, start, out)
                : read_behind(parameters, depth, along, across, start, out);
        };
        float n  = 0.0f;
        float w  = 0.0f;
        float n1 = 0.0f;
        float w1 = 0.0f;
        const bool n_ok  = read(edge - inward,       n);
        const bool w_ok  = read(edge,                w);
        const bool n1_ok = read(edge - (2 * inward), n1);
        const bool w1_ok = read(edge + inward,       w1);
        if (pair_has_gap(n_ok, n, w_ok, w, n1_ok, n1, w1_ok, w1, true)) {
            return true;
        }
    }
    // (ii) A silhouette anywhere inside the window, in either direction
    // and along either axis: a ray from the cell behind the window's sheet
    // reaches the shared edge behind the neighbour's.
    return tile_has_inner_gap(parameters, depth, tile_x, tile_y, start);
}

auto pvs_side_gap_details(
    const Pvs_disocclusion_parameters& parameters,
    const std::span<const uint32_t>    depth,
    const std::span<const uint32_t>    runs,
    const uint32_t                     tile_x,
    const uint32_t                     tile_y,
    const uint32_t                     layer,
    const uint32_t                     side
) -> Pvs_side_gap_details
{
    // A diagnostic twin of pvs_side_has_gap() reporting the pair closest
    // to a gap of each part.
    Pvs_side_gap_details details{};
    const int32_t  k     = static_cast<int32_t>(parameters.get_tile_size());
    const uint32_t tiles = parameters.get_tiles();
    const uint32_t neighbour_x = (side == 0u) ? (tile_x - 1u) : ((side == 1u) ? (tile_x + 1u) : tile_x);
    const uint32_t neighbour_y = (side == 2u) ? (tile_y - 1u) : ((side == 3u) ? (tile_y + 1u) : tile_y);
    const uint32_t run         = runs[(neighbour_y * tiles) + neighbour_x];
    const uint32_t run_first   = run & 0xffu;
    const uint32_t run_last    = (run >> 8u) & 0xffu;
    const uint32_t start       = ((layer >= run_first) && (layer <= run_last)) ? run_first : layer;
    const auto measure = [&](const int32_t ax, const int32_t ay, const int32_t dx, const int32_t dy, const bool directional, Pvs_gap_pair& worst) -> void {
        float a = 0.0f, b = 0.0f, a1 = 0.0f, b1 = 0.0f;
        const bool a_ok  = read_behind(parameters, depth, ax,            ay,            start, a);
        const bool b_ok  = read_behind(parameters, depth, ax + dx,       ay + dy,       start, b);
        const bool a1_ok = read_behind(parameters, depth, ax - dx,       ay - dy,       start, a1);
        const bool b1_ok = read_behind(parameters, depth, ax + (2 * dx), ay + (2 * dy), start, b1);
        float margin = -1.0e30f;
        if (a_ok && b_ok) {
            const float step  = std::max(a1_ok ? std::abs(a - a1) : 0.0f, b1_ok ? std::abs(b - b1) : 0.0f);
            const float limit = (pvs_gap_slope_tolerance * step) + (pvs_gap_depth_epsilon * std::max(a, b));
            margin = directional ? ((b - a) - limit) : (std::abs(b - a) - limit);
        } else if (a_ok != b_ok) {
            margin = (directional && !a_ok) ? -1.0e30f : 1.0e30f;
        }
        if (margin > worst.margin) {
            worst.margin = margin;
            worst.pixels = glm::ivec4{ax, ay, ax + dx, ay + dy};
            worst.depths = glm::vec4{a_ok ? a : 0.0f, a1_ok ? a1 : 0.0f, b_ok ? b : 0.0f, b1_ok ? b1 : 0.0f};
            worst.start  = start;
        }
    };
    const int32_t inward      = ((side & 1u) == 0u) ? 1 : -1;
    const int32_t tile_across = (side < 2u) ? static_cast<int32_t>(tile_x) : static_cast<int32_t>(tile_y);
    const int32_t tile_along  = (side < 2u) ? static_cast<int32_t>(tile_y) : static_cast<int32_t>(tile_x);
    const int32_t edge        = ((side & 1u) == 0u) ? (tile_across * k) : ((tile_across * k) + (k - 1));
    for (int32_t i = 0; i < k; ++i) {
        const int32_t along = (tile_along * k) + i;
        const int32_t n_across = edge - inward;
        if (side < 2u) {
            measure(n_across, along, inward, 0, true, details.edge);
        } else {
            measure(along, n_across, 0, inward, true, details.edge);
        }
    }
    const int32_t x0 = static_cast<int32_t>(tile_x) * k;
    const int32_t y0 = static_cast<int32_t>(tile_y) * k;
    for (int32_t axis = 0; axis < 2; ++axis) {
        for (int32_t j = 0; j < k; ++j) {
            for (int32_t i = 0; (i + 1) < k; ++i) {
                measure(x0 + ((axis == 0) ? i : j), y0 + ((axis == 0) ? j : i), (axis == 0) ? 1 : 0, (axis == 0) ? 0 : 1, false, details.inner);
            }
        }
    }
    details.edge_gap  = details.edge.margin  > 0.0f;
    details.inner_gap = details.inner.margin > 0.0f;
    return details;
}

void build_pvs_disoccluded_map(const Pvs_disocclusion_parameters& parameters, Pvs_disocclusion_result& result)
{
    const uint32_t tiles       = parameters.get_tiles();
    const uint32_t layer_count = parameters.get_layer_count();
    result.map.assign(pvs_map_word_count(parameters), 0u);
    for (uint32_t layer = 0; layer < layer_count; ++layer) {
        for (uint32_t y = 0; y < tiles; ++y) {
            for (uint32_t x = 0; x < tiles; ++x) {
                result.map[(((layer * tiles) + y) * tiles) + x] = is_pvs_tile_disoccluded(parameters, result, x, y, layer) ? 1u : 0u;
            }
        }
    }
    for (uint32_t level = 1; level < parameters.get_map_level_count(); ++level) {
        const uint32_t size        = pvs_map_level_size(tiles, level);
        const uint32_t child_size  = pvs_map_level_size(tiles, level - 1u);
        const uint32_t offset      = pvs_map_level_offset(parameters, level);
        const uint32_t child_offset = pvs_map_level_offset(parameters, level - 1u);
        for (uint32_t layer = 0; layer < layer_count; ++layer) {
            for (uint32_t y = 0; y < size; ++y) {
                for (uint32_t x = 0; x < size; ++x) {
                    uint32_t any = 0u;
                    for (uint32_t child = 0; child < 4u; ++child) {
                        const uint32_t cx = std::min((2u * x) + (child & 1u), child_size - 1u);
                        const uint32_t cy = std::min((2u * y) + (child >> 1u), child_size - 1u);
                        any |= result.map[child_offset + (((layer * child_size) + cy) * child_size) + cx];
                    }
                    result.map[offset + (((layer * size) + y) * size) + x] = any;
                }
            }
        }
    }
}

void run_pvs_disocclusion_reference(
    const Pvs_disocclusion_parameters& parameters,
    const std::span<const uint32_t>    layered_words,
    const std::span<const glm::vec4>   column_bounds,
    Pvs_disocclusion_result&           result
)
{
    const uint32_t k               = parameters.get_tile_size();
    const uint32_t samples         = k * k;
    const uint32_t layer_count     = parameters.get_layer_count();
    const uint32_t tiles           = parameters.get_tiles();
    const uint32_t tiles_per_layer = parameters.get_tiles_per_layer();
    const uint32_t tile_count      = parameters.get_tile_count();
    const uint32_t region_words    = parameters.get_words_per_layer() * layer_count;
    ERHE_VERIFY(layered_words.size() >= static_cast<std::size_t>(region_words));
    ERHE_VERIFY(column_bounds.size() >= (2u * static_cast<std::size_t>(parameters.get_column_count())));
    const std::span<const uint32_t> depth = layered_words.subspan(0, region_words);

    result.closed      .assign(tile_count, 0u);
    result.runs        .assign(tiles_per_layer, layer_count);
    result.column_masks.assign(static_cast<std::size_t>(tiles_per_layer) * 2u, 0u);
    result.tile_masks  .assign(static_cast<std::size_t>(tile_count) * 2u, 0u);
    result.cones.clear();
    if (result.column_words.size() != parameters.get_column_word_count()) {
        result.column_words.assign(parameters.get_column_word_count(), 0u);
    }

    // tiles: per-layer coverage and the contiguous-run closure
    for (uint32_t y = 0; y < tiles; ++y) {
        for (uint32_t x = 0; x < tiles; ++x) {
            uint64_t first_hits = 0;    // bit L: some pixel's first hit is L
            bool     all_hit    = true;
            for (uint32_t sample = 0; sample < samples; ++sample) {
                uint32_t first_hit = layer_count;
                for (uint32_t layer = 0; layer < layer_count; ++layer) {
                    if (depth[sample_word(parameters, x, y, layer, sample)] != c_cleared_depth) {
                        first_hit = layer;
                        break;
                    }
                }
                if (first_hit == layer_count) {
                    all_hit = false;
                } else {
                    first_hits |= (uint64_t{1} << first_hit);
                }
            }
            // A gap-free run: the set bits are contiguous.
            uint32_t run_first = layer_count;
            uint32_t run_last  = 0;
            if (all_hit && (first_hits != 0u)) {
                const uint64_t shifted = first_hits >> std::countr_zero(first_hits);
                if ((shifted & (shifted + 1u)) == 0u) {
                    run_first = static_cast<uint32_t>(std::countr_zero(first_hits));
                    run_last  = 63u - static_cast<uint32_t>(std::countl_zero(first_hits));
                }
            }
            // Rule 1 needs a continuous sheet: a run with a silhouette
            // inside the tile does not close it (a ray passes from the free
            // space in front of one part to behind the other).
            if ((run_first < layer_count) && tile_has_inner_gap(parameters, depth, x, y, 0u)) {
                run_first = layer_count;
                run_last  = 0;
            }
            // The run's surface passes its own layers in front of its last:
            // a sample of the run at layer L needs bits run_first .. L - 1,
            // which no window of the closed tile gives.
            result.runs[(y * tiles) + x] = run_first | (run_last << 8u);
            for (uint32_t layer = run_first; layer < run_last; ++layer) {
                result.column_masks[(((y * tiles) + x) * 2u) + (layer >> 5u)] |= (1u << (layer & 31u));
            }
            for (uint32_t layer = 0; layer < layer_count; ++layer) {
                uint32_t count = 0;
                for (uint32_t sample = 0; sample < samples; ++sample) {
                    if (depth[sample_word(parameters, x, y, layer, sample)] != c_cleared_depth) {
                        ++count;
                    }
                }
                const bool in_run = (layer >= run_first) && (layer <= run_last);
                result.closed[tile_index(tiles, x, y, layer)] = ((count == samples) || in_run) ? 1u : 0u;
            }
        }
    }

    // per layer, front to back: classify (gated), then scatter the cones
    const auto is_closed = [&](const uint32_t x, const uint32_t y, const uint32_t layer) -> bool {
        return result.closed[tile_index(tiles, x, y, layer)] != 0u;
    };
    std::vector<glm::uvec2> layer_cones;
    for (uint32_t layer = 0; (layer + 1u) < layer_count; ++layer) {
        layer_cones.clear();
        for (uint32_t y = 0; y < tiles; ++y) {
            for (uint32_t x = 0; x < tiles; ++x) {
                if (is_closed(x, y, layer) || !is_pvs_tile_disoccluded(parameters, result, x, y, layer)) {
                    continue;
                }
                const uint32_t left  = std::max(x, 1u) - 1u;
                const uint32_t right = std::min(x + 1u, tiles - 1u);
                const uint32_t down  = std::max(y, 1u) - 1u;
                const uint32_t up    = std::min(y + 1u, tiles - 1u);
                const std::span<const uint32_t> runs{result.runs};
                const bool tests = (parameters.columns.z != 0u); // rule 3 on
                uint32_t sides = 0u;
                if (is_closed(left,  y,    layer) && (!tests || pvs_side_has_gap(parameters, depth, runs, x, y, layer, 0u))) { sides |= 1u; }
                if (is_closed(right, y,    layer) && (!tests || pvs_side_has_gap(parameters, depth, runs, x, y, layer, 1u))) { sides |= 2u; }
                if (is_closed(x,     down, layer) && (!tests || pvs_side_has_gap(parameters, depth, runs, x, y, layer, 2u))) { sides |= 4u; }
                if (is_closed(x,     up,   layer) && (!tests || pvs_side_has_gap(parameters, depth, runs, x, y, layer, 3u))) { sides |= 8u; }
                if (sides == 0u) {
                    result.column_masks[(((y * tiles) + x) * 2u) + (layer >> 5u)] |= (1u << (layer & 31u));
                } else {
                    layer_cones.push_back(glm::uvec2{tile_index(tiles, x, y, layer), sides});
                }
            }
        }
        for (const glm::uvec2& cone : layer_cones) {
            const uint32_t x = cone.x % tiles;
            const uint32_t y = (cone.x / tiles) % tiles;
            for (uint32_t target_layer = layer + 1u; target_layer < layer_count; ++target_layer) {
                const glm::uvec4 rectangle = pvs_cone_rectangle(parameters, x, y, layer, cone.y, target_layer);
                for (uint32_t ty = rectangle.y; ty < rectangle.w; ++ty) {
                    for (uint32_t tx = rectangle.x; tx < rectangle.z; ++tx) {
                        result.tile_masks[(tile_index(tiles, tx, ty, target_layer) * 2u) + (layer >> 5u)] |= (1u << (layer & 31u));
                    }
                }
            }
            result.cones.push_back(cone);
        }
    }

    build_pvs_disoccluded_map(parameters, result);

    // bounds vote
    for (uint32_t column = 0, end = parameters.get_column_count(); column < end; ++column) {
        const glm::vec4 bounds_min = column_bounds[(2u * column) + 0u];
        const glm::vec4 bounds_max = column_bounds[(2u * column) + 1u];
        if (pvs_bounds_vote(parameters, glm::vec3{bounds_min}, glm::vec3{bounds_max}, std::span<const uint32_t>{result.map})) {
            result.column_words[column >> 5u] |= (1u << (column & 31u));
        }
    }
}

auto is_pvs_tile_disoccluded(
    const Pvs_disocclusion_parameters& parameters,
    const Pvs_disocclusion_result&     result,
    const uint32_t                     tile_x,
    const uint32_t                     tile_y,
    const uint32_t                     layer
) -> bool
{
    if (layer == 0u) {
        return true;
    }
    const uint32_t tiles  = parameters.get_tiles();
    const uint32_t tile   = tile_index(tiles, tile_x, tile_y, layer);
    const uint32_t column = (tile_y * tiles) + tile_x;
    const uint32_t low    = result.tile_masks[(tile * 2u) + 0u] | result.column_masks[(column * 2u) + 0u];
    const uint32_t high   = result.tile_masks[(tile * 2u) + 1u] | result.column_masks[(column * 2u) + 1u];
    const uint32_t need_low  = below_mask_low (layer);
    const uint32_t need_high = below_mask_high(layer);
    return ((low & need_low) == need_low) && ((high & need_high) == need_high);
}

} // namespace erhe::scene_renderer
