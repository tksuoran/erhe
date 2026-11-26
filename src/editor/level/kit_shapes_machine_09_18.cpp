#include "level/kit_shapes.hpp"
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

// The shapes after the first eight of the build order of doc/plans/level_kit_machine_elements.md.
// A mesh's topology follows from its integer values; floats and the seed only move vertices.

namespace editor {

namespace {

constexpr float pi     = std::numbers::pi_v<float>;
constexpr float two_pi = 2.0f * pi;
// Turned surfaces of eight or more segments shade round; hexagons, teeth and steps keep their edges.
constexpr float machine_hard_edge_degrees = 50.0f;
constexpr glm::vec3 x_axis{1.0f, 0.0f, 0.0f};
constexpr glm::vec3 y_axis{0.0f, 1.0f, 0.0f};
constexpr glm::vec3 z_axis{0.0f, 0.0f, 1.0f};

// The unit direction at an angle about Y, from +X toward -Z.
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

// Upright, with its x along a level unit direction.
[[nodiscard]] auto make_turned_placement(const glm::vec3& origin, const glm::vec3& direction) -> Placement
{
    return Placement{.origin = origin, .x = direction, .y = y_axis, .z = glm::cross(direction, y_axis)};
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

[[nodiscard]] auto make_base_connector() -> Kit_connector
{
    return Kit_connector{.name = "base", .position = glm::vec3{0.0f}, .outward = glm::vec3{0.0f, -1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}};
}

// A prism from the level outline at its top down to its copy moved by `depth` (downward); the outline may run
// either way around.
void add_flat_prism(Kit_mesh_builder& builder, std::vector<glm::vec3> outline, const glm::vec3& depth)
{
    // Twice the outline's area seen from above: positive when it runs counter-clockwise.
    float area = 0.0f;
    for (std::size_t i = 0; i < outline.size(); ++i) {
        const glm::vec3& a = outline[i];
        const glm::vec3& b = outline[(i + 1) % outline.size()];
        area += (a.z * b.x) - (a.x * b.z);
    }
    if (area < 0.0f) {
        std::reverse(outline.begin(), outline.end());
    }
    add_prism_between(builder, outline, depth);
}

// A gear tooth from y0 up to y1: its middle on the pitch line at `center` halfway up, standing out along `out`
// (unit, level), leaning `lean` along out x y for each unit up (a helical tooth; 0 for a spur tooth).
// An involute's stand-in: wide at the root, half a pitch wide on the pitch line, narrow at the tip.
void add_gear_tooth(
    Kit_mesh_builder& builder,
    const glm::vec3&  center,
    const glm::vec3&  out,
    const float       module,
    const float       y0,
    const float       y1,
    const float       lean
)
{
    const glm::vec3 along  = glm::cross(y_axis, out);
    const float     root   = -1.4f * module; // a little inside the root line
    const float     tip    = module;
    const float     pitch  = 0.25f * pi * module;
    const float     height = y1 - y0;
    const glm::vec3 top    = glm::vec3{center.x, y1, center.z} + (((0.5f * height) * lean) * along);
    const std::vector<glm::vec3> outline{
        top + (root * out) - ((1.05f * module) * along), top - (pitch * along), top + (tip * out) - ((0.4f * module) * along),
        top + (tip * out) + ((0.4f * module) * along), top + (pitch * along), top + (root * out) + ((1.05f * module) * along)
    };
    add_flat_prism(builder, outline, glm::vec3{0.0f, -height, 0.0f} - ((height * lean) * along));
}

// A point of a wire's path: its middle, and the two unit directions its four corners stand off along: `up`, and
// `side` (the direction of travel crossed with `up`).
class Wire_point
{
public:
    glm::vec3 center{0.0f};
    glm::vec3 up    {0.0f, 1.0f, 0.0f};
    glm::vec3 side  {0.0f, 0.0f, 1.0f};
};

// A wire along its points: a tube of four corners in triangles, between two end faces; its corners stand
// `half_up` off along `up` and `half_side` off along `side`.
void add_wire(Kit_mesh_builder& builder, const std::span<const Wire_point> points, const float half_up, const float half_side)
{
    const uint32_t count = static_cast<uint32_t>(points.size());
    const uint32_t first = static_cast<uint32_t>(builder.get_positions().size());
    for (const Wire_point& point : points) {
        builder.add_vertex(point.center - (half_up * point.up));
        builder.add_vertex(point.center - (half_side * point.side));
        builder.add_vertex(point.center + (half_up * point.up));
        builder.add_vertex(point.center + (half_side * point.side));
    }
    for (uint32_t r = 0; (r + 1) < count; ++r) {
        const uint32_t lower = first + (4 * r);
        const uint32_t upper = lower + 4;
        for (uint32_t i = 0; i < 4; ++i) {
            const uint32_t j = (i + 1) % 4;
            builder.add_triangle(lower + i, lower + j, upper + j);
            builder.add_triangle(lower + i, upper + j, upper + i);
        }
    }
    const uint32_t last = first + (4 * (count - 1));
    builder.add_quad(first + 3, first + 2, first + 1, first);
    builder.add_quad(last, last + 1, last + 2, last + 3);
}

void add_wire(Kit_mesh_builder& builder, const std::span<const Wire_point> points, const float half)
{
    add_wire(builder, points, half, half);
}

// A lathe's placement with its axis along +X from the origin given.
[[nodiscard]] auto make_x_placement(const glm::vec3& origin) -> Placement
{
    return Placement{.origin = origin, .x = y_axis, .y = x_axis, .z = -z_axis};
}

// A round bar along +X from `origin`.
void add_x_bar(Kit_mesh_builder& builder, const glm::vec3& origin, const float radius, const float length, const uint32_t slot)
{
    const std::array<Lathe_ring, 2> rings{{{radius, 0.0f}, {radius, length}}};
    add_lathe(builder, make_x_placement(origin), Lathe{.rings = rings, .segments = 12, .slot = slot});
}

// A ring around the placement's y: from y0 to y1 between the radii `inner` and `outer`.
void add_ring(Kit_mesh_builder& builder, const Placement& place, const float inner, const float outer, const float y0, const float y1, const int segments, const uint32_t slot)
{
    const std::array<Lathe_ring, 4> rings{{{inner, y0}, {outer, y0}, {outer, y1}, {inner, y1}}};
    add_lathe(builder, place, Lathe{.rings = rings, .segments = segments, .bottom = Lathe_cap::loop, .top = Lathe_cap::loop, .slot = slot});
}

// A block under a shaft along X: from the ground up into the shaft.
void add_shaft_stand(Kit_mesh_builder& builder, const float x, const float length, const float axis_y, const float shaft_radius)
{
    builder.add_box(
        glm::vec3{x - (0.5f * length), 0.0f, -1.6f * shaft_radius},
        glm::vec3{x + (0.5f * length), axis_y - (0.3f * shaft_radius), 1.6f * shaft_radius}
    );
}

// A pear cam's distance from its axis at an angle from its nose: the base circle and a nose circle joined by the
// two lines that touch both.
[[nodiscard]] auto get_pear_reach(const float radius, const float lift, const float angle) -> float
{
    const float nose  = 0.45f * radius;
    const float off   = radius + lift - nose;
    const float theta = (angle > pi) ? (two_pi - angle) : angle;
    const float touch = std::acos((radius - nose) / off);
    const float onto  = std::atan2(nose * std::sin(touch), off + (nose * std::cos(touch)));
    if (theta >= touch) {
        return radius;
    }
    if (theta >= onto) {
        return radius / std::cos(touch - theta);
    }
    const float s = off * std::sin(theta);
    return (off * std::cos(theta)) + std::sqrt(std::max((nose * nose) - (s * s), 0.0f));
}

// A plate with a round hole: its outline's point j lies reach[j] from the center at the angle of point j plus
// `turn`, from e1 toward e2; the plate is `thickness` along `axis`, and (e1, e2, axis) is right-handed.
class Bored_plate
{
public:
    glm::vec3              center{0.0f};
    glm::vec3              e1    {1.0f, 0.0f, 0.0f};
    glm::vec3              e2    {0.0f, 0.0f, -1.0f};
    glm::vec3              axis  {0.0f, 1.0f, 0.0f};
    float                  thickness{0.0f};
    float                  hole  {0.0f};
    float                  turn  {0.0f};
    std::span<const float> reach;
};

void add_bored_plate(Kit_mesh_builder& builder, const Bored_plate& plate)
{
    const std::size_t count = plate.reach.size();
    std::array<std::vector<uint32_t>, 2> rim;
    std::array<std::vector<uint32_t>, 2> hole;
    for (std::size_t level = 0; level < 2; ++level) {
        const glm::vec3 center = plate.center + ((static_cast<float>(level) * plate.thickness) * plate.axis);
        for (std::size_t j = 0; j < count; ++j) {
            const float     angle = plate.turn + (two_pi * static_cast<float>(j) / static_cast<float>(count));
            const glm::vec3 out   = (std::cos(angle) * plate.e1) + (std::sin(angle) * plate.e2);
            rim [level].push_back(builder.add_vertex(center + (plate.reach[j] * out)));
            hole[level].push_back(builder.add_vertex(center + (plate.hole * out)));
        }
    }
    for (std::size_t j = 0; j < count; ++j) {
        const std::size_t n = (j + 1) % count;
        builder.add_quad(rim[1][j], rim[1][n], hole[1][n], hole[1][j]);
        builder.add_quad(rim[0][n], rim[0][j], hole[0][j], hole[0][n]);
        builder.add_quad(rim[0][j], rim[0][n], rim[1][n], rim[1][j]);
        builder.add_quad(hole[0][n], hole[0][j], hole[1][j], hole[1][n]);
    }
}

//
// Connecting rod: lying flat, its two eyes' axes along Y: the small end at -X, the big end with its two cap bolts at +X.
//

constexpr uint32_t rod_metal_slot  = 0;
constexpr uint32_t rod_steel_slot  = 1;
constexpr int      rod_small_round = 12; // points around the small eye
constexpr int      rod_big_round   = 16;

class Rod_form
{
public:
    explicit Rod_form(const Kit_parameter_values& values)
        : length   {values.get_float("length")}
        , thickness{values.get_float("thickness")}
    {
        // The eyes stay apart on a short rod.
        small_bore  = std::min(0.5f * values.get_float("small_bore"), 0.1f * length);
        big_bore    = std::min(0.5f * values.get_float("big_bore"), 0.16f * length);
        small_hole  = small_bore / std::cos(pi / static_cast<float>(rod_small_round));
        big_hole    = big_bore / std::cos(pi / static_cast<float>(rod_big_round));
        small_outer = 1.8f * small_bore;
        big_outer   = 1.6f * big_bore;
    }

    float length     {0.0f}; // between the two eyes' axes
    float thickness  {0.0f};
    float small_bore {0.0f}; // radii: a pin of the bore's diameter fits between the hole's flats
    float big_bore   {0.0f};
    float small_hole {0.0f}; // the holes' corners
    float big_hole   {0.0f};
    float small_outer{0.0f};
    float big_outer  {0.0f};
};

// How far a ring of `n` points (the first on +X) of the radius reaches toward +X at the distance z beside its axis.
[[nodiscard]] auto get_ring_reach(const int n, const float radius, const float z) -> float
{
    float reach = 0.0f;
    for (int i = 0; i < n; ++i) {
        const glm::vec2 a = radius * get_kit_circle_point(i, n);
        const glm::vec2 b = radius * get_kit_circle_point((i + 1) % n, n);
        if ((std::min(a.y, b.y) <= z) && (z <= std::max(a.y, b.y)) && (a.y != b.y)) {
            reach = std::max(reach, a.x + ((b.x - a.x) * ((z - a.y) / (b.y - a.y))));
        }
    }
    return reach;
}

void generate_connecting_rod(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Rod_form rod{values};
    const float half = 0.5f * rod.length;
    const float t    = rod.thickness;
    Kit_mesh_builder builder;
    add_ring(builder, Placement{.origin = glm::vec3{-half, 0.0f, 0.0f}}, rod.small_hole, rod.small_outer, 0.0f, t, rod_small_round, rod_metal_slot);
    add_ring(builder, Placement{.origin = glm::vec3{ half, 0.0f, 0.0f}}, rod.big_hole, rod.big_outer, 0.0f, t, rod_big_round, rod_metal_slot);
    builder.set_slot(rod_metal_slot);
    // The shank: a web between two flanges, from inside one eye's wall to inside the other's.
    const float x0 = -half + (0.9f * rod.small_outer);
    const float x1 = half - (0.9f * rod.big_outer);
    const float z  = 0.75f * rod.small_outer;
    builder.add_box(glm::vec3{x0, 0.3f * t, -0.8f * z}, glm::vec3{x1, 0.7f * t, 0.8f * z});
    // The flanges start at the eyes' outlines at their outer sides, so their ends lie in the eyes; beside an eye
    // too small to reach them, where the web starts.
    const float small_reach = get_ring_reach(rod_small_round, rod.small_outer, z);
    const float big_reach   = get_ring_reach(rod_big_round, rod.big_outer, z);
    const float f0 = -half + ((small_reach > 0.0f) ? small_reach : (0.9f * rod.small_outer));
    const float f1 = half - ((big_reach > 0.0f) ? big_reach : (0.9f * rod.big_outer));
    builder.add_box(glm::vec3{f0, 0.1f * t, -z}, glm::vec3{f1, 0.9f * t, -0.75f * z});
    builder.add_box(glm::vec3{f0, 0.1f * t,  0.75f * z}, glm::vec3{f1, 0.9f * t, z});
    // The cap's two bolts beside the big end.
    builder.set_slot(rod_steel_slot);
    for (const float side : {-1.0f, 1.0f}) {
        const float zc = side * 1.05f * rod.big_outer;
        builder.add_box(
            glm::vec3{half - (0.5f * rod.big_outer), 0.15f * t, zc - (0.22f * rod.big_outer)},
            glm::vec3{half + (0.5f * rod.big_outer), 0.85f * t, zc + (0.22f * rod.big_outer)}
        );
    }
    builder.write(mesh);
}

//
// Crankshaft: along X on two stands: main journals, and for each throw two webs and a crank pin.
//

constexpr uint32_t crank_metal_slot = 0;
constexpr uint32_t crank_steel_slot = 1;
constexpr int      crank_throw_max  = 6;

class Crank_form
{
public:
    explicit Crank_form(const Kit_parameter_values& values)
        : throws {std::clamp(values.get_int("throws"), 1, crank_throw_max)}
        , cross  {values.get_enum("phase") == "cross"}
        , radius {0.5f * values.get_float("stroke")}
        , journal{values.get_float("journal")}
        , web    {values.get_float("web")}
    {
        main   = 1.5f * journal;
        pin    = journal;
        length = (static_cast<float>(throws + 1) * main) + (static_cast<float>(throws) * ((2.0f * web) + pin));
        axis_y = std::hypot(radius + (0.8f * journal), 0.8f * journal) + (0.25f * journal);
    }

    // The unit direction from the axis to throw k's pin: the first throw's pin stands up.
    [[nodiscard]] auto get_throw(const int k) const -> glm::vec3
    {
        const float angle = (cross ? (0.5f * pi) : pi) * static_cast<float>(k);
        return glm::vec3{0.0f, std::cos(angle), std::sin(angle)};
    }
    // Where throw k's first web begins along X.
    [[nodiscard]] auto get_throw_x(const int k) const -> float
    {
        return (-0.5f * length) + main + (static_cast<float>(k) * (main + (2.0f * web) + pin));
    }

    int   throws {1};
    bool  cross  {false};
    float radius {0.0f}; // of the crank: half the stroke
    float journal{0.0f}; // diameter of the journals and the pins
    float web    {0.0f}; // thickness of a web
    float main   {0.0f}; // length of a main journal
    float pin    {0.0f}; // length of a crank pin
    float length {0.0f};
    float axis_y {0.0f};
};

void generate_crankshaft(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Crank_form crank{values};
    const float rj = 0.5f * crank.journal;
    Kit_mesh_builder builder;
    for (int k = 0; k <= crank.throws; ++k) {
        // A main journal, a little into the webs beside it.
        const float x0 = (k == 0) ? (-0.5f * crank.length) : (crank.get_throw_x(k) - crank.main - (0.1f * crank.web));
        const float x1 = (k == crank.throws) ? (0.5f * crank.length) : (crank.get_throw_x(k) + (0.1f * crank.web));
        add_x_bar(builder, glm::vec3{x0, crank.axis_y, 0.0f}, rj, x1 - x0, crank_steel_slot);
    }
    for (int k = 0; k < crank.throws; ++k) {
        const glm::vec3 out = crank.get_throw(k);
        const float     x   = crank.get_throw_x(k);
        add_x_bar(builder, glm::vec3{x + (0.9f * crank.web), crank.axis_y, 0.0f} + (crank.radius * out), rj, crank.pin + (0.2f * crank.web), crank_steel_slot);
        builder.set_slot(crank_metal_slot);
        for (const float at : {x, x + crank.web + crank.pin}) {
            // A web from its counterweight past the axis out to the pin.
            const Placement place{.origin = glm::vec3{at, crank.axis_y, 0.0f}, .x = x_axis, .y = out, .z = glm::cross(x_axis, out)};
            add_placed_box(
                builder, place,
                glm::vec3{0.0f, -((0.5f * crank.radius) + (0.8f * crank.journal)), -0.8f * crank.journal},
                glm::vec3{crank.web, crank.radius + (0.8f * crank.journal), 0.8f * crank.journal}
            );
        }
    }
    builder.set_slot(crank_metal_slot);
    for (const float side : {-1.0f, 1.0f}) {
        add_shaft_stand(builder, side * 0.5f * (crank.length - crank.main), 0.5f * crank.main, crank.axis_y, rj);
    }
    builder.write(mesh);
}

//
// Follower: standing on its contact: a knife edge, a roller in a fork or a flat face, under a stem in a guide sleeve.
//

constexpr uint32_t follower_steel_slot = 0;
constexpr uint32_t follower_metal_slot = 1;

// Where the stem begins over the contact, for the stem's diameter.
[[nodiscard]] auto get_follower_head(const std::string_view form, const float d) -> float
{
    if (form == "roller") {
        return 2.2f * d;
    }
    return (form == "flat") ? (0.27f * d) : (1.4f * d);
}

void generate_follower(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const std::string_view form = values.get_enum("form");
    const float            d    = values.get_float("diameter");
    const float            stem = values.get_float("stem");
    const float            head = get_follower_head(form, d);
    Kit_mesh_builder builder;
    if (form == "roller") {
        // The roller's axis lies along Z; a side of its ring is on the ground, around the origin.
        const Placement place{.origin = glm::vec3{0.0f, d * std::cos(pi / 12.0f), 0.0f}, .x = -y_axis, .y = z_axis, .z = -x_axis};
        const std::array<Lathe_ring, 2> rings{{{d, -0.4f * d}, {d, 0.4f * d}}};
        add_lathe(builder, place, Lathe{.rings = rings, .segments = 12, .phase = Lathe_phase::side, .slot = follower_steel_slot});
        // The axle through the roller, from one fork plate's outer face to the other's.
        const std::array<Lathe_ring, 2> axle{{{0.2f * d, -0.7f * d}, {0.2f * d, 0.7f * d}}};
        add_lathe(builder, place, Lathe{.rings = axle, .segments = 8, .slot = follower_metal_slot});
        builder.set_slot(follower_metal_slot);
        for (const float side : {-1.0f, 1.0f}) {
            const float z0 = side * 0.45f * d;
            const float z1 = side * 0.7f * d;
            builder.add_box(glm::vec3{-0.35f * d, 0.7f * d, std::min(z0, z1)}, glm::vec3{0.35f * d, 2.3f * d, std::max(z0, z1)});
        }
        const std::array<Lathe_ring, 2> flange{{{0.85f * d, head}, {0.85f * d, head + (0.3f * d)}}};
        add_lathe(builder, Placement{}, Lathe{.rings = flange, .segments = 10, .phase = Lathe_phase::side, .slot = follower_steel_slot});
    } else if (form == "flat") {
        const std::array<Lathe_ring, 2> rings{{{1.5f * d, 0.0f}, {1.5f * d, 0.3f * d}}};
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = 12, .phase = Lathe_phase::side, .slot = follower_steel_slot});
    } else {
        // A wedge whose edge, a narrow land around the origin, lies along Z on the ground.
        builder.set_slot(follower_steel_slot);
        const std::array<glm::vec3, 4> front{{
            {-0.6f * d, 1.5f * d, 0.6f * d}, {-0.05f * d, 0.0f, 0.6f * d}, {0.05f * d, 0.0f, 0.6f * d}, {0.6f * d, 1.5f * d, 0.6f * d}
        }};
        add_prism_between(builder, front, glm::vec3{0.0f, 0.0f, -1.2f * d});
    }
    {
        const std::array<Lathe_ring, 2> rings{{{0.5f * d, head}, {0.5f * d, head + stem}}};
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = 10, .slot = follower_steel_slot});
    }
    // The sleeve's bore lies a little inside the stem, so the sleeve holds it.
    add_ring(builder, Placement{}, 0.48f * d, d, head + (0.45f * stem), head + (0.75f * stem), 10, follower_metal_slot);
    builder.write(mesh);
}

