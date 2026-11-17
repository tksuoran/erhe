#include "level/kit_shapes.hpp"
#include "level/kit_light.hpp"
#include "level/kit_mesh_builder.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The fourth eight shapes of the build order of doc/plans/level_kit_extra_shapes.md (doc/editor/level_tools.md).
// A mesh's topology follows from its integer values; floats and the seed only move vertices.

namespace editor {

namespace {

constexpr float pi     = std::numbers::pi_v<float>;
constexpr float two_pi = 2.0f * pi;
constexpr glm::vec3 x_axis{1.0f, 0.0f, 0.0f};
constexpr glm::vec3 y_axis{0.0f, 1.0f, 0.0f};
constexpr glm::vec3 z_axis{0.0f, 0.0f, 1.0f};

[[nodiscard]] auto next_signed(Kit_random& random) -> float
{
    return random.next_range(-1.0f, 1.0f);
}

// Unit vector at the angle around +Y, from +X toward -Z.
[[nodiscard]] auto around_y(const float angle) -> glm::vec3
{
    return glm::vec3{std::cos(angle), 0.0f, -std::sin(angle)};
}

// Where a part stands: a local point's place is origin + x * p.x + y * p.y + z * p.z; (x, y, z) is right-handed.
class Placement
{
public:
    [[nodiscard]] auto at(const glm::vec3& p) const -> glm::vec3
    {
        return origin + (p.x * x) + (p.y * y) + (p.z * z);
    }

    glm::vec3 origin{0.0f};
    glm::vec3 x     {1.0f, 0.0f, 0.0f};
    glm::vec3 y     {0.0f, 1.0f, 0.0f};
    glm::vec3 z     {0.0f, 0.0f, 1.0f};
};

// Turned about the upright through the origin: local +X toward `direction` (unit, level).
[[nodiscard]] auto make_turned_placement(const glm::vec3& origin, const glm::vec3& direction) -> Placement
{
    return Placement{.origin = origin, .x = direction, .y = y_axis, .z = glm::cross(direction, y_axis)};
}

// Upright but leaning: a local point moves by `lean` for each meter of its height.
[[nodiscard]] auto make_leaning_placement(const glm::vec3& origin, const glm::vec2 lean) -> Placement
{
    return Placement{.origin = origin, .x = x_axis, .y = glm::vec3{lean.x, 1.0f, lean.y}, .z = z_axis};
}

void add_placed_box(Kit_mesh_builder& builder, const Placement& place, const glm::vec3& lo, const glm::vec3& hi)
{
    const uint32_t v0 = builder.add_vertex(place.at(glm::vec3{lo.x, lo.y, lo.z}));
    const uint32_t v1 = builder.add_vertex(place.at(glm::vec3{hi.x, lo.y, lo.z}));
    const uint32_t v2 = builder.add_vertex(place.at(glm::vec3{lo.x, hi.y, lo.z}));
    const uint32_t v3 = builder.add_vertex(place.at(glm::vec3{lo.x, lo.y, hi.z}));
    const uint32_t v4 = builder.add_vertex(place.at(glm::vec3{hi.x, hi.y, lo.z}));
    const uint32_t v5 = builder.add_vertex(place.at(glm::vec3{hi.x, lo.y, hi.z}));
    const uint32_t v6 = builder.add_vertex(place.at(glm::vec3{lo.x, hi.y, hi.z}));
    const uint32_t v7 = builder.add_vertex(place.at(glm::vec3{hi.x, hi.y, hi.z}));
    builder.add_quad(v1, v4, v7, v5);
    builder.add_quad(v2, v6, v7, v4);
    builder.add_quad(v3, v5, v7, v6);
    builder.add_quad(v0, v3, v6, v2);
    builder.add_quad(v0, v1, v5, v3);
    builder.add_quad(v0, v2, v4, v1);
}

// A convex polygon, counter-clockwise seen from its front, and its copy moved by `depth` behind it.
void add_prism_between(Kit_mesh_builder& builder, const std::span<const glm::vec3> front_points, const glm::vec3& depth)
{
    std::vector<uint32_t> front;
    std::vector<uint32_t> back;
    for (const glm::vec3& point : front_points) {
        front.push_back(builder.add_vertex(point));
        back .push_back(builder.add_vertex(point + depth));
    }
    builder.add_polygon(front);
    const std::vector<uint32_t> reversed(back.rbegin(), back.rend());
    builder.add_polygon(reversed);
    for (std::size_t i = 0; i < front.size(); ++i) {
        const std::size_t j = (i + 1) % front.size();
        builder.add_quad(front[i], back[i], back[j], front[j]);
    }
}

// One ring of a lathe: its radius and its height along the placement's y.
class Lathe_ring
{
public:
    float radius{0.0f};
    float y     {0.0f};
};

enum class Lathe_phase : unsigned int {
    point = 0, // a point of the ring on local +X
    side       // a side of the ring across local +X
};

enum class Lathe_cap : unsigned int {
    polygon = 0, // the end ring's polygon
    point,       // a fan to a point on the axis
    loop         // no caps: the last ring joins the first
};

// Rings around the placement's y of `segments` points each, counter-clockwise seen from +y, the profile traced with
// the solid on its left: from the bottom outward, up and back inward. A band is planar quads.
class Lathe
{
public:
    std::span<const Lathe_ring> rings;
    int                         segments  {8};
    Lathe_phase                 phase     {Lathe_phase::point};
    Lathe_cap                   bottom    {Lathe_cap::polygon};
    Lathe_cap                   top       {Lathe_cap::polygon};
    float                       bottom_y  {0.0f};  // of a point cap
    float                       top_y     {0.0f};
    uint32_t                    slot      {0};
    std::span<const uint32_t>   band_slots{};      // when given: the slot of the band after each ring
    std::span<const float>      scales    {};      // when given: each point's share of the ring's radius
};

// Point i of n of the unit ring as (x, z); mirror images about z = 0 are equal to the bit.
[[nodiscard]] auto get_ring_point(const int i, const int n, const Lathe_phase phase) -> glm::vec2
{
    return (phase == Lathe_phase::point) ? get_kit_circle_point(i, n) : get_kit_circle_point((2 * i) + 1, 2 * n);
}

void add_lathe(Kit_mesh_builder& builder, const Placement& place, const Lathe& lathe)
{
    const uint32_t n     = static_cast<uint32_t>(lathe.segments);
    const uint32_t rings = static_cast<uint32_t>(lathe.rings.size());
    const uint32_t first = static_cast<uint32_t>(builder.get_positions().size());
    for (const Lathe_ring& ring : lathe.rings) {
        for (uint32_t i = 0; i < n; ++i) {
            const glm::vec2 point  = get_ring_point(static_cast<int>(i), lathe.segments, lathe.phase);
            const float     radius = lathe.scales.empty() ? ring.radius : (ring.radius * lathe.scales[i]);
            builder.add_vertex(place.at(glm::vec3{radius * point.x, ring.y, radius * point.y}));
        }
    }
    const auto get_band_slot = [&lathe](const uint32_t band) -> uint32_t {
        return lathe.band_slots.empty() ? lathe.slot : lathe.band_slots[band];
    };
    const bool     loop  = (lathe.bottom == Lathe_cap::loop);
    const uint32_t bands = loop ? rings : (rings - 1);
    for (uint32_t r = 0; r < bands; ++r) {
        const uint32_t lower = first + (r * n);
        const uint32_t upper = first + (((r + 1) % rings) * n);
        builder.set_slot(get_band_slot(r));
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t j = (i + 1) % n;
            builder.add_quad(lower + i, lower + j, upper + j, upper + i);
        }
    }
    if (loop) {
        return;
    }
    const uint32_t last = first + ((rings - 1) * n);
    builder.set_slot(get_band_slot(0));
    if (lathe.bottom == Lathe_cap::point) {
        const uint32_t apex = builder.add_vertex(place.at(glm::vec3{0.0f, lathe.bottom_y, 0.0f}));
        for (uint32_t i = 0; i < n; ++i) {
            builder.add_triangle(apex, first + ((i + 1) % n), first + i);
        }
    } else {
        std::vector<uint32_t> corners;
        for (uint32_t i = 0; i < n; ++i) {
            corners.push_back(first + (n - 1 - i));
        }
        builder.add_polygon(corners);
    }
    builder.set_slot(get_band_slot(bands - 1));
    if (lathe.top == Lathe_cap::point) {
        const uint32_t apex = builder.add_vertex(place.at(glm::vec3{0.0f, lathe.top_y, 0.0f}));
        for (uint32_t i = 0; i < n; ++i) {
            builder.add_triangle(last + i, last + ((i + 1) % n), apex);
        }
    } else {
        std::vector<uint32_t> corners;
        for (uint32_t i = 0; i < n; ++i) {
            corners.push_back(last + i);
        }
        builder.add_polygon(corners);
    }
}

// A level slab that rises by `slope` for each meter toward +Z of the origin.
[[nodiscard]] auto make_sloping_placement(const glm::vec3& origin, const float slope) -> Placement
{
    return Placement{.origin = origin, .x = x_axis, .y = y_axis, .z = glm::vec3{0.0f, slope, 1.0f}};
}

// A ratio parameter within its own range.
[[nodiscard]] auto make_share_parameter(const char* name, const char* description, const float default_value, const float min_value, const float max_value) -> Kit_parameter
{
    Kit_parameter parameter = make_ratio_parameter(name, description, default_value);
    parameter.float_min = min_value;
    parameter.float_max = max_value;
    return parameter;
}

[[nodiscard]] auto make_base_connector() -> Kit_connector
{
    return Kit_connector{.name = "base", .position = glm::vec3{0.0f}, .outward = glm::vec3{0.0f, -1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}};
}

// A level bar that rises by `slope` for each meter toward +X of the origin.
[[nodiscard]] auto make_rising_placement(const glm::vec3& origin, const float slope) -> Placement
{
    return Placement{.origin = origin, .x = glm::vec3{1.0f, slope, 0.0f}, .y = y_axis, .z = z_axis};
}

//
// Cargo: a pallet under layers of four goods each (crates, sacks or barrels) under a folded tarp.
//

constexpr uint32_t cargo_wood_slot  = 0;
constexpr uint32_t cargo_cloth_slot = 1;
constexpr int      cargo_layer_max  = 3;

enum class Cargo_kind : unsigned int { crates = 0, sacks, barrels };

// What layer k of the load is: a mixed load is crates under sacks under barrels.
[[nodiscard]] auto get_cargo_kind(const std::string_view load, const int layer) -> Cargo_kind
{
    if (load == "sacks") {
        return Cargo_kind::sacks;
    }
    if (load == "barrels") {
        return Cargo_kind::barrels;
    }
    if (load == "mixed") {
        return (layer == 0) ? Cargo_kind::crates : ((layer == 1) ? Cargo_kind::sacks : Cargo_kind::barrels);
    }
    return Cargo_kind::crates;
}

// A layer's height in widths.
[[nodiscard]] auto get_cargo_layer_height(const Cargo_kind kind) -> float
{
    return (kind == Cargo_kind::crates) ? 0.36f : ((kind == Cargo_kind::sacks) ? 0.15f : 0.4f);
}

class Cargo_form
{
public:
    explicit Cargo_form(const Kit_parameter_values& values)
        : load  {values.get_enum("load")}
        , layers{std::clamp(values.get_int("layers"), 1, cargo_layer_max)}
        , width {values.get_float("width")}
        , fill  {values.get_float("fill")}
    {
        pallet = std::clamp(0.14f * width, 0.08f, 0.16f);
        tarp   = 0.02f * width;
    }

