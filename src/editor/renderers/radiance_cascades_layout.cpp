#include "renderers/radiance_cascades_layout.hpp"

#include <algorithm>
#include <cmath>

namespace editor {

auto Radiance_cascade::get_probe_count() const -> int
{
    return grid.get_probe_count();
}

auto Radiance_cascade::get_atlas_width() const -> int
{
    return tiles_per_row * tile_texels;
}

auto Radiance_cascade::get_atlas_height() const -> int
{
    return tile_rows * tile_texels;
}

auto Radiance_cascade::get_texel_count() const -> int64_t
{
    return static_cast<int64_t>(get_probe_count()) * static_cast<int64_t>(tile_texels) * static_cast<int64_t>(tile_texels);
}

auto Radiance_cascade::get_atlas_texel_count() const -> int64_t
{
    return static_cast<int64_t>(get_atlas_width()) * static_cast<int64_t>(get_atlas_height());
}

auto Radiance_cascade::get_tile_origin(const int probe_index) const -> glm::ivec2
{
    return glm::ivec2{
        (probe_index % tiles_per_row) * tile_texels,
        (probe_index / tiles_per_row) * tile_texels
    };
}

auto Radiance_cascades_layout::is_valid() const -> bool
{
    return cascade_count > 0;
}

auto Radiance_cascades_layout::get_cascades() const -> std::span<const Radiance_cascade>
{
    return std::span<const Radiance_cascade>{cascades.data(), static_cast<std::size_t>(cascade_count)};
}

auto Radiance_cascades_layout::get_total_texels() const -> int64_t
{
    int64_t sum = 0;
    for (const Radiance_cascade& cascade : get_cascades()) {
        sum += cascade.get_texel_count();
    }
    return sum;
}

auto Radiance_cascades_layout::get_total_probes() const -> int64_t
{
    int64_t sum = 0;
    for (const Radiance_cascade& cascade : get_cascades()) {
        sum += cascade.get_probe_count();
    }
    return sum;
}

auto Radiance_cascades_layout::operator==(const Radiance_cascades_layout& other) const -> bool
{
    if ((cascade_count != other.cascade_count) || (r0 != other.r0)) {
        return false;
    }
    for (int i = 0; i < cascade_count; ++i) {
        const Radiance_cascade& a = cascades[static_cast<std::size_t>(i)];
        const Radiance_cascade& b = other.cascades[static_cast<std::size_t>(i)];
        if (
            (a.grid           != b.grid          ) ||
            (a.tile_texels    != b.tile_texels   ) ||
            (a.interval_start != b.interval_start) ||
            (a.interval_end   != b.interval_end  ) ||
            (a.tiles_per_row  != b.tiles_per_row ) ||
            (a.tile_rows      != b.tile_rows     )
        ) {
            return false;
        }
    }
    return true;
}

auto get_radiance_interval(const float r0, const int cascade) -> glm::vec2
{
    const float scale = static_cast<float>(1 << cascade); // 2^i
    return glm::vec2{
        r0 * (scale - 1.0f),
        r0 * ((2.0f * scale) - 1.0f)
    };
}

auto get_upper_grid(const Probe_grid& lower) -> Probe_grid
{
    Probe_grid upper{};
    upper.counts  = (lower.counts + glm::ivec3{1}) / 2;
    upper.spacing = 2.0f * lower.spacing;
    upper.origin  = lower.origin + (0.5f * lower.spacing);
    return upper;
}

namespace {

// Atlas tiling of one cascade: rows of probe tiles, close to square, with
// no side above max_texture_size. Returns false when even the widest
// allowed row leaves too many rows.
[[nodiscard]] auto place_tiles(Radiance_cascade& cascade, const int max_texture_size) -> bool
{
    const int probe_count     = cascade.get_probe_count();
    const int max_tiles_side  = max_texture_size / cascade.tile_texels;
    if ((probe_count <= 0) || (max_tiles_side <= 0)) {
        return false;
    }
    const int square_side = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(probe_count))));
    cascade.tiles_per_row = std::clamp(square_side, 1, max_tiles_side);
    cascade.tile_rows     = (probe_count + cascade.tiles_per_row - 1) / cascade.tiles_per_row;
    return cascade.tile_rows <= max_tiles_side;
}

} // anonymous namespace

