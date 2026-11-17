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

// The first eight shapes of the build order of doc/plans/level_kit_machine_elements.md.
// A mesh's topology follows from its integer values; floats and the seed only move vertices.

namespace editor {

namespace {

constexpr float pi     = std::numbers::pi_v<float>;
constexpr float two_pi = 2.0f * pi;
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

// A prism from the level outline at its top down by the height; the outline may run either way around.
void add_flat_prism(Kit_mesh_builder& builder, std::vector<glm::vec3> outline, const float height)
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
    add_prism_between(builder, outline, glm::vec3{0.0f, -height, 0.0f});
}

// A gear tooth from y0 up to y1: its middle on the pitch line at `center`, standing out along `out` (unit, level).
// An involute's stand-in: wide at the root, half a pitch wide on the pitch line, narrow at the tip.
void add_gear_tooth(Kit_mesh_builder& builder, const glm::vec3& center, const glm::vec3& out, const float module, const float y0, const float y1)
{
    const glm::vec3 along = glm::cross(y_axis, out);
    const float     root  = -1.4f * module; // a little inside the root line
    const float     tip   = module;
    const float     pitch = 0.25f * pi * module;
    const glm::vec3 top   = glm::vec3{center.x, y1, center.z};
    const std::vector<glm::vec3> outline{
        top + (root * out) - ((1.05f * module) * along), top - (pitch * along), top + (tip * out) - ((0.4f * module) * along),
        top + (tip * out) + ((0.4f * module) * along), top + (pitch * along), top + (root * out) + ((1.05f * module) * along)
    };
    add_flat_prism(builder, outline, y1 - y0);
}

// A prism of `sides` sides around the upright through the origin from y0 to y1, its corners `outer` from the axis
// (the first toward +X), with a round bore of twice as many points, `bore` from the axis.
void add_bored_prism(Kit_mesh_builder& builder, const int sides, const float outer, const float bore, const float y0, const float y1)
{
    const uint32_t m = static_cast<uint32_t>(sides);
    std::array<std::vector<uint32_t>, 2> rim;  // at y0 and at y1
    std::array<std::vector<uint32_t>, 2> hole;
    for (std::size_t level = 0; level < 2; ++level) {
        const float y = (level == 0) ? y0 : y1;
        for (uint32_t k = 0; k < m; ++k) {
            const glm::vec2 point = outer * get_kit_circle_point(static_cast<int>(k), sides);
            rim[level].push_back(builder.add_vertex(glm::vec3{point.x, y, point.y}));
        }
        for (uint32_t j = 0; j < (2 * m); ++j) {
            const glm::vec2 point = bore * get_kit_circle_point(static_cast<int>(j), 2 * sides);
            hole[level].push_back(builder.add_vertex(glm::vec3{point.x, y, point.y}));
        }
    }
    for (uint32_t k = 0; k < m; ++k) {
        const uint32_t n  = (k + 1) % m;
        const uint32_t j0 = 2 * k;
        const uint32_t j1 = j0 + 1;
        const uint32_t j2 = (j0 + 2) % (2 * m);
        // The top and the bottom between a side and the bore's two stretches under it.
        builder.add_triangle(rim[1][k], rim[1][n], hole[1][j1]);
        builder.add_triangle(rim[1][k], hole[1][j1], hole[1][j0]);
        builder.add_triangle(rim[1][n], hole[1][j2], hole[1][j1]);
        builder.add_triangle(rim[0][n], rim[0][k], hole[0][j1]);
        builder.add_triangle(hole[0][j1], rim[0][k], hole[0][j0]);
        builder.add_triangle(hole[0][j2], rim[0][n], hole[0][j1]);
        builder.add_quad(rim[0][k], rim[0][n], rim[1][n], rim[1][k]);
        builder.add_quad(hole[0][j1], hole[0][j0], hole[1][j0], hole[1][j1]);
        builder.add_quad(hole[0][j2], hole[0][j1], hole[1][j1], hole[1][j2]);
    }
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

// A wire along its points: a tube of four corners in triangles, between two end faces.
void add_wire(Kit_mesh_builder& builder, const std::span<const Wire_point> points, const float half)
{
    const uint32_t count = static_cast<uint32_t>(points.size());
    const uint32_t first = static_cast<uint32_t>(builder.get_positions().size());
    for (const Wire_point& point : points) {
        builder.add_vertex(point.center - (half * point.up));
        builder.add_vertex(point.center - (half * point.side));
        builder.add_vertex(point.center + (half * point.up));
        builder.add_vertex(point.center + (half * point.side));
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

//
// Gear: a spur gear lying flat: teeth around a rim on a solid, webbed or spoked body with a bored hub.
//

constexpr uint32_t gear_metal_slot = 0;
constexpr int      gear_tooth_min  = 8;
constexpr int      gear_tooth_max  = 40;
constexpr int      gear_round_max  = 48; // the most points around the body

enum class Gear_body : unsigned int { solid = 0, web, spokes };

[[nodiscard]] auto get_gear_body(const std::string_view name) -> Gear_body
{
    if (name == "web") {
        return Gear_body::web;
    }
    if (name == "spokes") {
        return Gear_body::spokes;
    }
    return Gear_body::solid;
}

class Gear_form
{
public:
    explicit Gear_form(const Kit_parameter_values& values)
        : body  {get_gear_body(values.get_enum("body"))}
        , teeth {std::clamp(values.get_int("teeth"), gear_tooth_min, gear_tooth_max)}
        , spokes{std::clamp(values.get_int("spokes"), 3, 8)}
        , module{values.get_float("module")}
        , width {values.get_float("face_width")}
    {
        pitch = 0.5f * module * static_cast<float>(teeth);
        root  = pitch - (1.25f * module);
        bore  = std::min(0.5f * values.get_float("bore"), 0.3f * root);
        hub   = std::min(std::max(1.6f * bore, bore + (0.4f * module)), 0.55f * root);
        rim   = std::max(root - (1.2f * module), 0.7f * root);
        boss  = 0.25f * width;
    }

    // The angle of tooth i's middle: a quarter pitch before the mesh connector on +X.
    [[nodiscard]] auto get_tooth_angle(const int i) const -> float
    {
        return (two_pi * (static_cast<float>(i) - 0.25f)) / static_cast<float>(teeth);
    }
    [[nodiscard]] auto get_top() const -> float
    {
        return width + boss;
    }

    Gear_body body  {Gear_body::solid};
    int       teeth {16};
    int       spokes{5};
    float     module{0.0f};
    float     width {0.0f};
    float     pitch {0.0f}; // radius of the pitch circle
    float     root  {0.0f}; // radius of the root circle
    float     bore  {0.0f}; // radius of the bore
    float     hub   {0.0f}; // radius of the hub
    float     rim   {0.0f}; // inner radius of the rim
    float     boss  {0.0f}; // how far the hub stands over the face
};

void generate_gear(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Gear_form form{values};
    const float w        = form.width;
    const int   segments = std::clamp(2 * form.teeth, 16, gear_round_max);
    const Lathe_cap loop = Lathe_cap::loop;
    Kit_mesh_builder builder;
    builder.set_slot(gear_metal_slot);
    if (form.body == Gear_body::solid) {
        const std::array<Lathe_ring, 6> rings{{{form.bore, 0.0f}, {form.root, 0.0f}, {form.root, w}, {form.hub, w}, {form.hub, form.get_top()}, {form.bore, form.get_top()}}};
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = segments, .bottom = loop, .top = loop, .slot = gear_metal_slot});
    } else if (form.body == Gear_body::web) {
        // A web a third of the face thick between the hub and the rim.
        const std::array<Lathe_ring, 12> rings{{
            {form.bore, 0.0f}, {form.hub, 0.0f}, {form.hub, 0.33f * w}, {form.rim, 0.33f * w}, {form.rim, 0.0f}, {form.root, 0.0f},
            {form.root, w}, {form.rim, w}, {form.rim, 0.67f * w}, {form.hub, 0.67f * w}, {form.hub, form.get_top()}, {form.bore, form.get_top()}
        }};
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = segments, .bottom = loop, .top = loop, .slot = gear_metal_slot});
    } else {
        const std::array<Lathe_ring, 4> outer{{{form.rim, 0.0f}, {form.root, 0.0f}, {form.root, w}, {form.rim, w}}};
        const std::array<Lathe_ring, 4> inner{{{form.bore, 0.0f}, {form.hub, 0.0f}, {form.hub, form.get_top()}, {form.bore, form.get_top()}}};
        add_lathe(builder, Placement{}, Lathe{.rings = outer, .segments = segments, .bottom = loop, .top = loop, .slot = gear_metal_slot});
        add_lathe(builder, Placement{}, Lathe{.rings = inner, .segments = segments, .bottom = loop, .top = loop, .slot = gear_metal_slot});
        builder.set_slot(gear_metal_slot);
        const float half = std::min(0.5f * form.hub, form.module);
        for (int k = 0; k < form.spokes; ++k) {
            const Placement place = make_turned_placement(glm::vec3{0.0f}, around_y(two_pi * static_cast<float>(k) / static_cast<float>(form.spokes)));
            add_placed_box(builder, place, glm::vec3{0.9f * form.hub, 0.25f * w, -half}, glm::vec3{form.rim + (0.3f * (form.root - form.rim)), 0.75f * w, half});
        }
    }
    builder.set_slot(gear_metal_slot);
    for (int i = 0; i < form.teeth; ++i) {
        const glm::vec3 out = around_y(form.get_tooth_angle(i));
        add_gear_tooth(builder, form.pitch * out, out, form.module, 0.0f, w);
    }
    builder.write(mesh);
}

