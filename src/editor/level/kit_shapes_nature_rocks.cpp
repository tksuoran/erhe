#include "level/kit_mesh_builder.hpp"
#include "level/kit_shapes.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string_view>
#include <vector>

namespace editor {

namespace {

constexpr float spire_ring_spacing = 0.6f;  // base rings about this far apart
constexpr int   spire_max_rings    = 30;    // with the segments' maximum the spire stays within the kit's ceiling
constexpr float spire_shelf_rise   = 0.08f; // height of a ledge's or a band's shelf edge

// What a spire's form sets (doc/editor/level_kit_shapes.md "Rock spire").
enum class Spire_form : unsigned int {
    pinnacle = 0,
    stack,
    hoodoo,
    mesa
};

[[nodiscard]] auto get_spire_form(const std::string_view form) -> Spire_form
{
    if (form == "stack") {
        return Spire_form::stack;
    }
    if (form == "hoodoo") {
        return Spire_form::hoodoo;
    }
    if (form == "mesa") {
        return Spire_form::mesa;
    }
    return Spire_form::pinnacle;
}

// One ring of the spire before jitter: its height, its radius per meter of the radius parameter, the shelves below
// it, and whether it is a shelf's or a cap's ring (no height jitter).
class Spire_ring
{
public:
    float y;
    float scale;
    int   setbacks;
    bool  fixed;
};

// The radius profile per form at t = y / height, for a taper of 0 - 1.
[[nodiscard]] auto spire_profile(const Spire_form form, const float t, const float taper) -> float
{
    switch (form) {
        case Spire_form::stack: {
            return 1.0f - (0.35f * taper * t);
        }
        case Spire_form::hoodoo: {
            // A column narrowing to a neck under a wider cap rock.
            if (t > 0.86f) {
                return 0.68f - (0.12f * taper);
            }
            const float column = 1.0f - ((0.45f + (0.2f * taper)) * t);
            const float neck   = std::max(0.0f, (t - 0.65f) / 0.21f);
            return column * (1.0f - (0.35f * neck * neck));
        }
        case Spire_form::mesa: {
            // A short talus slope, then steep walls to a flat top.
            const float talus = std::max(0.0f, 1.0f - (t / 0.2f));
            return (0.8f - (0.1f * taper * t)) + (0.2f * talus * talus);
        }
        case Spire_form::pinnacle:
        default: {
            const float top = 0.14f + (0.3f * (1.0f - taper));
            return 1.0f + ((top - 1.0f) * std::pow(t, 0.8f));
        }
    }
}

// The shelves of a form: their heights (shares of the height) and how deep each sets the face back on its deep side.
class Spire_shelves
{
public:
    std::vector<float> heights;
    float              depth;
    bool               cap_rock; // the last shelf is a hoodoo's cap rock's underside
};

[[nodiscard]] auto get_spire_shelves(const Spire_form form) -> Spire_shelves
{
    switch (form) {
        case Spire_form::stack:  return Spire_shelves{{0.45f, 0.75f}, 0.14f, false};
        case Spire_form::hoodoo: return Spire_shelves{{0.25f, 0.45f, 0.86f}, 0.1f, true};
        case Spire_form::mesa:   return Spire_shelves{{0.5f, 0.8f}, 0.12f, false};
        case Spire_form::pinnacle:
        default:                 return Spire_shelves{{0.3f, 0.55f, 0.78f}, 0.22f, false};
    }
}

[[nodiscard]] auto make_spire_rings(const Kit_parameter_values& values) -> std::vector<Spire_ring>
{
    const Spire_form    form    = get_spire_form(values.get_enum("form"));
    const float         height  = values.get_float("height");
    const float         taper   = values.get_float("taper");
    const Spire_shelves shelves = get_spire_shelves(form);
    const int base_rings = std::clamp(static_cast<int>(std::lround(height / spire_ring_spacing)), 3, spire_max_rings - 1 - (2 * static_cast<int>(shelves.heights.size())));
    const float cell = height / static_cast<float>(base_rings);
    const float rise = std::min(spire_shelf_rise, 0.3f * cell);
    std::vector<Spire_ring> rings;
    std::size_t next_shelf = 0;
    int         setbacks   = 0;
    for (int j = 0; j <= base_rings; ++j) {
        const float t = static_cast<float>(j) / static_cast<float>(base_rings);
        rings.push_back(Spire_ring{.y = t * height, .scale = spire_profile(form, t, taper), .setbacks = setbacks, .fixed = (j == 0) || (j == base_rings)});
        // A shelf between this row and the next: its edge, then the face above it set back on its deep side.
        if ((j < base_rings) && (next_shelf < shelves.heights.size())) {
            const float shelf_t = shelves.heights[next_shelf];
            const float next_t  = static_cast<float>(j + 1) / static_cast<float>(base_rings);
            if ((shelf_t >= t) && (shelf_t < next_t)) {
                const float y = std::clamp(shelf_t * height, (t * height) + rise, (next_t * height) - (2.0f * rise));
                const float s = spire_profile(form, y / height, taper);
                if (shelves.cap_rock && (next_shelf + 1 == shelves.heights.size())) {
                    // A hoodoo's cap rock overhangs its neck.
                    rings.push_back(Spire_ring{.y = y,        .scale = 0.65f * s, .setbacks = setbacks, .fixed = true});
                    rings.push_back(Spire_ring{.y = y + rise, .scale = 1.6f * s,  .setbacks = setbacks, .fixed = true});
                } else {
                    rings.push_back(Spire_ring{.y = y, .scale = s, .setbacks = setbacks, .fixed = true});
                    ++setbacks;
                    rings.push_back(Spire_ring{.y = y + rise, .scale = s, .setbacks = setbacks, .fixed = true});
                }
                ++next_shelf;
            }
        }
    }
    return rings;
}

void generate_rock_spire(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Spire_form form      = get_spire_form(values.get_enum("form"));
    const float      height    = values.get_float("height");
    const float      radius    = values.get_float("radius");
    const float      lean      = std::tan(glm::radians(values.get_float("lean")));
    const float      roughness = values.get_float("roughness");
    const int        segments  = values.get_int("segments");
    const std::vector<Spire_ring> rings   = make_spire_rings(values);
    const Spire_shelves           shelves = get_spire_shelves(form);
    // Height jitter stays well inside the gaps between rings, shelves included.
    const float cell   = height / static_cast<float>(rings.size() - 1);
    const float jitter = roughness * std::min(0.12f * cell, 0.4f * std::min(spire_shelf_rise, 0.3f * cell));

    Kit_random random{values.get_seed(), "rock spire"};
    const float pi      = std::numbers::pi_v<float>;
    const float heading = random.next_range(0.0f, 2.0f * pi);
    const glm::vec2 lean_direction{std::cos(heading), -std::sin(heading)};
    // Lobes shared by every ring: the plan is no circle.
    const float phase_2 = random.next_range(0.0f, 2.0f * pi);
    const float phase_3 = random.next_range(0.0f, 2.0f * pi);
    std::vector<float> angles       (static_cast<std::size_t>(segments));
    std::vector<float> angle_factors(static_cast<std::size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        // Unequal sides: each angle shifts by up to a third of a side.
        const float angle = 2.0f * pi * (static_cast<float>(i) + (roughness * random.next_range(-0.33f, 0.33f))) / static_cast<float>(segments);
        angles       [static_cast<std::size_t>(i)] = angle;
        angle_factors[static_cast<std::size_t>(i)] = 1.0f + (roughness * ((0.2f * std::sin((2.0f * angle) + phase_2)) + (0.12f * std::sin((3.0f * angle) + phase_3))));
    }
    // Ribs and gullies winding up the rock: waves over the angle and the height.
    constexpr std::array<float, 4> wave_amplitudes{0.26f, 0.18f, 0.12f, 0.08f};
    constexpr std::array<float, 4> wave_orders    {1.0f, 2.0f, 4.0f, 7.0f};
    std::array<float, 4> wave_heights{};
    std::array<float, 4> wave_phases {};
    for (std::size_t k = 0; k < wave_heights.size(); ++k) {
        wave_heights[k] = random.next_range(-1.5f, 1.5f) / std::max(0.5f, radius);
        wave_phases [k] = random.next_range(0.0f, 2.0f * pi);
    }
    // Each shelf's deep side, and a stack's wave-cut notch.
    std::vector<float> shelf_headings(shelves.heights.size());
    for (float& shelf_heading : shelf_headings) {
        shelf_heading = random.next_range(0.0f, 2.0f * pi);
    }
    const float notch_heading = random.next_range(0.0f, 2.0f * pi);
    // The bedding dips: every ring rises on one side and falls on the other by the same amount, eased in above the base.
    const float dip_heading = random.next_range(0.0f, 2.0f * pi);
    const float dip         = std::min(0.25f * radius * roughness, 0.6f * cell);

    std::vector<glm::vec3> points;
    points.reserve(rings.size() * static_cast<std::size_t>(segments));
    glm::vec2 wander{0.0f};
    glm::vec2 top_center{0.0f};
    float     top_y = 0.0f;
    for (std::size_t r = 0; r < rings.size(); ++r) {
        const Spire_ring& ring = rings[r];
        const float t          = ring.y / height;
        // Strata: each ring bulges in or out a little; the center wanders and leans with the height.
        const float band = (r == 0) ? 1.0f : (1.0f + (roughness * random.next_range(-0.05f, 0.05f)));
        if (r > 0) {
            wander += (roughness * 0.05f * radius) * glm::vec2{random.next_range(-1.0f, 1.0f), random.next_range(-1.0f, 1.0f)};
        }
        const glm::vec2 center = (r == 0) ? glm::vec2{0.0f} : (wander + ((lean * ring.y * std::pow(t, 0.3f)) * lean_direction));
        const float notch = (form == Spire_form::stack) ? (0.22f * std::exp(-((t - 0.1f) * (t - 0.1f)) / 0.004f)) : 0.0f;
        for (int i = 0; i < segments; ++i) {
            const float angle = angles[static_cast<std::size_t>(i)];
            float wave = 0.0f;
            for (std::size_t k = 0; k < wave_heights.size(); ++k) {
                wave += wave_amplitudes[k] * std::sin((wave_orders[k] * angle) + (wave_heights[k] * ring.y) + wave_phases[k]);
            }
            float setback = 1.0f;
            for (int k = 0; k < ring.setbacks; ++k) {
                setback *= 1.0f - (shelves.depth * std::max(0.0f, std::cos(angle - shelf_headings[static_cast<std::size_t>(k)])));
            }
            const float cut    = 1.0f - (notch * (0.3f + (0.7f * std::max(0.0f, std::cos(angle - notch_heading)))));
            const float factor = angle_factors[static_cast<std::size_t>(i)] * band * setback * cut *
                                 (1.0f + (roughness * (wave + random.next_range(-0.08f, 0.08f))));
            const float r_i    = radius * ring.scale * factor;
            const float bedding = dip * std::min(1.0f, ring.y / cell) * std::sin(angle - dip_heading);
            const float dy      = bedding + (ring.fixed ? 0.0f : (jitter * random.next_range(-1.0f, 1.0f)));
            points.emplace_back(center.x + (r_i * std::cos(angle)), ring.y + dy, center.y - (r_i * std::sin(angle)));
        }
        top_center = center;
        top_y      = ring.y;
    }
    // A pinnacle ends in a point above its last ring, off its center; the others in a flat, slightly domed top.
    const float     top_radius = radius * rings.back().scale;
    const float     apex       = (form == Spire_form::pinnacle) ? (top_y + (0.5f * top_radius) + (0.05f * radius)) : (top_y + (0.04f * top_radius));
    const glm::vec2 apex_shift = (form == Spire_form::pinnacle) ? ((0.3f * top_radius * roughness) * glm::vec2{random.next_range(-1.0f, 1.0f), random.next_range(-1.0f, 1.0f)}) : glm::vec2{0.0f};
    Kit_mesh_builder builder;
    builder.add_ring_solid(points, segments, glm::vec3{0.0f}, glm::vec3{top_center.x + apex_shift.x, apex, top_center.y + apex_shift.y});
    builder.write(mesh);
}

// Hexagonal lattice cells (q, r) of the basalt columns: center spacing s along X, and rows half a cell shifted.
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
        shape.kit             = "nature";
        shape.display_name    = "Rock spire";
        shape.description     =
            "Standing rock on a flat base at its origin: a pinnacle tapering to a point, a sea stack with near vertical sides "
            "and a wave-cut notch, a hoodoo whose narrowing column carries a wider cap rock, or a mesa of steep walls over a "
            "talus foot with a flat top; lobed, banded and stepped by ledges, leaning toward a seeded heading.";
        shape.tags            = {"obstacle", "rock", "coast", "size:2x2x6"};
        shape.parameters      = {
            derive_enum(make_enum_parameter("form", "Pinnacle, sea stack, hoodoo or mesa", {"pinnacle", "stack", "hoodoo", "mesa"}, 0)),
            from_seed_by_default(derive_float(make_length_parameter("height", "Height of the top", 2.0f, Kit_default_scale::wall_heights, 0.5f, 60.0f), 1.0f, 6.0f, 0.1f)),
            from_seed_by_default(derive_float(make_length_parameter("radius", "Radius of the base", 1.0f, Kit_default_scale::modules, 0.2f, 32.0f), 0.5f, 4.0f, 0.1f)),
            derive_float(make_ratio_parameter("taper", "How much narrower the top is", 0.5f), 0.2f, 0.8f, 0.05f),
            from_seed_by_default(derive_float(make_angle_parameter("lean", "Lean of the rock from the vertical", 3.0f, 0.0f, 15.0f), 0.0f, 8.0f, 0.5f)),
            derive_float(make_ratio_parameter("roughness", "Amount of lobes, ribs, dip and jitter", 0.6f), 0.3f, 0.9f, 0.05f),
            make_seed_parameter(),
            make_count_parameter("segments", "Sides around the axis", 12, 6, 20)
        };
        shape.material_slots  = {"rock"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 760;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.randomness      = Kit_randomness::seeded;
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
        shape.normal_style    = Kit_normal_style::faceted;
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