//
// Camshaft: a shaft along X on two stands, with pear lobes at stepped angles.
//

constexpr uint32_t camshaft_metal_slot = 0;
constexpr uint32_t camshaft_steel_slot = 1;
constexpr int      camshaft_lobe_max   = 8;
constexpr int      camshaft_points     = 16; // around a lobe

void generate_camshaft(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const int   lobes  = std::clamp(values.get_int("lobes"), 1, camshaft_lobe_max);
    const float length = values.get_float("length");
    const float radius = values.get_float("radius");
    const float lift   = values.get_float("lift");
    const float step   = glm::radians(values.get_float("step"));
    const float shaft  = 0.6f * radius;
    const float axis_y = (1.3f * radius) + lift;
    const float cell   = length / static_cast<float>(lobes + 1);
    const float width  = 0.4f * cell;
    Kit_mesh_builder builder;
    add_x_bar(builder, glm::vec3{-0.5f * length, axis_y, 0.0f}, shaft, length, camshaft_steel_slot);
    std::array<float, camshaft_points> reach{};
    for (std::size_t j = 0; j < reach.size(); ++j) {
        reach[j] = get_pear_reach(radius, lift, two_pi * static_cast<float>(j) / static_cast<float>(camshaft_points));
    }
    builder.set_slot(camshaft_steel_slot);
    for (int k = 0; k < lobes; ++k) {
        const float x = (-0.5f * length) + (cell * static_cast<float>(k + 1)) - (0.5f * width);
        add_bored_plate(builder, Bored_plate{
            .center = glm::vec3{x, axis_y, 0.0f}, .e1 = y_axis, .e2 = z_axis, .axis = x_axis,
            .thickness = width, .hole = 0.9f * shaft, .turn = step * static_cast<float>(k), .reach = reach
        });
    }
    builder.set_slot(camshaft_metal_slot);
    for (const float side : {-1.0f, 1.0f}) {
        add_shaft_stand(builder, side * 0.44f * length, 0.05f * length, axis_y, shaft);
    }
    builder.write(mesh);
}