//
// Rack: a bar along X, lying flat, with teeth toward +Z.
//

constexpr uint32_t rack_metal_slot = 0;
constexpr int      rack_tooth_min  = 4;
constexpr int      rack_tooth_max  = 40;

class Rack_form
{
public:
    explicit Rack_form(const Kit_parameter_values& values)
        : teeth {std::clamp(values.get_int("teeth"), rack_tooth_min, rack_tooth_max)}
        , module{values.get_float("module")}
        , width {values.get_float("face_width")}
        , height{values.get_float("height")}
    {
        pitch  = pi * module;
        half_l = 0.5f * pitch * static_cast<float>(teeth);
    }

    // The middle of tooth k along X.
    [[nodiscard]] auto get_tooth_x(const int k) const -> float
    {
        return -half_l + (pitch * (static_cast<float>(k) + 0.5f));
    }
    // The mesh connector's x: a quarter pitch after the tooth nearest the middle.
    [[nodiscard]] auto get_mesh_x() const -> float
    {
        return ((teeth % 2) == 0) ? (-0.25f * pitch) : (0.25f * pitch);
    }
    // The pitch line's z: the root line lies on z = 0.
    [[nodiscard]] auto get_pitch_z() const -> float
    {
        return 1.25f * module;
    }

    int   teeth {12};
    float module{0.0f};
    float width {0.0f};
    float height{0.0f}; // of the bar behind the teeth, along Z
    float pitch {0.0f};
    float half_l{0.0f};
};

void generate_rack(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Rack_form form{values};
    Kit_mesh_builder builder;
    builder.set_slot(rack_metal_slot);
    builder.add_box(glm::vec3{-form.half_l, 0.0f, -form.height}, glm::vec3{form.half_l, form.width, 0.0f});
    for (int k = 0; k < form.teeth; ++k) {
        add_gear_tooth(builder, glm::vec3{form.get_tooth_x(k), 0.0f, form.get_pitch_z()}, z_axis, form.module, 0.0f, form.width);
    }
    builder.write(mesh);
}

//
// Bolt: a shank standing on its tip, threaded in rings from the tip, under a head.
//

constexpr uint32_t bolt_steel_slot = 0;
constexpr int      bolt_round      = 10; // points around the shank
constexpr int      bolt_turn_min   = 4;
constexpr int      bolt_turn_max   = 40;

enum class Bolt_head : unsigned int { hex = 0, socket, round, countersunk, none };

[[nodiscard]] auto get_bolt_head(const std::string_view name) -> Bolt_head
{
    if (name == "socket") {
        return Bolt_head::socket;
    }
    if (name == "round") {
        return Bolt_head::round;
    }
    if (name == "countersunk") {
        return Bolt_head::countersunk;
    }
    if (name == "none") {
        return Bolt_head::none;
    }
    return Bolt_head::hex;
}