auto fit_radiance_cascades(const erhe::math::Aabb& bounds, const Radiance_cascades_layout_settings& settings) -> Radiance_cascades_layout
{
    Radiance_cascades_layout layout{};
    if (!bounds.is_valid()) {
        return layout;
    }

    const int   max_cascades     = std::clamp(settings.max_cascades, 1, c_max_radiance_cascades);
    const int   q0               = std::max(1, settings.cascade0_tile_texels);
    const float interval_scale   = std::max(1.0f, settings.interval_scale);
    const int   max_texture_size = std::max(1, settings.max_texture_size);
    int         probe_budget     = std::max(8, settings.max_probes_cascade0);

    for (;;) {
        layout = Radiance_cascades_layout{};

        const Probe_grid grid0 = fit_probe_grid(bounds, settings.probe_spacing_m, probe_budget);
        if (!grid0.is_valid()) {
            return Radiance_cascades_layout{};
        }

        // r0 >= sqrt(3) * s0 keeps Chebyshev visibility valid against the
        // cascade 0 distances (doc/plans/radiance_cascades.md section 2).
        // The per-axis spacing can differ slightly; the largest one bounds
        // the cell diagonal.
        const float s0 = std::max(grid0.spacing.x, std::max(grid0.spacing.y, grid0.spacing.z));
        layout.r0 = interval_scale * std::sqrt(3.0f) * s0;

        bool fits = true;
        Probe_grid grid = grid0;
        for (int i = 0; i < max_cascades; ++i) {
            if (i > 0) {
                const Probe_grid& below = layout.cascades[static_cast<std::size_t>(i - 1)].grid;
                if (std::max(below.counts.x, std::max(below.counts.y, below.counts.z)) <= 2) {
                    break; // the cascade below is already the top cascade
                }
                grid = get_upper_grid(below);
            }
            Radiance_cascade& cascade = layout.cascades[static_cast<std::size_t>(i)];
            const glm::vec2 interval = get_radiance_interval(layout.r0, i);
            cascade.grid           = grid;
            cascade.tile_texels    = q0 << i;
            cascade.interval_start = interval.x;
            cascade.interval_end   = interval.y;
            if (!place_tiles(cascade, max_texture_size)) {
                fits = false;
                break;
            }
            layout.cascade_count = i + 1;
        }
        if (fits) {
            return layout;
        }
        if ((grid0.counts.x == 2) && (grid0.counts.y == 2) && (grid0.counts.z == 2)) {
            return Radiance_cascades_layout{}; // cannot get any coarser
        }
        // Lower the cascade 0 budget, which grows s0 and shrinks every
        // cascade's atlas; strictly decreasing, so this terminates.
        probe_budget = std::max(8, std::min(probe_budget - 1, (grid0.get_probe_count() * 4) / 5));
    }
}

auto get_upper_probe_axis(const int lower_index, const int upper_count) -> Upper_probe_axis
{
    // Lower probe k at upper grid coordinate k / 2 - 1 / 4:
    //   k = 2m     -> between m - 1 (weight 0.25) and m     (weight 0.75)
    //   k = 2m + 1 -> between m     (weight 0.75) and m + 1 (weight 0.25)
    Upper_probe_axis axis{};
    const int last = std::max(0, upper_count - 1);
    if ((lower_index % 2) == 0) {
        const int m = lower_index / 2;
        axis.index  = {std::clamp(m - 1, 0, last), std::clamp(m, 0, last)};
        axis.weight = {0.25f, 0.75f};
    } else {
        const int m = (lower_index - 1) / 2;
        axis.index  = {std::clamp(m, 0, last), std::clamp(m + 1, 0, last)};
        axis.weight = {0.75f, 0.25f};
    }
    return axis;
}

auto get_upper_probes(const glm::ivec3& lower_coords, const glm::ivec3& upper_counts) -> Upper_probes
{
    const Upper_probe_axis x = get_upper_probe_axis(lower_coords.x, upper_counts.x);
    const Upper_probe_axis y = get_upper_probe_axis(lower_coords.y, upper_counts.y);
    const Upper_probe_axis z = get_upper_probe_axis(lower_coords.z, upper_counts.z);
    Upper_probes probes{};
    std::size_t  n = 0;
    for (std::size_t k = 0; k < 2; ++k) {
        for (std::size_t j = 0; j < 2; ++j) {
            for (std::size_t i = 0; i < 2; ++i) {
                probes.coords [n] = glm::ivec3{x.index[i], y.index[j], z.index[k]};
                probes.weights[n] = x.weight[i] * y.weight[j] * z.weight[k];
                ++n;
            }
        }
    }
    return probes;
}

auto get_child_texels(const glm::ivec2& texel) -> std::array<glm::ivec2, 4>
{
    const glm::ivec2 base = 2 * texel;
    return {
        base + glm::ivec2{0, 0},
        base + glm::ivec2{1, 0},
        base + glm::ivec2{0, 1},
        base + glm::ivec2{1, 1}
    };
}

auto get_parent_texel(const glm::ivec2& child_texel) -> glm::ivec2
{
    return child_texel / 2;
}

auto octahedral_encode(const glm::vec3& direction) -> glm::vec2
{
    const glm::vec3 n = direction / (std::abs(direction.x) + std::abs(direction.y) + std::abs(direction.z));
    glm::vec2 f{n.x, n.y};
    if (n.z < 0.0f) {
        f = glm::vec2{
            (1.0f - std::abs(n.y)) * ((n.x >= 0.0f) ? 1.0f : -1.0f),
            (1.0f - std::abs(n.x)) * ((n.y >= 0.0f) ? 1.0f : -1.0f)
        };
    }
    return f;
}

