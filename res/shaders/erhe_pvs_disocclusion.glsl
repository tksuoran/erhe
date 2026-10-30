#ifndef ERHE_PVS_DISOCCLUSION_GLSL
#define ERHE_PVS_DISOCCLUSION_GLSL

// Shared by the disocclusion passes of the potentially visible set bake
// (pvs_tiles.comp, pvs_stencil.comp, pvs_cones.comp, pvs_map.comp,
// pvs_vote.comp, pvs_trace.comp; doc/plans/pvs.md "Bake" 5). The blocks are
// declared by erhe::scene_renderer::Pvs_disocclusion_pipelines:
//
// pvs_disocclusion_parameters (erhe_scene_renderer/pvs_disocclusion.hpp
//   Pvs_disocclusion_parameters): sizes (resolution, tile size K, layer
//   count N, tiles per side), counts (tiles per layer, tiles, column words,
//   words per layer), core (the 90 degree core's pixels: x first, x end, y
//   first, y end), pass (the layer or map level of a per-layer dispatch),
//   columns (column count, map levels), view_axes (the world axis of view
//   x, y, z; w: bit i = axis i negated), sample_point, tangents, tile_scale,
//   cell_min, cell_max, layers[N + 1]
// pvs_layered_samples: the face's layered buffer (Pvs_layered_interface):
//   info, trace, words[] = depth, id_min, id_max regions
// pvs_closure.closed[tile]: 1 when the tile is closed (pvs_tiles.comp)
// pvs_column_masks.words[2 * (y * tiles + x) + w]: the flat column masks
// pvs_tile_masks.words[2 * tile + w]: the tile masks the cones write
// pvs_lists.words[]: [L] the cone count of layer L (64 counters), then per
//   layer the cones (tile index, closed sides; capacity tiles per layer)
// pvs_map.words[]: the disoccluded map, every level (pvs_map.comp)
// pvs_column_bounds.bounds[2 * c], [2 * c + 1]: column c's world bounds
// pvs_column_words.words[]: bit c = column c is in the cell's PVS
// pvs_runs.words[y * tiles + x]: the column's contiguous run, first | last
//   << 8 (pvs_tiles.comp; first = N when it has none)
//
// Every float operation the C++ twins repeat is precise:
// erhe::scene_renderer::pvs_cone_rectangle() / pvs_bounds_vote() perform
// the same operations in the same order, compiled without contraction, so
// the results match bit for bit.

const uint c_pvs_cleared       = 0xffffffffu;
const uint c_pvs_lists_header  = 64u;
const uint c_pvs_group_size    = 64u;

uint erhe_pvs_tile_index(uint tiles, uint x, uint y, uint layer)
{
    return (((layer * tiles) + y) * tiles) + x;
}

// The word of sample `sample_index` (row-major in the tile) of tile (x, y, layer)
// in one region of the layered buffer.
uint erhe_pvs_sample_word(uint x, uint y, uint layer, uint sample_index)
{
    uint k          = pvs_disocclusion_parameters.sizes.y;
    uint resolution = pvs_disocclusion_parameters.sizes.x;
    uint px         = (x * k) + (sample_index % k);
    uint py         = (y * k) + (sample_index / k);
    return (layer * pvs_disocclusion_parameters.counts.w) + (py * resolution) + px;
}

uint erhe_pvs_below_mask_low(uint layer)
{
    return (layer >= 32u) ? 0xffffffffu : ((1u << layer) - 1u);
}

uint erhe_pvs_below_mask_high(uint layer)
{
    return (layer <= 32u) ? 0u : ((1u << (layer - 32u)) - 1u);
}

// The vote's predicate: tile (x, y, layer) is disoccluded when layer == 0
// or bits 0 .. layer - 1 are all set in (its tile mask | the column mask at
// (x, y)) (erhe::scene_renderer::is_pvs_tile_disoccluded()).
bool erhe_pvs_is_disoccluded(uint tiles, uint x, uint y, uint layer)
{
    if (layer == 0u) {
        return true;
    }
    uint tile   = erhe_pvs_tile_index(tiles, x, y, layer);
    uint column = (y * tiles) + x;
    uint low    = pvs_tile_masks.words[(tile * 2u) + 0u] | pvs_column_masks.words[(column * 2u) + 0u];
    uint high   = pvs_tile_masks.words[(tile * 2u) + 1u] | pvs_column_masks.words[(column * 2u) + 1u];
    uint need_low  = erhe_pvs_below_mask_low (layer);
    uint need_high = erhe_pvs_below_mask_high(layer);
    return ((low & need_low) == need_low) && ((high & need_high) == need_high);
}