    // The foot of layer k; of the tarp for k = layers.
    [[nodiscard]] auto get_layer_foot(const int k) const -> float
    {
        float y = pallet;
        for (int i = 0; i < k; ++i) {
            y += get_cargo_layer_height(get_cargo_kind(load, i)) * width;
        }
        return y;
    }
    [[nodiscard]] auto get_top() const -> float
    {
        return get_layer_foot(layers) + tarp;
    }

    std::string_view load  {};
    int              layers{2};
    float            width {0.0f};
    float            fill  {0.0f};
    float            pallet{0.0f}; // height of the pallet
    float            tarp  {0.0f}; // thickness of the tarp
};

void generate_cargo(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Cargo_form form{values};
    const float w  = form.width;
    const float hw = 0.5f * w;
    Kit_random random{values.get_seed()};
    Kit_mesh_builder builder;
    // The pallet: three runners along X under five boards along Z.
    builder.set_slot(cargo_wood_slot);
    for (int k = 0; k < 3; ++k) {
        const float z = (hw - (0.06f * w)) * (static_cast<float>(k) - 1.0f);
        builder.add_box(glm::vec3{-hw, 0.0f, z - (0.05f * w)}, glm::vec3{hw, 0.72f * form.pallet, z + (0.05f * w)});
    }
    for (int k = 0; k < 5; ++k) {
        const float x = (hw - (0.08f * w)) * ((0.5f * static_cast<float>(k)) - 1.0f);
        builder.add_box(glm::vec3{x - (0.075f * w), 0.7f * form.pallet, -(hw - 0.004f)}, glm::vec3{x + (0.075f * w), form.pallet, hw - 0.004f});
    }
    // Four goods to a layer about the middle; the fill is the share of the pallet they cover.
    const float side = hw * std::sqrt(form.fill) * 0.94f;
    for (int layer = 0; layer < form.layers; ++layer) {
        const Cargo_kind kind = get_cargo_kind(form.load, layer);
        const float      foot = form.get_layer_foot(layer);
        const float      high = get_cargo_layer_height(kind) * w;
        for (int k = 0; k < 4; ++k) {
            const float     off_x = 0.02f * w * next_signed(random);
            const float     off_z = 0.02f * w * next_signed(random);
            const float     turn  = 0.1f * next_signed(random);
            const float     x     = (((k % 2) == 0) ? -0.52f : 0.52f) * side;
            const float     z     = ((k < 2) ? -0.52f : 0.52f) * side;
            const glm::vec3 at{x + off_x, foot, z + off_z};
            const Placement place = make_turned_placement(at, around_y(turn));
            const float     half  = 0.5f * side;
            if (kind == Cargo_kind::crates) {
                builder.set_slot(cargo_wood_slot);
                add_placed_box(builder, place, glm::vec3{-0.94f * half, 0.0f, -0.94f * half}, glm::vec3{0.94f * half, 0.9f * high, 0.94f * half});
                add_placed_box(builder, place, glm::vec3{-half, 0.86f * high, -half}, glm::vec3{half, high, half});
            } else if (kind == Cargo_kind::sacks) {
                // A sack lies flat, longer than wide.
                Placement sack = place;
                sack.z = 0.74f * place.z;
                const std::array<Lathe_ring, 5> rings{{{0.6f * half, 0.0f}, {0.96f * half, 0.3f * high}, {half, 0.6f * high}, {0.78f * half, 0.9f * high}, {0.45f * half, high}}};
                add_lathe(builder, sack, Lathe{.rings = rings, .segments = 6, .slot = cargo_cloth_slot});
            } else {
                const float r = 0.92f * half;
                const std::array<Lathe_ring, 4> rings{{{0.8f * r, 0.0f}, {r, 0.3f * high}, {r, 0.7f * high}, {0.8f * r, high}}};
                add_lathe(builder, place, Lathe{.rings = rings, .segments = 8, .slot = cargo_wood_slot});
            }
        }
    }
    {
        // A folded tarp on the load.
        const float turn = 0.3f * next_signed(random);
        const Placement place = make_turned_placement(glm::vec3{0.0f, form.get_layer_foot(form.layers), 0.0f}, around_y(turn));
        builder.set_slot(cargo_cloth_slot);
        add_placed_box(builder, place, glm::vec3{-0.7f * side, -0.002f, -0.45f * side}, glm::vec3{0.7f * side, form.tarp, 0.45f * side});
    }
    builder.write(mesh);
}

//
// Crane: a derrick (a mast on two sills with a jib from its foot) or a lattice tower under a slewing jib; the jib
// points at the heading and rises by its angle, with a hook hanging from its end.
//

constexpr uint32_t crane_metal_slot = 0;
constexpr uint32_t crane_wood_slot  = 1;
constexpr int      crane_bays       = 4; // of the tower

class Crane_form
{
public:
    explicit Crane_form(const Kit_parameter_values& values)
        : tower {values.get_enum("form") == "tower"}
        , height{values.get_float("height")}
        , jib   {values.get_float("jib_length")}
    {
        const float heading = glm::radians(values.get_float("jib_heading"));
        const float angle   = glm::radians(values.get_float("jib_angle"));
        ahead = around_y(heading);
        along = (std::cos(angle) * ahead) + (std::sin(angle) * y_axis);
        post  = std::clamp(0.035f * height, 0.1f, 0.4f);
        chord = std::clamp(0.012f * height, 0.05f, 0.15f);
        half  = std::clamp(0.06f * height, 0.35f, 1.2f);
        deck  = 0.8f * height;
        // The jib's foot: before the mast's foot, or on the tower's deck.
        pivot = tower ? ((half * ahead) + ((deck + (3.0f * chord)) * y_axis)) : (((0.7f * post) * ahead) + ((0.12f * height) * y_axis));
        tip   = pivot + (jib * along);
    }

