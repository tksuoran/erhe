#include "level/kit_mesh_builder.hpp"
#include "level/kit_shapes.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <string_view>
#include <vector>

namespace editor {

namespace {

// The rock spire's topology follows from its integer values and integer draws alone; random floats only move vertices.

constexpr float    two_pi            = 2.0f * std::numbers::pi_v<float>;
constexpr uint32_t spire_rock_slot   = 0;
constexpr uint32_t spire_turf_slot   = 1; // ledge tops and a flat top
constexpr int      spire_strata_max  = 16;
constexpr int      spire_ledge_max   = 3;
constexpr int      spire_block_sides = 5;

[[nodiscard]] auto next_signed(Kit_random& random) -> float
{
    return random.next_range(-1.0f, 1.0f);
}

enum class Spire_kind : unsigned int {
    pinnacle = 0,
    stack,
    hoodoo,
    mesa
};

[[nodiscard]] auto get_spire_kind(const std::string_view form) -> Spire_kind
{
    if (form == "stack") {
        return Spire_kind::stack;
    }
    if (form == "hoodoo") {
        return Spire_kind::hoodoo;
    }
    if (form == "mesa") {
        return Spire_kind::mesa;
    }
    return Spire_kind::pinnacle;
}

// A row of the foot or of a cap rock: its height (share of the height), its radius and the share a notch cuts on its side.
class Spire_fixed_row
{
public:
    float t;
    float scale;
    float cut;
};

// Foot rows are shares of the base radius, the last one the shaft's; cap rows are shares of the shaft's top radius.
constexpr std::array<Spire_fixed_row, 2> spire_pinnacle_foot{{{0.00f, 1.00f, 0.0f}, {0.08f, 0.80f, 0.0f}}};
constexpr std::array<Spire_fixed_row, 4> spire_stack_foot   {{{0.00f, 1.00f, 0.0f}, {0.04f, 0.92f, 0.0f}, {0.11f, 0.84f, 0.24f}, {0.22f, 0.90f, 0.0f}}};
constexpr std::array<Spire_fixed_row, 2> spire_hoodoo_foot  {{{0.00f, 1.00f, 0.0f}, {0.08f, 0.76f, 0.0f}}};
constexpr std::array<Spire_fixed_row, 3> spire_mesa_foot    {{{0.00f, 1.00f, 0.0f}, {0.10f, 0.86f, 0.0f}, {0.22f, 0.74f, 0.0f}}};
constexpr std::array<Spire_fixed_row, 3> spire_hoodoo_cap   {{{0.835f, 1.55f, 0.0f}, {0.915f, 1.80f, 0.0f}, {0.975f, 1.30f, 0.0f}}};

enum class Spire_top : unsigned int {
    point = 0,
    flat
};

class Spire_form
{
public:
    std::span<const Spire_fixed_row> foot;
    std::span<const Spire_fixed_row> cap;
    float     strata_end;  // share of the height where the strata end
    int       ledges;
    float     ledge_depth; // share of the radius a ledge sets the face back
    Spire_top top;
    float     top_shift;   // share of the columns' vertical shift the top row takes
    uint32_t  top_slot;
    uint32_t  block_min;   // fallen blocks at the foot: block_min + [0, block_range)
    uint32_t  block_range;
};

[[nodiscard]] auto get_spire_form(const Spire_kind kind) -> Spire_form
{
    switch (kind) {
        case Spire_kind::stack:  return Spire_form{spire_stack_foot,    {},               1.00f, 2, 0.16f, Spire_top::flat,  0.5f, spire_turf_slot, 1, 2};
        case Spire_kind::hoodoo: return Spire_form{spire_hoodoo_foot,   spire_hoodoo_cap, 0.80f, 2, 0.12f, Spire_top::flat,  1.0f, spire_rock_slot, 2, 2};
        case Spire_kind::mesa:   return Spire_form{spire_mesa_foot,     {},               1.00f, 2, 0.14f, Spire_top::flat,  0.0f, spire_turf_slot, 3, 3};
        case Spire_kind::pinnacle:
        default:                 return Spire_form{spire_pinnacle_foot, {},               1.00f, 3, 0.22f, Spire_top::point, 1.0f, spire_rock_slot, 2, 3};
    }
}

// The shaft's radius along the strata, x from 0 at the foot's top to 1, as a share of the shaft's radius at x = 0.
[[nodiscard]] auto spire_profile(const Spire_kind kind, const float x, const float taper) -> float
{
    switch (kind) {
        case Spire_kind::stack: {
            return 1.0f - (0.35f * taper * x) + (0.05f * std::sin(std::numbers::pi_v<float> * x));
        }
        case Spire_kind::hoodoo: {
            // A column narrowing to a neck under its cap rock.
            const float column = 1.0f - ((0.36f + (0.2f * taper)) * x);
            const float neck   = std::max(0.0f, (x - 0.7f) / 0.3f);
            return column * (1.0f - (0.28f * neck * neck));
        }
        case Spire_kind::mesa: {
            return 1.0f - (0.12f * taper * x);
        }
        case Spire_kind::pinnacle:
        default: {
            const float top = 0.24f + (0.3f * (1.0f - taper));
            return 1.0f + ((top - 1.0f) * std::pow(x, 0.9f));
        }
    }
}

class Spire_row
{
public:
    float t;        // nominal height, share of the height
    float scale;    // radius before the column factors, share of the base radius
    float cut;
    int   lip_of;   // the ledge whose shelf starts at this row, or -1
    int   shelf_of; // the ledge whose shelf ends at this row, or -1
    int   stratum;  // 0 for a foot row, 1 .. strata up the shaft, -1 for a cap rock's row
    float band;     // the bed's own reach: hard beds stand out, soft ones are worn back
};

// The columns of a ledge: a window of the perimeter, its two end columns set back half as deep.
class Spire_ledge
{
public:
    uint32_t start {0};
    uint32_t length{0};