//
// Bevel gear: lying flat: teeth on a cone that narrows upward, for shafts at an angle.
//

constexpr uint32_t bevel_metal_slot = 0;
constexpr int      bevel_tooth_min  = 8;
constexpr int      bevel_tooth_max  = 32;

class Bevel_form
{
public:
    explicit Bevel_form(const Kit_parameter_values& values)
        : teeth {std::clamp(values.get_int("teeth"), bevel_tooth_min, bevel_tooth_max)}
        , module{values.get_float("module")}
        , cone  {glm::radians(values.get_float("cone"))}
    {
        pitch  = 0.5f * module * static_cast<float>(teeth);
        // The teeth's feet, 1.4 modules inside the pitch cone, stand over the ground.
        foot   = (0.6f * module) + (0.1f * values.get_float("face_width")) + ((1.1f * module) * std::sin(cone));
        // The teeth's small end: where the face width along the cone ends, at least 0.4 of the way to the apex.
        share  = std::clamp(1.0f - ((values.get_float("face_width") * std::sin(cone)) / pitch), 0.4f, 0.95f);
        apex   = foot + (pitch / std::tan(cone));
        // The body's cone is the root cone, 1.25 modules inside the pitch cone, with the same apex.
        root_r = pitch - ((1.25f * module) * std::cos(cone));
        root_y = foot - ((1.25f * module) * std::sin(cone));
        top    = apex + (share * (root_y - apex));
        bore   = std::min(0.5f * values.get_float("bore"), 0.5f * share * root_r);
    }