    bool      tower {false};
    float     height{0.0f};
    float     jib   {0.0f};
    glm::vec3 ahead {1.0f, 0.0f, 0.0f}; // the heading, level
    glm::vec3 along {1.0f, 0.0f, 0.0f}; // the jib's direction
    float     post  {0.0f};             // width of the derrick's mast
    float     chord {0.0f};             // width of the tower's members
    float     half  {0.0f};             // half the tower's width
    float     deck  {0.0f};             // the tower's top under its slewing deck
    glm::vec3 pivot {0.0f};
    glm::vec3 tip   {0.0f};
};

// The hook line down from the jib's end, and the hook.
void add_crane_hook(Kit_mesh_builder& builder, const Crane_form& form, const float thick)
{
    const float hook = 0.5f * form.tip.y;
    builder.set_slot(crane_metal_slot);
    builder.add_box(glm::vec3{form.tip.x - (0.2f * thick), hook, form.tip.z - (0.2f * thick)}, glm::vec3{form.tip.x + (0.2f * thick), form.tip.y - (0.1f * thick), form.tip.z + (0.2f * thick)});
    builder.add_box(glm::vec3{form.tip.x - (0.6f * thick), hook - (1.6f * thick), form.tip.z - (0.35f * thick)}, glm::vec3{form.tip.x + (0.6f * thick), hook + (0.1f * thick), form.tip.z + (0.35f * thick)});
}

void generate_crane(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Crane_form form{values};
    const float h = form.height;
    Kit_mesh_builder builder;
    if (!form.tower) {
        const float p    = form.post;
        const float foot = std::clamp(0.2f * h, 0.6f, 3.0f);
        builder.set_slot(crane_wood_slot);
        builder.add_box(glm::vec3{-foot, 0.0f, -0.5f * p}, glm::vec3{foot, p, 0.5f * p});
        builder.add_box(glm::vec3{-0.45f * p, 0.0f, -foot}, glm::vec3{0.45f * p, 0.9f * p, foot});
        builder.add_box(glm::vec3{-0.5f * p, 0.5f * p, -0.5f * p}, glm::vec3{0.5f * p, h - (0.3f * p), 0.5f * p});
        builder.add_beam(form.pivot, form.tip, 0.7f * p, 0.7f * p);
        builder.set_slot(crane_metal_slot);
        builder.add_box(glm::vec3{-0.6f * p, h - (0.6f * p), -0.6f * p}, glm::vec3{0.6f * p, h, 0.6f * p});
        // The topping line from the mast's head to the jib's end.
        builder.add_beam(glm::vec3{0.0f, h - (0.3f * p), 0.0f}, form.tip, 0.12f * p, 0.12f * p);
        add_crane_hook(builder, form, 0.5f * p);
    } else {
        const float c = form.chord;
        const float a = form.half;
        builder.set_slot(crane_metal_slot);
        for (const float side_x : {-1.0f, 1.0f}) {
            for (const float side_z : {-1.0f, 1.0f}) {
                const float x = side_x * a;
                const float z = side_z * a;
                builder.add_box(glm::vec3{x - (0.5f * c), 0.0f, z - (0.5f * c)}, glm::vec3{x + (0.5f * c), form.deck, z + (0.5f * c)});
            }
        }
        // Every bay: a rail around its top and a diagonal across each face, the other way in the next bay.
        const float bay = form.deck / static_cast<float>(crane_bays);
        for (int k = 0; k < crane_bays; ++k) {
            const float low   = bay * static_cast<float>(k);
            const float top   = low + bay;
            const float way   = ((k % 2) == 0) ? 1.0f : -1.0f;
            const float slope = way * (0.8f * bay) / (2.0f * a);
            for (const float side : {-1.0f, 1.0f}) {
                builder.add_box(glm::vec3{-a, top - (1.2f * c), (side * a) - (0.3f * c)}, glm::vec3{a, top - (0.4f * c), (side * a) + (0.3f * c)});
                builder.add_box(glm::vec3{(side * a) - (0.3f * c), top - (2.1f * c), -a}, glm::vec3{(side * a) + (0.3f * c), top - (1.3f * c), a});
                add_placed_box(
                    builder, make_rising_placement(glm::vec3{0.0f, low + (0.45f * bay), side * a}, slope),
                    glm::vec3{-a, -0.3f * c, -0.2f * c}, glm::vec3{a, 0.3f * c, 0.2f * c}
                );
                add_placed_box(
                    builder, make_sloping_placement(glm::vec3{side * a, low + (0.45f * bay), 0.0f}, slope),
                    glm::vec3{-0.2f * c, -0.3f * c, -a}, glm::vec3{0.2f * c, 0.3f * c, a}
                );
            }
        }
        // The slewing deck, the head over it, the jib and the counter jib with its weight.
        builder.add_box(glm::vec3{-1.2f * a, form.deck - (0.2f * c), -1.2f * a}, glm::vec3{1.2f * a, form.deck + (2.0f * c), 1.2f * a});
        builder.add_box(glm::vec3{-c, form.deck + c, -c}, glm::vec3{c, h, c});
        builder.add_beam(form.pivot, form.tip, 2.2f * c, 2.2f * c);
        const glm::vec3 behind = ((form.deck + (3.0f * c)) * y_axis) - ((0.35f * form.jib) * form.ahead);
        builder.add_beam(((form.deck + (3.0f * c)) * y_axis) + ((0.5f * c) * form.ahead), behind, 2.0f * c, 2.0f * c);
        const Placement turned = make_turned_placement(behind, form.ahead);
        add_placed_box(builder, turned, glm::vec3{-0.1f * form.jib, -4.0f * c, -0.7f * a}, glm::vec3{0.02f * form.jib, -0.8f * c, 0.7f * a});
        // The pendants from the head to the two jibs' ends.
        builder.add_beam(glm::vec3{0.0f, h - (0.5f * c), 0.0f}, form.tip, 0.3f * c, 0.3f * c);
        builder.add_beam(glm::vec3{0.0f, h - (0.8f * c), 0.0f}, behind + ((0.5f * c) * y_axis), 0.3f * c, 0.3f * c);
        // The cab on the deck beside the jib's foot.
        builder.set_slot(crane_wood_slot);
        add_placed_box(builder, turned, glm::vec3{(0.35f * form.jib) - (0.2f * a), -1.2f * c, 0.5f * a}, glm::vec3{(0.35f * form.jib) + a, 0.1f * h, 1.5f * a});
        add_crane_hook(builder, form, 2.0f * c);
    }
    builder.write(mesh);
}

//
// Lighthouse: a tapering tower of bands on its foot, under a gallery around a glowing lantern room and a roof.
//

constexpr uint32_t lighthouse_wall_slot = 0;
constexpr uint32_t lighthouse_trim_slot = 1;
constexpr uint32_t lighthouse_glow_slot = 2;
constexpr int      lighthouse_band_max  = 6;

class Lighthouse_form
{
public:
    explicit Lighthouse_form(const Kit_parameter_values& values)
        : railed  {values.get_enum("gallery") == "rail"}
        , bands   {std::clamp(values.get_int("bands"), 1, lighthouse_band_max)}
        , segments{std::max(values.get_int("segments"), 6)}
        , height  {values.get_float("height")}
        , foot    {values.get_float("base_radius")}
        , head    {values.get_float("taper") * foot}
    {
        shaft  = 0.76f * height;
        room   = 0.62f * head;
        glass  = glm::vec2{shaft + (0.012f * height), 0.9f * height};
    }

    // The light: beside the lantern room toward +X at its middle height, shining level toward +X.
    [[nodiscard]] auto get_light() const -> glm::vec3
    {
        return glm::vec3{room + std::max(0.15f, 0.12f * head), 0.5f * (glass.x + glass.y), 0.0f};
    }