    [[nodiscard]] auto weight(const uint32_t column, const uint32_t column_count) const -> float
    {
        const uint32_t offset = (column + column_count - start) % column_count;
        if (offset >= length) {
            return 0.0f;
        }
        return ((offset == 0) || ((offset + 1) == length)) ? 0.5f : 1.0f;
    }
};

// A fallen block on the ground about the center: a five-sided stump with an uneven top.
void add_spire_block(Kit_mesh_builder& builder, Kit_random& random, const glm::vec3& center, const float size)
{
    const float     phase   = random.next_range(0.0f, two_pi);
    const float     shift_x = next_signed(random);
    const float     shift_z = next_signed(random);
    const glm::vec3 shift   = (0.15f * size) * glm::vec3{shift_x, 0.0f, shift_z};
    std::array<glm::vec3, 2 * spire_block_sides> points{};
    for (int r = 0; r < 2; ++r) {
        for (int i = 0; i < spire_block_sides; ++i) {
            const float angle_draw  = next_signed(random);
            const float radius_draw = next_signed(random);
            const float height_draw = random.next_range(0.45f, 0.85f);
            const float angle       = phase + (two_pi * (static_cast<float>(i) + (0.25f * angle_draw)) / static_cast<float>(spire_block_sides));
            const float radius      = size * ((r == 0) ? 0.6f : 0.42f) * (1.0f + (0.2f * radius_draw));
            const glm::vec3 point   = center + glm::vec3{radius * std::cos(angle), 0.0f, -radius * std::sin(angle)};
            points[static_cast<std::size_t>((r * spire_block_sides) + i)] = (r == 0) ? point : (point + shift + glm::vec3{0.0f, size * height_draw, 0.0f});
        }
    }
    builder.add_ring_solid(points, spire_block_sides, center, center + shift + glm::vec3{0.0f, 0.92f * size, 0.0f});
}

void generate_rock_spire(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Spire_kind    kind      = get_spire_kind(values.get_enum("form"));
    const Spire_form form      = get_spire_form(kind);
    const float         height    = values.get_float("height");
    const float         radius    = values.get_float("radius");
    const float         taper     = values.get_float("taper");
    const float         lean      = std::tan(glm::radians(values.get_float("lean")));
    const float         roughness = values.get_float("roughness");
    const int           strata    = std::clamp(values.get_int("strata"), 2, spire_strata_max);
    const uint32_t      n         = static_cast<uint32_t>(values.get_int("segments"));

    Kit_random random{values.get_seed(), "rock spire v2"};       // moves vertices
    Kit_random cells {values.get_seed(), "rock spire v2 cells"}; // decides the topology: integer draws only

    // The rows: the foot, the strata of uneven thickness with a ledge's shelf row after some, the cap rock.
    std::vector<Spire_row> rows;
    for (const Spire_fixed_row& row : form.foot) {
        rows.push_back(Spire_row{.t = row.t, .scale = row.scale, .cut = row.cut, .lip_of = -1, .shelf_of = -1, .stratum = 0, .band = 1.0f});
    }
    const float foot_t = form.foot.back().t;
    const float shaft  = form.foot.back().scale;
    std::array<float, spire_strata_max + 1> along{};
    float weight_sum = 0.0f;
    for (int k = 1; k <= strata; ++k) {
        const float draw = next_signed(random);
        weight_sum += 1.0f + (0.45f * roughness * draw);
        along[static_cast<std::size_t>(k)] = weight_sum;
    }
    for (int k = 1; k <= strata; ++k) {
        along[static_cast<std::size_t>(k)] /= weight_sum;
    }
    int next_ledge = 0;
    for (int k = 1; k <= strata; ++k) {
        const float x         = along[static_cast<std::size_t>(k)];
        const float band_draw = random.next_range(0.3f, 1.0f);
        const float band      = 1.0f + (0.09f * roughness * band_draw * (((k % 2) == 0) ? 1.0f : -1.0f));
        const bool  is_lip = (next_ledge < form.ledges) && (k < strata) && (k == (((next_ledge + 1) * strata) / (form.ledges + 1)));
        rows.push_back(Spire_row{
            .t = foot_t + ((form.strata_end - foot_t) * x), .scale = shaft * spire_profile(kind, x, taper), .cut = 0.0f,
            .lip_of = is_lip ? next_ledge : -1, .shelf_of = -1, .stratum = k, .band = band
        });
        if (is_lip) {
            const float middle = 0.5f * (x + along[static_cast<std::size_t>(k + 1)]);
            rows.push_back(Spire_row{
                .t = foot_t + ((form.strata_end - foot_t) * middle), .scale = shaft * spire_profile(kind, middle, taper), .cut = 0.0f,
                .lip_of = -1, .shelf_of = next_ledge, .stratum = k, .band = 1.0f
            });
            ++next_ledge;
        }
    }
    const float shaft_top = rows.back().scale;
    for (const Spire_fixed_row& row : form.cap) {
        rows.push_back(Spire_row{.t = row.t, .scale = shaft_top * row.scale, .cut = row.cut, .lip_of = -1, .shelf_of = -1, .stratum = -1, .band = 1.0f});
    }
    const std::size_t row_count = rows.size();

    // What a row's vertices may move up or down: a share of the gap to the nearer neighbour row, so rows never cross.
    std::vector<float> freedom(row_count, 0.0f);
    for (std::size_t r = 1; r < row_count; ++r) {
        const float below = (rows[r].t - rows[r - 1].t) * height;
        const float above = ((r + 1) < row_count) ? ((rows[r + 1].t - rows[r].t) * height) : below;
        freedom[r] = 0.35f * std::min(below, above) * (((r + 1) == row_count) ? form.top_shift : 1.0f);
    }

    // The columns: unequal angles, a lobed plan, grooves, and a vertical shift of dipping beds broken by faults.
    const float heading       = random.next_range(0.0f, two_pi);
    const float phase_2       = random.next_range(0.0f, two_pi);
    const float phase_3       = random.next_range(0.0f, two_pi);
    const float dip_heading   = random.next_range(0.0f, two_pi);
    const float notch_heading = random.next_range(0.0f, two_pi);
    const float kink_heading  = random.next_range(0.0f, two_pi);
    const float kink_draw     = random.next_range(0.4f, 1.0f);
    std::vector<float> angles(n);
    std::vector<float> plan  (n); // radius factor of the column at every row
    std::vector<float> bias  (n); // the column leans in or out with the height
    std::vector<float> shift (n); // in [-1, 1]
    std::vector<float> skirt (n); // the base ring's reach
    std::vector<int>   shoulder_at   (n); // above this stratum the column is a buttress's top, set back
    std::vector<float> shoulder_depth(n);
    float fault = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        const float    angle_draw  = next_signed(random);
        const float    flute_draw  = next_signed(random);
        const float    bias_draw   = next_signed(random);
        const float    fault_draw  = next_signed(random);
        const float    skirt_draw  = next_signed(random);
        const float    depth_draw  = random.next_range(0.3f, 1.0f);
        const uint32_t shoulder    = cells.next_u32() % static_cast<uint32_t>(strata - 1);
        const uint32_t groove_draw = cells.next_u32() % 3u;
        const uint32_t fault_new   = cells.next_u32() % 3u;
        const float    angle       = two_pi * (static_cast<float>(i) + (0.3f * roughness * angle_draw)) / static_cast<float>(n);
        const float    lobes       = 1.0f + (roughness * ((0.18f * std::sin((2.0f * angle) + phase_2)) + (0.10f * std::sin((3.0f * angle) + phase_3))));
        const float    flute       = (groove_draw == 0u) ? (1.0f - (0.34f * roughness)) : (1.0f + (0.12f * roughness * flute_draw));
        if ((i == 0) || (fault_new == 0u)) {
            fault = fault_draw;
        }
        angles[i] = angle;
        plan[i]   = lobes * flute;
        bias[i]   = 0.16f * roughness * bias_draw;
        shift[i]  = roughness * ((0.45f * std::sin(angle - dip_heading)) + (0.55f * fault));
        skirt[i]  = 1.0f + (0.2f * roughness * skirt_draw);
        shoulder_at[i]    = 1 + static_cast<int>(shoulder);
        shoulder_depth[i] = 0.26f * roughness * depth_draw;
    }
    std::array<Spire_ledge, spire_ledge_max> ledges{};
    for (int l = 0; l < form.ledges; ++l) {
        const uint32_t start_draw  = cells.next_u32();
        const uint32_t length_draw = cells.next_u32();
        ledges[static_cast<std::size_t>(l)].start  = start_draw % n;
        ledges[static_cast<std::size_t>(l)].length = 2u + (length_draw % (n / 3u));
    }