    int   teeth {16};
    float module{0.0f};
    float cone  {0.0f}; // between the axis and the pitch cone
    float pitch {0.0f}; // radius of the pitch circle at the large end
    float foot  {0.0f}; // height of the large end's pitch circle
    float share {0.0f}; // the small end's size in large ends
    float apex  {0.0f}; // height of the cone's apex
    float root_r{0.0f}; // the root cone at the large end
    float root_y{0.0f};
    float top   {0.0f}; // of the body, where the root cone ends at the small end
    float bore  {0.0f};
};

void generate_bevel_gear(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Bevel_form bevel{values};
    const float m = bevel.module;
    Kit_mesh_builder builder;
    {
        const std::array<Lathe_ring, 5> rings{{
            {bevel.bore, 0.0f}, {bevel.root_r, 0.0f}, {bevel.root_r, bevel.root_y}, {bevel.share * bevel.root_r, bevel.top}, {bevel.bore, bevel.top}
        }};
        add_lathe(builder, Placement{}, Lathe{
            .rings = rings, .segments = std::clamp(2 * bevel.teeth, 16, 48), .bottom = Lathe_cap::loop, .top = Lathe_cap::loop, .slot = bevel_metal_slot
        });
    }
    builder.set_slot(bevel_metal_slot);
    const glm::vec3 apex{0.0f, bevel.apex, 0.0f};
    for (int i = 0; i < bevel.teeth; ++i) {
        // A tooth narrows toward the cone's apex; its large end is a trapezoid across the cone, from a foot in the
        // body to a tip a module outside the pitch cone.
        const float     angle  = (two_pi * (static_cast<float>(i) - 0.25f)) / static_cast<float>(bevel.teeth);
        const glm::vec3 out    = around_y(angle);
        const glm::vec3 along  = glm::cross(y_axis, out);
        const glm::vec3 normal = (std::cos(bevel.cone) * out) + (std::sin(bevel.cone) * y_axis);
        const glm::vec3 center = (bevel.pitch * out) + (bevel.foot * y_axis);
        const std::array<glm::vec3, 4> large{{
            center - ((1.05f * m) * along) - ((1.4f * m) * normal), center + ((1.05f * m) * along) - ((1.4f * m) * normal),
            center + ((0.4f * m) * along) + (m * normal),           center - ((0.4f * m) * along) + (m * normal)
        }};
        std::array<uint32_t, 4> b{};
        std::array<uint32_t, 4> s{};
        for (std::size_t c = 0; c < 4; ++c) {
            b[c] = builder.add_vertex(large[c]);
            s[c] = builder.add_vertex(apex + (bevel.share * (large[c] - apex)));
        }
        builder.add_quad(b[0], b[1], b[2], b[3]);
        builder.add_quad(s[3], s[2], s[1], s[0]);
        for (std::size_t c = 0; c < 4; ++c) {
            const std::size_t n = (c + 1) % 4;
            builder.add_quad(b[c], s[c], s[n], b[n]);
        }
    }
    builder.write(mesh);
}

//
// Worm: a shaft along X with a helical thread, lying on its thread; or the worm wheel it drives, lying flat.
//

constexpr uint32_t worm_metal_slot = 0;
constexpr uint32_t worm_steel_slot = 1;
constexpr int      worm_stretches  = 12; // of the thread to a turn
constexpr int      worm_turn_min   = 2;
constexpr int      worm_turn_max   = 6;
constexpr int      worm_start_max  = 3;
constexpr int      worm_tooth_min  = 16;
constexpr int      worm_tooth_max  = 40;

class Worm_form
{
public:
    explicit Worm_form(const Kit_parameter_values& values)
        : wheel {values.get_enum("form") == "wheel"}
        , starts{std::clamp(values.get_int("starts"), 1, worm_start_max)}
        , turns {std::clamp(values.get_int("turns"), worm_turn_min, worm_turn_max)}
        , teeth {std::clamp(values.get_int("teeth"), worm_tooth_min, worm_tooth_max)}
        , module{values.get_float("module")}
    {
        radius = std::max(0.5f * values.get_float("diameter"), 1.5f * module);
        half   = 0.5f * module;
        lead   = pi * module * static_cast<float>(starts);
        length = lead * static_cast<float>(turns);
        // The thread reaches a module outside the pitch cylinder and 1.25 modules inside it, like a rack's tooth.
        depth  = 1.125f * module;
        middle = radius - (0.125f * module);
        // Over the thread's inner chords and under a wheel's tooth tips, so a lying worm rests on its thread.
        shaft  = (radius - (1.15f * module)) * std::cos(pi / static_cast<float>(worm_stretches));
        axis_y = (radius + module) * std::cos(pi / static_cast<float>(worm_stretches));
        // The thread crosses the top a quarter of the axial pitch along +X from a gap's middle, so a joined wheel's
        // tooth, a quarter pitch beside its mesh connector, stands in that gap.
        const float s = static_cast<float>(starts);
        float phase = 0.25f - (s * (11.0f / 24.0f)) + (0.5f * s * static_cast<float>(turns));
        phase -= std::round(phase);
        shift  = pi * module * phase;
        reach  = (0.5f * length) + (2.0f * shaft) + std::abs(shift);
        pitch  = 0.5f * module * static_cast<float>(teeth);
        width  = 1.2f * radius;
        lean   = lead / (two_pi * radius);
    }

    bool  wheel {false};
    int   starts{1};
    int   turns {4};
    int   teeth {24};
    float module{0.0f};
    float radius{0.0f}; // of the worm's pitch cylinder
    float half  {0.0f}; // half the thread's width along the axis
    float depth {0.0f}; // half the thread's height across the axis
    float middle{0.0f}; // radius of the thread's middle
    float lead  {0.0f}; // the thread's advance in a turn
    float length{0.0f}; // of the threaded part
    float shaft {0.0f}; // radius of the shaft under the thread
    float axis_y{0.0f}; // height of a lying worm's axis
    float shift {0.0f}; // of the thread along X from the middle
    float reach {0.0f}; // of the shaft's ends from the middle
    float pitch {0.0f}; // radius of the wheel's pitch circle
    float width {0.0f}; // face width of the wheel
    float lean  {0.0f}; // of the wheel's teeth: the thread's lead over the pitch circumference
};

void generate_worm(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Worm_form worm{values};
    Kit_mesh_builder builder;
    if (worm.wheel) {
        const float root = worm.pitch - (1.25f * worm.module);
        const float bore = 0.15f * root;
        add_ring(builder, Placement{}, bore, root, 0.0f, worm.width, std::clamp(2 * worm.teeth, 16, 48), worm_metal_slot);
        // A bush in the bore.
        add_ring(builder, Placement{}, 0.7f * bore, 1.4f * bore, 0.0f, 1.25f * worm.width, 12, worm_steel_slot);
        builder.set_slot(worm_metal_slot);
        for (int i = 0; i < worm.teeth; ++i) {
            const glm::vec3 out = around_y((two_pi * (static_cast<float>(i) - 0.25f)) / static_cast<float>(worm.teeth));
            add_gear_tooth(builder, worm.pitch * out, out, worm.module, 0.0f, worm.width, worm.lean);
        }
    } else {
        add_x_bar(builder, glm::vec3{-worm.reach, worm.axis_y, 0.0f}, worm.shaft, 2.0f * worm.reach, worm_steel_slot);
        builder.set_slot(worm_metal_slot);
        const int total = worm.turns * worm_stretches;
        for (int start = 0; start < worm.starts; ++start) {
            // No point of a turn lies straight under the axis, so the worm rests on two lines.
            std::vector<Wire_point> points;
            for (int k = 0; k <= total; ++k) {
                const float     turn  = static_cast<float>(k) / static_cast<float>(worm_stretches);
                const float     angle = two_pi * (turn + (0.5f / static_cast<float>(worm_stretches)) + (static_cast<float>(start) / static_cast<float>(worm.starts)));
                const glm::vec3 out{0.0f, -std::cos(angle), -std::sin(angle)};
                const float     x     = (-0.5f * worm.length) + worm.shift + (worm.lead * turn);
                points.push_back(Wire_point{.center = glm::vec3{x, worm.axis_y, 0.0f} + (worm.middle * out), .up = x_axis, .side = out});
            }
            add_wire(builder, points, worm.half, worm.depth);
        }
    }
    builder.write(mesh);
}