    bool      railed  {true};
    int       bands   {3};
    int       segments{12};
    float     height  {0.0f};
    float     foot    {0.0f};  // radius at the base
    float     head    {0.0f};  // radius under the gallery
    float     shaft   {0.0f};  // height of the tower under the gallery
    float     room    {0.0f};  // radius of the lantern room
    glm::vec2 glass   {0.0f};  // the lantern room's glass from and to
};

void generate_lighthouse(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Lighthouse_form form{values};
    const float h = form.height;
    Kit_mesh_builder builder;
    {
        // The tower: a stripe of wall and a band of trim in turn.
        const int stripes = (2 * form.bands) + 1;
        std::vector<Lathe_ring> rings;
        std::vector<uint32_t>   slots;
        for (int k = 0; k <= stripes; ++k) {
            const float share = static_cast<float>(k) / static_cast<float>(stripes);
            rings.push_back(Lathe_ring{form.foot + ((form.head - form.foot) * share), form.shaft * share});
            if (k < stripes) {
                slots.push_back(((k % 2) == 1) ? lighthouse_trim_slot : lighthouse_wall_slot);
            }
        }
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = form.segments, .band_slots = slots});
    }
    const float deck = form.shaft + (0.012f * h);
    const float out  = 1.35f * form.head;
    {
        const std::array<Lathe_ring, 3> rings{{{0.98f * form.head, form.shaft - (0.02f * h)}, {out, form.shaft - (0.003f * h)}, {out, deck}}};
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = form.segments, .slot = lighthouse_trim_slot});
    }
    if (form.railed) {
        // A post at every point of the gallery under a rail.
        const float rail = std::clamp(0.04f * h, 0.7f, 1.2f);
        const float at   = out - (0.06f * form.head);
        const float post = std::clamp(0.03f * form.head, 0.02f, 0.06f);
        builder.set_slot(lighthouse_trim_slot);
        for (int i = 0; i < form.segments; ++i) {
            const glm::vec2 point = at * get_ring_point(i, form.segments, Lathe_phase::point);
            builder.add_box(glm::vec3{point.x - post, deck - (0.002f * h), point.y - post}, glm::vec3{point.x + post, deck + rail, point.y + post});
        }
        const std::array<Lathe_ring, 4> rings{{{at + (1.5f * post), deck + rail - (2.0f * post)}, {at + (1.5f * post), deck + rail + post}, {at - (1.5f * post), deck + rail + post}, {at - (1.5f * post), deck + rail - (2.0f * post)}}};
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = form.segments, .bottom = Lathe_cap::loop, .top = Lathe_cap::loop, .slot = lighthouse_trim_slot});
    }
    {
        const std::array<Lathe_ring, 2> rings{{{form.room, form.glass.x - (0.004f * h)}, {form.room, form.glass.y}}};
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = form.segments, .slot = lighthouse_glow_slot});
    }
    {
        const std::array<Lathe_ring, 3> rings{{{0.8f * form.head, form.glass.y - (0.004f * h)}, {0.74f * form.head, form.glass.y + (0.012f * h)}, {0.2f * form.head, 0.97f * h}}};
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = form.segments, .top = Lathe_cap::point, .top_y = h, .slot = lighthouse_trim_slot});
    }
    // The door stands out of the foot toward +X, over a sill.
    builder.set_slot(lighthouse_trim_slot);
    builder.add_box(glm::vec3{0.8f * form.foot, 0.02f, -0.5f}, glm::vec3{form.foot + 0.06f, std::min(2.2f, 0.12f * h), 0.5f});
    builder.write(mesh);
}

//
// Tank: a drum with flat, domed or pointed ends and bands around it: upright on four legs, or lying along X on two
// saddles.
//

constexpr uint32_t tank_metal_slot = 0;
constexpr uint32_t tank_trim_slot  = 1;
constexpr int      tank_band_max   = 4;

enum class Tank_ends : unsigned int { flat = 0, dome, cone };

[[nodiscard]] auto get_tank_ends(const std::string_view name) -> Tank_ends
{
    if (name == "dome") {
        return Tank_ends::dome;
    }
    if (name == "cone") {
        return Tank_ends::cone;
    }
    return Tank_ends::flat;
}

class Tank_form
{
public:
    explicit Tank_form(const Kit_parameter_values& values)
        : lying   {values.get_enum("pose") == "lying"}
        , ends    {get_tank_ends(values.get_enum("ends"))}
        , bands   {std::clamp(values.get_int("bands"), 0, tank_band_max)}
        , segments{std::max(values.get_int("segments"), 6)}
        , radius  {values.get_float("radius")}
        , height  {values.get_float("height")}
    {
        clear  = std::clamp(0.25f * radius, 0.12f, 0.6f);
        step   = std::min(0.03f * radius, 0.04f);
        // A lying tank's axis: over the saddles' clearance by its widest ring's lowest side.
        axis_y = clear + ((radius + step) * get_ring_point(0, segments, Lathe_phase::side).x);
        length = lying ? height : (height - clear);
    }

    [[nodiscard]] auto get_top() const -> float
    {
        return lying ? (axis_y + (radius * get_ring_point(0, segments, Lathe_phase::side).x)) : height;
    }

    bool      lying   {false};
    Tank_ends ends    {Tank_ends::dome};
    int       bands   {2};
    int       segments{12};
    float     radius  {0.0f};
    float     height  {0.0f};
    float     clear   {0.0f}; // from the ground to the tank
    float     step    {0.0f}; // how far a band stands out
    float     axis_y  {0.0f};
    float     length  {0.0f}; // of the tank along its axis
};

void generate_tank(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Tank_form form{values};
    const float r = form.radius;
    const float l = form.length;
    // An end's length along the axis: none, a dome's, a cone's; at most a third of the tank.
    const float end = std::min((form.ends == Tank_ends::flat) ? 0.0f : ((form.ends == Tank_ends::dome) ? (0.35f * r) : (0.55f * r)), 0.33f * l);
    std::vector<Lathe_ring> rings;
    std::vector<uint32_t>   slots;
    const auto add_ring = [&](const float radius, const float y, const uint32_t slot) {
        if (!rings.empty()) {
            slots.push_back(slot);
        }
        rings.push_back(Lathe_ring{radius, y});
    };
    if (form.ends == Tank_ends::dome) {
        add_ring(0.45f * r, 0.0f, tank_metal_slot);
        add_ring(0.85f * r, 0.4f * end, tank_metal_slot);
    }
    add_ring(r, end, tank_metal_slot);
    // The bands of trim, evenly along the drum.
    const float drum = l - (2.0f * end);
    const float half = std::min(0.04f * r, 0.3f * drum / static_cast<float>(tank_band_max + 1));
    for (int k = 0; k < form.bands; ++k) {
        const float y = end + (drum * static_cast<float>(k + 1) / static_cast<float>(form.bands + 1));
        add_ring(r, y - half, tank_metal_slot);
        add_ring(r + form.step, y - half, tank_trim_slot);
        add_ring(r + form.step, y + half, tank_trim_slot);
        add_ring(r, y + half, tank_trim_slot);
    }
    add_ring(r, l - end, tank_metal_slot);
    if (form.ends == Tank_ends::dome) {
        add_ring(0.85f * r, l - (0.4f * end), tank_metal_slot);
        add_ring(0.45f * r, l, tank_metal_slot);
    }
    const Lathe_cap cap = (form.ends == Tank_ends::cone) ? Lathe_cap::point : Lathe_cap::polygon;
    Kit_mesh_builder builder;
    if (form.lying) {
        const Placement place{.origin = glm::vec3{-0.5f * l, form.axis_y, 0.0f}, .x = -y_axis, .y = x_axis, .z = z_axis};
        add_lathe(builder, place, Lathe{.rings = rings, .segments = form.segments, .phase = Lathe_phase::side, .bottom = cap, .top = cap, .bottom_y = 0.0f, .top_y = l, .band_slots = slots});
        // Two saddles under the drum.
        builder.set_slot(tank_trim_slot);
        for (const float side : {-1.0f, 1.0f}) {
            const float x = side * 0.3f * drum;
            builder.add_box(glm::vec3{x - (0.1f * r), 0.0f, -0.85f * r}, glm::vec3{x + (0.1f * r), form.axis_y - (0.5f * r), 0.85f * r});
        }
    } else {
        const Placement place{.origin = glm::vec3{0.0f, form.clear, 0.0f}};
        add_lathe(builder, place, Lathe{.rings = rings, .segments = form.segments, .bottom = cap, .top = cap, .bottom_y = 0.0f, .top_y = l, .band_slots = slots});
        // Four legs into the drum's foot.
        builder.set_slot(tank_trim_slot);
        for (const float side_x : {-1.0f, 1.0f}) {
            for (const float side_z : {-1.0f, 1.0f}) {
                const float x = side_x * 0.58f * r;
                const float z = side_z * 0.58f * r;
                builder.add_box(glm::vec3{x - (0.06f * r), 0.0f, z - (0.06f * r)}, glm::vec3{x + (0.06f * r), form.clear + end + (0.15f * drum), z + (0.06f * r)});
            }
        }
    }
    builder.write(mesh);
}

//
// Truss: four chords along X tied by struts across, with a lattice in its two faces toward +Z and -Z.
//

constexpr uint32_t truss_metal_slot = 0;
constexpr int      truss_bay_min    = 2;
constexpr int      truss_bay_max    = 16;