    const glm::vec2 lean_direction{std::cos(heading), -std::sin(heading)};
    const glm::vec2 kink = (0.35f * roughness * kink_draw * radius * shaft) * glm::vec2{std::cos(kink_heading), -std::sin(kink_heading)};
    const auto center_at = [&lean_direction, &kink, lean, height](const float y) -> glm::vec2 {
        const float t    = y / height;
        const float bend = std::clamp((t - 0.5f) / 0.5f, 0.0f, 1.0f);
        return ((lean * y * std::pow(t, 0.3f)) * lean_direction) + ((bend * bend) * kink);
    };

    Kit_mesh_builder builder;
    std::vector<float>     setback(n, 1.0f);
    std::vector<float>     lip_y  (n, 0.0f);
    std::vector<glm::vec3> ring   (n);
    uint32_t first = 0;
    for (std::size_t r = 0; r < row_count; ++r) {
        const Spire_row& row     = rows[r];
        const float      nominal = row.t * height;
        const float      x       = std::clamp((row.t - foot_t) / (1.0f - foot_t), 0.0f, 1.0f);
        for (uint32_t i = 0; i < n; ++i) {
            const float noise_draw = next_signed(random);
            float y     = (r == 0) ? 0.0f : (nominal + (freedom[r] * shift[i]));
            float reach = radius * row.scale * row.band * plan[i] * (1.0f + (bias[i] * (x - 0.4f)));
            if (r == 0) {
                reach *= skirt[i];
            } else {
                reach *= 1.0f + (0.10f * roughness * noise_draw);
            }
            if (row.stratum > shoulder_at[i]) {
                reach *= 1.0f - shoulder_depth[i];
            }
            if (row.shelf_of >= 0) {
                // In the ledge's window the shelf rises a little from the lip row; the face above stays set back.
                const float weight = ledges[static_cast<std::size_t>(row.shelf_of)].weight(i, n);
                const float rise   = 0.25f * (nominal - (rows[r - 1].t * height));
                setback[i] *= 1.0f - (form.ledge_depth * weight);
                y = y + (weight * ((lip_y[i] + rise) - y));
            }
            if (row.stratum >= 0) {
                reach *= setback[i]; // a cap rock overhangs its neck all around
            }
            if (row.lip_of >= 0) {
                reach *= 1.0f + (0.05f * ledges[static_cast<std::size_t>(row.lip_of)].weight(i, n));
                lip_y[i] = y;
            }
            reach *= 1.0f - (row.cut * (0.15f + (0.85f * std::max(0.0f, std::cos(angles[i] - notch_heading)))));
            const glm::vec2 center = center_at(y);
            ring[i] = glm::vec3{center.x + (reach * std::cos(angles[i])), y, center.y - (reach * std::sin(angles[i]))};
        }
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t v = builder.add_vertex(ring[i]);
            if ((r == 0) && (i == 0)) {
                first = v;
            }
        }
    }

