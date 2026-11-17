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

// The last four shapes of the build order of doc/plans/level_kit_extra_shapes.md (doc/editor/level_tools.md).
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

// A rope along the centers: a tube of four corners (down, toward -Z, up, toward +Z) between two end faces. Each
// stretch is a sheared box, so its faces are planar wherever the centers lie.
void add_rope(Kit_mesh_builder& builder, const std::span<const glm::vec3> centers, const float half, const uint32_t slot)
{
    const uint32_t count = static_cast<uint32_t>(centers.size());
    const uint32_t first = static_cast<uint32_t>(builder.get_positions().size());
    for (const glm::vec3& center : centers) {
        builder.add_vertex(center - (half * y_axis));
        builder.add_vertex(center - (half * z_axis));
        builder.add_vertex(center + (half * y_axis));
        builder.add_vertex(center + (half * z_axis));
    }
    builder.set_slot(slot);
    for (uint32_t r = 0; (r + 1) < count; ++r) {
        const uint32_t lower = first + (4 * r);
        const uint32_t upper = lower + 4;
        for (uint32_t i = 0; i < 4; ++i) {
            const uint32_t j = (i + 1) % 4;
            builder.add_quad(lower + i, lower + j, upper + j, upper + i);
        }
    }
    const uint32_t last = first + (4 * (count - 1));
    builder.add_quad(first + 3, first + 2, first + 1, first);
    builder.add_quad(last, last + 1, last + 2, last + 3);
}

//
// Garland: a wire sagging between its two ends with a bulb, a flag or a paper lantern hanging at every point along
// it, a ribbon at each end, and a light at mid span.
//

constexpr uint32_t garland_metal_slot = 0;
constexpr uint32_t garland_glow_slot  = 1;
constexpr uint32_t garland_cloth_slot = 2;
constexpr int      garland_count_min  = 3;
constexpr int      garland_count_max  = 24;
constexpr int      garland_round      = 6;     // points around a bulb and a paper lantern
constexpr float    garland_lamp       = 0.07f; // size of the lamp under a garland of flags

enum class Garland_item : unsigned int { bulb = 0, flag, paper_lantern };

[[nodiscard]] auto get_garland_item(const std::string_view name) -> Garland_item
{
    if (name == "flag") {
        return Garland_item::flag;
    }
    if (name == "paper_lantern") {
        return Garland_item::paper_lantern;
    }
    return Garland_item::bulb;
}

class Garland_form
{
public:
    explicit Garland_form(const Kit_parameter_values& values)
        : item {get_garland_item(values.get_enum("item"))}
        , count{std::clamp(values.get_int("count"), garland_count_min, garland_count_max)}
        , span {values.get_float("span")}
        , sag  {values.get_float("sag") * span}
    {
        pitch = span / static_cast<float>(count + 1);
        wire  = std::clamp(0.003f * span, 0.006f, 0.012f);
        if (item == Garland_item::bulb) {
            size  = std::clamp(0.45f * pitch, 0.04f, 0.1f);
            depth = 1.5f * size;
        } else if (item == Garland_item::flag) {
            size  = std::min(0.42f * pitch, 0.3f);
            depth = 1.8f * size;
        } else {
            size  = std::clamp(0.45f * pitch, 0.05f, 0.16f);
            depth = 1.6f * size;
        }
        // The lowest point: the item nearest the middle, or the lamp under the flags.
        const float nearest = ((count % 2) == 1) ? 0.0f : (0.5f * pitch);
        float low = get_drop(nearest) - depth;
        if (item == Garland_item::flag) {
            low = std::min(low, -get_lamp_depth());
        }
        middle = -low;
    }

    // How far the wire lies over its middle at x.
    [[nodiscard]] auto get_drop(const float x) const -> float
    {
        const float share = 2.0f * x / span;
        return sag * share * share;
    }
    // The wire's height at x.
    [[nodiscard]] auto get_wire(const float x) const -> float
    {
        return middle + get_drop(x);
    }
    [[nodiscard]] auto get_x(const int i) const -> float
    {
        return (-0.5f * span) + (pitch * static_cast<float>(i));
    }
    // How far the lamp of a garland of flags hangs under the wire's middle.
    [[nodiscard]] auto get_lamp_depth() const -> float
    {
        return depth + 0.04f + (1.5f * garland_lamp);
    }
    // The light: at mid span beside what hangs there, toward +Z.
    [[nodiscard]] auto get_light() const -> glm::vec3
    {
        const float under = (item == Garland_item::flag) ? (get_lamp_depth() - (0.6f * garland_lamp)) : (0.6f * depth);
        return glm::vec3{0.0f, get_wire(0.0f) - under, 0.22f};
    }

    Garland_item item  {Garland_item::bulb};
    int          count {8};
    float        span  {0.0f};
    float        sag   {0.0f}; // of the wire's middle under its ends
    float        pitch {0.0f}; // from an item to the next
    float        wire  {0.0f}; // half the wire's thickness
    float        size  {0.0f}; // of an item
    float        depth {0.0f}; // how far an item hangs under the wire
    float        middle{0.0f}; // the wire's height at mid span
};

// A bulb hanging from `top` in a socket: `size` its radius.
void add_garland_bulb(Kit_mesh_builder& builder, const glm::vec3& top, const float size)
{
    builder.set_slot(garland_metal_slot);
    builder.add_box(top - glm::vec3{0.3f * size, 0.5f * size, 0.3f * size}, top + glm::vec3{0.3f * size, 0.0f, 0.3f * size});
    // Turned upside down: the lathe's rings run down to the bulb's point.
    const Placement down{.origin = top, .x = x_axis, .y = -y_axis, .z = -z_axis};
    const std::array<Lathe_ring, 3> rings{{{0.22f * size, 0.4f * size}, {0.5f * size, 0.8f * size}, {0.45f * size, 1.15f * size}}};
    add_lathe(builder, down, Lathe{.rings = rings, .segments = garland_round, .top = Lathe_cap::point, .top_y = 1.5f * size, .slot = garland_glow_slot});
}