enum class Truss_pattern : unsigned int { warren = 0, pratt, cross };

[[nodiscard]] auto get_truss_pattern(const std::string_view name) -> Truss_pattern
{
    if (name == "pratt") {
        return Truss_pattern::pratt;
    }
    if (name == "cross") {
        return Truss_pattern::cross;
    }
    return Truss_pattern::warren;
}

void generate_truss(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Truss_pattern pattern = get_truss_pattern(values.get_enum("pattern"));
    const int   bays   = std::clamp(values.get_int("bays"), truss_bay_min, truss_bay_max);
    const float half_l = 0.5f * values.get_float("length");
    const float half_w = 0.5f * values.get_float("width");
    const float height = values.get_float("height");
    const float m      = std::clamp(0.16f * std::min(2.0f * half_w, height), 0.02f, 0.12f); // width of a chord
    const float bay    = (2.0f * half_l) / static_cast<float>(bays);
    const float face   = half_w - (0.5f * m); // z of a face's middle
    Kit_mesh_builder builder;
    builder.set_slot(truss_metal_slot);
    for (const float side : {-1.0f, 1.0f}) {
        const float z0 = std::min(side * (half_w - m), side * half_w);
        const float z1 = std::max(side * (half_w - m), side * half_w);
        builder.add_box(glm::vec3{-half_l, 0.0f, z0}, glm::vec3{half_l, m, z1});
        builder.add_box(glm::vec3{-half_l, height - m, z0}, glm::vec3{half_l, height, z1});
    }
    // At every node: a strut across under the top chords and over the bottom ones, and a Pratt truss's posts.
    for (int i = 0; i <= bays; ++i) {
        const float x = std::clamp(-half_l + (bay * static_cast<float>(i)), -half_l + (0.3f * m), half_l - (0.3f * m));
        builder.add_box(glm::vec3{x - (0.3f * m), 0.15f * m, -(half_w - m)}, glm::vec3{x + (0.3f * m), 0.85f * m, half_w - m});
        builder.add_box(glm::vec3{x - (0.3f * m), height - (0.85f * m), -(half_w - m)}, glm::vec3{x + (0.3f * m), height - (0.15f * m), half_w - m});
        if (pattern == Truss_pattern::pratt) {
            for (const float side : {-1.0f, 1.0f}) {
                builder.add_box(glm::vec3{x - (0.3f * m), 0.5f * m, (side * face) - (0.3f * m)}, glm::vec3{x + (0.3f * m), height - (0.5f * m), (side * face) + (0.3f * m)});
            }
        }
    }
    // In every bay of both faces: a diagonal (a Warren truss's the other way in the next bay, a Pratt truss's toward
    // the middle), or two that cross.
    const float rise = (height - m) / bay;
    for (int i = 0; i < bays; ++i) {
        const float x   = -half_l + (bay * (static_cast<float>(i) + 0.5f));
        const float way = (pattern == Truss_pattern::pratt) ? (((2 * i) < bays) ? -1.0f : 1.0f) : ((((i % 2) == 0)) ? 1.0f : -1.0f);
        for (const float side : {-1.0f, 1.0f}) {
            add_placed_box(
                builder, make_rising_placement(glm::vec3{x, 0.5f * height, side * face}, way * rise),
                glm::vec3{-0.49f * bay, -0.35f * m, -0.25f * m}, glm::vec3{0.49f * bay, 0.35f * m, 0.25f * m}
            );
            if (pattern == Truss_pattern::cross) {
                add_placed_box(
                    builder, make_rising_placement(glm::vec3{x, 0.5f * height, side * (face - (0.1f * m))}, -way * rise),
                    glm::vec3{-0.49f * bay, -0.3f * m, -0.2f * m}, glm::vec3{0.49f * bay, 0.3f * m, 0.2f * m}
                );
            }
        }
    }
    builder.write(mesh);
}

//
// Container: a shipping container along X: a frame around ribbed sides, a roof, a floor and a back, with two door
// leaves at its +X end.
//

constexpr uint32_t container_metal_slot = 0;
constexpr uint32_t container_trim_slot  = 1;
constexpr float    container_unit       = 3.0f;  // a short container's length
constexpr float    container_width      = 2.4f;
constexpr float    container_height     = 2.6f;
constexpr float    container_frame      = 0.12f; // width of the frame's members

// The container's length in units.
[[nodiscard]] auto get_container_units(const std::string_view name) -> int
{
    return (name == "short") ? 1 : ((name == "long") ? 4 : 2);
}

void generate_container(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const int              units  = get_container_units(values.get_enum("length"));
    const int              ribs   = std::clamp(values.get_int("ribs"), 2, 6) * units;
    const std::string_view door   = values.get_enum("door");
    const float            swing  = glm::radians(values.get_float("door_angle"));
    const float            half_l = 0.5f * container_unit * static_cast<float>(units);
    const float            half_w = 0.5f * container_width;
    const float            h      = container_height;
    const float            f      = container_frame;
    Kit_mesh_builder builder;
    // The frame: four posts, and at the top and the bottom two rails along X and two across.
    builder.set_slot(container_trim_slot);
    for (const float side_x : {-1.0f, 1.0f}) {
        for (const float side_z : {-1.0f, 1.0f}) {
            const float x0 = side_x * (half_l - f);
            const float x1 = side_x * half_l;
            const float z0 = side_z * (half_w - f);
            const float z1 = side_z * half_w;
            builder.add_box(glm::vec3{std::min(x0, x1), 0.0f, std::min(z0, z1)}, glm::vec3{std::max(x0, x1), h, std::max(z0, z1)});
        }
    }
    for (const float y : {0.0f, h - f}) {
        const float low  = y + (0.01f * f);
        const float high = y + (0.99f * f);
        for (const float side : {-1.0f, 1.0f}) {
            const float z0 = side * (half_w - (0.9f * f));
            const float z1 = side * (half_w - (0.1f * f));
            builder.add_box(glm::vec3{-(half_l - (0.5f * f)), low, std::min(z0, z1)}, glm::vec3{half_l - (0.5f * f), high, std::max(z0, z1)});
            const float x0 = side * (half_l - (0.9f * f));
            const float x1 = side * (half_l - (0.1f * f));
            builder.add_box(glm::vec3{std::min(x0, x1), low + (0.02f * f), -(half_w - (0.5f * f))}, glm::vec3{std::max(x0, x1), high - (0.02f * f), half_w - (0.5f * f)});
        }
    }
    // The panels: two sides, the roof, the floor and the back.
    builder.set_slot(container_metal_slot);
    const float inner_l = half_l - (0.5f * f);
    const float inner_w = half_w - (0.5f * f);
    for (const float side : {-1.0f, 1.0f}) {
        const float z0 = side * (half_w - 0.07f);
        const float z1 = side * (half_w - 0.04f);
        builder.add_box(glm::vec3{-inner_l, 0.5f * f, std::min(z0, z1)}, glm::vec3{inner_l, h - (0.5f * f), std::max(z0, z1)});
    }
    builder.add_box(glm::vec3{-inner_l + 0.01f, h - 0.07f, -inner_w}, glm::vec3{inner_l - 0.01f, h - 0.03f, inner_w});
    builder.add_box(glm::vec3{-inner_l + 0.01f, 0.04f, -inner_w}, glm::vec3{inner_l - 0.01f, 0.1f, inner_w});
    builder.add_box(glm::vec3{-half_l + 0.03f, 0.6f * f, -inner_w + 0.01f}, glm::vec3{-half_l + 0.06f, h - (0.6f * f), inner_w - 0.01f});
    // The ribs stand out of the sides between the posts.
    const float pitch = (2.0f * (half_l - f)) / static_cast<float>(ribs);
    for (int k = 0; k < ribs; ++k) {
        const float x = -(half_l - f) + (pitch * (static_cast<float>(k) + 0.5f));
        for (const float side : {-1.0f, 1.0f}) {
            const float z0 = side * (half_w - 0.045f);
            const float z1 = side * (half_w - 0.005f);
            builder.add_box(glm::vec3{x - (0.25f * pitch), 1.2f * f, std::min(z0, z1)}, glm::vec3{x + (0.25f * pitch), h - (1.2f * f), std::max(z0, z1)});
        }
    }
    // The door leaves hang on the posts at the +X end, each under two locking bars.
    for (const float side : {-1.0f, 1.0f}) {
        float angle = 0.0f;
        if (door == "open") {
            angle = (0.5f * pi) + swing;
        } else if ((door == "ajar") && (side > 0.0f)) {
            angle = swing;
        }
        // The leaf's frame: from its hinge along the leaf, up, and out of its outer face.
        const glm::vec3 along {std::sin(angle), 0.0f, -side * std::cos(angle)};
        const glm::vec3 outer {std::cos(angle), 0.0f, side * std::sin(angle)};
        const glm::vec3 across = glm::cross(along, y_axis);
        const bool      same   = glm::dot(across, outer) > 0.0f;
        const Placement place{.origin = glm::vec3{half_l - 0.03f, 0.0f, side * (half_w - f)}, .x = along, .y = y_axis, .z = across};
        const float     leaf   = half_w - f - 0.01f;
        // A box from the leaf's outer face by `from` to `to` behind it (negative: before it).
        const auto add_leaf_box = [&](const float u0, const float u1, const float y0, const float y1, const float from, const float to) {
            const float z0 = same ? -to : from;
            const float z1 = same ? -from : to;
            add_placed_box(builder, place, glm::vec3{u0, y0, std::min(z0, z1)}, glm::vec3{u1, y1, std::max(z0, z1)});
        };
        builder.set_slot(container_metal_slot);
        add_leaf_box(0.0f, leaf, 1.1f * f, h - (1.1f * f), 0.0f, 0.04f);
        builder.set_slot(container_trim_slot);
        for (const float share : {0.3f, 0.7f}) {
            add_leaf_box((share * leaf) - 0.02f, (share * leaf) + 0.02f, 1.6f * f, h - (1.6f * f), -0.03f, 0.01f);
        }
    }
    builder.write(mesh);
}