// The head's height over the shank's end, in diameters.
[[nodiscard]] auto get_bolt_head_height(const Bolt_head head) -> float
{
    switch (head) {
        case Bolt_head::hex:         return 0.65f;
        case Bolt_head::socket:      return 1.0f;
        case Bolt_head::round:       return 0.52f;
        case Bolt_head::countersunk: return 0.04f;
        case Bolt_head::none:        return 0.0f;
    }
    return 0.0f;
}

void generate_bolt(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Bolt_head head   = get_bolt_head(values.get_enum("head"));
    const float     d      = values.get_float("diameter");
    const float     length = values.get_float("length");
    const int       turns  = std::clamp(values.get_int("turns"), bolt_turn_min, bolt_turn_max);
    const float     r      = 0.5f * d;
    const float     minor  = 0.84f * r;
    const float     thread = 0.88f * values.get_float("thread") * length;
    const float     turn   = thread / static_cast<float>(turns);
    Kit_mesh_builder builder;
    {
        // The shank from its tip: a ridge to a turn, then plain up to the head.
        std::vector<Lathe_ring> rings;
        rings.push_back(Lathe_ring{0.7f * r, 0.0f});
        for (int k = 0; k < turns; ++k) {
            const float y = turn * static_cast<float>(k);
            rings.push_back(Lathe_ring{minor, y + (0.15f * turn)});
            rings.push_back(Lathe_ring{r, y + (0.65f * turn)});
        }
        rings.push_back(Lathe_ring{minor, thread + (0.15f * turn)});
        rings.push_back(Lathe_ring{r, thread + (0.5f * turn)});
        rings.push_back(Lathe_ring{r, length});
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = bolt_round, .slot = bolt_steel_slot});
    }
    if (head == Bolt_head::hex) {
        const std::array<Lathe_ring, 3> rings{{{0.9f * d, length}, {0.9f * d, length + (0.55f * d)}, {0.76f * d, length + (0.65f * d)}}};
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = 6, .slot = bolt_steel_slot});
    } else if (head == Bolt_head::socket) {
        // A round head with a socket in its top.
        const std::array<Lathe_ring, 5> rings{{{0.75f * d, length}, {0.75f * d, length + (0.94f * d)}, {0.7f * d, length + d}, {0.42f * d, length + d}, {0.42f * d, length + (0.45f * d)}}};
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = 12, .slot = bolt_steel_slot});
    } else if (head == Bolt_head::round) {
        const std::array<Lathe_ring, 3> rings{{{0.88f * d, length}, {0.82f * d, length + (0.25f * d)}, {0.5f * d, length + (0.45f * d)}}};
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = 12, .top = Lathe_cap::point, .top_y = length + (0.52f * d), .slot = bolt_steel_slot});
    } else if (head == Bolt_head::countersunk) {
        // A cone that widens up to a flat top; it stays over the tip of a short bolt.
        const float depth = std::min(0.45f * d, 0.4f * length);
        const std::array<Lathe_ring, 3> rings{{{0.52f * d, length - depth}, {0.92f * d, length}, {0.92f * d, length + (0.04f * d)}}};
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = 12, .slot = bolt_steel_slot});
    }
    builder.write(mesh);
}

//
// Nut: a bored hexagon or square, a round nut with two wings, or a hexagon under a dome.
//

constexpr uint32_t nut_steel_slot = 0;

enum class Nut_form : unsigned int { hex = 0, square, wing, cap };

[[nodiscard]] auto get_nut_form(const std::string_view name) -> Nut_form
{
    if (name == "square") {
        return Nut_form::square;
    }
    if (name == "wing") {
        return Nut_form::wing;
    }
    if (name == "cap") {
        return Nut_form::cap;
    }
    return Nut_form::hex;
}

// The nut's top over its base.
[[nodiscard]] auto get_nut_top(const Kit_parameter_values& values) -> float
{
    const Nut_form form   = get_nut_form(values.get_enum("form"));
    const float    height = values.get_float("height");
    if (form == Nut_form::wing) {
        return 1.9f * height;
    }
    return (form == Nut_form::cap) ? (height + (0.75f * values.get_float("diameter"))) : height;
}

void generate_nut(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Nut_form form = get_nut_form(values.get_enum("form"));
    const float    d    = values.get_float("diameter");
    const float    h    = values.get_float("height");
    const float    bore = 0.42f * d;
    Kit_mesh_builder builder;
    builder.set_slot(nut_steel_slot);
    if (form == Nut_form::square) {
        add_bored_prism(builder, 4, 1.05f * d, bore, 0.0f, h);
    } else if (form == Nut_form::wing) {
        const std::array<Lathe_ring, 4> rings{{{bore, 0.0f}, {0.8f * d, 0.0f}, {0.8f * d, h}, {bore, h}}};
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = 12, .bottom = Lathe_cap::loop, .top = Lathe_cap::loop, .slot = nut_steel_slot});
        builder.set_slot(nut_steel_slot);
        // A wing leans out of each side.
        for (const float side : {-1.0f, 1.0f}) {
            const Placement place{.origin = glm::vec3{side * 0.72f * d, 0.15f * h, 0.0f}, .y = glm::vec3{side * 0.55f, 1.0f, 0.0f}};
            const float     inner = (side > 0.0f) ? -0.3f : -0.4f; // each wing reaches further outward
            add_placed_box(builder, place, glm::vec3{inner * d, 0.0f, -0.08f * d}, glm::vec3{(inner + 0.7f) * d, 1.75f * h, 0.08f * d});
        }
    } else {
        add_bored_prism(builder, 6, 0.92f * d, bore, 0.0f, h);
        if (form == Nut_form::cap) {
            // A dome closes the bore.
            const std::array<Lathe_ring, 3> rings{{{0.76f * d, 0.96f * h}, {0.7f * d, h + (0.3f * d)}, {0.42f * d, h + (0.62f * d)}}};
            add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = 12, .top = Lathe_cap::point, .top_y = h + (0.75f * d), .slot = nut_steel_slot});
        }
    }
    builder.write(mesh);
}

//
// Spring: a wire along a helix. Compression: standing, its two end coils closed and flat. Extension: lying along
// X, tightly wound, with a hook at each end. Torsion: standing, tightly wound, with a leg from each end.
//

