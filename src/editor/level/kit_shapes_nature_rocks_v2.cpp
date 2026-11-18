#include "level/kit_shapes.hpp"
#include "level/kit_mesh_builder.hpp"

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

// The recreated rocks (doc/editor/level_kit_shapes.md "Rock spire (F)").
// The mesh's topology follows from its integer values and integer draws alone; random floats only move vertices.

namespace editor {

namespace {

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

class Spire_form_v2
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

[[nodiscard]] auto get_spire_form(const Spire_kind kind) -> Spire_form_v2
{
    switch (kind) {
        case Spire_kind::stack:  return Spire_form_v2{spire_stack_foot,    {},               1.00f, 2, 0.16f, Spire_top::flat,  0.5f, spire_turf_slot, 1, 2};
        case Spire_kind::hoodoo: return Spire_form_v2{spire_hoodoo_foot,   spire_hoodoo_cap, 0.80f, 2, 0.12f, Spire_top::flat,  1.0f, spire_rock_slot, 2, 2};
        case Spire_kind::mesa:   return Spire_form_v2{spire_mesa_foot,     {},               1.00f, 2, 0.14f, Spire_top::flat,  0.0f, spire_turf_slot, 3, 3};
        case Spire_kind::pinnacle:
        default:                 return Spire_form_v2{spire_pinnacle_foot, {},               1.00f, 3, 0.22f, Spire_top::point, 1.0f, spire_rock_slot, 2, 3};
    }
}

// The shaft's radius along the strata, x from 0 at the foot's top to 1, as a share of the shaft's radius at x = 0.
[[nodiscard]] auto spire_profile_v2(const Spire_kind kind, const float x, const float taper) -> float
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
    const Spire_form_v2 form      = get_spire_form(kind);
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
            .t = foot_t + ((form.strata_end - foot_t) * x), .scale = shaft * spire_profile_v2(kind, x, taper), .cut = 0.0f,
            .lip_of = is_lip ? next_ledge : -1, .shelf_of = -1, .stratum = k, .band = band
        });
        if (is_lip) {
            const float middle = 0.5f * (x + along[static_cast<std::size_t>(k + 1)]);
            rows.push_back(Spire_row{
                .t = foot_t + ((form.strata_end - foot_t) * middle), .scale = shaft * spire_profile_v2(kind, middle, taper), .cut = 0.0f,
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

} // anonymous namespace

void add_nature_rocks_shapes_v2(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape = make_recreated_shape(shapes, "nature.rock_spire", generate_rock_spire, 440);
        shape.description =
            "Standing rock on a flat base at its origin, a weathered shaft over a flaring foot with fallen blocks about it: a "
            "pinnacle tapering to a leaning point, a sea stack over a wave-cut notch, a hoodoo whose narrowing column carries "
            "a wider cap rock, or a mesa of steep walls over a talus foot with a flat top. Strata of uneven thickness dip and "
            "are broken by faults from column to column, grooves run up the faces, and ledges set a part of the perimeter "
            "back; the ledge tops and a flat top are slot grass.";
        shape.parameters  = {
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
        shape.material_slots = {"rock", "grass"};
        shapes.push_back(std::move(shape));
    }
}

} // namespace editor