//
// Vent: a rooftop unit within its width and height: a box of louvres, a fan in a drum, a stack under a rain cap, or
// a duct that turns toward +Z.
//

constexpr uint32_t vent_metal_slot = 0;
constexpr int      vent_round      = 10; // points around a drum and a stack

// Slats across X on the face at z, from y0 up to y1, leaning out and down toward `out` (+1 or -1 along Z).
void add_vent_slats(Kit_mesh_builder& builder, const int slats, const float half_x, const float y0, const float y1, const float z, const float out, const float reach)
{
    const float pitch = (y1 - y0) / static_cast<float>(slats);
    for (int k = 0; k < slats; ++k) {
        const float y = y0 + (pitch * (static_cast<float>(k) + 0.7f));
        const Placement place = make_sloping_placement(glm::vec3{0.0f, y, z}, -out * 0.8f);
        // A slat stands out by at most what keeps it over the one below.
        const float far = std::min(reach, 0.6f * pitch);
        const float z0  = std::min(0.0f, out * far);
        const float z1  = std::max(0.0f, out * far);
        add_placed_box(builder, place, glm::vec3{-half_x, -0.12f * pitch, z0}, glm::vec3{half_x, 0.12f * pitch, z1});
    }
}

void generate_vent(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const std::string_view form  = values.get_enum("form");
    const float            hw    = 0.5f * values.get_float("width");
    const float            h     = values.get_float("height");
    const int              slats = std::clamp(values.get_int("slats"), 3, 8);
    Kit_mesh_builder builder;
    builder.set_slot(vent_metal_slot);
    if (form == "fan") {
        // A curb, a drum, blades across its mouth and a hub.
        builder.add_box(glm::vec3{-hw, 0.0f, -hw}, glm::vec3{hw, 0.14f * h, hw});
        const std::array<Lathe_ring, 4> drum{{{0.86f * hw, 0.1f * h}, {0.86f * hw, 0.8f * h}, {0.92f * hw, 0.8f * h}, {0.92f * hw, 0.86f * h}}};
        add_lathe(builder, Placement{}, Lathe{.rings = drum, .segments = vent_round, .slot = vent_metal_slot});
        builder.set_slot(vent_metal_slot);
        for (int k = 0; k < slats; ++k) {
            const float angle = pi * static_cast<float>(k) / static_cast<float>(slats);
            const Placement place = make_turned_placement(glm::vec3{0.0f, 0.87f * h, 0.0f}, around_y(angle));
            add_placed_box(builder, place, glm::vec3{-0.9f * hw, 0.0f, -0.03f * hw}, glm::vec3{0.9f * hw, (0.03f + (0.004f * static_cast<float>(k))) * h, 0.03f * hw});
        }
        const std::array<Lathe_ring, 2> hub{{{0.22f * hw, 0.86f * h}, {0.16f * hw, 0.97f * h}}};
        add_lathe(builder, Placement{}, Lathe{.rings = hub, .segments = vent_round, .top = Lathe_cap::point, .top_y = h, .slot = vent_metal_slot});
    } else if (form == "stack") {
        // A flange, a pipe, and a rain cap on struts around the pipe's mouth.
        builder.add_box(glm::vec3{-0.7f * hw, 0.0f, -0.7f * hw}, glm::vec3{0.7f * hw, 0.05f * h, 0.7f * hw});
        const std::array<Lathe_ring, 2> pipe{{{0.45f * hw, 0.03f * h}, {0.45f * hw, 0.8f * h}}};
        add_lathe(builder, Placement{}, Lathe{.rings = pipe, .segments = vent_round, .slot = vent_metal_slot});
        builder.set_slot(vent_metal_slot);
        for (int k = 0; k < slats; ++k) {
            const glm::vec2 point = (0.45f * hw) * get_kit_circle_point(k, slats);
            builder.add_box(glm::vec3{point.x - (0.03f * hw), 0.74f * h, point.y - (0.03f * hw)}, glm::vec3{point.x + (0.03f * hw), 0.9f * h, point.y + (0.03f * hw)});
        }
        const std::array<Lathe_ring, 2> cap{{{hw, 0.87f * h}, {0.97f * hw, 0.9f * h}}};
        add_lathe(builder, Placement{}, Lathe{.rings = cap, .segments = vent_round, .top = Lathe_cap::point, .top_y = h, .slot = vent_metal_slot});
    } else if (form == "duct_elbow") {
        // A duct up from a curb at the back, turning toward +Z to a mouth of slats.
        builder.add_box(glm::vec3{-hw, 0.0f, -hw}, glm::vec3{hw, 0.08f * h, 0.1f * hw});
        builder.add_box(glm::vec3{-0.8f * hw, 0.05f * h, -0.9f * hw}, glm::vec3{0.8f * hw, 0.7f * h, -0.05f * hw});
        builder.add_box(glm::vec3{-0.82f * hw, 0.5f * h, -0.92f * hw}, glm::vec3{0.82f * hw, h, 0.84f * hw});
        add_vent_slats(builder, slats, 0.74f * hw, 0.54f * h, 0.96f * h, 0.84f * hw, 1.0f, 0.16f * hw);
    } else {
        // A box on a curb under a cap, with slats on its faces toward +Z and -Z.
        builder.add_box(glm::vec3{-hw, 0.0f, -0.8f * hw}, glm::vec3{hw, 0.08f * h, 0.8f * hw});
        builder.add_box(glm::vec3{-0.92f * hw, 0.05f * h, -0.7f * hw}, glm::vec3{0.92f * hw, 0.9f * h, 0.7f * hw});
        builder.add_box(glm::vec3{-0.99f * hw, 0.88f * h, -0.78f * hw}, glm::vec3{0.99f * hw, h, 0.78f * hw});
        add_vent_slats(builder, slats, 0.82f * hw, 0.14f * h, 0.84f * h, 0.7f * hw, 1.0f, 0.1f * hw);
        add_vent_slats(builder, slats, 0.82f * hw, 0.14f * h, 0.84f * h, -0.7f * hw, -1.0f, 0.1f * hw);
    }
    builder.write(mesh);
}

//
// Antenna: a mast on a plate with a seeded set of elements: dishes, whips over a short mast, or bars.
//

constexpr uint32_t antenna_metal_slot  = 0;
constexpr int      antenna_element_max = 5;
constexpr int      antenna_dish        = 8; // points around a dish