// erhe::scene_renderer::pvs_cone_rectangle() (pvs_disocclusion.hpp): the
// tiles of layer target_layer the cone of open tile (tile_x, tile_y,
// layer) with closed sides `sides` reaches; (lo.x, lo.y, hi.x, hi.y), hi
// exclusive.
uvec4 erhe_pvs_cone_rectangle(uint tile_x, uint tile_y, uint layer, uint sides, uint target_layer)
{
    uint k     = pvs_disocclusion_parameters.sizes.y;
    uint tiles = pvs_disocclusion_parameters.sizes.w;
    vec4 tangents   = pvs_disocclusion_parameters.tangents;
    vec4 tile_scale = pvs_disocclusion_parameters.tile_scale;
    vec4 cell_min   = pvs_disocclusion_parameters.cell_min;
    vec4 cell_max   = pvs_disocclusion_parameters.cell_max;
    vec4 window     = pvs_disocclusion_parameters.layers[layer];

    precise float u_x0 = tangents.x + (float(tile_x * k)        * tangents.y);
    precise float u_x1 = tangents.x + (float((tile_x + 1u) * k) * tangents.y);
    precise float u_y0 = tangents.z + (float(tile_y * k)        * tangents.w);
    precise float u_y1 = tangents.z + (float((tile_y + 1u) * k) * tangents.w);
    precise float w_x0 = window.x * u_x0;
    precise float w_x1 = window.x * u_x1;
    precise float w_y0 = window.x * u_y0;
    precise float w_y1 = window.x * u_y1;

    float min_x = 0.0;
    float max_x = 0.0;
    float min_y = 0.0;
    float max_y = 0.0;
    bool  first = true;
    for (uint plane = 0u; plane < 2u; ++plane) {
        vec4 target = pvs_disocclusion_parameters.layers[target_layer + plane];
        for (uint depth_corner = 0u; depth_corner < 2u; ++depth_corner) {
            float cd      = (depth_corner == 0u) ? cell_min.z : cell_max.z;
            float inv_gap = (depth_corner == 0u) ? window.z   : window.w;
            precise float t = (target.x - cd) * inv_gap;
            for (uint corner = 0u; corner < 2u; ++corner) {
                float cx = (corner == 0u) ? cell_min.x : cell_max.x;
                float cy = (corner == 0u) ? cell_min.y : cell_max.y;
                for (uint window_corner = 0u; window_corner < 2u; ++window_corner) {
                    float wx = (window_corner == 0u) ? w_x0 : w_x1;
                    float wy = (window_corner == 0u) ? w_y0 : w_y1;
                    precise float x  = cx + (t * (wx - cx));
                    precise float y  = cy + (t * (wy - cy));
                    precise float fx = ((x * target.y) - tangents.x) * tile_scale.x;
                    precise float fy = ((y * target.y) - tangents.z) * tile_scale.y;
                    if (first) {
                        min_x = fx;
                        max_x = fx;
                        min_y = fy;
                        max_y = fy;
                        first = false;
                    } else {
                        min_x = min(min_x, fx);
                        max_x = max(max_x, fx);
                        min_y = min(min_y, fy);
                        max_y = max(max_y, fy);
                    }
                }
            }
        }
    }
    float tiles_f = float(tiles);
    precise float hi_x_f = floor(max_x) + 1.0;
    precise float hi_y_f = floor(max_y) + 1.0;
    uint lo_x = uint(clamp(floor(min_x), 0.0, tiles_f));
    uint lo_y = uint(clamp(floor(min_y), 0.0, tiles_f));
    uint hi_x = uint(clamp(hi_x_f,       0.0, tiles_f));
    uint hi_y = uint(clamp(hi_y_f,       0.0, tiles_f));
    if ((sides & 1u) == 0u) { lo_x = max(lo_x, tile_x); }
    if ((sides & 2u) == 0u) { hi_x = min(hi_x, tile_x + 1u); }
    if ((sides & 4u) == 0u) { lo_y = max(lo_y, tile_y); }
    if ((sides & 8u) == 0u) { hi_y = min(hi_y, tile_y + 1u); }
    return uvec4(lo_x, lo_y, hi_x, hi_y);
}

