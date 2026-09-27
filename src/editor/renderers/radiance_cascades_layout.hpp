#pragma once

#include "renderers/probe_grid.hpp"

#include "erhe_math/aabb.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace editor {

// The pure layout math of world-space radiance cascades
// (doc/editor/radiance_cascades.md, doc/plans/radiance_cascades.md sections
// 1 and 3): cascade fit, interval bounds, atlas tiling, octahedral texel
// nesting and the trilinear upper-probe weights of the merge. No graphics
// or scene dependency, so it is unit tested directly
// (src/editor/renderers/test/).

// Upper bound on the cascade count. Each cascade has 1/8 of the probes of
// the one below it, so even a 4096^3 cascade 0 is covered by 12 cascades.
constexpr int c_max_radiance_cascades = 12;

class Radiance_cascades_layout_settings
{
public:
    float probe_spacing_m     {0.5f};    // cascade 0 target spacing s0
    int   max_probes_cascade0 {65536};   // cascade 0 probe budget; s0 grows until it fits
    int   max_cascades        {8};       // clamped to [1, c_max_radiance_cascades]
    int   cascade0_tile_texels{4};       // q0: octahedral tile side of cascade 0
    float interval_scale      {1.0f};    // r0 = interval_scale * sqrt(3) * s0, >= 1
    int   max_texture_size    {16384};   // Device_info::max_texture_size

    [[nodiscard]] auto operator==(const Radiance_cascades_layout_settings& other) const -> bool = default;
};

// One cascade: its probe grid, octahedral tile and radiance interval, and
// where its probe tiles sit in the cascade's 2D atlas.
class Radiance_cascade
{
public:
    Probe_grid grid;                 // probe i sits at grid.origin + coords * grid.spacing
    int        tile_texels   {0};    // q_i = q0 * 2^i
    float      interval_start{0.0f}; // t_i, metres from the probe
    float      interval_end  {0.0f}; // t_{i+1}
    int        tiles_per_row {0};    // probe tiles per atlas row
    int        tile_rows     {0};

    [[nodiscard]] auto get_probe_count         () const -> int;
    [[nodiscard]] auto get_atlas_width         () const -> int;
    [[nodiscard]] auto get_atlas_height        () const -> int;
    // Texels carrying probe directions: probes x q_i^2 (the atlas can have
    // a partly filled last row).
    [[nodiscard]] auto get_texel_count         () const -> int64_t;
    // Texels of the allocated atlas: width x height.
    [[nodiscard]] auto get_atlas_texel_count   () const -> int64_t;
    // Top-left atlas texel of a probe's tile: probe_index wrapped into rows.
    [[nodiscard]] auto get_tile_origin         (int probe_index) const -> glm::ivec2;
};

class Radiance_cascades_layout
{
public:
    std::array<Radiance_cascade, c_max_radiance_cascades> cascades{};
    int   cascade_count{0};
    float r0           {0.0f}; // cascade 0 interval length

    [[nodiscard]] auto is_valid            () const -> bool;
    [[nodiscard]] auto get_cascades        () const -> std::span<const Radiance_cascade>;
    [[nodiscard]] auto get_total_texels    () const -> int64_t; // sum of get_texel_count()
    [[nodiscard]] auto get_total_probes    () const -> int64_t;
    [[nodiscard]] auto operator==          (const Radiance_cascades_layout& other) const -> bool;
    [[nodiscard]] auto operator!=          (const Radiance_cascades_layout& other) const -> bool { return !(*this == other); }
};

// Interval [start, end] of cascade i: [r0 * (2^i - 1), r0 * (2^(i+1) - 1)].
// Adjacent cascades share their boundary, cascade 0 starts at the probe.
[[nodiscard]] auto get_radiance_interval(float r0, int cascade) -> glm::vec2;

// The grid of the cascade above: spacing doubled, counts ceil(lower / 2)
// per axis, centred on the lower grid so every cascade covers the same
// volume. Per axis, an even lower count puts the upper probes at the
// centres of the lower pairs (origin moved by half a lower spacing), an odd
// one on the even lower probes (origin unchanged, both grids span the same
// extent).
[[nodiscard]] auto get_upper_grid(const Probe_grid& lower) -> Probe_grid;