constexpr uint32_t spring_steel_slot = 0;
constexpr int      spring_stretches  = 12;    // of the wire to a coil
constexpr int      spring_hook       = 8;     // stretches of a hook
constexpr int      spring_coil_min   = 3;
constexpr int      spring_coil_max   = 16;
constexpr float    spring_closed     = 1.08f; // a closed coil's pitch in wires
constexpr float    spring_leg        = 1.5f;  // a torsion spring's leg in coil diameters
constexpr float    spring_eye        = 0.9f;  // a hook's radius in coil radii, at most

enum class Spring_form : unsigned int { compression = 0, extension, torsion };

[[nodiscard]] auto get_spring_form(const std::string_view name) -> Spring_form
{
    if (name == "extension") {
        return Spring_form::extension;
    }
    if (name == "torsion") {
        return Spring_form::torsion;
    }
    return Spring_form::compression;
}

class Spring_values
{
public:
    explicit Spring_values(const Kit_parameter_values& values)
        : form  {get_spring_form(values.get_enum("form"))}
        , coils {std::clamp(values.get_int("coils"), spring_coil_min, spring_coil_max)}
        , radius{0.5f * values.get_float("diameter")}
        , wire  {values.get_float("wire")}
    {
        const float least = spring_closed * wire * static_cast<float>(coils + 1);
        const float tight = spring_closed * wire * static_cast<float>(coils);
        // A compression spring is its length high, at least its closed height; the others are wound tight.
        length = (form == Spring_form::compression) ? std::max(values.get_float("length"), least) : (tight + wire);
        // A lying spring's axis is over the ground by its coils' lowest sides; a hook reaches no lower than they do.
        axis_y = (radius + (0.5f * wire)) * std::cos(pi / static_cast<float>(spring_stretches));
        eye    = std::min(spring_eye * radius, axis_y - (0.5f * wire));
    }

    Spring_form form  {Spring_form::compression};
    int         coils {8};
    float       radius{0.0f}; // of the coil's middle
    float       wire  {0.0f}; // the wire's thickness
    float       length{0.0f}; // along the axis
    float       axis_y{0.0f};
    float       eye   {0.0f}; // a hook's radius
};

void generate_spring(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Spring_values spring{values};
    const float half  = 0.5f * spring.wire;
    const float r     = spring.radius;
    const int   total = spring.coils * spring_stretches;
    Kit_mesh_builder builder;
    builder.set_slot(spring_steel_slot);
    std::vector<Wire_point> points;
    if (spring.form == Spring_form::extension) {
        // Along X; no point of a coil lies straight under the axis, so the spring rests on two lines.
        const float pitch = spring_closed * spring.wire;
        const float start = -0.5f * pitch * static_cast<float>(spring.coils);
        for (int k = 0; k <= total; ++k) {
            const float     turn  = static_cast<float>(k) / static_cast<float>(spring_stretches);
            const float     angle = two_pi * (turn + (0.5f / static_cast<float>(spring_stretches)));
            const glm::vec3 out{0.0f, -std::cos(angle), -std::sin(angle)};
            points.push_back(Wire_point{.center = glm::vec3{start + (pitch * turn), spring.axis_y, 0.0f} + (r * out), .up = x_axis, .side = out});
        }
        add_wire(builder, points, half);
        // A hook at each end: the last coil bent up into half a ring in the upright plane through the axis.
        for (const float side : {-1.0f, 1.0f}) {
            const glm::vec3 center{side * (-start), spring.axis_y, 0.0f};
            std::vector<Wire_point> hook;
            for (int k = 0; k <= spring_hook; ++k) {
                const float     angle = pi * ((static_cast<float>(k) / static_cast<float>(spring_hook)) - 0.5f);
                const glm::vec3 out{side * std::cos(angle), std::sin(angle), 0.0f};
                // `up` turns with the end, so the travel crossed with `up` is `out` at both.
                hook.push_back(Wire_point{.center = center + (spring.eye * out), .up = side * z_axis, .side = out});
            }
            add_wire(builder, hook, half);
        }
    } else {
        // About Y from +X toward -Z: a flat coil on the ground, then the coils up to the top.
        const bool  compression = (spring.form == Spring_form::compression);
        const float second      = half + (spring_closed * spring.wire);
        const float top         = spring.length - half;
        for (int k = 0; k <= total; ++k) {
            const float     angle = two_pi * static_cast<float>(k) / static_cast<float>(spring_stretches);
            const glm::vec3 out   = around_y(angle);
            float y = half;
            if (k >= spring_stretches) {
                if (compression) {
                    // The last coil lies flat at the top.
                    const int   rising = total - (2 * spring_stretches);
                    const float share  = std::min(static_cast<float>(k - spring_stretches) / static_cast<float>(rising), 1.0f);
                    y = (k > (total - spring_stretches)) ? top : (second + ((top - (spring_closed * spring.wire) - second) * share));
                } else {
                    y = second + ((top - second) * static_cast<float>(k - spring_stretches) / static_cast<float>(total - spring_stretches));
                }
            }
            points.push_back(Wire_point{.center = (r * out) + (y * y_axis), .up = y_axis, .side = out});
        }
        add_wire(builder, points, half);
        if (!compression) {
            // A leg from each end along the wire's tangent: toward +Z on the ground, toward -Z at the top.
            const float leg = spring_leg * 2.0f * r;
            builder.add_box(glm::vec3{r - half, 0.0f, -0.4f * half}, glm::vec3{r + half, spring.wire, leg});
            builder.add_box(glm::vec3{r - half, top - half, -leg}, glm::vec3{r + half, top + half, 0.4f * half});
        }
    }
    builder.write(mesh);
}

//
// Bearing: an outer and an inner race around balls, rollers or needles in a cage, lying flat.
//

constexpr uint32_t bearing_steel_slot  = 0;
constexpr uint32_t bearing_metal_slot  = 1;
constexpr int      bearing_round       = 16; // points around the races
constexpr int      bearing_element     = 8;  // points around a rolling element
constexpr int      bearing_element_min = 6;
constexpr int      bearing_element_max = 20;