auto octahedral_decode(const glm::vec2& f) -> glm::vec3
{
    glm::vec3   n{f.x, f.y, 1.0f - std::abs(f.x) - std::abs(f.y)};
    const float t = std::max(-n.z, 0.0f);
    n.x += (n.x > 0.0f) ? -t : t;
    n.y += (n.y > 0.0f) ? -t : t;
    return glm::normalize(n);
}

auto get_texel_direction(const glm::ivec2& texel, const int tile_texels) -> glm::vec3
{
    const glm::vec2 uv = (glm::vec2{texel} + glm::vec2{0.5f}) / static_cast<float>(tile_texels);
    return octahedral_decode((uv * 2.0f) - glm::vec2{1.0f});
}

auto get_direction_texel(const glm::vec3& direction, const int tile_texels) -> glm::ivec2
{
    const glm::vec2  uv    = (octahedral_encode(direction) * 0.5f) + glm::vec2{0.5f};
    const glm::ivec2 texel = glm::ivec2{glm::floor(uv * static_cast<float>(tile_texels))};
    return glm::clamp(texel, glm::ivec2{0}, glm::ivec2{tile_texels - 1});
}

auto get_octahedral_texel_subdivisions(const int tile_texels) -> int
{
    return std::max(1, c_octahedral_integration_cells / std::max(1, tile_texels));
}

namespace {

// Calls visit(texel_index, direction, solid_angle) for every subdivision
// square of every texel of a tile_texels^2 octahedral tile.
template <typename Visit>
void for_each_texel_subdivision(const int tile_texels, Visit&& visit)
{
    const int    s          = get_octahedral_texel_subdivisions(tile_texels);
    const int    cells      = tile_texels * s;
    const double cell_side  = 2.0 / static_cast<double>(cells);
    const double cell_area  = cell_side * cell_side;
    for (int v = 0; v < tile_texels; ++v) {
        for (int u = 0; u < tile_texels; ++u) {
            const int texel_index = (v * tile_texels) + u;
            for (int b = 0; b < s; ++b) {
                for (int a = 0; a < s; ++a) {
                    const glm::vec2 f{
                        static_cast<float>(-1.0 + ((static_cast<double>((u * s) + a) + 0.5) * cell_side)),
                        static_cast<float>(-1.0 + ((static_cast<double>((v * s) + b) + 0.5) * cell_side))
                    };
                    const glm::vec3 w      = octahedral_decode(f);
                    const double    l1     = static_cast<double>(std::abs(w.x) + std::abs(w.y) + std::abs(w.z));
                    visit(texel_index, w, cell_area * l1 * l1 * l1);
                }
            }
        }
    }
}

} // anonymous namespace

void compute_octahedral_texel_solid_angles(const int tile_texels, std::vector<float>& out)
{
    const std::size_t   texel_count = static_cast<std::size_t>(tile_texels) * static_cast<std::size_t>(tile_texels);
    std::vector<double> sums(texel_count, 0.0);
    for_each_texel_subdivision(tile_texels, [&](const int texel_index, const glm::vec3&, const double solid_angle) {
        sums[static_cast<std::size_t>(texel_index)] += solid_angle;
    });
    out.clear();
    out.reserve(texel_count);
    for (const double sum : sums) {
        out.push_back(static_cast<float>(sum));
    }
}

void compute_octahedral_lobe_weights(const int output_texels, const int tile_texels, const float exponent, std::vector<float>& out)
{
    const std::size_t   tile_count   = static_cast<std::size_t>(tile_texels) * static_cast<std::size_t>(tile_texels);
    const std::size_t   output_count = static_cast<std::size_t>(output_texels) * static_cast<std::size_t>(output_texels);
    std::vector<double> sums(output_count * tile_count, 0.0);
    std::vector<glm::vec3> output_directions;
    output_directions.reserve(output_count);
    for (int y = 0; y < output_texels; ++y) {
        for (int x = 0; x < output_texels; ++x) {
            output_directions.push_back(get_texel_direction(glm::ivec2{x, y}, output_texels));
        }
    }
    const double power = static_cast<double>(exponent);
    for_each_texel_subdivision(tile_texels, [&](const int texel_index, const glm::vec3& w, const double solid_angle) {
        for (std::size_t n = 0; n < output_count; ++n) {
            const double cos_angle = static_cast<double>(glm::dot(output_directions[n], w));
            if (cos_angle <= 0.0) {
                continue;
            }
            sums[(n * tile_count) + static_cast<std::size_t>(texel_index)] += std::pow(cos_angle, power) * solid_angle;
        }
    });
    out.clear();
    out.reserve(sums.size());
    for (const double sum : sums) {
        out.push_back(static_cast<float>(sum));
    }
}

} // namespace editor