void generate_antenna(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const std::string_view form     = values.get_enum("form");
    const float            height   = values.get_float("height");
    const int              elements = std::clamp(values.get_int("elements"), 1, antenna_element_max);
    const float            tilt     = glm::radians(values.get_float("tilt"));
    const float            u        = std::clamp(0.03f * height, 0.04f, 0.15f); // the mast's width
    const float            mast     = (form == "whip") ? (0.55f * height) : height;
    Kit_random random{values.get_seed()};
    Kit_mesh_builder builder;
    builder.set_slot(antenna_metal_slot);
    builder.add_box(glm::vec3{-2.5f * u, 0.0f, -2.5f * u}, glm::vec3{2.5f * u, 0.4f * u, 2.5f * u});
    {
        const std::array<Lathe_ring, 2> rings{{{0.55f * u, 0.2f * u}, {0.45f * u, mast}}};
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = 6, .slot = antenna_metal_slot});
    }
    for (int k = 0; k < elements; ++k) {
        const float     heading = random.next_range(0.0f, two_pi);
        const float     size    = random.next_range(0.6f, 1.0f);
        const glm::vec3 ahead   = around_y(heading);
        if (form == "dish") {
            // A dish on the mast's side, its axis tilted up from the heading.
            const float     r     = std::clamp(0.1f * height, 0.15f, 0.7f) * size;
            const float     y     = height * (0.86f - (0.15f * static_cast<float>(k)));
            const glm::vec3 axis  = (std::cos(tilt) * ahead) + (std::sin(tilt) * y_axis);
            const glm::vec3 aside = glm::normalize(glm::cross(y_axis, axis));
            const Placement place{.origin = (y * y_axis) + ((0.3f * u) * ahead), .x = aside, .y = axis, .z = glm::cross(aside, axis)};
            const std::array<Lathe_ring, 5> rings{{{0.14f * r, 0.0f}, {0.6f * r, 0.12f * r}, {r, 0.34f * r}, {0.94f * r, 0.34f * r}, {0.5f * r, 0.14f * r}}};
            add_lathe(builder, place, Lathe{.rings = rings, .segments = antenna_dish, .slot = antenna_metal_slot});
            builder.set_slot(antenna_metal_slot);
            add_placed_box(builder, place, glm::vec3{-0.03f * r, 0.1f * r, -0.03f * r}, glm::vec3{0.03f * r, 0.6f * r, 0.03f * r});
        } else if (form == "whip") {
            // A rod from the mast's head, leaning out along the heading; the first reaches the height.
            const float     reach = (k == 0) ? 1.0f : size;
            const float     foot  = 0.5f * height;
            const float     slope = std::tan(tilt);
            const Placement place = make_leaning_placement((foot * y_axis) + ((0.5f * u) * ahead), slope * glm::vec2{ahead.x, ahead.z});
            add_placed_box(builder, place, glm::vec3{-0.12f * u, 0.0f, -0.12f * u}, glm::vec3{0.12f * u, (height - foot) * reach, 0.12f * u});
        } else {
            // A bar across the mast, level along the heading and rolled by the tilt.
            const float     y     = height * (0.92f - (0.12f * static_cast<float>(k)));
            const float     half  = 0.5f * std::clamp(0.3f * height, 0.4f, 2.0f) * size;
            const glm::vec3 aside = glm::cross(ahead, y_axis);
            const glm::vec3 run   = (std::cos(tilt) * ahead) + (std::sin(tilt) * y_axis);
            const Placement place{.origin = (y * y_axis) + ((0.6f * u) * aside), .x = run, .y = glm::cross(aside, run), .z = aside};
            add_placed_box(builder, place, glm::vec3{-half, -0.15f * u, -0.15f * u}, glm::vec3{half, 0.15f * u, 0.15f * u});
        }
    }
    builder.write(mesh);
}

} // anonymous namespace