void generate_garland(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Garland_form form{values};
    Kit_mesh_builder builder;
    std::vector<glm::vec3> centers;
    for (int i = 0; i <= (form.count + 1); ++i) {
        const float x = form.get_x(i);
        centers.push_back(glm::vec3{x, form.get_wire(x), 0.0f});
    }
    add_rope(builder, centers, form.wire, garland_metal_slot);
    const float s = form.size;
    for (int i = 1; i <= form.count; ++i) {
        const glm::vec3 top = centers[static_cast<std::size_t>(i)];
        if (form.item == Garland_item::bulb) {
            add_garland_bulb(builder, top, s);
        } else if (form.item == Garland_item::flag) {
            // A pennant: its upper edge along the wire, its point down.
            const std::array<glm::vec3, 3> outline{{
                top + glm::vec3{-s, 0.3f * form.wire, 0.003f}, top + glm::vec3{0.0f, -form.depth, 0.003f}, top + glm::vec3{s, 0.3f * form.wire, 0.003f}
            }};
            builder.set_slot(garland_cloth_slot);
            add_prism_between(builder, outline, glm::vec3{0.0f, 0.0f, -0.006f});
        } else {
            // A paper lantern on a cord under a cap.
            builder.set_slot(garland_metal_slot);
            builder.add_box(top - glm::vec3{0.04f * s, 0.34f * s, 0.04f * s}, top + glm::vec3{0.04f * s, 0.0f, 0.04f * s});
            builder.add_box(top - glm::vec3{0.32f * s, 0.46f * s, 0.32f * s}, top + glm::vec3{0.32f * s, -0.3f * s, 0.32f * s});
            const Placement down{.origin = top, .x = x_axis, .y = -y_axis, .z = -z_axis};
            const std::array<Lathe_ring, 4> rings{{{0.36f * s, 0.42f * s}, {0.8f * s, 0.76f * s}, {0.8f * s, 1.26f * s}, {0.36f * s, 1.6f * s}}};
            add_lathe(builder, down, Lathe{.rings = rings, .segments = garland_round, .slot = garland_glow_slot});
        }
    }
    if (form.item == Garland_item::flag) {
        // The lamp at mid span, on a cord under the flags.
        const glm::vec3 top{0.0f, form.get_wire(0.0f) - form.depth - 0.04f, 0.0f};
        builder.set_slot(garland_metal_slot);
        builder.add_box(glm::vec3{-0.005f, top.y - (0.2f * garland_lamp), -0.005f}, glm::vec3{0.005f, form.get_wire(0.0f) - form.wire, 0.005f});
        add_garland_bulb(builder, top, garland_lamp);
    }
    // Two ends of a ribbon hang from each end of the wire.
    builder.set_slot(garland_cloth_slot);
    const float ribbon = std::min({0.35f, (0.8f * form.sag) + 0.1f, 0.7f * (form.middle + form.sag)});
    for (const float side : {-1.0f, 1.0f}) {
        const glm::vec3 end = (side < 0.0f) ? centers.front() : centers.back();
        for (const float lean : {0.12f, 0.4f}) {
            const Placement place = make_leaning_placement(end - glm::vec3{side * 0.03f, 0.0f, 0.0f}, glm::vec2{side * lean, 0.3f * (lean - 0.26f)});
            add_placed_box(builder, place, glm::vec3{-0.02f, -ribbon * (1.2f - lean), -0.003f}, glm::vec3{0.02f, 0.5f * form.wire, 0.003f});
        }
    }
    builder.write(mesh);
}

//
// Rope bridge: planks across two ropes that hang in a parabola between its two ends, under two hand ropes on
// hangers, between two posts at each end. A loose plank lies on the next one.
//

constexpr uint32_t bridge_wood_slot  = 0;
constexpr uint32_t bridge_cloth_slot = 1;
constexpr int      bridge_plank_min  = 4;
constexpr int      bridge_plank_max  = 64;
constexpr int      bridge_stretches  = 15;    // of a rope: odd, so two points lie beside the middle
constexpr float    bridge_plank      = 0.2f;  // width of a plank along the bridge
constexpr float    bridge_thick      = 0.04f; // thickness of a plank
constexpr float    bridge_rope       = 0.02f; // half a rope's thickness

class Bridge_form
{
public:
    explicit Bridge_form(const Kit_parameter_values& values)
        : span  {values.get_float("span")}
        , half_w{0.5f * values.get_float("width")}
        , sag   {values.get_float("sag") * span}
        , gap   {values.get_float("plank_gap")}
        , rail  {values.get_float("rail_height")}
        , loose {values.get_float("missing")}
    {
        planks = std::clamp(static_cast<int>(std::lround(span / (bridge_plank + gap))), bridge_plank_min, bridge_plank_max);
        pitch  = span / static_cast<float>(planks);
        // The lowest point: the deck ropes beside the middle, or the posts' feet.
        const float beside = 0.5f * span / static_cast<float>(bridge_stretches);
        const float ropes  = get_drop(beside) - bridge_thick - (2.0f * bridge_rope);
        const float posts  = sag - 0.12f;
        middle = -std::min(ropes, posts);
    }