void generate_bearing(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const std::string_view form     = values.get_enum("form");
    const float            outer    = 0.5f * values.get_float("diameter");
    const float            inner    = std::min(0.5f * values.get_float("bore"), 0.8f * outer);
    const float            width    = values.get_float("width");
    const int              elements = std::clamp(values.get_int("elements"), bearing_element_min, bearing_element_max);
    const float            space    = outer - inner;
    const float            middle   = 0.5f * (outer + inner);
    const Lathe_cap        loop     = Lathe_cap::loop;
    Kit_mesh_builder builder;
    {
        const std::array<Lathe_ring, 4> rings{{{inner, 0.0f}, {inner + (0.22f * space), 0.0f}, {inner + (0.22f * space), width}, {inner, width}}};
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = bearing_round, .bottom = loop, .top = loop, .slot = bearing_steel_slot});
    }
    {
        const std::array<Lathe_ring, 4> rings{{{outer - (0.22f * space), 0.0f}, {outer, 0.0f}, {outer, width}, {outer - (0.22f * space), width}}};
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = bearing_round, .bottom = loop, .top = loop, .slot = bearing_steel_slot});
    }
    {
        // The cage: a thin ring between the races.
        const std::array<Lathe_ring, 4> rings{{{middle - (0.07f * space), 0.3f * width}, {middle + (0.07f * space), 0.3f * width}, {middle + (0.07f * space), 0.7f * width}, {middle - (0.07f * space), 0.7f * width}}};
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = bearing_round, .bottom = loop, .top = loop, .slot = bearing_metal_slot});
    }
    // The rolling elements stand between the races, no wider than their neighbours leave room for.
    const float room = 0.92f * middle * std::sin(pi / static_cast<float>(elements));
    for (int k = 0; k < elements; ++k) {
        const glm::vec2 point = middle * get_kit_circle_point(k, elements);
        const Placement place{.origin = glm::vec3{point.x, 0.5f * width, point.y}};
        if (form == "ball") {
            const float r = std::min({0.3f * space, 0.46f * width, room});
            const std::array<Lathe_ring, 3> rings{{{0.71f * r, -0.71f * r}, {r, 0.0f}, {0.71f * r, 0.71f * r}}};
            add_lathe(builder, place, Lathe{
                .rings = rings, .segments = bearing_element, .bottom = Lathe_cap::point, .top = Lathe_cap::point, .bottom_y = -r, .top_y = r, .slot = bearing_steel_slot
            });
        } else {
            const float r = std::min(((form == "needle") ? 0.14f : 0.29f) * space, room);
            const std::array<Lathe_ring, 2> rings{{{r, -0.4f * width}, {r, 0.4f * width}}};
            add_lathe(builder, place, Lathe{.rings = rings, .segments = bearing_element, .slot = bearing_steel_slot});
        }
    }
    builder.write(mesh);
}

//
// Piston: a crown over ring grooves and a skirt, hollow from below, with a wrist pin along Z.
//

constexpr uint32_t piston_metal_slot = 0;
constexpr uint32_t piston_steel_slot = 1;
constexpr int      piston_round      = 16;    // points around the piston
constexpr int      piston_ring_max   = 4;
constexpr float    piston_pin        = 0.42f; // the pin's height in heights

void generate_piston(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float r      = 0.5f * values.get_float("diameter");
    const float height = values.get_float("height");
    const int   rings  = std::clamp(values.get_int("rings"), 1, piston_ring_max);
    Kit_mesh_builder builder;
    {
        // From the hollow's ceiling down its wall, around the skirt's foot, up the outside over the rings to the crown.
        std::vector<Lathe_ring> profile;
        std::vector<uint32_t>   slots;
        const auto add_ring = [&](const float radius, const float y, const uint32_t slot) {
            if (!profile.empty()) {
                slots.push_back(slot);
            }
            profile.push_back(Lathe_ring{radius, y});
        };
        add_ring(0.8f * r, 0.62f * height, piston_metal_slot);
        add_ring(0.8f * r, 0.0f, piston_metal_slot);
        add_ring(r, 0.0f, piston_metal_slot);
        // The rings stand a little out of their grooves, between 0.66 and 0.93 of the height.
        const float zone = 0.27f * height;
        const float band = 0.45f * zone / static_cast<float>(piston_ring_max);
        for (int k = 0; k < rings; ++k) {
            const float y = (0.66f * height) + (zone * (static_cast<float>(k) + 0.5f) / static_cast<float>(rings));
            add_ring(r, y - band, piston_metal_slot);
            add_ring(1.015f * r, y - band, piston_steel_slot);
            add_ring(1.015f * r, y + band, piston_steel_slot);
            add_ring(r, y + band, piston_steel_slot);
        }
        add_ring(r, 0.96f * height, piston_metal_slot);
        add_ring(0.94f * r, height, piston_metal_slot);
        add_lathe(builder, Placement{}, Lathe{.rings = profile, .segments = piston_round, .phase = Lathe_phase::side, .band_slots = slots});
    }
    {
        // The wrist pin: its ends show on the skirt's flats at +Z and -Z.
        const Placement place{.origin = glm::vec3{0.0f, piston_pin * height, 0.0f}, .x = -y_axis, .y = z_axis, .z = -x_axis};
        const float pin = std::min(0.17f * r, 0.12f * height);
        const std::array<Lathe_ring, 2> pins{{{pin, -0.992f * r}, {pin, 0.992f * r}}};
        add_lathe(builder, place, Lathe{.rings = pins, .segments = 8, .slot = piston_steel_slot});
    }
    builder.write(mesh);
}

//
// Cam: a plate cam lying flat: a profile around a bore, under a collar.
//

constexpr uint32_t cam_metal_slot = 0;
constexpr uint32_t cam_steel_slot = 1;
constexpr int      cam_points     = 32; // around the profile
constexpr int      cam_round      = 16; // around the collar

class Cam_form
{
public:
    explicit Cam_form(const Kit_parameter_values& values)
        : profile  {values.get_enum("profile")}
        , radius   {values.get_float("radius")}
        , lift     {values.get_float("lift")}
        , thickness{values.get_float("thickness")}
        , bore     {std::min(0.5f * values.get_float("bore"), 0.5f * radius)}
    {
    }