//
// Pulley: lying flat: a belt pulley with V grooves around it, or a chain sprocket on a hub.
//

constexpr uint32_t pulley_metal_slot = 0;
constexpr int      pulley_groove_max = 4;
constexpr int      pulley_tooth_min  = 8;
constexpr int      pulley_tooth_max  = 32;

void generate_pulley(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const bool  sprocket = (values.get_enum("form") == "sprocket");
    const float radius   = 0.5f * values.get_float("diameter");
    const float width    = values.get_float("width");
    const int   grooves  = std::clamp(values.get_int("grooves"), 1, pulley_groove_max);
    const int   teeth    = std::clamp(values.get_int("teeth"), pulley_tooth_min, pulley_tooth_max);
    Kit_mesh_builder builder;
    if (sprocket) {
        // The pitch circle is the diameter: a thin toothed plate under a hub.
        const float module = (2.0f * radius) / static_cast<float>(teeth);
        const float root   = radius - (1.25f * module);
        const float bore   = std::min(0.5f * values.get_float("bore"), 0.3f * root);
        const float hub    = std::min(2.0f * bore, 0.6f * root);
        const float plate  = 0.3f * width;
        const std::array<Lathe_ring, 6> rings{{{bore, 0.0f}, {root, 0.0f}, {root, plate}, {hub, plate}, {hub, width}, {bore, width}}};
        add_lathe(builder, Placement{}, Lathe{
            .rings = rings, .segments = std::clamp(2 * teeth, 16, 48), .bottom = Lathe_cap::loop, .top = Lathe_cap::loop, .slot = pulley_metal_slot
        });
        builder.set_slot(pulley_metal_slot);
        for (int i = 0; i < teeth; ++i) {
            const glm::vec3 out = around_y((two_pi * (static_cast<float>(i) - 0.25f)) / static_cast<float>(teeth));
            add_gear_tooth(builder, radius * out, out, module, 0.0f, plate, 0.0f);
        }
    } else {
        const float bore  = std::min(0.5f * values.get_float("bore"), 0.4f * radius);
        const float cell  = width / static_cast<float>(grooves);
        const float depth = std::min(0.35f * cell, 0.4f * (radius - bore));
        std::vector<Lathe_ring> rings;
        rings.push_back(Lathe_ring{bore, 0.0f});
        rings.push_back(Lathe_ring{radius, 0.0f});
        for (int k = 0; k < grooves; ++k) {
            const float y = cell * static_cast<float>(k);
            rings.push_back(Lathe_ring{radius, y + (0.15f * cell)});
            rings.push_back(Lathe_ring{radius - depth, y + (0.5f * cell)});
            rings.push_back(Lathe_ring{radius, y + (0.85f * cell)});
        }
        rings.push_back(Lathe_ring{radius, width});
        rings.push_back(Lathe_ring{bore, width});
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = 24, .bottom = Lathe_cap::loop, .top = Lathe_cap::loop, .slot = pulley_metal_slot});
    }
    builder.write(mesh);
}

//
// Washer: a flat ring, a split lock washer that rises around its turn, or a ring with teeth around it.
//

constexpr uint32_t washer_steel_slot = 0;
constexpr int      washer_stretches  = 12;
constexpr int      washer_flat       = 7;     // stretches of a lock washer that lie on the ground
constexpr int      washer_tooth_min  = 6;
constexpr int      washer_tooth_max  = 16;
constexpr float    washer_section    = 2.5f;  // a lock washer's section in thicknesses
constexpr float    washer_rise       = 0.6f;  // how far its end stands over its start, in sections

// The washer's top over the ground.
[[nodiscard]] auto get_washer_top(const Kit_parameter_values& values) -> float
{
    const float t = values.get_float("thickness");
    return (values.get_enum("form") == "lock") ? ((1.0f + washer_rise) * washer_section * t) : t;
}

void generate_washer(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const std::string_view form  = values.get_enum("form");
    const float            d     = values.get_float("diameter");
    const float            t     = values.get_float("thickness");
    const int              teeth = std::clamp(values.get_int("teeth"), washer_tooth_min, washer_tooth_max);
    Kit_mesh_builder builder;
    if (form == "lock") {
        builder.set_slot(washer_steel_slot);
        const float half   = 0.5f * washer_section * t;
        const float center = std::max(0.75f * d, (0.55f * d) + half); // the bore clears a bolt of the diameter
        std::vector<Wire_point> points;
        for (int k = 0; k <= washer_stretches; ++k) {
            const glm::vec3 out  = around_y(glm::radians(340.0f) * static_cast<float>(k) / static_cast<float>(washer_stretches));
            const float     rise = (k <= washer_flat) ? 0.0f : (static_cast<float>(k - washer_flat) / static_cast<float>(washer_stretches - washer_flat));
            points.push_back(Wire_point{.center = (center * out) + ((half + (rise * washer_rise * 2.0f * half)) * y_axis), .up = y_axis, .side = out});
        }
        add_wire(builder, points, half);
    } else {
        add_ring(builder, Placement{}, 0.55f * d, 1.1f * d, 0.0f, t, 16, washer_steel_slot);
        if (form == "toothed") {
            builder.set_slot(washer_steel_slot);
            const float half = (0.3f * pi * 1.1f * d) / static_cast<float>(teeth);
            for (int k = 0; k < teeth; ++k) {
                const Placement place = make_turned_placement(glm::vec3{0.0f}, around_y(two_pi * (static_cast<float>(k) + 0.25f) / static_cast<float>(teeth)));
                add_placed_box(builder, place, glm::vec3{d, 0.1f * t, -half}, glm::vec3{1.4f * d, 0.9f * t, half});
            }
        }
    }
    builder.write(mesh);
}

//
// Cylinder: a standing sleeve: a liner inside a jacket with a foot flange and cooling fins.
//

constexpr uint32_t cylinder_metal_slot = 0;
constexpr uint32_t cylinder_steel_slot = 1;
constexpr int      cylinder_fin_max    = 10;
constexpr int      cylinder_round      = 16;

void generate_cylinder(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float bore   = 0.515f * values.get_float("bore"); // a piston of the bore's diameter slides in it
    const float height = values.get_float("height");
    const float wall   = values.get_float("wall");
    const int   fins   = std::clamp(values.get_int("fins"), 0, cylinder_fin_max);
    const float inner  = bore + (0.4f * wall);
    const float outer  = bore + wall;
    Kit_mesh_builder builder;
    add_ring(builder, Placement{}, bore, bore + (0.6f * wall), 0.0f, height, cylinder_round, cylinder_steel_slot);
    std::vector<Lathe_ring> rings;
    rings.push_back(Lathe_ring{inner, 0.0f});
    rings.push_back(Lathe_ring{outer + (1.5f * wall), 0.0f});
    rings.push_back(Lathe_ring{outer + (1.5f * wall), 0.08f * height});
    rings.push_back(Lathe_ring{outer, 0.08f * height});
    const float low  = 0.14f * height;
    const float zone = 0.8f * height;
    for (int k = 0; k < fins; ++k) {
        const float cell = zone / static_cast<float>(fins);
        const float y    = low + (cell * (static_cast<float>(k) + 0.5f));
        const float a    = std::min(0.25f * cell, 0.02f * height);
        rings.push_back(Lathe_ring{outer, y - a});
        rings.push_back(Lathe_ring{outer + (1.2f * wall), y - a});
        rings.push_back(Lathe_ring{outer + (1.2f * wall), y + a});
        rings.push_back(Lathe_ring{outer, y + a});
    }
    rings.push_back(Lathe_ring{outer, 0.97f * height});
    rings.push_back(Lathe_ring{inner, 0.97f * height});
    add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = cylinder_round, .bottom = Lathe_cap::loop, .top = Lathe_cap::loop, .slot = cylinder_metal_slot});
    builder.write(mesh);
}