    // The base is a fan about a vertex on the origin.
    builder.set_slot(spire_rock_slot);
    const uint32_t origin = builder.add_vertex(glm::vec3{0.0f});
    for (uint32_t i = 0; i < n; ++i) {
        builder.add_triangle(origin, first + ((i + 1) % n), first + i);
    }
    // The cells: two triangles each, the diagonal by an integer draw; a ledge's shelf cells are turf.
    for (std::size_t r = 0; (r + 1) < row_count; ++r) {
        const uint32_t lower  = first + (static_cast<uint32_t>(r) * n);
        const uint32_t upper  = lower + n;
        const int      lip_of = rows[r].lip_of;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t j        = (i + 1) % n;
            const uint32_t diagonal = cells.next_u32() % 2u;
            const bool     turf     = (lip_of >= 0) &&
                (ledges[static_cast<std::size_t>(lip_of)].weight(i, n) > 0.0f) && (ledges[static_cast<std::size_t>(lip_of)].weight(j, n) > 0.0f);
            builder.set_slot(turf ? spire_turf_slot : spire_rock_slot);
            if (diagonal == 0u) {
                builder.add_triangle(lower + i, lower + j, upper + j);
                builder.add_triangle(lower + i, upper + j, upper + i);
            } else {
                builder.add_triangle(lower + i, lower + j, upper + i);
                builder.add_triangle(lower + j, upper + j, upper + i);
            }
        }
    }
    // The top: a point off the last ring's center, or a slightly domed flat.
    const std::size_t last_row   = row_count - 1;
    const uint32_t    last       = first + (static_cast<uint32_t>(last_row) * n);
    const float       top_radius = radius * rows[last_row].scale;
    const float       top_y      = (rows[last_row].t * height) + freedom[last_row];
    const float       apex_x     = next_signed(random);
    const float       apex_z     = next_signed(random);
    const bool        point      = (form.top == Spire_top::point);
    const float       apex_y     = top_y + (point ? std::min((1.1f * top_radius) + (0.04f * radius), 0.25f * height) : std::min(0.05f * top_radius, 0.02f * height));
    const glm::vec2   apex_shift = (point ? (0.45f * top_radius * roughness) : 0.0f) * glm::vec2{apex_x, apex_z};
    const glm::vec2   apex_at    = center_at(apex_y) + apex_shift;
    builder.set_slot(form.top_slot);
    const uint32_t apex = builder.add_vertex(glm::vec3{apex_at.x, apex_y, apex_at.y});
    for (uint32_t i = 0; i < n; ++i) {
        builder.add_triangle(last + i, last + ((i + 1) % n), apex);
    }

    // Fallen blocks half sunk in the foot.
    builder.set_slot(spire_rock_slot);
    const uint32_t block_count = form.block_min + (cells.next_u32() % form.block_range);
    for (uint32_t b = 0; b < block_count; ++b) {
        const float angle = random.next_range(0.0f, two_pi);
        const float reach = random.next_range(0.92f, 1.22f);
        const float size  = random.next_range(0.20f, 0.38f);
        add_spire_block(builder, random, (radius * reach) * glm::vec3{std::cos(angle), 0.0f, -std::sin(angle)}, radius * size);
    }
    builder.write(mesh);
}