// Fits all cascades to the padded content box (see
// doc/plans/radiance_cascades.md section 3): cascade 0 is fitted like DDGI
// (fit_probe_grid() with the cascade 0 budget), each next cascade from
// get_upper_grid(), until the top cascade has at most 2 probes on its
// longest axis or max_cascades is reached. The atlas of every cascade must
// stay within max_texture_size on both sides; when it would not, the
// cascade 0 budget is lowered (which grows s0) and the fit repeated.
// Invalid (cascade_count 0) for an invalid box.
[[nodiscard]] auto fit_radiance_cascades(const erhe::math::Aabb& bounds, const Radiance_cascades_layout_settings& settings) -> Radiance_cascades_layout;

// One axis of the trilinear interpolation from a lower cascade probe to the
// upper cascade (get_upper_grid(), centred on the lower grid): the two upper
// probe indices (clamped to the upper grid) and their weights. With an even
// lower count, lower probe k sits at upper grid coordinate k / 2 - 1 / 4
// (weights 0.25 and 0.75); with an odd one at k / 2 (weights 1 and 0 for
// even k, 0.5 and 0.5 for odd k).
class Upper_probe_axis
{
public:
    std::array<int,   2> index {};
    std::array<float, 2> weight{};
};
[[nodiscard]] auto get_upper_probe_axis(int lower_index, int lower_count, int upper_count) -> Upper_probe_axis;

// The 8 upper cascade probes a lower probe merges with, and their
// trilinear weights (products of the per-axis weights; they sum to 1).
// Indices are clamped at the upper grid's edges, so an edge probe can list
// the same upper probe twice.
class Upper_probes
{
public:
    std::array<glm::ivec3, 8> coords {};
    std::array<float,      8> weights{};
};
[[nodiscard]] auto get_upper_probes(const glm::ivec3& lower_coords, const glm::ivec3& lower_counts, const glm::ivec3& upper_counts) -> Upper_probes;

// Octahedral texel nesting: cascade i texel (u, v) covers the cascade i + 1
// texels (2u .. 2u+1, 2v .. 2v+1) of the same octahedral tile layout.
[[nodiscard]] auto get_child_texels(const glm::ivec2& texel) -> std::array<glm::ivec2, 4>;
[[nodiscard]] auto get_parent_texel(const glm::ivec2& child_texel) -> glm::ivec2;

// Octahedral mapping (Cigolle et al. 2014), the convention of
// res/shaders/erhe_ddgi.glsl: unit direction <-> [-1, 1]^2.
[[nodiscard]] auto octahedral_encode(const glm::vec3& direction) -> glm::vec2;
[[nodiscard]] auto octahedral_decode(const glm::vec2& f) -> glm::vec3;
// Direction through the centre of texel (u, v) of a tile_texels^2 tile.
[[nodiscard]] auto get_texel_direction(const glm::ivec2& texel, int tile_texels) -> glm::vec3;
// Texel of a tile_texels^2 tile that contains the direction.
[[nodiscard]] auto get_direction_texel(const glm::vec3& direction, int tile_texels) -> glm::ivec2;

// Integrals over the footprints of octahedral texels, for the reduce pass
// (doc/editor/radiance_cascades.md "Reduce"). Each texel of a
// tile_texels^2 tile is split into get_octahedral_texel_subdivisions()^2
// equal squares of the [-1, 1]^2 octahedral parameter; each square contributes at
// its centre direction w with the solid angle dA * |w|_1^3 (the octahedral
// map's area element: the unit octahedron point w / |w|_1 lies at distance
// 1 / |w|_1 from the origin and its face is seen at the cosine
// 1 / (sqrt(3) |w|_1), which with the face / parameter area ratio sqrt(3)
// gives dA / |o|^3). The solid angles of a whole tile sum to 4 pi.
// Subdivisions per texel axis: about c_octahedral_integration_cells squares
// across the whole tile, at least 1 per texel.
constexpr int c_octahedral_integration_cells = 32;
[[nodiscard]] auto get_octahedral_texel_subdivisions(int tile_texels) -> int;

// Solid angle of every texel of a tile_texels^2 octahedral tile, at index
// v * tile_texels + u. Clears and fills out.
void compute_octahedral_texel_solid_angles(int tile_texels, std::vector<float>& out);

// Lobe weights of texel footprints: for each texel n of an
// output_texels^2 tile (direction get_texel_direction(n, output_texels))
// and each texel j of a tile_texels^2 tile, the integral over the footprint
// of j of pow(max(0, dot(direction_n, w)), exponent) dw, at index
// (n.y * output_texels + n.x) * tile_texels^2 + (j.y * tile_texels + j.x).
// exponent 1 gives the cosine weights of the irradiance convolution.
// Clears and fills out.
void compute_octahedral_lobe_weights(int output_texels, int tile_texels, float exponent, std::vector<float>& out);

} // namespace editor