//
// Pillow block: a housing around a shaft along Z, on a pedestal and a base with bolts.
//

constexpr uint32_t pillow_metal_slot = 0;
constexpr uint32_t pillow_steel_slot = 1;

void generate_pillow_block(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float height = values.get_float("height");
    const float width  = values.get_float("width");
    const float depth  = values.get_float("depth");
    const float bore   = std::min(0.5f * values.get_float("bore"), 0.5f * height);
    const float outer  = bore + (0.3f * height);
    const int   pairs  = (values.get_enum("bolts") == "four") ? 2 : 1;
    const float foot   = 0.25f * height;
    Kit_mesh_builder builder;
    builder.set_slot(pillow_metal_slot);
    builder.add_box(glm::vec3{-0.5f * width, 0.0f, -0.5f * depth}, glm::vec3{0.5f * width, foot, 0.5f * depth});
    // The pedestal stops under the bush, out of the shaft's way.
    builder.add_box(glm::vec3{-0.8f * outer, 0.8f * foot, -0.4f * depth}, glm::vec3{0.8f * outer, height - (1.1f * bore), 0.4f * depth});
    // The housing and the bush in it, around Z; the bush takes a shaft of the bore.
    const Placement around_z{.origin = glm::vec3{0.0f, height, -0.5f * depth}, .x = x_axis, .y = z_axis, .z = -y_axis};
    add_ring(builder, around_z, 1.1f * bore, outer, 0.05f * depth, 0.95f * depth, 16, pillow_metal_slot);
    add_ring(builder, around_z, bore / std::cos(pi / 12.0f), 1.15f * bore, 0.0f, depth, 12, pillow_steel_slot); // flats at the bore
    // The bolts' heads on the base's two ends.
    const float head = std::min(0.05f * width, 0.2f * depth);
    for (const float side : {-1.0f, 1.0f}) {
        for (int k = 0; k < pairs; ++k) {
            const float z = (pairs == 1) ? 0.0f : ((k == 0) ? (-0.25f * depth) : (0.25f * depth));
            const std::array<Lathe_ring, 2> rings{{{head, 0.9f * foot}, {head, foot + (0.7f * head)}}};
            add_lathe(builder, Placement{.origin = glm::vec3{side * 0.4f * width, 0.0f, z}}, Lathe{.rings = rings, .segments = 6, .slot = pillow_steel_slot});
        }
    }
    builder.write(mesh);
}

} // anonymous namespace