class Basalt_cell
{
public:
    int       q;
    int       r;
    glm::vec2 center;
};

constexpr int basalt_max_columns = 72; // 20 triangles each: within the kit's ceiling at every value

void basalt_cells(const float radius, const float spacing, std::vector<Basalt_cell>& out_cells)
{
    out_cells.clear();
    const float row   = spacing * (std::sqrt(3.0f) / 2.0f);
    const float limit = std::max(0.0f, radius - (0.5f * spacing));
    const int   reach = static_cast<int>(std::ceil(radius / row)) + 1;
    for (int r = -reach; r <= reach; ++r) {
        for (int q = -2 * reach; q <= 2 * reach; ++q) {
            const glm::vec2 center{spacing * (static_cast<float>(q) + (0.5f * static_cast<float>(r))), row * static_cast<float>(r)};
            if ((glm::length(center) <= limit) || ((q == 0) && (r == 0))) {
                out_cells.push_back(Basalt_cell{.q = q, .r = r, .center = center});
            }
        }
    }
}

// The lattice spacing: the column width, widened until the columns are at most basalt_max_columns.
[[nodiscard]] auto basalt_spacing(const float radius, const float column_width, std::vector<Basalt_cell>& out_cells) -> float
{
    float spacing = column_width;
    basalt_cells(radius, spacing, out_cells);
    while (out_cells.size() > static_cast<std::size_t>(basalt_max_columns)) {
        spacing *= 1.05f;
        basalt_cells(radius, spacing, out_cells);
    }
    return spacing;
}