    // How far the walking surface lies over its middle at x, and its height there.
    [[nodiscard]] auto get_drop(const float x) const -> float
    {
        const float share = 2.0f * x / span;
        return sag * share * share;
    }
    [[nodiscard]] auto get_deck(const float x) const -> float
    {
        return middle + get_drop(x);
    }
    // The walking surface's rise for each meter along the bridge at x.
    [[nodiscard]] auto get_slope(const float x) const -> float
    {
        return 8.0f * sag * x / (span * span);
    }
    [[nodiscard]] auto get_plank_x(const int i) const -> float
    {
        return (-0.5f * span) + (pitch * (static_cast<float>(i) + 0.5f));
    }
    // The widest gap the agent steps over: between two planks, or where a plank is loose.
    [[nodiscard]] auto get_widest_gap() const -> float
    {
        const float between = pitch - get_plank_width();
        return (loose > 0.0f) ? (get_plank_width() + (2.0f * between)) : between;
    }
    [[nodiscard]] auto get_plank_width() const -> float
    {
        return pitch * bridge_plank / (bridge_plank + gap);
    }

    float span  {0.0f};
    float half_w{0.0f};
    float sag   {0.0f}; // of the deck's middle under its ends
    float gap   {0.0f};
    float rail  {0.0f};
    float loose {0.0f}; // share of planks that lie loose
    int   planks{4};
    float pitch {0.0f}; // a plank and its gap
    float middle{0.0f}; // the walking surface's height at mid span
};

void generate_rope_bridge(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Bridge_form form{values};
    const float half_l = 0.5f * form.span;
    const float wide   = form.get_plank_width();
    Kit_random random{values.get_seed()};
    Kit_mesh_builder builder;
    builder.set_slot(bridge_wood_slot);
    bool before = false; // the plank before lies loose
    for (int i = 0; i < form.planks; ++i) {
        const float chance = random.next_unit();
        const float shift  = 0.02f * next_signed(random);
        const float low    = 0.008f * random.next_unit();
        const float turn   = random.next_range(0.25f, 0.6f);
        const float way    = (random.next_unit() < 0.5f) ? -1.0f : 1.0f;
        // A loose plank: not the first two, not the last two, and never two in a row.
        const bool  is_loose = (chance < form.loose) && !before && (i > 1) && (i < (form.planks - 2));
        before = is_loose;
        if (!is_loose) {
            const float x = form.get_plank_x(i);
            const Placement place = make_rising_placement(glm::vec3{x, form.get_deck(x) - low, shift}, form.get_slope(x));
            add_placed_box(builder, place, glm::vec3{-0.5f * wide, -bridge_thick, -form.half_w}, glm::vec3{0.5f * wide, 0.0f, form.half_w});
        } else {
            // It lies turned on the next plank.
            const float x     = form.get_plank_x(i + 1) - (0.2f * wide);
            const float slope = form.get_slope(x);
            const float c     = std::cos(way * turn);
            const float s     = std::sin(way * turn);
            const Placement place{.origin = glm::vec3{x, form.get_deck(x) + bridge_thick + 0.004f, shift}, .x = glm::vec3{c, slope * c, -s}, .y = y_axis, .z = glm::vec3{s, slope * s, c}};
            add_placed_box(builder, place, glm::vec3{-0.5f * wide, -bridge_thick, -form.half_w}, glm::vec3{0.5f * wide, 0.0f, form.half_w});
        }
    }
    // The ropes: two under the planks and two hand ropes, which hang a little deeper than the deck.
    const auto get_hand = [&form, half_l](const float x) -> float {
        const float share = x / half_l;
        return form.get_deck(x) + (form.rail * (0.78f + (0.22f * share * share)));
    };
    for (const float side : {-1.0f, 1.0f}) {
        std::vector<glm::vec3> deck;
        std::vector<glm::vec3> hand;
        for (int k = 0; k <= bridge_stretches; ++k) {
            const float x = -half_l + (form.span * static_cast<float>(k) / static_cast<float>(bridge_stretches));
            deck.push_back(glm::vec3{x, form.get_deck(x) - bridge_thick - bridge_rope, side * 0.84f * form.half_w});
            hand.push_back(glm::vec3{x, get_hand(x), side * form.half_w});
        }
        add_rope(builder, deck, bridge_rope, bridge_cloth_slot);
        add_rope(builder, hand, bridge_rope, bridge_cloth_slot);
    }
    // A hanger from each hand rope to the deck's edge at every fourth plank.
    builder.set_slot(bridge_cloth_slot);
    for (int i = 2; i < form.planks; i += 4) {
        const float x = form.get_plank_x(i);
        for (const float side : {-1.0f, 1.0f}) {
            const float z = side * (form.half_w - 0.012f);
            builder.add_box(glm::vec3{x - 0.008f, form.get_deck(x) - (0.8f * bridge_thick), z - 0.008f}, glm::vec3{x + 0.008f, get_hand(x), z + 0.008f});
        }
    }
    // Two posts at each end carry the hand ropes.
    builder.set_slot(bridge_wood_slot);
    for (const float side_x : {-1.0f, 1.0f}) {
        for (const float side_z : {-1.0f, 1.0f}) {
            const float x = side_x * (half_l - 0.06f);
            const float z = side_z * (form.half_w + 0.05f);
            builder.add_box(glm::vec3{x - 0.06f, form.get_deck(half_l) - 0.12f, z - 0.06f}, glm::vec3{x + 0.06f, form.get_deck(half_l) + form.rail + 0.1f, z + 0.06f});
        }
    }
    builder.write(mesh);
}