// The disoccluded map (pvs_map.comp): texels per side of a level, and the
// word offset of a level (erhe::scene_renderer::pvs_map_level_size() /
// pvs_map_level_offset()).
uint erhe_pvs_map_level_size(uint tiles, uint level)
{
    return (tiles + (1u << level) - 1u) >> level;
}

uint erhe_pvs_map_level_offset(uint tiles, uint layer_count, uint level)
{
    uint offset = 0u;
    for (uint i = 0u; i < level; ++i) {
        uint size = erhe_pvs_map_level_size(tiles, i);
        offset += layer_count * size * size;
    }
    return offset;
}

// The boundaries b in [0, tiles] of the tile grid a point (v, z) of the
// view plane is at or past (erhe::scene_renderer pvs_disocclusion.cpp
// boundary_count(), operation for operation).
uint erhe_pvs_boundary_count(float v, float z, float t0, float dt, uint k, uint tiles)
{
    uint low  = 0u;
    uint high = tiles + 1u;
    while (low < high) {
        uint middle = (low + high) >> 1u;
        precise float tangent  = t0 + (float(middle * k) * dt);
        precise float boundary = tangent * z;
        bool past = (dt > 0.0) ? (v >= boundary) : (v <= boundary);
        if (past) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return low;
}

// The last layer k in [0, N - 1] with z >= z_k (layer_of_front()).
uint erhe_pvs_layer_of_front(float z)
{
    uint low  = 1u;
    uint high = pvs_disocclusion_parameters.sizes.z;
    while (low < high) {
        uint middle = (low + high) >> 1u;
        if (z >= pvs_disocclusion_parameters.layers[middle].x) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return low - 1u;
}


// The pixel's first sample at or behind layer `start` (false: none, an
// infinitely deep pixel, or outside the face; erhe::scene_renderer
// read_behind()).
bool erhe_pvs_read_behind(int px, int py, uint start, out float value)
{
    int  resolution      = int(pvs_disocclusion_parameters.sizes.x);
    uint layer_count     = pvs_disocclusion_parameters.sizes.z;
    uint words_per_layer = pvs_disocclusion_parameters.counts.w;
    value = 0.0;
    if ((px < 0) || (py < 0) || (px >= resolution) || (py >= resolution)) {
        return false;
    }
    uint pixel = uint((py * resolution) + px);
    for (uint l = start; l < layer_count; ++l) {
        uint word = pvs_layered_samples.words[(l * words_per_layer) + pixel];
        if (word != c_pvs_cleared) {
            value = uintBitsToFloat(word);
            return true;
        }
    }
    return false;
}

// The shared gap test of rules 1 and 3 (erhe::scene_renderer
// pair_has_gap(), operation for operation).
bool erhe_pvs_pair_has_gap(bool a_ok, float a, bool b_ok, float b, bool a1_ok, float a1, bool b1_ok, float b1, bool directional)
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
    precise float a_step = a1_ok ? abs(a - a1) : 0.0;
    precise float b_step = b1_ok ? abs(b - b1) : 0.0;
    precise float step   = max(a_step, b_step);
    precise float limit  = (ERHE_PVS_GAP_SLOPE_TOLERANCE * step) + (ERHE_PVS_GAP_DEPTH_EPSILON * max(a, b));
    precise float b_limit = a + limit;
    if (b > b_limit) {
        return true;
    }
    precise float a_limit = b + limit;
    return !directional && (a > a_limit);
}

// Any 4-adjacent pixel pair of tile column (tile_x, tile_y) with a gap,
// each pixel's first sample at or behind layer `start`
// (tile_has_inner_gap()).
bool erhe_pvs_tile_has_inner_gap(uint tile_x, uint tile_y, uint start)
{
    int k  = int(pvs_disocclusion_parameters.sizes.y);
    int x0 = int(tile_x) * k;
    int y0 = int(tile_y) * k;
    for (int axis = 0; axis < 2; ++axis) {
        int dx = (axis == 0) ? 1 : 0;
        int dy = (axis == 0) ? 0 : 1;
        for (int j = 0; j < k; ++j) {
            for (int i = 0; (i + 1) < k; ++i) {
                int ax = x0 + ((axis == 0) ? i : j);
                int ay = y0 + ((axis == 0) ? j : i);
                float a;
                float b;
                float a1;
                float b1;
                bool a_ok  = erhe_pvs_read_behind(ax,            ay,            start, a);
                bool b_ok  = erhe_pvs_read_behind(ax + dx,       ay + dy,       start, b);
                bool a1_ok = erhe_pvs_read_behind(ax - dx,       ay - dy,       start, a1);
                bool b1_ok = erhe_pvs_read_behind(ax + (2 * dx), ay + (2 * dy), start, b1);
                if (erhe_pvs_pair_has_gap(a_ok, a, b_ok, b, a1_ok, a1, b1_ok, b1, false)) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Rule 3 of classify: whether the closed neighbour on `side` (0 -x, 1 +x,
// 2 -y, 3 +y) of window (tile_x, tile_y, layer) is a sloped side
// (erhe::scene_renderer::pvs_side_has_gap(), operation for operation): (i)
// on some boundary row the window's edge pixel deeper than the
// neighbour's by more than the tolerance, or (ii) a gap between any two
// 4-adjacent pixels inside the window; the pixels' first samples at or
// behind layer S (the neighbour's run's first layer when the layer is in
// its run, else the layer).
bool erhe_pvs_side_has_gap(uint tile_x, uint tile_y, uint layer, uint side_u)
{
    uint tiles       = pvs_disocclusion_parameters.sizes.w;
    int  k           = int(pvs_disocclusion_parameters.sizes.y);
    uint neighbour_x = (side_u == 0u) ? (tile_x - 1u) : ((side_u == 1u) ? (tile_x + 1u) : tile_x);
    uint neighbour_y = (side_u == 2u) ? (tile_y - 1u) : ((side_u == 3u) ? (tile_y + 1u) : tile_y);
    uint run         = pvs_runs.words[(neighbour_y * tiles) + neighbour_x];
    uint run_first   = run & 0xffu;
    uint run_last    = (run >> 8u) & 0xffu;
    uint start       = ((layer >= run_first) && (layer <= run_last)) ? run_first : layer;
    int  inward      = ((side_u & 1u) == 0u) ? 1 : -1;
    int  tile_across = (side_u < 2u) ? int(tile_x) : int(tile_y);
    int  tile_along  = (side_u < 2u) ? int(tile_y) : int(tile_x);
    int  edge        = ((side_u & 1u) == 0u) ? (tile_across * k) : ((tile_across * k) + (k - 1));
    for (int i = 0; i < k; ++i) {
        int along = (tile_along * k) + i;
        float n;
        float w;
        float n1;
        float w1;
        bool n_ok;
        bool w_ok;
        bool n1_ok;
        bool w1_ok;
        if (side_u < 2u) {
            n_ok  = erhe_pvs_read_behind(edge - inward,       along, start, n);
            w_ok  = erhe_pvs_read_behind(edge,                along, start, w);
            n1_ok = erhe_pvs_read_behind(edge - (2 * inward), along, start, n1);
            w1_ok = erhe_pvs_read_behind(edge + inward,       along, start, w1);
        } else {
            n_ok  = erhe_pvs_read_behind(along, edge - inward,       start, n);
            w_ok  = erhe_pvs_read_behind(along, edge,                start, w);
            n1_ok = erhe_pvs_read_behind(along, edge - (2 * inward), start, n1);
            w1_ok = erhe_pvs_read_behind(along, edge + inward,       start, w1);
        }
        if (erhe_pvs_pair_has_gap(n_ok, n, w_ok, w, n1_ok, n1, w1_ok, w1, true)) {
            return true;
        }
    }
    return erhe_pvs_tile_has_inner_gap(tile_x, tile_y, start);
}

#endif // ERHE_PVS_DISOCCLUSION_GLSL