void generate_basalt_columns(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float radius = values.get_float("radius");
    const float height = values.get_float("height");
    const float spread = values.get_float("height_spread");
    const float step   = values.get_float("step");
    std::vector<Basalt_cell> cells;
    const float spacing = basalt_spacing(radius, values.get_float("column_width"), cells);

    // Heights: whole steps down from the height, a smooth field rising toward a seeded side and toward the middle with a
    // little noise, then at most one step between neighbours: terraces.
    Kit_random random{values.get_seed(), "basalt columns"};
    const float     heading = random.next_range(0.0f, 2.0f * std::numbers::pi_v<float>);
    const glm::vec2 rise_direction{std::cos(heading), -std::sin(heading)};
    const float     wave_phase = random.next_range(0.0f, 2.0f * std::numbers::pi_v<float>);
    const int       max_steps  = static_cast<int>(std::floor(((height * spread) / step) + 1.0e-4f));
    const float     extent     = std::max(radius, 1.0e-3f);
    std::vector<int> steps(cells.size());
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const glm::vec2 p      = cells[i].center / extent;
        const float     side   = 0.5f + (0.5f * glm::dot(p, rise_direction));
        const float     middle = 1.0f - glm::dot(p, p);
        const float     wave   = 0.5f + (0.5f * std::sin((3.0f * glm::dot(p, glm::vec2{-rise_direction.y, rise_direction.x})) + wave_phase));
        const float     level  = (0.45f * side) + (0.35f * middle) + (0.2f * wave) + random.next_range(-0.08f, 0.08f);
        steps[i] = static_cast<int>(std::lround(static_cast<float>(max_steps) * std::clamp(1.0f - level, 0.0f, 1.0f)));
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < cells.size(); ++i) {
            for (std::size_t j = 0; j < cells.size(); ++j) {
                const int dq = cells[j].q - cells[i].q;
                const int dr = cells[j].r - cells[i].r;
                const bool neighbour = ((dr == 0) && (std::abs(dq) == 1)) || ((dr == 1) && ((dq == 0) || (dq == -1))) || ((dr == -1) && ((dq == 0) || (dq == 1)));
                if (neighbour && (steps[i] > steps[j] + 1)) {
                    steps[i] = steps[j] + 1;
                    changed  = true;
                }
            }
        }
    }

    Kit_mesh_builder builder;
    const float pi = std::numbers::pi_v<float>;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        // Joints: each column stands 2 - 4 cm in from its cell, so neighbours share no vertex.
        const float joint     = 0.02f + (0.03f * random.next_unit());
        const float corner    = ((0.5f * spacing) - joint) * (2.0f / std::sqrt(3.0f));
        const float top       = height - (static_cast<float>(steps[i]) * step);
        const glm::vec2 c     = cells[i].center;
        std::array<uint32_t, 6> bottom{};
        std::array<uint32_t, 6> upper{};
        for (int k = 0; k < 6; ++k) {
            const float angle = (pi / 6.0f) + (static_cast<float>(k) * (pi / 3.0f));
            const float x     = c.x + (corner * std::cos(angle));
            const float z     = c.y - (corner * std::sin(angle));
            bottom[static_cast<std::size_t>(k)] = builder.add_vertex(glm::vec3{x, 0.0f, z});
            upper [static_cast<std::size_t>(k)] = builder.add_vertex(glm::vec3{x, top, z});
        }
        const std::array<uint32_t, 6> bottom_cap{bottom[5], bottom[4], bottom[3], bottom[2], bottom[1], bottom[0]};
        builder.add_polygon(upper);
        builder.add_polygon(bottom_cap);
        for (std::size_t k = 0; k < 6; ++k) {
            const std::size_t l = (k + 1) % 6;
            builder.add_quad(bottom[k], bottom[l], upper[l], upper[k]);
        }
    }
    builder.write(mesh);
}

} // anonymous namespace