void add_machine_09_18_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape;
        shape.id              = "machine.connecting_rod";
        shape.kit             = "machine";
        shape.display_name    = "Connecting rod";
        shape.description     =
            "Connecting rod lying flat, its eyes' axes along Y: the small end at -X, an I-section shank, and the big "
            "end with its cap's two bolts (slot steel) at +X. The small end joins a piston's pin connector, the big "
            "end a crankshaft's pin connector.";
        shape.tags            = {"decoration", "machine", "piston", "rod", "size:0.7x0.06"};
        shape.parameters      = {
            derive_float(make_length_parameter("length",     "Between the two eyes' axes", 0.5f, Kit_default_scale::meters, 0.1f, 4.0f), 0.4f, 0.7f, 0.1f),
            derive_float(make_length_parameter("small_bore", "Diameter of the small end's bore: a piston's wrist pin", 0.05f, Kit_default_scale::meters, 0.01f, 1.0f), 0.04f, 0.06f, 0.01f),
            derive_float(make_length_parameter("big_bore",   "Diameter of the big end's bore: a crank pin", 0.08f, Kit_default_scale::meters, 0.02f, 1.0f), 0.08f, 0.1f, 0.01f),
            derive_float(make_length_parameter("thickness",  "Thickness along Y", 0.06f, Kit_default_scale::meters, 0.01f, 1.0f), 0.04f, 0.08f, 0.02f),
            make_seed_parameter()
        };
        shape.material_slots  = {"metal", "steel"};
        shape.normal_style            = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = machine_hard_edge_degrees;
        shape.default_shading = Kit_shading::smooth;
        shape.triangle_budget = 400;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.origin_rule     = Kit_origin_rule::base_contains_origin; // the big end reaches further
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::z};
        shape.generator       = generate_connecting_rod;
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const Rod_form rod{values};
            out_connectors.push_back(make_base_connector());
            // The eyes' middles: outward along the rod, up along the eye's axis.
            add_side_connector(out_connectors, "small_end", glm::vec3{-0.5f * rod.length, 0.5f * rod.thickness, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "big_end",   glm::vec3{ 0.5f * rod.length, 0.5f * rod.thickness, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "machine.crankshaft";
        shape.kit             = "machine";
        shape.display_name    = "Crankshaft";
        shape.description     =
            "Crankshaft along X on two stands: main journals, and for each throw two webs with counterweights and a "
            "crank pin half the stroke off the axis. The first throw's pin stands up; the next throws follow half a "
            "turn apart (flat) or a quarter turn apart (cross). A connecting rod's big end joins the pin connector.";
        shape.tags            = {"decoration", "obstacle", "machine", "piston", "crankshaft", "size:0.5x0.4"};
        shape.parameters      = {
            derive_int  (make_count_parameter ("throws",  "Throws", 2, 1, crank_throw_max), 1, 4),
            derive_float(make_length_parameter("stroke",  "Twice the crank pins' distance from the axis", 0.2f, Kit_default_scale::meters, 0.05f, 2.0f), 0.15f, 0.3f, 0.05f),
            derive_float(make_length_parameter("journal", "Diameter of the journals and the pins", 0.08f, Kit_default_scale::meters, 0.02f, 1.0f), 0.08f, 0.1f, 0.01f),
            derive_float(make_length_parameter("web",     "Thickness of a web", 0.04f, Kit_default_scale::meters, 0.01f, 0.5f), 0.03f, 0.05f, 0.01f),
            derive_enum (make_enum_parameter  ("phase",   "Throws half a turn apart, or a quarter turn", {"flat", "cross"}, 0)),
            make_seed_parameter()
        };
        shape.material_slots  = {"metal", "steel"};
        shape.normal_style            = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = machine_hard_edge_degrees;
        shape.default_shading = Kit_shading::smooth;
        shape.triangle_budget = 400;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.origin_rule     = Kit_origin_rule::base_contains_origin; // the stands' feet
        shape.generator       = generate_crankshaft;
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const Crank_form crank{values};
            out_connectors.push_back(Kit_connector{.name = "start", .position = glm::vec3{-0.5f * crank.length, crank.axis_y, 0.0f}, .outward = -x_axis, .up = y_axis});
            out_connectors.push_back(Kit_connector{.name = "end",   .position = glm::vec3{ 0.5f * crank.length, crank.axis_y, 0.0f}, .outward =  x_axis, .up = y_axis});
            out_connectors.push_back(make_base_connector());
            // The first throw's pin: outward from the axis, up along the shaft.
            const float x = crank.get_throw_x(0) + crank.web + (0.5f * crank.pin);
            out_connectors.push_back(Kit_connector{.name = "pin", .position = glm::vec3{x, crank.axis_y + crank.radius, 0.0f}, .outward = y_axis, .up = x_axis});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "machine.follower";
        shape.kit             = "machine";
        shape.display_name    = "Cam follower";
        shape.description     =
            "Cam follower standing on its contact at the origin: a knife edge along Z, a roller on an axle in a fork, its axis "
            "lies along Z, or a flat face, under a stem in a guide sleeve (slot metal). The contact connector joins "
            "a cam's follower connector: the stem then points away from the cam and the roller's axis lies along the "
            "cam's.";
        shape.tags            = {"decoration", "machine", "cam", "follower", "size:0.1x0.5"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("form",     "Knife edge, roller or flat face", {"knife", "roller", "flat"}, 1)),
            derive_float(make_length_parameter("stem",     "Length of the stem over the head", 0.4f, Kit_default_scale::meters, 0.05f, 4.0f), 0.3f, 0.5f, 0.1f),
            derive_float(make_length_parameter("diameter", "Diameter of the stem; the roller is twice as wide", 0.04f, Kit_default_scale::meters, 0.01f, 0.5f), 0.03f, 0.05f, 0.01f),
            make_seed_parameter()
        };
        shape.material_slots  = {"steel", "metal"};
        shape.normal_style            = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = machine_hard_edge_degrees;
        shape.default_shading = Kit_shading::smooth;
        shape.triangle_budget = 260;
        shape.collision       = Kit_collision::convex_hull;
        shape.origin_rule     = Kit_origin_rule::base_contains_origin; // a knife and a roller stand on a narrow strip
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.generator       = generate_follower;
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float top = get_follower_head(values.get_enum("form"), values.get_float("diameter")) + values.get_float("stem");
            out_connectors.push_back(make_base_connector());
            out_connectors.push_back(Kit_connector{.name = "contact", .position = glm::vec3{0.0f}, .outward = -y_axis, .up = z_axis});
            out_connectors.push_back(Kit_connector{.name = "top", .position = glm::vec3{0.0f, top, 0.0f}, .outward = y_axis, .up = z_axis});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "machine.camshaft";
        shape.kit             = "machine";
        shape.display_name    = "Camshaft";
        shape.description     =
            "Camshaft along X on two stands: a shaft with pear lobes spaced evenly along it, each turned by the step "
            "from the one before; the first lobe's nose stands up.";
        shape.tags            = {"decoration", "machine", "cam", "camshaft", "size:1x0.2"};
        shape.parameters      = {
            derive_int  (make_count_parameter ("lobes",  "Lobes", 4, 1, camshaft_lobe_max), 2, 6),
            derive_float(make_length_parameter("length", "Length of the shaft", 1.0f, Kit_default_scale::meters, 0.2f, 8.0f), 0.8f, 1.6f, 0.2f),
            derive_float(make_length_parameter("lift",   "How far a lobe's nose reaches beyond its base circle", 0.04f, Kit_default_scale::meters, 0.005f, 0.5f), 0.03f, 0.05f, 0.01f),
            derive_float(make_length_parameter("radius", "Radius of the lobes' base circle", 0.06f, Kit_default_scale::meters, 0.02f, 1.0f), 0.05f, 0.08f, 0.01f),
            make_angle_parameter("step", "Degrees between a lobe and the next", 90.0f, 0.0f, 180.0f),
            make_seed_parameter()
        };
        shape.material_slots  = {"metal", "steel"};
        shape.normal_style            = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = machine_hard_edge_degrees;
        shape.default_shading = Kit_shading::smooth;
        shape.triangle_budget = 620;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.origin_rule     = Kit_origin_rule::base_contains_origin; // the stands' feet
        shape.generator       = generate_camshaft;
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float half   = 0.5f * values.get_float("length");
            const float axis_y = (1.3f * values.get_float("radius")) + values.get_float("lift");
            out_connectors.push_back(Kit_connector{.name = "start", .position = glm::vec3{-half, axis_y, 0.0f}, .outward = -x_axis, .up = y_axis});
            out_connectors.push_back(Kit_connector{.name = "end",   .position = glm::vec3{ half, axis_y, 0.0f}, .outward =  x_axis, .up = y_axis});
            out_connectors.push_back(make_base_connector());
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "machine.bevel_gear";
        shape.kit             = "machine";
        shape.display_name    = "Bevel gear";
        shape.description     =
            "Bevel gear lying flat, its axis along Y: teeth on a cone that narrows upward, each narrowing toward the "
            "cone's apex. The cone is the angle between the axis and the pitch cone: two gears of 45 degrees joined "
            "by their mesh connectors turn shafts at a right angle.";
        shape.tags            = {"decoration", "obstacle", "machine", "gear", "bevel", "size:0.5x0.15"};
        shape.parameters      = {
            derive_int  (make_count_parameter ("teeth",      "Teeth", 16, bevel_tooth_min, bevel_tooth_max), 10, 24),
            derive_float(make_length_parameter("module",     "Pitch diameter at the large end for each tooth", 0.03f, Kit_default_scale::meters, 0.005f, 0.2f), 0.02f, 0.04f, 0.01f),
            derive_float(make_length_parameter("face_width", "Length of the teeth along the cone", 0.08f, Kit_default_scale::meters, 0.01f, 1.0f), 0.06f, 0.12f, 0.02f),
            make_angle_parameter("cone", "Degrees between the axis and the pitch cone", 45.0f, 30.0f, 60.0f),
            derive_float(make_length_parameter("bore",       "Diameter of the bore; at most half the small end", 0.08f, Kit_default_scale::meters, 0.01f, 1.0f), 0.06f, 0.1f, 0.02f),
            make_seed_parameter()
        };
        shape.material_slots  = {"metal"};
        shape.normal_style            = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = machine_hard_edge_degrees;
        shape.default_shading = Kit_shading::smooth;
        shape.triangle_budget = 520;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.generator       = generate_bevel_gear;
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const Bevel_form bevel{values};
            out_connectors.push_back(make_base_connector());
            out_connectors.push_back(Kit_connector{.name = "axis", .position = glm::vec3{0.0f, bevel.top, 0.0f}, .outward = y_axis, .up = z_axis});
            // On the large end's pitch circle: outward across the cone, up along it toward the apex.
            out_connectors.push_back(Kit_connector{
                .name     = "mesh",
                .position = glm::vec3{bevel.pitch, bevel.foot, 0.0f},
                .outward  = glm::vec3{std::cos(bevel.cone), std::sin(bevel.cone), 0.0f},
                .up       = glm::vec3{-std::sin(bevel.cone), std::cos(bevel.cone), 0.0f}
            });
            for (int quarter = 1; quarter <= 3; ++quarter) {
                const glm::vec2 p = get_kit_gear_boundary_direction(quarter, bevel.teeth);
                const glm::vec3 out{p.x, 0.0f, p.y};
                out_connectors.push_back(Kit_connector{
                    .name     = c_kit_gear_boundary_names[static_cast<std::size_t>(quarter - 1)],
                    .position = (bevel.pitch * out) + glm::vec3{0.0f, bevel.foot, 0.0f},
                    .outward  = (std::cos(bevel.cone) * out) + glm::vec3{0.0f, std::sin(bevel.cone), 0.0f},
                    .up       = (-std::sin(bevel.cone) * out) + glm::vec3{0.0f, std::cos(bevel.cone), 0.0f}
                });
            }
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "machine.worm";
        shape.kit             = "machine";
        shape.display_name    = "Worm";
        shape.description     =
            "Worm: a shaft (slot steel) along X with a helical thread of twelve stretches to a turn, lying on its "
            "thread; its lead is pi times the module for each start. Or the worm wheel it drives, lying flat with a "
            "bush (slot steel) in its bore: its pitch diameter is its teeth times the module.";
        shape.tags            = {"decoration", "machine", "gear", "worm", "size:0.5x0.15"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("form",     "The worm or its wheel", {"worm", "wheel"}, 0)),
            derive_float(make_length_parameter("module",   "Pitch over pi: the wheel's pitch diameter for each tooth", 0.03f, Kit_default_scale::meters, 0.005f, 0.2f), 0.02f, 0.04f, 0.01f),
            derive_float(make_length_parameter("diameter", "Pitch diameter of the worm; at least three modules", 0.12f, Kit_default_scale::meters, 0.02f, 1.0f), 0.1f, 0.16f, 0.02f),
            derive_int  (make_count_parameter ("starts",   "Threads of the worm", 1, 1, worm_start_max), 1, 2),
            derive_int  (make_count_parameter ("turns",    "Turns of each thread", 4, worm_turn_min, worm_turn_max), 3, 5),
            derive_int  (make_count_parameter ("teeth",    "Teeth of the wheel", 24, worm_tooth_min, worm_tooth_max), 20, 30),
            make_seed_parameter()
        };
        shape.material_slots  = {"metal", "steel"};
        shape.normal_style            = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = machine_hard_edge_degrees;
        shape.default_shading = Kit_shading::smooth;
        shape.triangle_budget = 800;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.origin_rule     = Kit_origin_rule::base_contains_origin; // a lying worm rests on two lines
        shape.generator       = generate_worm;
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const Worm_form worm{values};
            out_connectors.push_back(make_base_connector());
            if (worm.wheel) {
                out_connectors.push_back(Kit_connector{.name = "axis", .position = glm::vec3{0.0f, 1.25f * worm.width, 0.0f}, .outward = y_axis, .up = z_axis});
                add_side_connector(out_connectors, "mesh", glm::vec3{worm.pitch, 0.5f * worm.width, 0.0f}, x_axis);
                for (int quarter = 1; quarter <= 3; ++quarter) {
                    const glm::vec2 p = get_kit_gear_boundary_direction(quarter, worm.teeth);
                    const glm::vec3 out{p.x, 0.0f, p.y};
                    add_side_connector(out_connectors, c_kit_gear_boundary_names[static_cast<std::size_t>(quarter - 1)], (worm.pitch * out) + glm::vec3{0.0f, 0.5f * worm.width, 0.0f}, out);
                }
            } else {
                out_connectors.push_back(Kit_connector{.name = "axis", .position = glm::vec3{worm.reach, worm.axis_y, 0.0f}, .outward = x_axis, .up = y_axis});
                // Up across the axis, so that joined to a wheel's mesh connector the worm's axis crosses the wheel's.
                out_connectors.push_back(Kit_connector{.name = "mesh", .position = glm::vec3{0.0f, worm.axis_y + worm.radius, 0.0f}, .outward = y_axis, .up = z_axis});
            }
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "machine.pulley";
        shape.kit             = "machine";
        shape.display_name    = "Pulley";
        shape.description     =
            "Belt pulley lying flat, its axis along Y, with V grooves around it; or a chain sprocket: a thin toothed "
            "plate, its pitch circle the diameter, under a hub.";
        shape.tags            = {"decoration", "machine", "pulley", "sprocket", "size:0.3x0.06"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("form",     "Belt pulley or chain sprocket", {"pulley", "sprocket"}, 0)),
            derive_float(make_length_parameter("diameter", "Outside diameter of a pulley, pitch diameter of a sprocket", 0.3f, Kit_default_scale::meters, 0.08f, 4.0f), 0.2f, 0.4f, 0.05f),
            derive_float(make_length_parameter("width",    "Size along Y", 0.06f, Kit_default_scale::meters, 0.02f, 1.0f), 0.04f, 0.1f, 0.02f),
            derive_int  (make_count_parameter ("grooves",  "Grooves of a pulley", 1, 1, pulley_groove_max), 1, 3),
            derive_int  (make_count_parameter ("teeth",    "Teeth of a sprocket", 16, pulley_tooth_min, pulley_tooth_max), 12, 24),
            derive_float(make_length_parameter("bore",     "Diameter of the bore", 0.05f, Kit_default_scale::meters, 0.01f, 1.0f), 0.04f, 0.08f, 0.01f),
            make_seed_parameter()
        };
        shape.material_slots  = {"metal"};
        shape.normal_style            = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = machine_hard_edge_degrees;
        shape.default_shading = Kit_shading::smooth;
        shape.triangle_budget = 700;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.generator       = generate_pulley;
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            out_connectors.push_back(make_base_connector());
            out_connectors.push_back(Kit_connector{.name = "axis", .position = glm::vec3{0.0f, values.get_float("width"), 0.0f}, .outward = y_axis, .up = z_axis});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "machine.washer";
        shape.kit             = "machine";
        shape.display_name    = "Washer";
        shape.description     =
            "Washer lying flat for a bolt of its diameter: a flat ring, a split lock washer whose wire rises around "
            "its turn, or a ring with teeth around it.";
        shape.tags            = {"decoration", "machine", "fastener", "washer", "size:0.13x0.01"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("form",      "Flat, split lock, or toothed", {"flat", "lock", "toothed"}, 0)),
            derive_float(make_length_parameter("diameter",  "Diameter of the bolt it fits", 0.06f, Kit_default_scale::meters, 0.01f, 0.5f), 0.04f, 0.08f, 0.01f),
            derive_float(make_length_parameter("thickness", "Thickness of a flat washer", 0.01f, Kit_default_scale::meters, 0.002f, 0.1f), 0.008f, 0.012f, 0.002f),
            derive_int  (make_count_parameter ("teeth",     "Teeth of a toothed washer", 10, washer_tooth_min, washer_tooth_max), 8, 12),
            make_seed_parameter()
        };
        shape.material_slots  = {"steel"};
        shape.normal_style            = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = machine_hard_edge_degrees;
        shape.default_shading = Kit_shading::smooth;
        shape.triangle_budget = 260;
        shape.collision       = Kit_collision::convex_hull;
        shape.origin_rule     = Kit_origin_rule::base_contains_origin; // a lock washer rests on part of its turn
        shape.generator       = generate_washer;
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            add_base_and_top_connectors(out_connectors, get_washer_top(values));
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "machine.cylinder";
        shape.kit             = "machine";
        shape.display_name    = "Cylinder";
        shape.description     =
            "Cylinder sleeve standing on its foot flange: a liner (slot steel) inside a jacket with cooling fins "
            "around it. A piston of the bore's diameter slides in it.";
        shape.tags            = {"decoration", "obstacle", "machine", "piston", "cylinder", "size:0.45x0.5"};
        shape.parameters      = {
            derive_float(make_length_parameter("bore",   "Diameter of the piston it takes", 0.3f, Kit_default_scale::meters, 0.05f, 2.0f), 0.2f, 0.4f, 0.05f),
            derive_float(make_length_parameter("height", "Size along Y", 0.5f, Kit_default_scale::meters, 0.1f, 4.0f), 0.4f, 0.7f, 0.1f),
            derive_int  (make_count_parameter ("fins",   "Cooling fins", 6, 0, cylinder_fin_max), 4, 8),
            derive_float(make_length_parameter("wall",   "Thickness of the wall", 0.03f, Kit_default_scale::meters, 0.01f, 0.5f), 0.02f, 0.04f, 0.01f),
            make_seed_parameter()
        };
        shape.material_slots  = {"metal", "steel"};
        shape.normal_style            = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = machine_hard_edge_degrees;
        shape.default_shading = Kit_shading::smooth;
        shape.triangle_budget = 1100;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.generator       = generate_cylinder;
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            add_base_and_top_connectors(out_connectors, values.get_float("height"));
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "machine.pillow_block";
        shape.kit             = "machine";
        shape.display_name    = "Pillow block";
        shape.description     =
            "Pillow block: a housing with a bush (slot steel) around a shaft along Z, on a pedestal and a base along "
            "X held by two or four bolts (slot steel). The height is the axis' over the ground.";
        shape.tags            = {"decoration", "obstacle", "machine", "bearing", "size:0.4x0.25"};
        shape.parameters      = {
            derive_float(make_length_parameter("bore",   "Diameter of the shaft; at most the height", 0.1f, Kit_default_scale::meters, 0.02f, 2.0f), 0.08f, 0.12f, 0.02f),
            derive_float(make_length_parameter("height", "Height of the axis", 0.15f, Kit_default_scale::meters, 0.05f, 2.0f), 0.15f, 0.2f, 0.05f),
            derive_float(make_length_parameter("width",  "Length of the base along X", 0.4f, Kit_default_scale::meters, 0.1f, 4.0f), 0.4f, 0.5f, 0.1f),
            derive_float(make_length_parameter("depth",  "Size along the shaft", 0.1f, Kit_default_scale::meters, 0.03f, 1.0f), 0.08f, 0.12f, 0.02f),
            derive_enum (make_enum_parameter  ("bolts",  "Two bolts, or four", {"two", "four"}, 0)),
            make_seed_parameter()
        };
        shape.material_slots  = {"metal", "steel"};
        shape.normal_style            = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = machine_hard_edge_degrees;
        shape.default_shading = Kit_shading::smooth;
        shape.triangle_budget = 320;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.generator       = generate_pillow_block;
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            out_connectors.push_back(make_base_connector());
            add_side_connector(out_connectors, "axis", glm::vec3{0.0f, values.get_float("height"), 0.5f * values.get_float("depth")}, z_axis);
        };
        shapes.push_back(std::move(shape));
    }
}

} // namespace editor