[[nodiscard]] auto check_bridge_walkability(const Kit_parameter_values& values, const Kit_agent& agent) -> std::string
{
    const Bridge_form form{values};
    const std::string& id = values.get_shape()->id;
    const float min_width = (2.0f * agent.radius) + kit_opening_margin;
    const float clear     = (2.0f * form.half_w) - (2.0f * bridge_rope);
    if (clear < min_width) {
        return fmt::format("{}: the clear width {} m between the hand ropes is narrower than the agent's {} m", id, clear, min_width);
    }
    const float steepest = glm::degrees(std::atan(form.get_slope(0.5f * form.span)));
    if (steepest > agent.max_slope_degrees) {
        return fmt::format("{}: the deck's ends rise at {} degrees, steeper than the agent's {}", id, steepest, agent.max_slope_degrees);
    }
    if (form.get_widest_gap() > agent.radius) {
        return fmt::format("{}: the widest gap {} m between planks is wider than the agent's radius {} m", id, form.get_widest_gap(), agent.radius);
    }
    return {};
}

//
// Cracked floor: the floor slab cut into rows and each row into fragments by seeded straight cuts; each fragment
// is a prism that lies a little sunk and tilted, or deep where it is missing.
//

constexpr uint32_t cracked_floor_slot  = 0;
constexpr int      cracked_piece_min   = 3;
constexpr int      cracked_piece_max   = 24;
constexpr float    cracked_tilt_rise   = 0.1f;  // the most a fragment's tilt lifts a corner over its middle
constexpr float    cracked_sink        = 0.02f; // the most a fragment sinks besides
constexpr float    cracked_missing     = 0.3f;  // the most a missing fragment lies under the slab's top

// The step between two fragments is at most this: one at the top, the other sunk and tilted, or missing.
[[nodiscard]] auto get_cracked_step(const Kit_parameter_values& values) -> float
{
    const float thickness = values.get_float("thickness");
    const float tilted    = (values.get_float("tilt") > 0.0f) ? (2.0f * cracked_tilt_rise) : 0.0f;
    const float sunk      = std::min(tilted + cracked_sink, 0.7f * thickness);
    const float missing   = (values.get_float("missing") > 0.0f) ? std::min(0.85f * thickness, cracked_missing) : 0.0f;
    return std::max(sunk, missing);
}

void generate_cracked_floor(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float half_w    = 0.5f * values.get_float("width");
    const float half_d    = 0.5f * values.get_float("depth");
    const float thickness = values.get_float("thickness");
    const int   pieces    = std::clamp(values.get_int("fragments"), cracked_piece_min, cracked_piece_max);
    const float slope     = std::tan(glm::radians(values.get_float("tilt")));
    const float missing   = values.get_float("missing");
    Kit_random random{values.get_seed()};
    // Rows across Z: as many as the root of the count; the first rows take the fragments left over.
    int rows = 1;
    while (((rows + 1) * (rows + 1)) <= pieces) {
        ++rows;
    }
    const int each  = pieces / rows;
    const int extra = pieces % rows;
    // The cuts between rows: each from the slab's -X side to its +X side at seeded places.
    std::vector<glm::vec2> row_cuts; // z at -X and at +X
    for (int r = 0; r <= rows; ++r) {
        const float left   = 0.22f * next_signed(random);
        const float right  = 0.22f * next_signed(random);
        const bool  inside = (r > 0) && (r < rows);
        const float base   = static_cast<float>(r);
        const float count  = static_cast<float>(rows);
        row_cuts.push_back((2.0f * half_d) * glm::vec2{((base + (inside ? left : 0.0f)) / count) - 0.5f, ((base + (inside ? right : 0.0f)) / count) - 0.5f});
    }
    const auto get_point = [&](const int row_cut, const float x) -> glm::vec2 {
        const glm::vec2 cut = row_cuts[static_cast<std::size_t>(row_cut)];
        return glm::vec2{x, cut.x + ((cut.y - cut.x) * (x + half_w) / (2.0f * half_w))};
    };
    Kit_mesh_builder builder;
    builder.set_slot(cracked_floor_slot);
    for (int r = 0; r < rows; ++r) {
        const int   cells = each + ((r < extra) ? 1 : 0);
        const float count = static_cast<float>(cells);
        // The cuts across the row: each from the row's near cut to its far cut at seeded places.
        std::vector<glm::vec2> cuts; // x on the near cut and on the far cut
        for (int k = 0; k <= cells; ++k) {
            const float at_near = 0.22f * next_signed(random);
            const float at_far  = 0.22f * next_signed(random);
            const bool  inside = (k > 0) && (k < cells);
            const float base   = static_cast<float>(k);
            cuts.push_back((2.0f * half_w) * glm::vec2{((base + (inside ? at_near : 0.0f)) / count) - 0.5f, ((base + (inside ? at_far : 0.0f)) / count) - 0.5f});
        }
        for (int k = 0; k < cells; ++k) {
            const float heading = random.next_range(0.0f, two_pi);
            const float share   = random.next_range(0.3f, 1.0f);
            const float sink    = random.next_unit();
            const float chance  = random.next_unit();
            // The fragment's corners, counter-clockwise seen from above.
            const std::size_t index = static_cast<std::size_t>(k);
            std::array<glm::vec2, 4> corners{
                get_point(r, cuts[index].x), get_point(r + 1, cuts[index].y), get_point(r + 1, cuts[index + 1].y), get_point(r, cuts[index + 1].x)
            };
            const glm::vec2 center = 0.25f * (corners[0] + corners[1] + corners[2] + corners[3]);
            float reach = 0.0f;
            for (const glm::vec2& corner : corners) {
                reach = std::max(reach, glm::distance(corner, center));
            }
            // A crack around it: every corner stands back toward the middle, but along the slab's sides.
            const float crack = std::min(0.012f, 0.06f * reach);
            for (glm::vec2& corner : corners) {
                const glm::vec2 moved = corner + ((crack / glm::distance(corner, center)) * (center - corner));
                corner = glm::vec2{(std::abs(corner.x) == half_w) ? corner.x : moved.x, (std::abs(corner.y) == half_d) ? corner.y : moved.y};
            }
            // Its top: a plane through its middle, tilted toward the heading; the highest corner is at most the top.
            float rise  = std::min(slope * share, cracked_tilt_rise / reach) * reach;
            float lower = cracked_sink * sink;
            const float most = 0.7f * thickness;
            if (((2.0f * rise) + lower) > most) {
                const float fit = most / ((2.0f * rise) + lower);
                rise  *= fit;
                lower *= fit;
            }
            const bool      is_missing = chance < missing;
            const float     middle     = is_missing ? (thickness - std::min(0.85f * thickness, cracked_missing)) : (thickness - rise - lower);
            const glm::vec2 fall       = is_missing ? glm::vec2{0.0f} : ((rise / reach) * glm::vec2{std::cos(heading), std::sin(heading)});
            std::array<uint32_t, 4> bottom{};
            std::array<uint32_t, 4> top{};
            for (std::size_t j = 0; j < 4; ++j) {
                bottom[j] = builder.add_vertex(glm::vec3{corners[j].x, 0.0f, corners[j].y});
                top[j]    = builder.add_vertex(glm::vec3{corners[j].x, middle + glm::dot(fall, corners[j] - center), corners[j].y});
            }
            builder.add_quad(top[0], top[1], top[2], top[3]);
            builder.add_quad(bottom[3], bottom[2], bottom[1], bottom[0]);
            for (std::size_t j = 0; j < 4; ++j) {
                const std::size_t n = (j + 1) % 4;
                builder.add_quad(bottom[j], bottom[n], top[n], top[j]);
            }
        }
    }
    builder.write(mesh);
}