    // The profile's distance from the axis at point j: the most at point 0, on +X.
    [[nodiscard]] auto get_reach(const int j) const -> float
    {
        const float angle = two_pi * static_cast<float>(j) / static_cast<float>(cam_points);
        if (profile == "eccentric") {
            // A circle whose middle stands half the lift off the axis.
            const float off = 0.5f * lift;
            const float s   = off * std::sin(angle);
            return (off * std::cos(angle)) + std::sqrt(((radius + off) * (radius + off)) - (s * s));
        }
        if (profile == "pear") {
            // The base circle and a nose circle on +X, joined by the two lines that touch both.
            const float nose   = 0.45f * radius;
            const float off    = radius + lift - nose;
            const float theta  = (angle > pi) ? (two_pi - angle) : angle;
            const float touch  = std::acos((radius - nose) / off); // where the lines leave the base circle
            const float onto   = std::atan2(nose * std::sin(touch), off + (nose * std::cos(touch)));
            if (theta >= touch) {
                return radius;
            }
            if (theta >= onto) {
                return radius / std::cos(touch - theta);
            }
            const float s = off * std::sin(theta);
            return (off * std::cos(theta)) + std::sqrt(std::max((nose * nose) - (s * s), 0.0f));
        }
        if (profile == "heart") {
            // The reach falls evenly to the far side and rises evenly back.
            const int away = std::min(j, cam_points - j);
            return radius + (lift * (1.0f - (static_cast<float>(2 * away) / static_cast<float>(cam_points))));
        }
        // A snail: the reach falls evenly around the turn and steps back up before point 0.
        return radius + (lift * (1.0f - (static_cast<float>(j) / static_cast<float>(cam_points - 1))));
    }
    [[nodiscard]] auto get_top() const -> float
    {
        return 1.6f * thickness;
    }

    std::string_view profile  {};
    float            radius   {0.0f}; // of the base circle
    float            lift     {0.0f};
    float            thickness{0.0f};
    float            bore     {0.0f}; // radius of the bore
};

void generate_cam(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Cam_form cam{values};
    Kit_mesh_builder builder;
    std::array<std::vector<uint32_t>, 2> rim;  // at the base and at the top
    std::array<std::vector<uint32_t>, 2> hole;
    for (std::size_t level = 0; level < 2; ++level) {
        const float y = (level == 0) ? 0.0f : cam.thickness;
        for (int j = 0; j < cam_points; ++j) {
            const glm::vec2 point = get_kit_circle_point(j, cam_points);
            rim [level].push_back(builder.add_vertex(glm::vec3{cam.get_reach(j) * point.x, y, cam.get_reach(j) * point.y}));
            hole[level].push_back(builder.add_vertex(glm::vec3{cam.bore * point.x, y, cam.bore * point.y}));
        }
    }
    builder.set_slot(cam_metal_slot);
    for (std::size_t j = 0; j < cam_points; ++j) {
        const std::size_t n = (j + 1) % cam_points;
        builder.add_quad(rim[1][j], rim[1][n], hole[1][n], hole[1][j]);
        builder.add_quad(rim[0][n], rim[0][j], hole[0][j], hole[0][n]);
        builder.add_quad(rim[0][j], rim[0][n], rim[1][n], rim[1][j]);
        builder.add_quad(hole[0][n], hole[0][j], hole[1][j], hole[1][n]);
    }
    {
        // The collar: a sleeve in the bore that stands over the plate.
        const std::array<Lathe_ring, 4> rings{{{0.82f * cam.bore, 0.9f * cam.thickness}, {1.7f * cam.bore, 0.9f * cam.thickness}, {1.7f * cam.bore, cam.get_top()}, {0.82f * cam.bore, cam.get_top()}}};
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = cam_round, .bottom = Lathe_cap::loop, .top = Lathe_cap::loop, .slot = cam_steel_slot});
    }
    builder.write(mesh);
}

} // anonymous namespace

