#pragma once

#include "erhe_math/aabb.hpp"

#include <glm/glm.hpp>

namespace editor {

// A regular probe grid: the shared grid fit of the indirect diffuse
// producers (doc/editor/ddgi.md "Data layout", doc/editor/radiance_cascades.md).
// probe_index = x + counts.x * (y + counts.y * z).
class Probe_grid
{
public:
    glm::vec3  origin {0.0f}; // world position of probe (0,0,0)
    glm::vec3  spacing{1.0f}; // world distance between adjacent probes, per axis
    glm::ivec3 counts {0};    // probes per axis

    [[nodiscard]] auto get_probe_count   () const -> int;
    // At least 2 probes on every axis: a cell to interpolate inside.
    [[nodiscard]] auto is_valid          () const -> bool;
    [[nodiscard]] auto get_probe_index   (const glm::ivec3& coords) const -> int;
    [[nodiscard]] auto get_probe_position(const glm::ivec3& coords) const -> glm::vec3;
    [[nodiscard]] auto operator==        (const Probe_grid& other) const -> bool;
    [[nodiscard]] auto operator!=        (const Probe_grid& other) const -> bool { return !(*this == other); }
};

// Fits a grid to the given box: probe counts from target_spacing, at least 2
// per axis, the spacing grown until the probe count fits max_probes. The
// spacing is the box extent divided by (counts - 1) per axis, so the first
// and last probe planes sit exactly on the box faces. Invalid (counts 0)
// for an invalid box.
[[nodiscard]] auto fit_probe_grid(const erhe::math::Aabb& bounds, float target_spacing, int max_probes) -> Probe_grid;

// Probe field atlas tiling, shared by the producers of the indirect diffuse
// probe field and res/shaders/erhe_ddgi_tiles.glsl (doc/editor/ddgi.md "Data
// layout"): probe (x, y, z) has the tile index
// x + counts.x * (z + counts.z * y), wrapped into rows of tiles_per_row
// tiles. tiles_per_row = counts.x * counts.z (one tile row per y layer) when
// both atlas sides fit max_texture_size with tiles of tile_texels, otherwise
// ceil(sqrt(probe count)), which keeps the atlas close to square.
[[nodiscard]] auto get_probe_field_tiles_per_row(const glm::ivec3& counts, int tile_texels, int max_texture_size) -> int;
[[nodiscard]] auto get_probe_field_tile_rows    (const glm::ivec3& counts, int tiles_per_row) -> int;
[[nodiscard]] auto get_probe_field_tile         (const glm::ivec3& coords, const glm::ivec3& counts, int tiles_per_row) -> glm::ivec2;
// The largest probe count whose atlas of tiles of tile_texels fits
// max_texture_size on both sides: the probe budget a fit must stay within.
[[nodiscard]] auto get_probe_field_max_probes   (int tile_texels, int max_texture_size) -> int;

// Why a probe volume is being refitted, in priority order: a fit setting
// changed (refit to the content exactly), the content moved out of or far
// inside the volume, or only a budget changed (keep the volume).
enum class Volume_refit_cause : unsigned int
{
    none     = 0,
    settings = 1,
    content  = 2,
    budget   = 3
};

// The padded box a probe volume was fitted to, and the rule that decides
// when the scene content moved enough to refit. Refitting on every content
// transform would reallocate the probe textures (and throw away their
// converged contents) every frame, so the volume is only refitted when the
// content leaves it or shrinks to less than half of it.
class Probe_volume_bounds
{
public:
    // True when content (the padded content box) left the volume, shrank to
    // less than half of it, or there is no volume yet.
    [[nodiscard]] auto content_changed(const erhe::math::Aabb& content) const -> bool;

    // The box to fit the grid to for the given cause. settings: the content
    // exactly. content: the content grown by the current volume when the
    // content only left it (a mesh oscillating across the boundary does not
    // retrigger a refit every other frame), the content itself when it
    // shrank. budget: the current volume.
    [[nodiscard]] auto get_fit_bounds(const erhe::math::Aabb& content, Volume_refit_cause cause) const -> erhe::math::Aabb;

    [[nodiscard]] auto get    () const -> const erhe::math::Aabb&;
    void               set    (const erhe::math::Aabb& bounds);
    void               reset  ();

private:
    [[nodiscard]] auto is_outside     (const erhe::math::Aabb& content) const -> bool;
    [[nodiscard]] auto is_much_smaller(const erhe::math::Aabb& content) const -> bool;

    erhe::math::Aabb m_bounds{};
};

} // namespace editor