//
// Boat: an open hull of two skins lofted over stations along its keel, with a gunwale, thwarts and two sleepers
// under it; it stands level or heeled toward +Z.
//

constexpr uint32_t boat_wood_slot   = 0;
constexpr uint32_t boat_trim_slot   = 1;
constexpr int      boat_bands       = 10; // along the keel
constexpr int      boat_strakes     = 8;  // across the hull: even, so a line of points lies on the keel
constexpr int      boat_thwart_max  = 4;

enum class Boat_form : unsigned int { rowboat = 0, skiff, barge };

[[nodiscard]] auto get_boat_form(const std::string_view name) -> Boat_form
{
    if (name == "skiff") {
        return Boat_form::skiff;
    }
    if (name == "barge") {
        return Boat_form::barge;
    }
    return Boat_form::rowboat;
}

// The hull's lines at u, 0 at the stern to 1 at the bow.
class Boat_lines
{
public:
    explicit Boat_lines(const Kit_parameter_values& values)
        : form  {get_boat_form(values.get_enum("form"))}
        , length{values.get_float("length")}
        , beam  {values.get_float("beam")}
        , sheer {values.get_float("sheer")}
        , heel  {glm::radians(values.get_float("heel"))}
    {
        depth = ((form == Boat_form::barge) ? 0.3f : 0.36f) * beam;
        skin  = std::clamp(0.03f * beam, 0.015f, 0.05f);
    }

    // Half the hull's width at the gunwale.
    [[nodiscard]] auto get_half_beam(const float u) const -> float
    {
        float share = 1.0f;
        if (form == Boat_form::rowboat) {
            share = std::max(std::pow(std::max(std::sin(pi * (0.22f + (0.78f * u))), 0.0f), 0.75f), 0.05f);
        } else if (form == Boat_form::skiff) {
            share = std::max(std::pow(std::max(std::sin(pi * (0.3f + (0.7f * u))), 0.0f), 0.6f), 0.07f);
        } else {
            share = std::min(1.0f, 0.82f + (2.2f * std::min(u, 1.0f - u)));
        }
        return 0.5f * beam * share;
    }
    // The keel's height over the lowest point: it rises toward the bow, and a barge's toward both ends.
    [[nodiscard]] auto get_keel(const float u) const -> float
    {
        const float ahead  = std::max((2.0f * u) - 1.0f, 0.0f);
        const float astern = std::max(1.0f - (2.0f * u), 0.0f);
        if (form == Boat_form::rowboat) {
            return depth * ((0.7f * ahead * ahead * ahead) + (0.2f * astern * astern * astern));
        }
        if (form == Boat_form::skiff) {
            return depth * 0.55f * ahead * ahead;
        }
        const float rake = std::clamp((std::abs((2.0f * u) - 1.0f) - 0.7f) / 0.3f, 0.0f, 1.0f);
        return 0.6f * depth * rake;
    }
    // The gunwale's height: the sheer lifts the ends, the bow more.
    [[nodiscard]] auto get_gunwale(const float u) const -> float
    {
        const float off = (2.0f * u) - 1.0f;
        return depth + (0.25f * sheer * length * off * off * ((off > 0.0f) ? 1.4f : 0.8f));
    }
    // A point of the outer skin at u, across at t from -1 (the gunwale at -Z) over 0 (the keel) to 1.
    [[nodiscard]] auto get_outer(const float u, const float t) const -> glm::vec3
    {
        const float s    = std::abs(t);
        const float side = (t < 0.0f) ? -1.0f : 1.0f;
        float out  = 0.0f;
        float up   = 0.0f;
        if (form == Boat_form::rowboat) {
            out = std::sin(0.5f * pi * s);
            up  = std::pow(s, 2.2f);
        } else {
            // A flat bottom out to the chine, then the side.
            const float chine = (form == Boat_form::barge) ? 0.94f : 0.82f;
            out = (s <= 0.5f) ? (2.0f * chine * s) : (chine + (2.0f * (1.0f - chine) * (s - 0.5f)));
            up  = (s <= 0.5f) ? 0.0f : (2.0f * (s - 0.5f));
        }
        const float keel = get_keel(u);
        return glm::vec3{length * (u - 0.5f), keel + ((get_gunwale(u) - keel) * up), side * get_half_beam(u) * out};
    }
    // The inner skin's point: toward the middle by the skin, over the bottom by the skin, level with the gunwale.
    [[nodiscard]] auto get_inner(const float u, const float t) const -> glm::vec3
    {
        const glm::vec3 outer = get_outer(u, t);
        const float     keel  = get_keel(u);
        const float     up    = (outer.y - keel) / std::max(get_gunwale(u) - keel, 1.0e-4f);
        const float     ends  = (u == 0.0f) ? skin : ((u == 1.0f) ? -skin : 0.0f);
        return glm::vec3{outer.x + ends, outer.y + (skin * (1.0f - up)), outer.z * std::max(1.0f - (skin / get_half_beam(u)), 0.35f)};
    }