void add_machine_01_08_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape;
        shape.id              = "machine.gear";
        shape.kit             = "machine";
        shape.display_name    = "Gear";
        shape.description     =
            "Spur gear lying flat, its axis along Y: teeth around a rim on a solid body, a web or spokes, with a bored "
            "hub that stands over the face. Its pitch diameter is its teeth times the module. Two gears, or a gear and "
            "a rack, of one module engage when joined by their mesh connectors.";
        shape.tags            = {"decoration", "obstacle", "machine", "gear", "size:0.5x0.1"};
        shape.parameters      = {
            derive_int  (make_count_parameter ("teeth",      "Teeth", 16, gear_tooth_min, gear_tooth_max), 10, 24),
            derive_float(make_length_parameter("module",     "Pitch diameter for each tooth", 0.03f, Kit_default_scale::meters, 0.005f, 0.2f), 0.02f, 0.04f, 0.01f),
            derive_float(make_length_parameter("face_width", "Thickness of the teeth along Y", 0.08f, Kit_default_scale::meters, 0.01f, 1.0f), 0.06f, 0.12f, 0.02f),
            derive_float(make_length_parameter("bore",       "Diameter of the bore; at most 0.3 of the root circle", 0.08f, Kit_default_scale::meters, 0.01f, 1.0f), 0.06f, 0.1f, 0.02f),
            derive_enum (make_enum_parameter  ("body",       "A solid body, a web, or spokes", {"solid", "web", "spokes"}, 1)),
            derive_int  (make_count_parameter ("spokes",     "Spokes of a spoked body", 5, 3, 8), 4, 6),
            make_seed_parameter()
        };
        shape.material_slots  = {"metal"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 1100;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.generator       = generate_gear;
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const Gear_form form{values};
            out_connectors.push_back(make_base_connector());
            out_connectors.push_back(Kit_connector{.name = "axis", .position = glm::vec3{0.0f, form.get_top(), 0.0f}, .outward = y_axis, .up = z_axis});
            add_side_connector(out_connectors, "mesh", glm::vec3{form.pitch, 0.5f * form.width, 0.0f}, x_axis);
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "machine.rack";
        shape.kit             = "machine";
        shape.display_name    = "Gear rack";
        shape.description     =
            "Gear rack lying flat: a bar along X, its teeth times the pitch long (the pitch is pi times the module), "
            "with teeth toward +Z from the root line on z = 0. Racks chain end to start with their teeth in step, and "
            "a gear of the same module engages at the mesh connector, a quarter pitch beside the middle.";
        shape.tags            = {"decoration", "obstacle", "machine", "gear", "rack", "size:1.1x0.1"};
        shape.parameters      = {
            derive_int  (make_count_parameter ("teeth",      "Teeth", 12, rack_tooth_min, rack_tooth_max), 8, 20),
            derive_float(make_length_parameter("module",     "Pitch over pi: a meshing gear's module", 0.03f, Kit_default_scale::meters, 0.005f, 0.2f), 0.02f, 0.04f, 0.01f),
            derive_float(make_length_parameter("face_width", "Thickness along Y", 0.08f, Kit_default_scale::meters, 0.01f, 1.0f), 0.06f, 0.12f, 0.02f),
            derive_float(make_length_parameter("height",     "Size of the bar behind the teeth, along Z", 0.08f, Kit_default_scale::meters, 0.01f, 1.0f), 0.06f, 0.12f, 0.02f),
            make_seed_parameter()
        };
        shape.material_slots  = {"metal"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 260;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.origin_rule     = Kit_origin_rule::base_contains_origin; // the bar lies behind the root line
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x};
        shape.generator       = generate_rack;
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const Rack_form form{values};
            add_side_connector(out_connectors, "start", glm::vec3{-form.half_l, 0.0f, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "end",   glm::vec3{ form.half_l, 0.0f, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
            out_connectors.push_back(make_base_connector());
            add_side_connector(out_connectors, "mesh", glm::vec3{form.get_mesh_x(), 0.5f * form.width, form.get_pitch_z()}, z_axis);
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "machine.bolt";
        shape.kit             = "machine";
        shape.display_name    = "Bolt";
        shape.description     =
            "Bolt standing on its tip at the origin, its head up: a shank threaded from the tip in rings, a ridge to a "
            "turn, under a hexagon head, a socket head, a round head or a countersunk head; without a head it is a "
            "stud. The seat connector is under the head, and a nut joins the nut connector near the tip.";
        shape.tags            = {"decoration", "machine", "fastener", "bolt", "size:0.1x0.3"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("head",     "Hexagon, socket, round, countersunk, or none", {"hex", "socket", "round", "countersunk", "none"}, 0)),
            derive_float(make_length_parameter("diameter", "Diameter of the shank", 0.06f, Kit_default_scale::meters, 0.01f, 0.5f), 0.04f, 0.08f, 0.01f),
            derive_float(make_length_parameter("length",   "From the tip to under the head", 0.3f, Kit_default_scale::meters, 0.05f, 4.0f), 0.2f, 0.5f, 0.05f),
            derive_float(make_share_parameter ("thread",   "Share of the length that is threaded", 0.6f, 0.2f, 1.0f), 0.4f, 1.0f, 0.1f),
            derive_int  (make_count_parameter ("turns",    "Ridges of the thread", 10, bolt_turn_min, bolt_turn_max), 8, 16),
            make_seed_parameter()
        };
        shape.material_slots  = {"steel"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 540;
        shape.collision       = Kit_collision::convex_hull;
        shape.generator       = generate_bolt;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float length = values.get_float("length");
            const float down   = std::min(0.8f * values.get_float("diameter"), 0.4f * values.get_float("thread") * length);
            out_connectors.push_back(make_base_connector());
            out_connectors.push_back(Kit_connector{.name = "seat", .position = glm::vec3{0.0f, length, 0.0f}, .outward = -y_axis, .up = z_axis});
            out_connectors.push_back(Kit_connector{.name = "nut",  .position = glm::vec3{0.0f, down, 0.0f},   .outward = -y_axis, .up = z_axis});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "machine.nut";
        shape.kit             = "machine";
        shape.display_name    = "Nut";
        shape.description     =
            "Nut lying flat at its origin, bored along Y for a bolt of its diameter: a hexagon, a square, a round nut "
            "with two wings toward +X and -X, or a hexagon under a dome that closes the bore. Its top connector "
            "joins a bolt's nut connector.";
        shape.tags            = {"decoration", "machine", "fastener", "nut", "size:0.1x0.05"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("form",     "Hexagon, square, wing or cap nut", {"hex", "square", "wing", "cap"}, 0)),
            derive_float(make_length_parameter("diameter", "Diameter of the bolt it fits", 0.06f, Kit_default_scale::meters, 0.01f, 0.5f), 0.04f, 0.08f, 0.01f),
            derive_float(make_length_parameter("height",   "Thickness along Y", 0.05f, Kit_default_scale::meters, 0.005f, 0.5f), 0.04f, 0.06f, 0.01f),
            make_seed_parameter()
        };
        shape.material_slots  = {"steel"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 90;
        shape.collision       = Kit_collision::convex_hull;
        shape.generator       = generate_nut;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            add_base_and_top_connectors(out_connectors, get_nut_top(values));
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "machine.spring";
        shape.kit             = "machine";
        shape.display_name    = "Spring";
        shape.description     =
            "Coil spring, its wire along a helix of twelve stretches to a coil. Compression: standing on the origin, "
            "its length high, its two end coils closed and flat. Extension: lying along X, wound tight, with a hook at "
            "each end. Torsion: standing, wound tight, with a leg from each end, toward +Z on the ground and toward "
            "-Z at the top. Start and end are its two ends.";
        shape.tags            = {"decoration", "machine", "spring", "size:0.2x0.4"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("form",     "Compression, extension or torsion", {"compression", "extension", "torsion"}, 0)),
            derive_float(make_length_parameter("diameter", "Diameter of the coil at the wire's middle", 0.2f, Kit_default_scale::meters, 0.04f, 2.0f), 0.15f, 0.3f, 0.05f),
            derive_float(make_length_parameter("wire",     "Thickness of the wire", 0.02f, Kit_default_scale::meters, 0.004f, 0.1f), 0.015f, 0.03f, 0.005f),
            derive_int  (make_count_parameter ("coils",    "Coils, a compression spring's two end coils among them", 8, spring_coil_min, spring_coil_max), 5, 10),
            derive_float(make_length_parameter("length",   "Height of a compression spring; at least its closed height", 0.4f, Kit_default_scale::meters, 0.05f, 4.0f), 0.3f, 0.6f, 0.05f),
            make_seed_parameter()
        };
        shape.material_slots  = {"steel"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 780;
        shape.collision       = Kit_collision::convex_hull;
        shape.origin_rule     = Kit_origin_rule::base_contains_origin; // a lying spring rests on two lines, a torsion spring's leg on the ground
        shape.generator       = generate_spring;
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const Spring_values spring{values};
            if (spring.form == Spring_form::compression) {
                out_connectors.push_back(Kit_connector{.name = "start", .position = glm::vec3{0.0f}, .outward = -y_axis, .up = z_axis});
                out_connectors.push_back(Kit_connector{.name = "end", .position = glm::vec3{0.0f, spring.length, 0.0f}, .outward = y_axis, .up = z_axis});
            } else if (spring.form == Spring_form::extension) {
                // The hooks' far points.
                const float reach = (0.5f * spring_closed * spring.wire * static_cast<float>(spring.coils)) + spring.eye;
                add_side_connector(out_connectors, "start", glm::vec3{-reach, spring.axis_y, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
                add_side_connector(out_connectors, "end",   glm::vec3{ reach, spring.axis_y, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
            } else {
                // The legs' ends.
                const float leg = spring_leg * 2.0f * spring.radius;
                add_side_connector(out_connectors, "start", glm::vec3{spring.radius, 0.5f * spring.wire, leg}, z_axis);
                add_side_connector(out_connectors, "end",   glm::vec3{spring.radius, spring.length - (0.5f * spring.wire), -leg}, -z_axis);
            }
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "machine.bearing";
        shape.kit             = "machine";
        shape.display_name    = "Bearing";
        shape.description     =
            "Rolling bearing lying flat, its axis along Y: an inner race around the bore and an outer race, with balls, "
            "rollers or needles between them in a cage (slot metal). The rolling elements take 0.6 of the space "
            "between the bore and the outside, and the races share the rest.";
        shape.tags            = {"decoration", "machine", "bearing", "size:0.3x0.08"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("form",     "Balls, rollers or needles", {"ball", "roller", "needle"}, 0)),
            derive_float(make_length_parameter("bore",     "Diameter of the bore; at most 0.8 of the diameter", 0.1f, Kit_default_scale::meters, 0.01f, 2.0f), 0.08f, 0.12f, 0.02f),
            derive_float(make_length_parameter("diameter", "Outside diameter", 0.3f, Kit_default_scale::meters, 0.05f, 4.0f), 0.25f, 0.4f, 0.05f),
            derive_float(make_length_parameter("width",    "Thickness along Y", 0.08f, Kit_default_scale::meters, 0.01f, 1.0f), 0.06f, 0.1f, 0.02f),
            derive_int  (make_count_parameter ("elements", "Rolling elements", 10, bearing_element_min, bearing_element_max), 8, 14),
            make_seed_parameter()
        };
        shape.material_slots  = {"steel", "metal"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 880;
        shape.collision       = Kit_collision::convex_hull;
        shape.generator       = generate_bearing;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::z};
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            out_connectors.push_back(make_base_connector());
            out_connectors.push_back(Kit_connector{.name = "axis", .position = glm::vec3{0.0f, values.get_float("width"), 0.0f}, .outward = y_axis, .up = z_axis});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "machine.piston";
        shape.kit             = "machine";
        shape.display_name    = "Piston";
        shape.description     =
            "Piston standing on its skirt at the origin: a crown over piston rings (slot steel) in their grooves and a "
            "skirt, hollow from below, with a wrist pin (slot steel) along Z whose ends show on the skirt. The pin "
            "connector is at the pin's middle, facing down: a connecting rod's small end joins it.";
        shape.tags            = {"decoration", "obstacle", "machine", "piston", "size:0.3x0.3"};
        shape.parameters      = {
            derive_float(make_length_parameter("diameter", "Diameter of the piston", 0.3f, Kit_default_scale::meters, 0.05f, 2.0f), 0.2f, 0.4f, 0.05f),
            derive_float(make_length_parameter("height",   "Size along Y", 0.3f, Kit_default_scale::meters, 0.05f, 2.0f), 0.25f, 0.4f, 0.05f),
            derive_int  (make_count_parameter ("rings",    "Piston rings", 3, 1, piston_ring_max), 2, 4),
            make_seed_parameter()
        };
        shape.material_slots  = {"metal", "steel"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 600;
        shape.collision       = Kit_collision::convex_hull;
        shape.generator       = generate_piston;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float height = values.get_float("height");
            out_connectors.push_back(make_base_connector());
            out_connectors.push_back(Kit_connector{.name = "crown", .position = glm::vec3{0.0f, height, 0.0f}, .outward = y_axis, .up = z_axis});
            out_connectors.push_back(Kit_connector{.name = "pin", .position = glm::vec3{0.0f, piston_pin * height, 0.0f}, .outward = -y_axis, .up = z_axis});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "machine.cam";
        shape.kit             = "machine";
        shape.display_name    = "Cam";
        shape.description     =
            "Plate cam lying flat, its axis along Y through the origin: a profile of 32 points around a bore, under a "
            "collar (slot steel). Its reach is the radius of the base circle plus the lift toward +X. Eccentric: a "
            "circle off its middle. Pear: a nose circle joined to the base circle by straight flanks. Heart: an even fall and rise. Snail: an even fall "
            "around the turn and a step. A follower's contact joins the follower connector on +X.";
        shape.tags            = {"decoration", "machine", "cam", "size:0.4x0.1"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("profile",   "Eccentric, pear, heart or snail", {"eccentric", "pear", "heart", "snail"}, 1)),
            derive_float(make_length_parameter("radius",    "Radius of the base circle", 0.15f, Kit_default_scale::meters, 0.03f, 2.0f), 0.1f, 0.2f, 0.05f),
            derive_float(make_length_parameter("lift",      "How far the profile reaches beyond the base circle", 0.1f, Kit_default_scale::meters, 0.01f, 1.0f), 0.06f, 0.12f, 0.02f),
            derive_float(make_length_parameter("thickness", "Thickness of the plate", 0.06f, Kit_default_scale::meters, 0.01f, 1.0f), 0.04f, 0.08f, 0.02f),
            derive_float(make_length_parameter("bore",      "Diameter of the bore; at most the radius", 0.06f, Kit_default_scale::meters, 0.01f, 1.0f), 0.04f, 0.08f, 0.02f),
            make_seed_parameter()
        };
        shape.material_slots  = {"metal", "steel"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 400;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.origin_rule     = Kit_origin_rule::base_contains_origin; // the profile reaches further toward +X
        shape.generator       = generate_cam;
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const Cam_form cam{values};
            out_connectors.push_back(make_base_connector());
            out_connectors.push_back(Kit_connector{.name = "axis", .position = glm::vec3{0.0f, cam.get_top(), 0.0f}, .outward = y_axis, .up = z_axis});
            add_side_connector(out_connectors, "follower", glm::vec3{cam.get_reach(0), 0.5f * cam.thickness, 0.0f}, x_axis);
        };
        shapes.push_back(std::move(shape));
    }
}

} // namespace editor