void add_nature_rock_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape;
        shape.id              = "nature.rock_spire";
        shape.former_ids      = {"nature.rock_spire.v2"};
        shape.kit             = "nature";
        shape.display_name    = "Rock spire";
        shape.description     =
            "Standing rock on a flat base at its origin, a weathered shaft over a flaring foot with fallen blocks about it: a "
            "pinnacle tapering to a leaning point, a sea stack over a wave-cut notch, a hoodoo whose narrowing column carries "
            "a wider cap rock, or a mesa of steep walls over a talus foot with a flat top. Strata of uneven thickness dip and "
            "are broken by faults from column to column, grooves run up the faces, and ledges set a part of the perimeter "
            "back; the ledge tops and a flat top are slot grass.";
        shape.tags            = {"obstacle", "rock", "coast", "size:2x2x6"};
        shape.parameters      = {
            derive_enum(make_enum_parameter("form", "Pinnacle, sea stack, hoodoo or mesa", {"pinnacle", "stack", "hoodoo", "mesa"}, 0)),
            from_seed_by_default(derive_float(make_length_parameter("height", "Height of the top", 2.0f, Kit_default_scale::wall_heights, 0.5f, 60.0f), 1.0f, 6.0f, 0.1f)),
            from_seed_by_default(derive_float(make_length_parameter("radius", "Radius of the base", 1.0f, Kit_default_scale::modules, 0.2f, 32.0f), 0.5f, 4.0f, 0.1f)),
            derive_float(make_ratio_parameter("taper", "How much narrower the top is", 0.5f), 0.2f, 0.8f, 0.05f),
            from_seed_by_default(derive_float(make_angle_parameter("lean", "Lean of the rock from the vertical", 3.0f, 0.0f, 15.0f), 0.0f, 8.0f, 0.5f)),
            derive_float(make_ratio_parameter("roughness", "Amount of lobes, grooves, dip, faults and lean of the top", 0.6f), 0.3f, 0.9f, 0.05f),
            derive_int(make_count_parameter("strata", "Bands of rock between the foot and the top", 9, 4, spire_strata_max), 7, 11),
            make_seed_parameter(),
            make_count_parameter("segments", "Columns around the axis", 12, 6, 20)
        };
        shape.material_slots  = {"rock", "grass"};
        shape.normal_style    = Kit_normal_style::smooth; // when the shading parameter is smooth
        shape.default_shading = Kit_shading::faceted;
        shape.triangle_budget = 440;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.randomness      = Kit_randomness::seeded;
        shape.detail_counts   = {{"segments", 1, 2}};
        shape.generator       = generate_rock_spire;
        shape.get_connectors  = [](const Kit_parameter_values&, std::vector<Kit_connector>& out_connectors) {
            out_connectors.push_back(Kit_connector{.name = "base", .position = glm::vec3{0.0f}, .outward = glm::vec3{0.0f, -1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.basalt_columns";
        shape.kit             = "nature";
        shape.display_name    = "Basalt columns";
        shape.description     =
            "Cluster of hexagonal basalt columns in a round footprint at its origin, their flat tops whole steps below the "
            "height, rising toward a seeded side; neighbouring tops differ by at most one step, so at the agent's climb the "
            "tops are stairs.";
        shape.tags            = {"walkable", "obstacle", "rock", "coast", "size:3x3x3"};
        Kit_parameter spread = derive_float(make_ratio_parameter("height_spread", "Share of the height the lowest columns stand below the highest", 0.35f), 0.1f, 0.6f, 0.05f);
        spread.float_max = 0.9f;
        shape.parameters      = {
            from_seed_by_default(derive_float(make_length_parameter("radius", "Radius of the footprint", 1.5f, Kit_default_scale::modules, 0.25f, 16.0f), 1.0f, 3.0f, 0.25f)),
            derive_float(make_length_parameter("column_width", "Width of a column across its flats", 0.5f, Kit_default_scale::modules, 0.15f, 4.0f), 0.4f, 0.8f, 0.05f),
            derive_float(make_length_parameter("height", "Height of the highest column", 1.0f, Kit_default_scale::wall_heights, 0.2f, 32.0f), 0.5f, 1.5f, 0.05f),
            from_seed_by_default(spread),
            make_length_parameter("step", "Height quantum of the tops", 0.3f, Kit_default_scale::meters, 0.05f, 2.0f),
            make_seed_parameter()
        };
        shape.material_slots  = {"rock"};
        shape.normal_style    = Kit_normal_style::smooth; // when the shading parameter is smooth
        shape.default_shading = Kit_shading::faceted;
        shape.triangle_budget = 760;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_basalt_columns;
        shape.check_walkability = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            if (values.get_float("step") > agent.max_climb) {
                return fmt::format("{}: step {} m is higher than the agent's climb {} m", values.get_shape()->id, values.get_float("step"), agent.max_climb);
            }
            return {};
        };
        shape.get_connectors  = [](const Kit_parameter_values&, std::vector<Kit_connector>& out_connectors) {
            out_connectors.push_back(Kit_connector{.name = "base", .position = glm::vec3{0.0f}, .outward = glm::vec3{0.0f, -1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
        };
        shapes.push_back(std::move(shape));
    }
}

} // namespace editor