    Boat_form form  {Boat_form::rowboat};
    float     length{0.0f};
    float     beam  {0.0f};
    float     sheer {0.0f};
    float     heel  {0.0f};
    float     depth {0.0f}; // of the hull amidships
    float     skin  {0.0f}; // thickness of the hull
};

void generate_boat(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Boat_lines lines{values};
    const int        thwarts = std::clamp(values.get_int("thwarts"), 0, boat_thwart_max);
    constexpr int    across  = boat_strakes + 1;
    // The two skins' points before the heel.
    std::vector<glm::vec3> outer;
    std::vector<glm::vec3> inner;
    for (int i = 0; i <= boat_bands; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(boat_bands);
        for (int j = 0; j < across; ++j) {
            const float t = (static_cast<float>(2 * j) / static_cast<float>(boat_strakes)) - 1.0f;
            outer.push_back(lines.get_outer(u, t));
            inner.push_back(lines.get_inner(u, t));
        }
    }
    // Heeled about the keel line toward +Z, then lifted so that its lowest point lies on the sleepers.
    const float c = std::cos(lines.heel);
    const float s = std::sin(lines.heel);
    Placement place{.x = x_axis, .y = glm::vec3{0.0f, c, s}, .z = glm::vec3{0.0f, -s, c}};
    float lowest = 0.0f;
    for (const glm::vec3& point : outer) {
        lowest = std::min(lowest, place.at(point).y);
    }
    const float sleeper = std::clamp(0.05f * lines.beam, 0.04f, 0.1f);
    place.origin.y = sleeper - lowest;

    Kit_mesh_builder builder;
    std::vector<uint32_t> out;
    std::vector<uint32_t> in;
    for (std::size_t k = 0; k < outer.size(); ++k) {
        out.push_back(builder.add_vertex(place.at(outer[k])));
        in .push_back(builder.add_vertex(place.at(inner[k])));
    }
    const auto at = [](const int i, const int j) -> std::size_t {
        return (static_cast<std::size_t>(i) * static_cast<std::size_t>(across)) + static_cast<std::size_t>(j);
    };
    for (int i = 0; i < boat_bands; ++i) {
        for (int j = 0; j < boat_strakes; ++j) {
            const std::size_t a = at(i, j);
            const std::size_t b = at(i + 1, j);
            const std::size_t d = at(i, j + 1);
            const std::size_t e = at(i + 1, j + 1);
            // The top strake of both sides is trim.
            builder.set_slot(((j == 0) || ((j + 1) == boat_strakes)) ? boat_trim_slot : boat_wood_slot);
            builder.add_triangle(out[a], out[b], out[e]);
            builder.add_triangle(out[a], out[e], out[d]);
            builder.set_slot(boat_wood_slot);
            builder.add_triangle(in[a], in[e], in[b]);
            builder.add_triangle(in[a], in[d], in[e]);
        }
    }
    // The rim between the two skins: along both gunwales and across the stern and the bow.
    builder.set_slot(boat_trim_slot);
    const auto add_rim = [&](const std::size_t p, const std::size_t q) {
        builder.add_triangle(out[q], out[p], in[p]);
        builder.add_triangle(out[q], in[p], in[q]);
    };
    for (int i = 0; i < boat_bands; ++i) {
        add_rim(at(i, 0), at(i + 1, 0));
        add_rim(at(i + 1, boat_strakes), at(i, boat_strakes));
    }
    for (int j = 0; j < boat_strakes; ++j) {
        add_rim(at(boat_bands, j), at(boat_bands, j + 1));
        add_rim(at(0, j + 1), at(0, j));
    }
    // A board closes each end inside the hull: the transom at the stern, the stem (a barge's second transom) at the bow.
    builder.set_slot(boat_wood_slot);
    for (const float end : {0.0f, 1.0f}) {
        const float u     = (end == 0.0f) ? 0.012f : 0.988f;
        const float thick = ((end == 0.0f) ? 1.0f : -1.0f) * 1.5f * lines.skin;
        // The section's corners from the gunwale at -Z over the keel to the gunwale at +Z: a round hull's nine
        // points, a flat one's four.
        std::vector<glm::vec3> outline;
        if (lines.form == Boat_form::rowboat) {
            for (int j = 0; j < across; ++j) {
                outline.push_back(place.at(lines.get_outer(u, (static_cast<float>(2 * j) / static_cast<float>(boat_strakes)) - 1.0f)));
            }
        } else {
            for (const float t : {-1.0f, -0.5f, 0.5f, 1.0f}) {
                outline.push_back(place.at(lines.get_outer(u, t)));
            }
        }
        if (end == 1.0f) {
            std::reverse(outline.begin(), outline.end());
        }
        add_prism_between(builder, outline, glm::vec3{thick, 0.0f, 0.0f});
    }
    // The thwarts: boards across the hull at 0.6 of its depth.
    builder.set_slot(boat_wood_slot);
    for (int k = 0; k < thwarts; ++k) {
        const float u     = 0.22f + (0.56f * (static_cast<float>(k) + 0.5f) / static_cast<float>(thwarts));
        const float keel  = lines.get_keel(u);
        const float seat  = keel + (0.62f * (lines.get_gunwale(u) - keel));
        const float reach = std::abs(lines.get_outer(u, (lines.form == Boat_form::rowboat) ? 0.72f : 0.8f).z) - (0.4f * lines.skin);
        const float x     = lines.length * (u - 0.5f);
        add_placed_box(builder, place, glm::vec3{x - (0.035f * lines.length), seat - (0.6f * lines.skin), -reach}, glm::vec3{x + (0.035f * lines.length), seat, reach});
    }
    // Two sleepers on the ground under the hull.
    for (const float side : {-1.0f, 1.0f}) {
        const float x = side * 0.2f * lines.length;
        builder.add_box(glm::vec3{x - (0.02f * lines.length), 0.0f, -0.42f * lines.beam}, glm::vec3{x + (0.02f * lines.length), 1.3f * sleeper, 0.42f * lines.beam});
    }
    builder.write(mesh);
}

} // anonymous namespace