void add_extra_25_32_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape;
        shape.id              = "harbour.cargo";
        shape.kit             = "harbour";
        shape.display_name    = "Cargo";
        shape.description     =
            "Pallet with a stack of goods at its origin: three runners under five boards, one to three layers of four "
            "goods each, and a folded tarp (slot cloth) on the load. The goods are crates, sacks (slot cloth), barrels, "
            "or mixed: crates under sacks under barrels. The fill is the share of the pallet the goods cover; the seed "
            "moves and turns each a little.";
        shape.tags            = {"obstacle", "decoration", "harbour", "cargo", "stack", "size:1x1"};
        shape.parameters      = {
            derive_enum(make_enum_parameter("load", "Crates, sacks, barrels, or a layer of each", {"crates", "sacks", "barrels", "mixed"}, 0)),
            from_seed_by_default(derive_int(make_count_parameter("layers", "Layers of goods", 2, 1, cargo_layer_max), 1, 3)),
            from_seed_by_default(derive_float(make_share_parameter("fill", "Share of the pallet the goods cover", 0.8f, 0.5f, 1.0f), 0.5f, 1.0f, 0.05f)),
            derive_float(make_length_parameter("width", "Size of the pallet along X and Z", 1.0f, Kit_default_scale::modules, 0.6f, 3.0f), 1.0f, 1.25f, 0.25f),
            make_seed_parameter()
        };
        shape.material_slots  = {"wood", "cloth"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 320;
        shape.collision       = Kit_collision::convex_hull;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_cargo;
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            add_base_and_top_connectors(out_connectors, Cargo_form{values}.get_top());
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "harbour.crane";
        shape.kit             = "harbour";
        shape.display_name    = "Crane";
        shape.description     =
            "Quay crane at its origin. Derrick: a timber mast on two crossed sills with a timber jib from its foot, held "
            "by a line from the mast's head. Tower: a lattice tower of four bays under a slewing deck with a head, a "
            "jib, a counter jib with its weight and a timber cab. The jib points at the heading (from +X toward -Z) and "
            "rises by the angle; a hook hangs from its end on a line.";
        shape.tags            = {"obstacle", "decoration", "harbour", "crane", "size:4x4.5"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("form",        "A derrick or a lattice tower", {"derrick", "tower"}, 0)),
            derive_float(make_length_parameter("height",      "Height of the mast or the tower's head", 1.5f, Kit_default_scale::wall_heights, 2.0f, 40.0f), 1.5f, 2.5f, 0.25f),
            derive_float(make_length_parameter("jib_length",  "Length of the jib", 4.0f, Kit_default_scale::meters, 1.0f, 20.0f), 3.0f, 6.0f, 0.5f),
            from_seed_by_default(derive_float(make_angle_parameter("jib_heading", "Where the jib points, from +X toward -Z", 0.0f, 0.0f, 360.0f), 0.0f, 345.0f, 15.0f)),
            derive_float(make_angle_parameter ("jib_angle",   "How far the jib rises from level", 30.0f, 10.0f, 60.0f), 10.0f, 60.0f, 5.0f),
            make_seed_parameter()
        };
        shape.material_slots  = {"metal", "wood"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 110;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.generator       = generate_crane;
        shape.get_connectors  = [](const Kit_parameter_values&, std::vector<Kit_connector>& out_connectors) {
            out_connectors.push_back(make_base_connector());
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "harbour.lighthouse";
        shape.kit             = "harbour";
        shape.display_name    = "Lighthouse";
        shape.description     =
            "Lighthouse at its origin: a tapering tower in stripes of wall and bands of trim, with a door toward +X, "
            "under a gallery (with or without a rail) around a glowing lantern room (slot glow) under a pointed roof. "
            "It carries one spot light beside the lantern room, shining level toward +X.";
        shape.tags            = {"obstacle", "landmark", "harbour", "lighthouse", "light", "size:4x12"};
        shape.parameters      = {
            derive_float(make_length_parameter("height",      "Size along Y", 4.0f, Kit_default_scale::wall_heights, 6.0f, 60.0f), 3.0f, 5.0f, 0.5f),
            derive_float(make_length_parameter("base_radius", "Radius of the tower's foot", 2.0f, Kit_default_scale::modules, 1.0f, 10.0f), 1.5f, 2.5f, 0.25f),
            derive_float(make_share_parameter ("taper",       "The radius under the gallery in base radii", 0.7f, 0.5f, 0.9f), 0.5f, 0.9f, 0.05f),
            derive_int  (make_count_parameter ("bands",       "Bands of trim around the tower", 3, 1, lighthouse_band_max), 1, 6),
            derive_enum (make_enum_parameter  ("gallery",     "The gallery alone, or with a rail", {"none", "rail"}, 1)),
            make_seed_parameter (),
            make_count_parameter("segments", "Points around the tower", 12, 8, 24)
        };
        shape.material_slots   = {"wall", "trim", "glow"};
        shape.normal_style     = Kit_normal_style::faceted;
        shape.triangle_budget  = 660;
        shape.collision        = Kit_collision::triangle_mesh;
        shape.generator        = generate_lighthouse;
        shape.mirror_planes    = std::vector<Kit_mirror_plane>{Kit_mirror_plane::z};
        shape.get_connectors   = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            add_base_and_top_connectors(out_connectors, values.get_float("height"));
        };
        shape.get_light_frames = [](const Kit_parameter_values& values, std::vector<Kit_light_frame>& out_frames) {
            // A beam for the far distance (P2): narrow, strong and of long reach.
            out_frames.push_back(Kit_light_frame{
                .name      = "light",
                .position  = Lighthouse_form{values}.get_light(),
                .direction = glm::vec3{1.0f, 0.0f, 0.0f},
                .start     = Kit_light_start{.type = Kit_light_type::spot, .intensity = 4000.0f, .range = 100.0f, .inner_spot_angle = 0.15f, .outer_spot_angle = 0.3f}
            });
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "industrial.tank";
        shape.kit             = "industrial";
        shape.display_name    = "Tank";
        shape.description     =
            "Tank at its origin: a drum with flat, domed or pointed ends and bands (slot trim) around it. Upright it "
            "stands on four legs, the height high; lying it lies along X on two saddles, the height long.";
        shape.tags            = {"obstacle", "decoration", "industrial", "tank", "size:2x3"};
        shape.parameters      = {
            derive_float(make_length_parameter("radius", "Radius of the drum", 1.0f, Kit_default_scale::modules, 0.3f, 8.0f), 0.75f, 1.5f, 0.25f),
            derive_float(make_length_parameter("height", "An upright tank's height with its legs, a lying one's length", 1.0f, Kit_default_scale::wall_heights, 1.0f, 32.0f), 0.75f, 1.5f, 0.25f),
            derive_enum (make_enum_parameter  ("pose",   "Upright on legs, or lying along X on saddles", {"upright", "lying"}, 0)),
            derive_enum (make_enum_parameter  ("ends",   "Flat, domed or pointed ends", {"flat", "dome", "cone"}, 1)),
            derive_int  (make_count_parameter ("bands",  "Bands around the drum", 2, 0, tank_band_max), 0, 4),
            make_seed_parameter (),
            make_count_parameter("segments", "Points around the drum", 12, 8, 24)
        };
        shape.material_slots  = {"metal", "trim"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 400;
        shape.collision       = Kit_collision::convex_hull;
        shape.generator       = generate_tank;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::z};
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            add_base_and_top_connectors(out_connectors, Tank_form{values}.get_top());
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "industrial.truss";
        shape.kit             = "industrial";
        shape.display_name    = "Truss";
        shape.description     =
            "Lattice beam along X: four chords tied by struts across at every node, with a lattice in its faces toward "
            "+Z and -Z. Warren: a diagonal to a bay, each the other way from the last. Pratt: posts at the nodes and "
            "diagonals toward the middle. Cross: two diagonals to a bay. Trusses chain end to start; turned upright "
            "one is a mast.";
        shape.tags            = {"obstacle", "industrial", "truss", "beam", "tiles", "size:4x0.5"};
        shape.parameters      = {
            derive_float(make_length_parameter("length", "Size along X", 4.0f, Kit_default_scale::modules, 1.0f, 32.0f), 3.0f, 8.0f, 1.0f),
            derive_float(make_length_parameter("width",  "Size along Z", 0.5f, Kit_default_scale::modules, 0.2f, 4.0f),  0.5f, 1.0f, 0.25f),
            derive_float(make_length_parameter("height", "Size along Y", 0.5f, Kit_default_scale::modules, 0.2f, 4.0f),  0.5f, 1.0f, 0.25f),
            derive_int  (make_count_parameter ("bays",   "Bays along the truss", 6, truss_bay_min, truss_bay_max), 4, 8),
            derive_enum (make_enum_parameter  ("pattern", "Warren, Pratt or crossed diagonals", {"warren", "pratt", "cross"}, 0)),
            make_seed_parameter()
        };
        shape.material_slots  = {"metal"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 370;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.generator       = generate_truss;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::z};
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float half_l = 0.5f * values.get_float("length");
            add_side_connector(out_connectors, "start", glm::vec3{-half_l, 0.0f, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "end",   glm::vec3{ half_l, 0.0f, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
            add_base_and_top_connectors(out_connectors, values.get_float("height"));
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "industrial.container";
        shape.kit             = "industrial";
        shape.display_name    = "Container";
        shape.description     =
            "Shipping container along X, 2.4 m wide and 2.6 m high, 3, 6 or 12 m long: a frame (slot trim) around "
            "ribbed sides, a roof, a floor and a back, hollow inside. Two door leaves hang at its +X end under "
            "locking bars: closed, one ajar by the door angle, or both open a quarter turn and the door angle. "
            "Containers stack base on top and stand side by side.";
        shape.tags            = {"obstacle", "decoration", "industrial", "container", "stack", "size:6x2.4x2.6"};
        shape.parameters      = {
            derive_enum(make_enum_parameter("length", "3, 6 or 12 m long", {"short", "standard", "long"}, 1)),
            derive_int (make_count_parameter("ribs",  "Ribs on a side to every 3 m of length", 4, 2, 6), 2, 6),
            derive_enum(make_enum_parameter("door",   "Closed, one leaf ajar, or both open", {"closed", "ajar", "open"}, 0)),
            from_seed_by_default(derive_float(make_angle_parameter("door_angle", "How far an ajar leaf stands open, and an open one beyond a quarter turn", 25.0f, 5.0f, 60.0f), 5.0f, 60.0f, 5.0f)),
            make_seed_parameter()
        };
        shape.material_slots  = {"metal", "trim"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 480;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.generator       = generate_container;
        shape.get_connectors  = [](const Kit_parameter_values&, std::vector<Kit_connector>& out_connectors) {
            add_base_and_top_connectors(out_connectors, container_height);
            add_side_connector(out_connectors, "front", glm::vec3{0.0f, 0.0f,  0.5f * container_width}, glm::vec3{0.0f, 0.0f,  1.0f});
            add_side_connector(out_connectors, "back",  glm::vec3{0.0f, 0.0f, -0.5f * container_width}, glm::vec3{0.0f, 0.0f, -1.0f});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "industrial.vent";
        shape.kit             = "industrial";
        shape.display_name    = "Vent";
        shape.description     =
            "Rooftop unit at its origin, within its width and height. Louvre box: a box on a curb under a cap, with "
            "slats on its faces toward +Z and -Z. Fan: a drum on a curb with blades across its mouth around a hub. "
            "Stack: a pipe on a flange under a rain cap on struts. Duct elbow: a duct up from a curb, turning toward "
            "+Z to a mouth of slats. The slats are the louvres, the blades or the struts.";
        shape.tags            = {"obstacle", "decoration", "industrial", "vent", "roof", "size:1x1"};
        shape.parameters      = {
            derive_enum(make_enum_parameter("form", "Louvre box, fan, stack or duct elbow", {"louvre_box", "fan", "stack", "duct_elbow"}, 0)),
            from_seed_by_default(derive_float(make_length_parameter("width",  "Size along X", 1.0f, Kit_default_scale::modules, 0.3f, 4.0f), 0.5f, 1.5f, 0.25f)),
            from_seed_by_default(derive_float(make_length_parameter("height", "Size along Y", 1.0f, Kit_default_scale::modules, 0.3f, 4.0f), 0.5f, 1.5f, 0.25f)),
            derive_int (make_count_parameter("slats", "Louvres, blades or struts", 5, 3, 8), 3, 8),
            make_seed_parameter()
        };
        shape.material_slots  = {"metal"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 170;
        shape.collision       = Kit_collision::convex_hull;
        shape.origin_rule     = Kit_origin_rule::base_contains_origin; // a duct elbow's curb stands behind its mouth
        shape.generator       = generate_vent;
        shape.get_connectors  = [](const Kit_parameter_values&, std::vector<Kit_connector>& out_connectors) {
            out_connectors.push_back(make_base_connector());
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "industrial.antenna";
        shape.kit             = "industrial";
        shape.display_name    = "Antenna";
        shape.description     =
            "Antenna mast on a plate at its origin with one to five elements at seeded headings. Dish: dishes down "
            "the mast's side, their axes tilted up. Whip: rods over a short mast, leaning out by the tilt, the first "
            "up to the height. Array: bars across the mast, one under the other, rolled by the tilt.";
        shape.tags            = {"decoration", "obstacle", "industrial", "antenna", "roof", "size:1x3"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("form",   "Dishes, whips or bars", {"dish", "whip", "array"}, 0)),
            derive_float(make_length_parameter("height", "Size along Y", 1.0f, Kit_default_scale::wall_heights, 1.0f, 20.0f), 0.75f, 1.5f, 0.25f),
            from_seed_by_default(derive_int(make_count_parameter("elements", "Dishes, whips or bars", 2, 1, antenna_element_max), 1, 5)),
            from_seed_by_default(derive_float(make_angle_parameter("tilt", "How far a dish looks up, a whip leans out, a bar rolls", 15.0f, 0.0f, 40.0f), 0.0f, 40.0f, 5.0f)),
            make_seed_parameter()
        };
        shape.material_slots  = {"metal"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 220;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_antenna;
        shape.get_connectors  = [](const Kit_parameter_values&, std::vector<Kit_connector>& out_connectors) {
            out_connectors.push_back(make_base_connector());
        };
        shapes.push_back(std::move(shape));
    }
}

} // namespace editor