void add_extra_33_36_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape;
        shape.id              = "light.garland";
        shape.kit             = "light";
        shape.display_name    = "Garland";
        shape.description     =
            "Garland along X between its two ends: a wire sagging in a parabola with a bulb, a flag or a paper lantern "
            "hanging at every point along it, and a ribbon (slot cloth) at each end. A garland of flags (slot cloth) "
            "has a lamp on a cord at mid span. The origin is under mid span at the lowest point; the ends are the "
            "connectors. It carries one point light at mid span.";
        shape.tags            = {"decoration", "light", "garland", "size:4x0.5"};
        shape.parameters      = {
            derive_float(make_length_parameter("span", "From end to end along X", 4.0f, Kit_default_scale::modules, 1.0f, 24.0f), 3.0f, 6.0f, 0.5f),
            from_seed_by_default(derive_float(make_share_parameter("sag", "How far the wire's middle hangs under its ends, in spans", 0.1f, 0.05f, 0.15f), 0.05f, 0.15f, 0.01f)),
            derive_int  (make_count_parameter("count", "Bulbs, flags or paper lanterns", 8, garland_count_min, garland_count_max), 6, 12),
            derive_enum (make_enum_parameter ("item",  "Bulbs, flags or paper lanterns", {"bulb", "flag", "paper_lantern"}, 0)),
            make_seed_parameter()
        };
        shape.material_slots   = {"metal", "glow", "cloth"};
        shape.normal_style     = Kit_normal_style::faceted;
        shape.triangle_budget  = 500;
        shape.collision        = Kit_collision::triangle_mesh;
        shape.generator        = generate_garland;
        shape.mirror_planes    = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x};
        shape.get_connectors   = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const Garland_form form{values};
            const float half = 0.5f * form.span;
            add_side_connector(out_connectors, "start", glm::vec3{-half, form.get_wire(half), 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "end",   glm::vec3{ half, form.get_wire(half), 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
        };
        shape.get_light_frames = [](const Kit_parameter_values& values, std::vector<Kit_light_frame>& out_frames) {
            out_frames.push_back(Kit_light_frame{
                .name      = "light",
                .position  = Garland_form{values}.get_light(),
                .direction = glm::vec3{0.0f, -1.0f, 0.0f}
            });
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "traversal.rope_bridge";
        shape.kit             = "traversal";
        shape.display_name    = "Rope bridge";
        shape.description     =
            "Rope bridge along X between its two ends: planks across two ropes that hang in a parabola, under two hand "
            "ropes (slot cloth) on hangers, between two posts at each end. With missing, a seeded share of the planks "
            "lie loose on the next plank and leave a gap. The origin is under mid span at the lowest point; the ends "
            "of the walking surface are the connectors.";
        shape.tags            = {"walkable", "traversal", "bridge", "rope", "size:6x1"};
        shape.parameters      = {
            derive_float(make_length_parameter("span", "From end to end along X", 6.0f, Kit_default_scale::modules, 2.0f, 16.0f), 4.0f, 10.0f, 1.0f),
            // In meters and not derived: the agent fixes the least width and the widest gap at every level length (R2.10).
            make_length_parameter("width", "Size along Z between the hand ropes", 1.0f, Kit_default_scale::meters, 1.0f, 3.0f),
            from_seed_by_default(derive_float(make_share_parameter("sag", "How far the deck's middle hangs under its ends, in spans", 0.06f, 0.03f, 0.1f), 0.03f, 0.1f, 0.01f)),
            make_length_parameter("plank_gap", "The gap between two planks", 0.03f, Kit_default_scale::meters, 0.01f, 0.04f),
            from_seed_by_default(derive_float(make_share_parameter("missing", "Share of planks that lie loose and leave a gap", 0.08f, 0.0f, 0.15f), 0.0f, 0.15f, 0.01f)),
            derive_float(make_length_parameter("rail_height", "Height of the hand ropes over the deck at the ends", 1.0f, Kit_default_scale::meters, 0.6f, 1.5f), 0.9f, 1.2f, 0.1f),
            make_seed_parameter()
        };
        shape.material_slots    = {"wood", "cloth"};
        shape.normal_style      = Kit_normal_style::faceted;
        shape.triangle_budget   = 1010;
        shape.collision         = Kit_collision::triangle_mesh;
        shape.randomness        = Kit_randomness::seeded;
        shape.generator         = generate_rope_bridge;
        shape.check_walkability = check_bridge_walkability;
        shape.get_connectors    = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const Bridge_form form{values};
            const float half = 0.5f * form.span;
            add_side_connector(out_connectors, "start", glm::vec3{-half, form.get_deck(half), 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "end",   glm::vec3{ half, form.get_deck(half), 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "ruin.cracked_floor";
        shape.kit             = "ruin";
        shape.display_name    = "Cracked floor";
        shape.description     =
            "Ruined floor slab with the parameters of the floor slab: seeded straight cuts split it into rows and each "
            "row into fragments with a crack around each. A fragment lies a little sunk and tilted under the slab's "
            "top; a missing one lies deep. Its sides are the slab's.";
        shape.tags            = {"walkable", "floor", "ruin", "tiles", "size:2x2"};
        shape.parameters      = {
            derive_float(make_length_parameter("width",     "Size along X",  2.0f, Kit_default_scale::modules, 0.1f,  64.0f), 1.0f, 4.0f, 0.5f),
            derive_float(make_length_parameter("depth",     "Size along Z",  2.0f, Kit_default_scale::modules, 0.1f,  64.0f), 1.0f, 4.0f, 0.5f),
            derive_float(make_length_parameter("thickness", "Size along Y",  0.2f, Kit_default_scale::meters,  0.01f, 4.0f), 0.1f, 0.4f, 0.05f),
            derive_int  (make_count_parameter ("fragments", "Fragments", 9, cracked_piece_min, cracked_piece_max), 6, 16),
            from_seed_by_default(derive_float(make_angle_parameter("tilt", "The most a fragment tilts", 2.0f, 0.0f, 4.0f), 0.0f, 4.0f, 0.5f)),
            from_seed_by_default(derive_float(make_share_parameter("missing", "Share of fragments that are missing", 0.1f, 0.0f, 0.2f), 0.0f, 0.2f, 0.05f)),
            make_seed_parameter()
        };
        shape.material_slots    = {"floor"};
        shape.normal_style      = Kit_normal_style::faceted;
        shape.triangle_budget   = 120;
        shape.collision         = Kit_collision::triangle_mesh;
        shape.randomness        = Kit_randomness::seeded;
        shape.generator         = generate_cracked_floor;
        shape.check_walkability = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            const float step = get_cracked_step(values);
            if (step > agent.max_climb) {
                return fmt::format("{}: the step {} m between fragments is higher than the agent's climb {} m", values.get_shape()->id, step, agent.max_climb);
            }
            return {};
        };
        shape.get_connectors    = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float thickness = values.get_float("thickness");
            const float half_x    = 0.5f * values.get_float("width");
            const float half_z    = 0.5f * values.get_float("depth");
            add_base_and_top_connectors(out_connectors, thickness);
            add_side_connector(out_connectors, "front", glm::vec3{0.0f, thickness,  half_z}, glm::vec3{ 0.0f, 0.0f,  1.0f});
            add_side_connector(out_connectors, "back",  glm::vec3{0.0f, thickness, -half_z}, glm::vec3{ 0.0f, 0.0f, -1.0f});
            add_side_connector(out_connectors, "left",  glm::vec3{-half_x, thickness, 0.0f}, glm::vec3{-1.0f, 0.0f,  0.0f});
            add_side_connector(out_connectors, "right", glm::vec3{ half_x, thickness, 0.0f}, glm::vec3{ 1.0f, 0.0f,  0.0f});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "harbour.boat";
        shape.kit             = "harbour";
        shape.display_name    = "Boat";
        shape.description     =
            "Open boat along X, its bow toward +X, on two sleepers: a hull of two skins under a gunwale and a top "
            "strake of slot trim, with thwarts across it. Rowboat: a round hull with a transom and a pointed bow. "
            "Skiff: a flat bottom under flared sides. Barge: a flat bottom with raked square ends. The sheer lifts the "
            "ends, and the heel lays the boat over toward +Z.";
        shape.tags            = {"obstacle", "decoration", "harbour", "boat", "size:4x1.4"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("form",   "Rowboat, skiff or barge", {"rowboat", "skiff", "barge"}, 0)),
            from_seed_by_default(derive_float(make_length_parameter("length", "Size along X", 4.0f, Kit_default_scale::meters, 2.0f, 12.0f), 3.0f, 8.0f, 0.5f)),
            derive_float(make_length_parameter("beam",   "Width amidships", 1.4f, Kit_default_scale::meters, 0.8f, 4.0f), 1.2f, 1.8f, 0.1f),
            derive_float(make_share_parameter ("sheer",  "How far the gunwale rises toward the ends", 0.15f, 0.0f, 0.3f), 0.0f, 0.3f, 0.05f),
            derive_int  (make_count_parameter ("thwarts", "Thwarts across the hull", 2, 0, boat_thwart_max), 0, 4),
            from_seed_by_default(derive_float(make_angle_parameter("heel", "How far the boat lies over toward +Z", 0.0f, 0.0f, 25.0f), 0.0f, 25.0f, 5.0f)),
            make_seed_parameter()
        };
        shape.material_slots  = {"wood", "trim"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 520;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.generator       = generate_boat;
        shape.get_connectors  = [](const Kit_parameter_values&, std::vector<Kit_connector>& out_connectors) {
            out_connectors.push_back(make_base_connector());
        };
        shapes.push_back(std::move(shape));
    }
}

} // namespace editor
