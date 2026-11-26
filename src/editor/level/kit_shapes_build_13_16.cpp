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

// The shapes of build-order lines 13 - 16 of doc/plans/level_kit_more_shapes.md (doc/editor/level_tools.md).
// Every mesh's topology follows from its integer values and integer draws alone; floats only move vertices.

namespace editor {

namespace {

constexpr float pi           = std::numbers::pi_v<float>;
constexpr float two_pi       = 2.0f * pi;
constexpr float golden_angle = 2.39996323f;
constexpr glm::vec3 y_axis{0.0f, 1.0f, 0.0f};

[[nodiscard]] auto next_signed(Kit_random& random) -> float
{
    return random.next_range(-1.0f, 1.0f);
}

// Unit vector at the angle around +Y, from +X toward -Z.
[[nodiscard]] auto around_y(const float angle) -> glm::vec3
{
    return glm::vec3{std::cos(angle), 0.0f, -std::sin(angle)};
}

// The right arc of a pointed head: a circle about (-center, 0) from the foot to the crown at `rise`, heights scaled by
// y_scale. A head lower than 1.2 of its half span is the head of that rise pressed down, so the arc never bulges.
class Pointed_arc
{
public:
    Pointed_arc(const float half_span, const float wanted_rise)
        : rise   {std::max(wanted_rise, 1.2f * half_span)}
        , center {((rise * rise) - (half_span * half_span)) / (2.0f * half_span)}
        , radius {half_span + center}
        , y_scale{wanted_rise / rise}
    {
    }

    float rise   {0.0f}; // of the circle's arc
    float center {0.0f};
    float radius {0.0f};
    float y_scale{1.0f};
};

} // anonymous namespace

auto make_kit_arch_head_points(
    const Kit_arch_head head,
    const float         half_span,
    const float         rise,
    const int           segments,
    const float         jitter,
    Kit_random* const   random
) -> std::vector<glm::vec2>
{
    const int n = (head == Kit_arch_head::pointed) ? (2 * std::max(1, segments / 2)) : std::max(2, segments);
    const Pointed_arc arc{half_span, rise};
    const float apex_angle = std::atan2(arc.rise, arc.center);
    std::vector<glm::vec2> points(static_cast<std::size_t>(n) + 1);
    for (int i = 0; (2 * i) <= n; ++i) {
        const bool foot  = (i == 0);
        const bool crown = ((2 * i) == n);
        glm::vec2 point{half_span, 0.0f};
        if (crown) {
            point = glm::vec2{0.0f, rise};
        } else if (!foot && (head == Kit_arch_head::round)) {
            const float angle = pi * static_cast<float>(i) / static_cast<float>(n);
            point = glm::vec2{half_span * std::cos(angle), rise * std::sin(angle)};
        } else if (!foot) {
            const float angle = apex_angle * static_cast<float>(2 * i) / static_cast<float>(n);
            point = glm::vec2{(arc.radius * std::cos(angle)) - arc.center, arc.y_scale * arc.radius * std::sin(angle)};
        }
        points[static_cast<std::size_t>(i)]     = point;
        points[static_cast<std::size_t>(n - i)] = glm::vec2{-point.x, point.y};
    }
    if (random != nullptr) {
        for (std::size_t i = 1; (i + 1) < points.size(); ++i) {
            points[i] *= 1.0f + (jitter * next_signed(*random));
        }
    }
    return points;
}

namespace {

//
// Window wall: the wall's faces are fans about its corners over the outermost of five rings around the opening; the
// rings are offsets of the opening's outline, so every quad between two of them is planar.
//

constexpr uint32_t window_wall_slot = 0;
constexpr uint32_t window_trim_slot = 1; // the surround: the reveal, the splay, the band and half the groove
constexpr uint32_t window_wood_slot = 2; // the casing and its bars

enum class Window_head : unsigned int { flat = 0, round, pointed };

[[nodiscard]] auto get_window_head(const std::string_view name) -> Window_head
{
    if (name == "round") {
        return Window_head::round;
    }
    if (name == "pointed") {
        return Window_head::pointed;
    }
    return Window_head::flat;
}

// A vertex of the opening's outline: `out` moves it per meter the jambs and the head move out, `sill` per meter the sill does.
class Window_vertex
{
public:
    glm::vec2 point{0.0f};
    glm::vec2 out  {0.0f};
    glm::vec2 sill {0.0f};
};

[[nodiscard]] auto window_offset(const Window_vertex& vertex, const float distance, const float sill_distance) -> glm::vec2
{
    return vertex.point + (distance * vertex.out) + (sill_distance * vertex.sill);
}

// Outward unit normal of the edge from a to b of a counter-clockwise outline.
[[nodiscard]] auto edge_normal(const glm::vec2 a, const glm::vec2 b) -> glm::vec2
{
    const glm::vec2 d = b - a;
    return glm::normalize(glm::vec2{d.y, -d.x});
}

// What the opening is at the values.
class Window_form
{
public:
    explicit Window_form(const Kit_parameter_values& values);

    [[nodiscard]] auto get_open_y(float x) const -> float; // the head's height over x, 0 <= x <= half_width

    Window_head head      {Window_head::flat};
    int         segments  {2};
    float       half_width{0.0f};
    float       sill_y    {0.0f};
    float       spring_y  {0.0f}; // a flat head's top
    float       rise      {0.0f};
};

Window_form::Window_form(const Kit_parameter_values& values)
    : head      {get_window_head(values.get_enum("head"))}
    , segments  {2 * std::max(1, values.get_int("segments") / 2)}
    , half_width{0.5f * values.get_float("opening_width")}
    , sill_y    {values.get_float("sill_height")}
{
    const float opening_height = values.get_float("opening_height");
    if (head == Window_head::round) {
        rise = std::min(half_width, 0.5f * opening_height);
    } else if (head == Window_head::pointed) {
        rise = std::min(1.5f * half_width, 0.6f * opening_height);
    }
    spring_y = sill_y + opening_height - rise;
}

auto Window_form::get_open_y(const float x) const -> float
{
    if (head == Window_head::round) {
        const float share = x / half_width;
        return spring_y + (rise * std::sqrt(std::max(1.0f - (share * share), 0.0f)));
    }
    if (head == Window_head::pointed) {
        const Pointed_arc arc{half_width, rise};
        const float       reach = x + arc.center;
        return spring_y + (arc.y_scale * std::sqrt(std::max((arc.radius * arc.radius) - (reach * reach), 0.0f)));
    }
    return spring_y;
}

// The outline counter-clockwise seen from +Z: the sill's left and right ends, then the head from right to left. The
// left half is the right half's mirror image.
[[nodiscard]] auto make_window_loop(const Window_form& form) -> std::vector<Window_vertex>
{
    const float a = form.half_width;
    std::vector<Window_vertex> loop;
    loop.push_back(Window_vertex{{-a, form.sill_y}, {-1.0f, 0.0f}, {0.0f, -1.0f}});
    loop.push_back(Window_vertex{{ a, form.sill_y}, { 1.0f, 0.0f}, {0.0f, -1.0f}});
    if (form.head == Window_head::flat) {
        loop.push_back(Window_vertex{{ a, form.spring_y}, { 1.0f, 1.0f}, {0.0f, 0.0f}});
        loop.push_back(Window_vertex{{-a, form.spring_y}, {-1.0f, 1.0f}, {0.0f, 0.0f}});
        return loop;
    }
    const Kit_arch_head          kind = (form.head == Window_head::round) ? Kit_arch_head::round : Kit_arch_head::pointed;
    const std::vector<glm::vec2> arc  = make_kit_arch_head_points(kind, a, form.rise, form.segments);
    const std::size_t            half = arc.size() / 2; // the crown
    for (std::size_t i = 0; i <= half; ++i) {
        const glm::vec2 before = (i == 0) ? glm::vec2{1.0f, 0.0f} : edge_normal(arc[i - 1], arc[i]);
        const glm::vec2 after  = (i == half) ? glm::vec2{-before.x, before.y} : edge_normal(arc[i], arc[i + 1]);
        glm::vec2 out = (before + after) / (1.0f + glm::dot(before, after));
        if (i == half) {
            out.x = 0.0f;
        }
        loop.push_back(Window_vertex{arc[i] + glm::vec2{0.0f, form.spring_y}, out, {0.0f, 0.0f}});
    }
    for (std::size_t i = half; i > 0; --i) {
        const Window_vertex mirrored = loop[2 + (i - 1)];
        loop.push_back(Window_vertex{{-mirrored.point.x, mirrored.point.y}, {-mirrored.out.x, mirrored.out.y}, {0.0f, 0.0f}});
    }
    return loop;
}

// The largest distance the whole outline moves in before one of its edges closes.
[[nodiscard]] auto get_window_inward_room(const std::vector<Window_vertex>& loop) -> float
{
    float room = 1.0e6f;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const std::size_t j      = (i + 1) % loop.size();
        const glm::vec2   edge   = loop[j].point - loop[i].point;
        const float       length = glm::length(edge);
        const glm::vec2   spread = (loop[j].out + loop[j].sill) - (loop[i].out + loop[i].sill);
        const float       rate   = glm::dot(spread, edge) / length; // the edge's growth per meter moved out
        room = std::min(room, length / std::max(rate, 1.0e-6f));
    }
    return room;
}

// One ring of the outline at a depth: its vertices on the front (+Z) and on the back.
class Window_ring
{
public:
    std::vector<uint32_t> front;
    std::vector<uint32_t> back;
};

[[nodiscard]] auto add_window_ring(
    Kit_mesh_builder&                 builder,
    const std::vector<Window_vertex>& loop,
    const float                       distance,
    const float                       sill_distance,
    const float                       z
) -> Window_ring
{
    Window_ring ring;
    for (const Window_vertex& vertex : loop) {
        const glm::vec2 p = window_offset(vertex, distance, sill_distance);
        ring.front.push_back(builder.add_vertex(glm::vec3{p.x, p.y,  z}));
        ring.back .push_back(builder.add_vertex(glm::vec3{p.x, p.y, -z}));
    }
    return ring;
}

// The faces between an inner and an outer ring, front and back.
void add_window_band(Kit_mesh_builder& builder, const Window_ring& inner, const Window_ring& outer)
{
    const std::size_t count = inner.front.size();
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t j = (i + 1) % count;
        builder.add_quad(inner.front[i], outer.front[i], outer.front[j], inner.front[j]);
        builder.add_quad(inner.back[i],  inner.back[j],  outer.back[j],  outer.back[i]);
    }
}

enum class Window_side : unsigned int { toward_opening = 0, toward_wall };

// The faces through the wall between a ring's front and its back.
void add_window_lining(Kit_mesh_builder& builder, const Window_ring& ring, const Window_side side)
{
    const std::size_t count = ring.front.size();
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t j = (i + 1) % count;
        if (side == Window_side::toward_opening) {
            builder.add_quad(ring.front[i], ring.front[j], ring.back[j], ring.back[i]);
        } else {
            builder.add_quad(ring.front[i], ring.back[i], ring.back[j], ring.front[j]);
        }
    }
}

enum class Wall_face : unsigned int { front = 0, back };

// A facet of a wall face given counter-clockwise seen from +Z; the back's is reversed.
void add_face_triangle(Kit_mesh_builder& builder, const Wall_face face, const uint32_t a, const uint32_t b, const uint32_t c)
{
    if (face == Wall_face::front) {
        builder.add_triangle(a, b, c);
    } else {
        builder.add_triangle(a, c, b);
    }
}

void add_face_quad(Kit_mesh_builder& builder, const Wall_face face, const uint32_t a, const uint32_t b, const uint32_t c, const uint32_t d)
{
    if (face == Wall_face::front) {
        builder.add_quad(a, b, c, d);
    } else {
        builder.add_quad(a, d, c, b);
    }
}

constexpr float window_side_room  = 0.32f; // the surround keeps its sizes where the jambs and the header are this wide
constexpr float window_sill_room  = 0.38f;
constexpr float window_least_room = 0.15f; // check_combination: the least jamb and header

void generate_window_wall(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float length    = values.get_float("length");
    const float height    = values.get_float("height");
    const float thickness = values.get_float("thickness");
    const int   mullions  = values.get_int("mullions");
    const int   transoms  = values.get_int("transoms");
    const Window_form form{values};
    const std::vector<Window_vertex> loop = make_window_loop(form);

    const float top_y  = form.spring_y + form.rise;
    const float jamb   = (0.5f * length) - form.half_width;
    const float header = height - top_y;
    const float scale  = std::min(1.0f, std::min(jamb, header) / window_side_room);
    const float low    = std::min(1.0f, form.sill_y / window_sill_room);
    const float half_t = 0.5f * thickness;
    const float splay  = 0.06f * scale;
    const float band   = 0.10f * scale;
    const float bevel  = 0.03f * scale;
    const float cut    = std::min(bevel, 0.16f * thickness);
    const float reveal = std::min(0.9f * splay, 0.28f * thickness);
    // The sill slopes down to the face over a wider splay.
    const float sill_splay = 0.13f * low;
    const float sill_band  = 0.09f * low;
    const float sill_bevel = 0.03f * low;

    Kit_mesh_builder builder;
    const std::array<Window_ring, 5> rings{
        add_window_ring(builder, loop, 0.0f,                          0.0f,                                         half_t - reveal),
        add_window_ring(builder, loop, splay,                         sill_splay,                                   half_t),
        add_window_ring(builder, loop, splay + band,                  sill_splay + sill_band,                       half_t),
        add_window_ring(builder, loop, splay + band + bevel,          sill_splay + sill_band + sill_bevel,          half_t - cut),
        add_window_ring(builder, loop, splay + band + (2.0f * bevel), sill_splay + sill_band + (2.0f * sill_bevel), half_t)
    };
    builder.set_slot(window_trim_slot);
    add_window_lining(builder, rings[0], Window_side::toward_opening);
    add_window_band(builder, rings[0], rings[1]);
    add_window_band(builder, rings[1], rings[2]);
    add_window_band(builder, rings[2], rings[3]);
    builder.set_slot(window_wall_slot);
    add_window_band(builder, rings[3], rings[4]);

    // The wall's corners: left and right of the bottom, right and left of the top.
    std::array<uint32_t, 4> front{};
    std::array<uint32_t, 4> back{};
    const std::array<glm::vec2, 4> corners{{{-0.5f * length, 0.0f}, {0.5f * length, 0.0f}, {0.5f * length, height}, {-0.5f * length, height}}};
    for (std::size_t k = 0; k < corners.size(); ++k) {
        front[k] = builder.add_vertex(glm::vec3{corners[k].x, corners[k].y,  half_t});
        back[k]  = builder.add_vertex(glm::vec3{corners[k].x, corners[k].y, -half_t});
    }
    const std::size_t count = loop.size();
    const bool        flat  = (form.head == Window_head::flat);
    const std::size_t crown = flat ? 2 : (2 + (static_cast<std::size_t>(form.segments) / 2));
    const std::size_t left  = flat ? 3 : crown; // the first outline vertex of the top left corner's fan
    for (const Wall_face face : {Wall_face::front, Wall_face::back}) {
        const std::vector<uint32_t>&   ring = (face == Wall_face::front) ? rings[4].front : rings[4].back;
        const std::array<uint32_t, 4>& wall = (face == Wall_face::front) ? front : back;
        add_face_quad    (builder, face, wall[0], wall[1], ring[1], ring[0]);
        add_face_triangle(builder, face, wall[1], wall[2], ring[1]);
        for (std::size_t i = 1; i < crown; ++i) {
            add_face_triangle(builder, face, wall[2], ring[i + 1], ring[i]);
        }
        if (flat) {
            add_face_quad(builder, face, wall[2], wall[3], ring[3], ring[2]);
        } else {
            add_face_triangle(builder, face, wall[2], wall[3], ring[crown]);
        }
        for (std::size_t i = left; i < count; ++i) {
            add_face_triangle(builder, face, wall[3], ring[(i + 1) % count], ring[i]);
        }
        add_face_triangle(builder, face, wall[3], wall[0], ring[0]);
    }
    builder.add_quad(front[1], back[1],  back[2], front[2]); // +x end
    builder.add_quad(front[0], front[3], back[3], back[0]);  // -x end
    builder.add_quad(front[3], front[2], back[2], back[3]);  // top
    builder.add_quad(front[0], back[0],  back[1], front[1]); // bottom

    // The casing stands in the middle of the wall, its outer edge inside the wall.
    builder.set_slot(window_wood_slot);
    const float casing       = std::min(0.05f, 0.4f * get_window_inward_room(loop));
    const float embed        = 0.2f * casing;
    const float casing_depth = std::min(0.03f, 0.5f * (half_t - reveal));
    const Window_ring casing_inner = add_window_ring(builder, loop, -casing, -casing, casing_depth);
    const Window_ring casing_outer = add_window_ring(builder, loop, embed,   embed,   casing_depth);
    add_window_band  (builder, casing_inner, casing_outer);
    add_window_lining(builder, casing_inner, Window_side::toward_opening);
    add_window_lining(builder, casing_outer, Window_side::toward_wall);

    const float bar = 0.7f * casing;
    for (int k = 1; k <= mullions; ++k) {
        const float x     = form.half_width * static_cast<float>((2 * k) - mullions - 1) / static_cast<float>(mullions + 1);
        const float bar_y = form.get_open_y(std::max(std::abs(x) - (0.5f * bar), 0.0f)) + embed;
        builder.add_box(glm::vec3{x - (0.5f * bar), form.sill_y - embed, -0.75f * casing_depth}, glm::vec3{x + (0.5f * bar), bar_y, 0.75f * casing_depth});
    }
    for (int k = 1; k <= transoms; ++k) {
        // Under a curved head the highest transom lies under the springing line.
        const float share = static_cast<float>(k) / static_cast<float>(flat ? (transoms + 1) : transoms);
        const float y     = form.sill_y + (share * (form.spring_y - form.sill_y)) - (flat ? 0.0f : (0.5f * bar));
        const float reach = form.half_width + embed;
        builder.add_box(glm::vec3{-reach, y - (0.5f * bar), -0.55f * casing_depth}, glm::vec3{reach, y + (0.5f * bar), 0.55f * casing_depth});
    }
    builder.write(mesh);
}

[[nodiscard]] auto check_window_wall(const Kit_parameter_values& values) -> std::string
{
    const float jamb   = 0.5f * (values.get_float("length") - values.get_float("opening_width"));
    const float header = values.get_float("height") - values.get_float("sill_height") - values.get_float("opening_height");
    if (jamb < window_least_room) {
        return fmt::format("{}: the opening leaves {} m of wall beside it, less than {} m", values.get_shape()->id, jamb, window_least_room);
    }
    if (header < window_least_room) {
        return fmt::format("{}: the opening leaves {} m of wall above it, less than {} m", values.get_shape()->id, header, window_least_room);
    }
    return {};
}

//
// Pediment: nested outlines step back from the raking cornice's face to the recessed field; dentils stand plumb under
// the rakes.
//

constexpr uint32_t pediment_trim_slot  = 0;
constexpr uint32_t pediment_wall_slot  = 1; // the field
constexpr int      pediment_dentil_max = 16;

// The pediment's sizes at the values; an outline k is a triangle inside the rakes, its base above the bottom.
class Pediment_form
{
public:
    explicit Pediment_form(const Kit_parameter_values& values);

    [[nodiscard]] auto get_rake_y    (std::size_t k, float x) const -> float; // outline k's rake over x >= 0
    [[nodiscard]] auto get_half_width(std::size_t k) const -> float;          // of outline k's base

    float half      {0.0f};
    float slope     {0.0f};
    float unit      {0.0f}; // the mouldings' measure
    float eave      {0.0f}; // height of the upright ends
    float apex      {0.0f};
    float half_depth{0.0f};
    std::array<float, 4> inset {}; // of each outline from the rakes, measured across them; 0 the outer one
    std::array<float, 4> bottom{}; // of each outline's base
    std::array<float, 4> face  {}; // z of the face inside each outline
};

Pediment_form::Pediment_form(const Kit_parameter_values& values)
    : half      {0.5f * values.get_float("width")}
    , slope     {std::tan(glm::radians(values.get_float("pitch")))}
    , half_depth{0.5f * values.get_float("depth")}
{
    const float depth = 2.0f * half_depth;
    unit = std::min(std::min(0.08f * half, 0.26f * half * slope), 1.2f * depth);
    eave = 0.55f * unit;
    apex = eave + (half * slope);
    // The bed lies two steps behind the corona, which leaves the dentils room to stand out.
    const float step = std::min(0.3f * unit, 0.07f * depth);
    inset  = {0.0f, 0.40f * unit, 0.90f * unit, 1.55f * unit};
    bottom = {0.0f, 0.30f * unit, 0.55f * unit, 0.80f * unit};
    face   = {half_depth, half_depth - step, half_depth - (3.0f * step), 0.0f};
    face[3] = face[2] - std::min(values.get_float("recess"), 0.5f * face[2]);
}

auto Pediment_form::get_rake_y(const std::size_t k, const float x) const -> float
{
    return apex - (inset[k] * std::sqrt(1.0f + (slope * slope))) - (x * slope);
}

auto Pediment_form::get_half_width(const std::size_t k) const -> float
{
    return (get_rake_y(k, 0.0f) - bottom[k]) / slope;
}

// A convex outline, counter-clockwise seen from +Z, from z_front back to z_back.
void add_extrusion(Kit_mesh_builder& builder, const std::span<const glm::vec2> outline, const float z_front, const float z_back)
{
    std::vector<uint32_t> front;
    std::vector<uint32_t> back;
    for (const glm::vec2& point : outline) {
        front.push_back(builder.add_vertex(glm::vec3{point.x, point.y, z_front}));
        back .push_back(builder.add_vertex(glm::vec3{point.x, point.y, z_back}));
    }
    builder.add_polygon(front);
    const std::vector<uint32_t> reversed(back.rbegin(), back.rend());
    builder.add_polygon(reversed);
    for (std::size_t i = 0; i < front.size(); ++i) {
        const std::size_t j = (i + 1) % front.size();
        builder.add_quad(front[i], back[i], back[j], front[j]);
    }
}

// Outline k of a face at a depth: the base's left and right ends and the apex.
[[nodiscard]] auto add_pediment_outline(Kit_mesh_builder& builder, const Pediment_form& form, const std::size_t k, const float z) -> std::array<uint32_t, 3>
{
    const float x = form.get_half_width(k);
    return std::array<uint32_t, 3>{
        builder.add_vertex(glm::vec3{-x, form.bottom[k], z}),
        builder.add_vertex(glm::vec3{ x, form.bottom[k], z}),
        builder.add_vertex(glm::vec3{0.0f, form.get_rake_y(k, 0.0f), z})
    };
}

void generate_pediment(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Pediment_form form{values};
    const int dentils = std::clamp(values.get_int("dentils"), 0, pediment_dentil_max);
    Kit_mesh_builder builder;

    // The outer outline: the bottom's left and right ends, the right eave, the apex, the left eave.
    const std::array<glm::vec2, 5> outline{{{-form.half, 0.0f}, {form.half, 0.0f}, {form.half, form.eave}, {0.0f, form.apex}, {-form.half, form.eave}}};
    std::array<std::array<uint32_t, 5>, 2> outer{};
    for (const Wall_face face : {Wall_face::front, Wall_face::back}) {
        const float       side = (face == Wall_face::front) ? 1.0f : -1.0f;
        const std::size_t f    = (face == Wall_face::front) ? 0 : 1;
        builder.set_slot(pediment_trim_slot);
        for (std::size_t i = 0; i < outline.size(); ++i) {
            outer[f][i] = builder.add_vertex(glm::vec3{outline[i].x, outline[i].y, side * form.face[0]});
        }
        std::array<uint32_t, 3> inner = add_pediment_outline(builder, form, 1, side * form.face[0]);
        add_face_quad    (builder, face, outer[f][0], outer[f][1], inner[1], inner[0]);
        add_face_triangle(builder, face, outer[f][1], outer[f][2], inner[1]);
        add_face_triangle(builder, face, outer[f][2], outer[f][3], inner[2]);
        add_face_triangle(builder, face, outer[f][2], inner[2],    inner[1]);
        add_face_triangle(builder, face, outer[f][0], inner[0],    outer[f][4]);
        add_face_triangle(builder, face, outer[f][4], inner[2],    outer[f][3]);
        add_face_triangle(builder, face, outer[f][4], inner[0],    inner[2]);
        for (std::size_t k = 1; k <= 3; ++k) {
            // The step down along outline k, then the band to the next outline or the field.
            const std::array<uint32_t, 3> lower = add_pediment_outline(builder, form, k, side * form.face[k]);
            for (std::size_t i = 0; i < 3; ++i) {
                const std::size_t j = (i + 1) % 3;
                add_face_quad(builder, face, inner[i], inner[j], lower[j], lower[i]);
            }
            if (k == 3) {
                builder.set_slot(pediment_wall_slot);
                add_face_triangle(builder, face, lower[0], lower[1], lower[2]);
                break;
            }
            inner = add_pediment_outline(builder, form, k + 1, side * form.face[k]);
            for (std::size_t i = 0; i < 3; ++i) {
                const std::size_t j = (i + 1) % 3;
                add_face_quad(builder, face, lower[i], lower[j], inner[j], inner[i]);
            }
        }
    }
    builder.set_slot(pediment_trim_slot);
    for (std::size_t i = 0; i < outline.size(); ++i) {
        const std::size_t j = (i + 1) % outline.size();
        builder.add_quad(outer[0][i], outer[1][i], outer[1][j], outer[0][j]); // bottom, eaves and rakes
    }

    // Dentils on the bed band (between outlines 2 and 3), their heads inside the corona.
    if (dentils > 0) {
        const float spacing = (0.98f * form.get_half_width(3)) / (static_cast<float>(dentils) + 0.25f);
        const float proud   = form.face[1] - form.face[2];
        const float z_front = form.face[2] + (0.75f * proud);
        const float z_back  = form.face[2] - (0.25f * proud);
        const float foot    = 0.12f * form.unit; // above the field's edge
        const float head    = 0.05f * form.unit; // into the corona
        for (int k = 0; k < dentils; ++k) {
            const float x0 = (static_cast<float>(k) + 0.25f) * spacing;
            const float x1 = x0 + (0.5f * spacing);
            const std::array<glm::vec2, 4> right{{
                {x0, form.get_rake_y(3, x0) + foot}, {x1, form.get_rake_y(3, x1) + foot}, {x1, form.get_rake_y(2, x1) + head}, {x0, form.get_rake_y(2, x0) + head}
            }};
            const std::array<glm::vec2, 4> mirrored{{
                {-right[1].x, right[1].y}, {-right[0].x, right[0].y}, {-right[3].x, right[3].y}, {-right[2].x, right[2].y}
            }};
            add_extrusion(builder, right,     z_front,  z_back);
            add_extrusion(builder, mirrored,  z_front,  z_back);
            add_extrusion(builder, right,    -z_back,  -z_front);
            add_extrusion(builder, mirrored, -z_back,  -z_front);
        }
    }
    builder.write(mesh);
}

//
// Stepped platform: tiers of slabs with a chamfered edge, the top a deck inside a rim.
//

constexpr uint32_t platform_stone_slot = 0;
constexpr uint32_t platform_floor_slot = 1; // the deck
constexpr int      platform_tier_max   = 9;

// A rectangle's corners at a height, counter-clockwise seen from above.
[[nodiscard]] auto add_rectangle(Kit_mesh_builder& builder, const float half_x, const float half_z, const float y) -> std::array<uint32_t, 4>
{
    return std::array<uint32_t, 4>{
        builder.add_vertex(glm::vec3{ half_x, y, -half_z}),
        builder.add_vertex(glm::vec3{-half_x, y, -half_z}),
        builder.add_vertex(glm::vec3{-half_x, y,  half_z}),
        builder.add_vertex(glm::vec3{ half_x, y,  half_z})
    };
}

// The faces from a ring to the one above or inside it.
void add_rectangle_band(Kit_mesh_builder& builder, const std::array<uint32_t, 4>& lower, const std::array<uint32_t, 4>& upper)
{
    for (std::size_t i = 0; i < 4; ++i) {
        const std::size_t j = (i + 1) % 4;
        builder.add_quad(lower[i], lower[j], upper[j], upper[i]);
    }
}

// What the tiers are at the values: the inset shrinks where the tiers would leave less than 0.4 of the smaller side.
class Platform_form
{
public:
    explicit Platform_form(const Kit_parameter_values& values)
        : half_x{0.5f * values.get_float("width")}
        , half_z{0.5f * values.get_float("depth")}
        , tiers {std::clamp(values.get_int("tiers"), 1, platform_tier_max)}
        , rise  {values.get_float("tier_rise")}
    {
        const float least = std::min(half_x, half_z);
        inset   = std::min(values.get_float("tier_inset"), (0.6f * least) / static_cast<float>(std::max(tiers - 1, 1)));
        chamfer = std::min(std::min(0.06f, 0.12f * rise), std::min(0.25f * inset, 0.1f * least));
    }

    float half_x {0.0f};
    float half_z {0.0f};
    int   tiers  {1};
    float rise   {0.0f};
    float inset  {0.0f};
    float chamfer{0.0f};
};

void generate_stepped_platform(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Platform_form form{values};
    Kit_mesh_builder builder;
    builder.set_slot(platform_stone_slot);
    std::array<uint32_t, 4> ring = add_rectangle(builder, form.half_x, form.half_z, 0.0f);
    builder.add_quad(ring[3], ring[2], ring[1], ring[0]);
    float tier_x = form.half_x;
    float tier_z = form.half_z;
    for (int k = 0; k < form.tiers; ++k) {
        const float top = form.rise * static_cast<float>(k + 1);
        tier_x = form.half_x - (form.inset * static_cast<float>(k));
        tier_z = form.half_z - (form.inset * static_cast<float>(k));
        if (k > 0) {
            const std::array<uint32_t, 4> foot = add_rectangle(builder, tier_x, tier_z, form.rise * static_cast<float>(k));
            add_rectangle_band(builder, ring, foot); // the tread
            ring = foot;
        }
        const std::array<uint32_t, 4> edge = add_rectangle(builder, tier_x, tier_z, top - form.chamfer);
        add_rectangle_band(builder, ring, edge);
        tier_x -= form.chamfer;
        tier_z -= form.chamfer;
        ring = add_rectangle(builder, tier_x, tier_z, top);
        add_rectangle_band(builder, edge, ring);
    }
    const float rim = std::min(0.18f, 0.25f * std::min(tier_x, tier_z));
    const std::array<uint32_t, 4> deck = add_rectangle(builder, tier_x - rim, tier_z - rim, form.rise * static_cast<float>(form.tiers));
    add_rectangle_band(builder, ring, deck);
    builder.set_slot(platform_floor_slot);
    builder.add_quad(deck[0], deck[1], deck[2], deck[3]);
    builder.write(mesh);
}

//
// Blades: the leaves of the succulent and the grass tuft. A blade is rings of three or four points along an arc.
//

// A blade's midrib: an arc in the vertical plane of `out` whose direction turns from elevation_0 at its base to
// elevation_1 at its tip; where it descends it descends by the share `descent`, which fit_blade_arc() sets.
class Blade_arc
{
public:
    glm::vec3 base       {0.0f};
    glm::vec3 out        {1.0f, 0.0f, 0.0f}; // unit, horizontal
    float     length     {1.0f};
    float     elevation_0{0.0f};             // radians above the horizontal
    float     elevation_1{0.0f};
    float     power      {1.0f};             // above 1 the base stays straight and the end bends
    float     roll       {0.0f};             // radians the section turns about the midrib
    float     descent    {1.0f};
};

class Blade_frame
{
public:
    glm::vec3 center;
    glm::vec3 along;
    glm::vec3 up;
    glm::vec3 side; // up x along
};

// The arc's direction of travel at s before normalization, (outward, upward).
[[nodiscard]] auto blade_velocity(const Blade_arc& arc, const float s) -> glm::vec2
{
    const float elevation = arc.elevation_0 + ((arc.elevation_1 - arc.elevation_0) * std::pow(s, arc.power));
    const float rise      = std::sin(elevation);
    return glm::vec2{std::cos(elevation), std::max(rise, 0.0f) + (arc.descent * std::min(rise, 0.0f))};
}

[[nodiscard]] auto blade_frame(const Blade_arc& arc, const float s) -> Blade_frame
{
    constexpr int steps = 10;
    const float ds = s / static_cast<float>(steps);
    glm::vec2 reach{0.0f};
    for (int k = 0; k < steps; ++k) {
        reach += (ds * arc.length) * blade_velocity(arc, (static_cast<float>(k) + 0.5f) * ds);
    }
    const glm::vec2 heading   = glm::normalize(blade_velocity(arc, s));
    const glm::vec3 along     = (heading.x * arc.out) + (heading.y * y_axis);
    const glm::vec3 flat_up   = (-heading.y * arc.out) + (heading.x * y_axis);
    const glm::vec3 flat_side = glm::cross(y_axis, arc.out);
    const float     c         = std::cos(arc.roll);
    const float     t         = std::sin(arc.roll);
    return Blade_frame{
        .center = arc.base + (reach.x * arc.out) + (reach.y * y_axis),
        .along  = along,
        .up     = (c * flat_up) - (t * flat_side),
        .side   = (c * flat_side) + (t * flat_up)
    };
}

// Sets the arc's descent so it ends no lower than lowest: the largest share that keeps it, a min of continuous terms.
void fit_blade_arc(Blade_arc& arc, const float lowest)
{
    constexpr int steps = 20;
    float ascent = 0.0f;
    float fall   = 0.0f;
    arc.descent = 1.0f;
    for (int k = 0; k < steps; ++k) {
        const float rise = blade_velocity(arc, (static_cast<float>(k) + 0.5f) / static_cast<float>(steps)).y;
        ascent += std::max(rise, 0.0f);
        fall   += std::max(-rise, 0.0f);
    }
    ascent *= arc.length / static_cast<float>(steps);
    fall   *= arc.length / static_cast<float>(steps);
    const float room = std::max((arc.base.y + ascent) - lowest, 0.02f * arc.length);
    arc.descent = std::min(1.0f, room / std::max(fall, 1.0e-6f));
}

// A ring of a blade: where on the arc, and its section in shares of the blade's half width.
class Blade_station
{
public:
    float at;
    float half_width;
    float lift;  // of the two edges over the midrib
    float depth; // of the keel under the midrib
};

enum class Blade_section : unsigned int {
    thin = 0, // the two edges and the keel
    thick     // the two edges, the midrib and the keel
};

enum class Blade_foot : unsigned int {
    on_arc = 0, // the blade closes at its arc's base
    on_ground   // the blade closes on the ground under its arc's base
};

class Blade
{
public:
    Blade_arc                      arc;
    std::span<const Blade_station> stations;
    Blade_section                  section   {Blade_section::thin};
    Blade_foot                     foot      {Blade_foot::on_arc};
    float                          half_width{0.05f};
    uint32_t                       slot      {0};
    uint32_t                       tip_slot  {0};
    std::size_t                    tip_from  {0}; // the bands from this one on and the tip are the tip slot's
    float                          detail    {1.0f}; // the detail factor (get_kit_detail_factor()) the stations follow
    int                            least_stations{3}; // the fewest stations at a low detail; the table's count keeps its shape
};

// The blade's stations at its detail: the table at the factor 1; more by interpolating between the table's stations
// (each gap taking a share by its length), fewer by keeping the first, the last and evenly spaced others. tip_from
// becomes the index of the first station at or past the table's tip station.
void get_blade_stations(const Blade& blade, std::vector<Blade_station>& out_stations, std::size_t& out_tip_from)
{
    const std::span<const Blade_station> table = blade.stations;
    const int own   = static_cast<int>(table.size());
    const int count = kit_detail_count(blade.detail, own, std::min(blade.least_stations, own), 2 * own);
    out_stations.clear();
    if (count == own) {
        out_stations.assign(table.begin(), table.end());
    } else if (count < own) {
        for (int i = 0; i < count; ++i) {
            out_stations.push_back(table[static_cast<std::size_t>(std::lround(static_cast<float>(i * (own - 1)) / static_cast<float>(count - 1)))]);
        }
    } else {
        const float length = table.back().at - table.front().at;
        const int   extra  = count - own;
        int         placed = 0;
        for (int g = 0; (g + 1) < own; ++g) {
            const Blade_station& a = table[static_cast<std::size_t>(g)];
            const Blade_station& b = table[static_cast<std::size_t>(g + 1)];
            // Stations placed up to the end of this gap, by the gaps' lengths.
            const int until = ((g + 2) == own) ? extra : static_cast<int>(std::lround(static_cast<float>(extra) * (b.at - table.front().at) / length));
            out_stations.push_back(a);
            const int inserts = until - placed;
            for (int k = 1; k <= inserts; ++k) {
                const float t = static_cast<float>(k) / static_cast<float>(inserts + 1);
                out_stations.push_back(Blade_station{
                    .at         = a.at + (t * (b.at - a.at)),
                    .half_width = a.half_width + (t * (b.half_width - a.half_width)),
                    .lift       = a.lift + (t * (b.lift - a.lift)),
                    .depth      = a.depth + (t * (b.depth - a.depth))
                });
            }
            placed = until;
        }
        out_stations.push_back(table.back());
    }
    out_tip_from = out_stations.size();
    if (blade.tip_from < table.size()) {
        const float tip_at = table[blade.tip_from].at;
        for (std::size_t r = 0; r < out_stations.size(); ++r) {
            if (out_stations[r].at >= tip_at) {
                out_tip_from = r;
                break;
            }
        }
    }
}

void add_blade(Kit_mesh_builder& builder, const Blade& blade)
{
    std::vector<Blade_station> stations;
    std::size_t                tip_from = 0;
    get_blade_stations(blade, stations, tip_from);
    const uint32_t    n          = (blade.section == Blade_section::thick) ? 4u : 3u;
    const std::size_t ring_count = stations.size();
    uint32_t first = 0;
    for (std::size_t r = 0; r < ring_count; ++r) {
        const Blade_station& station = stations[r];
        const Blade_frame    frame   = blade_frame(blade.arc, station.at);
        const glm::vec3      edge    = (blade.half_width * station.half_width) * frame.side;
        const glm::vec3      lift    = (blade.half_width * station.lift) * frame.up;
        const uint32_t       v       = builder.add_vertex(frame.center + edge + lift);
        if (r == 0) {
            first = v;
        }
        if (blade.section == Blade_section::thick) {
            builder.add_vertex(frame.center);
        }
        builder.add_vertex(frame.center - edge + lift);
        builder.add_vertex(frame.center - ((blade.half_width * station.depth) * frame.up));
    }
    const Blade_station& first_station = stations.front();
    const Blade_frame    start         = blade_frame(blade.arc, first_station.at);
    const glm::vec3      behind        = start.center
        - ((0.4f * blade.half_width * first_station.half_width) * start.along)
        - ((0.3f * blade.half_width * first_station.depth) * start.up);
    const glm::vec3      under{blade.arc.base.x, 0.0f, blade.arc.base.z};
    const uint32_t       base_cap = builder.add_vertex((blade.foot == Blade_foot::on_ground) ? under : behind);
    const uint32_t       tip      = builder.add_vertex(blade_frame(blade.arc, 1.0f).center);
    builder.set_slot(blade.slot);
    for (uint32_t i = 0; i < n; ++i) {
        builder.add_triangle(base_cap, first + ((i + 1) % n), first + i);
    }
    for (std::size_t r = 0; (r + 1) < ring_count; ++r) {
        builder.set_slot((r >= tip_from) ? blade.tip_slot : blade.slot);
        const uint32_t lower = first + (static_cast<uint32_t>(r) * n);
        const uint32_t upper = lower + n;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t j = (i + 1) % n;
            builder.add_triangle(lower + i, lower + j, upper + j);
            builder.add_triangle(lower + i, upper + j, upper + i);
        }
    }
    builder.set_slot(((ring_count - 1) >= tip_from) ? blade.tip_slot : blade.slot);
    const uint32_t last = first + (static_cast<uint32_t>(ring_count - 1) * n);
    for (uint32_t i = 0; i < n; ++i) {
        builder.add_triangle(last + i, last + ((i + 1) % n), tip);
    }
}

// A stem of rings around the Y axis from the ground; a ring is (share of the height, share of the radius). The sides
// are own_sides at the default detail.
void add_stem(
    Kit_mesh_builder&                builder,
    const std::span<const glm::vec2> rings,
    const int                        own_sides,
    const float                      radius,
    const float                      height,
    const glm::vec3&                 lean,
    const float                      top_y,
    const float                      detail
)
{
    const int sides = kit_detail_count(detail, own_sides, 4, 2 * own_sides);
    std::vector<glm::vec3> points;
    points.reserve(rings.size() * static_cast<std::size_t>(sides));
    for (const glm::vec2& ring : rings) {
        const glm::vec3 center{lean.x * ring.x * ring.x, ring.x * height, lean.z * ring.x * ring.x};
        for (int i = 0; i < sides; ++i) {
            const glm::vec2 circle = get_kit_circle_point(i, sides);
            points.push_back(center + glm::vec3{radius * ring.y * circle.x, 0.0f, radius * ring.y * circle.y});
        }
    }
    builder.add_ring_solid(points, sides, glm::vec3{0.0f}, glm::vec3{lean.x, top_y, lean.z});
}

//
// Succulent: a rosette of thick pointed leaves in a spiral, the old ones low and wide, the young ones upright.
//

constexpr uint32_t succulent_foliage_slot = 0;
constexpr uint32_t succulent_bark_slot    = 1; // a yucca's trunk and dead leaves, the leaves' dry points
constexpr uint32_t succulent_shoot_slot   = 2; // the youngest leaves

constexpr std::array<Blade_station, 5> agave_stations{{
    {0.00f, 0.60f, 0.28f, 0.50f}, {0.18f, 0.86f, 0.36f, 0.48f}, {0.42f, 1.00f, 0.36f, 0.36f}, {0.72f, 0.60f, 0.28f, 0.24f}, {0.92f, 0.13f, 0.06f, 0.12f}
}};
constexpr std::array<Blade_station, 5> aloe_stations{{
    {0.00f, 0.92f, 0.16f, 0.62f}, {0.22f, 1.00f, 0.22f, 0.60f}, {0.52f, 0.76f, 0.22f, 0.46f}, {0.78f, 0.44f, 0.16f, 0.28f}, {0.91f, 0.20f, 0.08f, 0.14f}
}};
constexpr std::array<Blade_station, 4> yucca_stations{{
    {0.00f, 0.55f, 0.30f, 0.50f}, {0.30f, 1.00f, 0.40f, 0.42f}, {0.68f, 0.80f, 0.32f, 0.30f}, {0.93f, 0.22f, 0.10f, 0.12f}
}};

constexpr std::array<glm::vec2, 3> succulent_core {{{0.0f, 1.0f}, {0.6f, 1.1f}, {1.0f, 0.6f}}};
constexpr std::array<glm::vec2, 5> succulent_trunk{{{0.0f, 1.45f}, {0.07f, 1.1f}, {0.5f, 0.92f}, {0.93f, 1.0f}, {1.0f, 0.8f}}};

void generate_succulent(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const std::string_view form   = values.get_enum("form");
    const float            size   = values.get_float("size");
    const int              leaves = values.get_int("leaves");
    const float            spread = glm::radians(values.get_float("spread"));
    const bool             agave  = (form == "agave");
    const bool             yucca  = (form == "yucca");
    const float            detail = get_kit_detail_factor(values);
    Kit_random random{values.get_seed(), "succulent"};
    Kit_mesh_builder builder;

    // The stem: a low core at the ground, or a yucca's trunk carrying the head.
    const float turn       = random.next_range(0.0f, two_pi);
    const float lean_angle = random.next_range(0.0f, two_pi);
    const float lean_share = random.next_range(0.02f, 0.10f);
    glm::vec3 crown{0.0f};
    float     core_radius = 0.0f;
    builder.set_slot(yucca ? succulent_bark_slot : succulent_foliage_slot);
    if (yucca) {
        const float     height = 0.5f * size;
        const glm::vec3 lean   = (lean_share * size) * around_y(lean_angle);
        core_radius = 0.06f * size;
        add_stem(builder, succulent_trunk, 7, core_radius, height, lean, height + (0.6f * core_radius), detail);
        crown = glm::vec3{lean.x, height, lean.z};
    } else {
        const float height = (agave ? 0.07f : 0.09f) * size;
        core_radius = 0.05f * size;
        add_stem(builder, succulent_core, 7, core_radius, height, glm::vec3{0.0f}, 1.15f * height, detail);
        crown = glm::vec3{0.0f, height, 0.0f};
    }

    // A yucca carries two thin swords for each leaf; its oldest hang dead down its trunk.
    const int   blades  = yucca ? (2 * leaves) : leaves;
    const int   dead    = yucca ? (blades / 4) : 0;
    const int   young   = std::max(2, blades / 5);
    const float floor_y = 0.012f * size;
    for (int k = 0; k < blades; ++k) {
        const float     rank       = (blades > 1) ? (static_cast<float>(k) / static_cast<float>(blades - 1)) : 1.0f; // 1: the youngest
        const float     azimuth    = turn + (golden_angle * static_cast<float>(k)) + (0.15f * next_signed(random));
        const float     length_var = 1.0f + (0.08f * next_signed(random));
        const float     lean_var   = glm::radians(5.0f) * next_signed(random);
        const float     roll       = 0.12f * next_signed(random);
        const glm::vec3 out        = around_y(azimuth);
        const float     outer      = std::pow(1.0f - rank, 0.8f); // 1 for the outermost leaf
        Blade blade;
        blade.section  = Blade_section::thick;
        blade.detail   = detail;
        blade.arc.out  = out;
        blade.arc.roll = roll;
        blade.slot     = (k >= (blades - young)) ? succulent_shoot_slot : succulent_foliage_slot;
        blade.tip_slot = succulent_bark_slot;
        if (yucca) {
            // A head of straight swords about the crown; the dead ones point down the trunk.
            const bool  is_dead  = (k < dead);
            const float live     = (blades > (dead + 1)) ? (static_cast<float>(k - dead) / static_cast<float>(blades - dead - 1)) : 1.0f;
            const float from_top = glm::radians(6.0f) + ((spread + glm::radians(40.0f)) * std::pow(std::max(1.0f - live, 0.0f), 0.8f));
            blade.section         = Blade_section::thin;
            blade.stations        = yucca_stations;
            blade.arc.length      = (is_dead ? 0.36f : 0.44f) * size * length_var;
            blade.arc.elevation_0 = is_dead ? (glm::radians(-56.0f - (9.0f * static_cast<float>(k % 3))) + lean_var) : ((0.5f * pi) - from_top + lean_var);
            blade.arc.elevation_1 = blade.arc.elevation_0 - glm::radians(is_dead ? 10.0f : 6.0f);
            blade.arc.base        = crown + ((0.7f * core_radius) * ((std::cos(blade.arc.elevation_0) * out) + (std::sin(blade.arc.elevation_0) * y_axis)));
            blade.half_width      = 0.055f * blade.arc.length;
            blade.tip_from        = 3;
            if (is_dead) {
                blade.slot     = succulent_bark_slot;
                blade.tip_from = 0;
            }
        } else {
            const float from_top = glm::radians(7.0f) + (std::max(spread - glm::radians(7.0f), 0.0f) * outer);
            blade.arc.elevation_0 = (0.5f * pi) - from_top + lean_var;
            blade.arc.base        = glm::vec3{0.0f, size * (0.05f + (0.035f * rank)), 0.0f} + ((core_radius * (0.8f - (0.6f * rank))) * out);
            if (agave) {
                // Broad channelled leaves ending in a dark spine; the outer ones bend back a little.
                blade.stations        = agave_stations;
                blade.arc.length      = 0.5f * size * (1.0f - (0.32f * rank)) * length_var;
                blade.arc.elevation_1 = blade.arc.elevation_0 - glm::radians(6.0f + (16.0f * outer));
                blade.arc.power       = 1.6f;
                blade.half_width      = 0.14f * blade.arc.length;
                blade.tip_from        = 4;
            } else {
                // Aloe: thick tapering leaves curving up and in, their ends dry.
                blade.stations        = aloe_stations;
                blade.arc.length      = 0.5f * size * (0.78f + (0.22f * std::sin(pi * rank))) * length_var;
                blade.arc.elevation_1 = std::min(blade.arc.elevation_0 + glm::radians(16.0f + (22.0f * outer)), glm::radians(86.0f));
                blade.arc.power       = 1.3f;
                blade.half_width      = 0.105f * blade.arc.length;
                blade.tip_from        = 4;
            }
        }
        fit_blade_arc(blade.arc, floor_y + (0.6f * blade.half_width));
        add_blade(builder, blade);
    }

    // The bud of unopened leaves at the center.
    builder.set_slot(succulent_shoot_slot);
    const int   bud_sides  = kit_detail_count(detail, 5, 4, 10);
    const float bud_radius = (yucca ? 0.55f : 0.42f) * core_radius;
    const float bud_height = (yucca ? 0.30f : (agave ? 0.30f : 0.22f)) * size;
    std::vector<glm::vec3> bud(static_cast<std::size_t>(2 * bud_sides));
    for (int i = 0; i < bud_sides; ++i) {
        const glm::vec2 circle = get_kit_circle_point(i, bud_sides);
        bud[static_cast<std::size_t>(i)]             = crown + glm::vec3{bud_radius * circle.x, -0.3f * crown.y, bud_radius * circle.y};
        bud[static_cast<std::size_t>(bud_sides + i)] = crown + glm::vec3{0.8f * bud_radius * circle.x, 0.35f * bud_height, 0.8f * bud_radius * circle.y};
    }
    builder.add_ring_solid(bud, bud_sides, crown - glm::vec3{0.0f, 0.32f * crown.y, 0.0f}, crown + glm::vec3{0.0f, bud_height, 0.0f});
    builder.write(mesh);
}

//
// Grass tuft: blades from a tussock, with ears, reed canes or pampas plumes.
//

constexpr uint32_t grass_blade_slot = 0;
constexpr uint32_t grass_plume_slot = 1; // ears, panicles and plumes

constexpr std::array<Blade_station, 5> grass_stations{{{0.00f, 0.60f, 0.20f, 0.40f}, {0.25f, 1.00f, 0.25f, 0.35f}, {0.50f, 0.90f, 0.22f, 0.30f}, {0.74f, 0.62f, 0.16f, 0.22f}, {0.92f, 0.26f, 0.08f, 0.12f}}};
constexpr std::array<Blade_station, 3> cane_stations {{{0.00f, 1.00f, 0.00f, 1.00f}, {0.50f, 0.85f, 0.00f, 0.85f}, {0.96f, 0.55f, 0.00f, 0.55f}}};
// A stalk whose end swells into an ear or a plume.
constexpr std::array<Blade_station, 5> ear_stations    {{{0.00f, 1.00f, 0.00f, 1.00f}, {0.70f, 0.80f, 0.00f, 0.80f}, {0.75f, 2.80f, 0.00f, 2.80f}, {0.87f, 3.00f, 0.00f, 3.00f}, {0.96f, 1.30f, 0.00f, 1.30f}}};
constexpr std::array<Blade_station, 5> plume_stations  {{{0.00f, 1.00f, 0.00f, 1.00f}, {0.56f, 0.80f, 0.00f, 0.80f}, {0.66f, 5.00f, 0.00f, 4.00f}, {0.80f, 4.40f, 0.00f, 3.40f}, {0.93f, 2.20f, 0.00f, 1.80f}}};
constexpr std::array<Blade_station, 4> panicle_stations{{{0.00f, 0.60f, 0.00f, 0.60f}, {0.25f, 2.60f, 0.00f, 2.20f}, {0.62f, 2.00f, 0.00f, 1.60f}, {0.90f, 0.80f, 0.00f, 0.70f}}};
constexpr std::array<glm::vec2, 2>     tussock_rings   {{{0.0f, 1.0f}, {0.55f, 0.8f}}};

// What a tuft's blades share.
class Tuft
{
public:
    float height{1.0f};
    float spread{0.5f};
    int   blades{12};
    float turn  {0.0f};
    float detail{1.0f}; // the detail factor (get_kit_detail_factor())
};

// An arching blade from the tussock, blade 0 at its center and the last at its edge.
void add_tuft_blade(Kit_mesh_builder& builder, Kit_random& random, const Tuft& tuft, const int k, const float reach, const float length, const float droop)
{
    const float     share   = (static_cast<float>(k) + 0.5f) / static_cast<float>(tuft.blades);
    const float     azimuth = tuft.turn + (golden_angle * static_cast<float>(k)) + (0.3f * next_signed(random));
    const float     lean    = glm::radians(6.0f + (58.0f * tuft.spread * share)) * random.next_range(0.8f, 1.15f);
    const float     bend    = droop * (0.35f + (0.65f * share)) * random.next_range(0.75f, 1.1f);
    const float     grow    = random.next_range(0.82f, 1.0f);
    const float     roll    = 0.5f * next_signed(random);
    const float     swing   = 0.35f * next_signed(random);
    const glm::vec3 out     = around_y(azimuth);
    Blade blade;
    blade.stations        = grass_stations;
    blade.detail          = tuft.detail;
    blade.foot            = Blade_foot::on_ground;
    blade.arc.out         = around_y(azimuth + swing);
    blade.arc.base        = ((reach * std::sqrt(share)) * out) + glm::vec3{0.0f, 0.02f * tuft.height, 0.0f};
    blade.arc.length      = length * (1.05f - (0.35f * share)) * grow;
    blade.arc.elevation_0 = (0.5f * pi) - lean;
    blade.arc.elevation_1 = blade.arc.elevation_0 - bend;
    blade.arc.power       = 1.5f;
    blade.arc.roll        = roll;
    blade.half_width      = 0.022f * length;
    blade.slot            = grass_blade_slot;
    blade.tip_slot        = grass_blade_slot;
    fit_blade_arc(blade.arc, 0.02f * tuft.height);
    add_blade(builder, blade);
}

// A straight stalk ending in an ear or a plume.
void add_tuft_stalk(
    Kit_mesh_builder&                    builder,
    Kit_random&                          random,
    const Tuft&                          tuft,
    const int                            k,
    const float                          reach,
    const float                          half_width,
    const float                          bow, // how many times its lean the stalk's end bows over
    const std::span<const Blade_station> stations
)
{
    const float     azimuth = tuft.turn + 1.0f + (golden_angle * static_cast<float>(k)) + (0.3f * next_signed(random));
    const float     lean    = glm::radians(3.0f + (14.0f * tuft.spread * random.next_unit()));
    const float     grow    = random.next_range(0.84f, 1.0f);
    const float     roll    = random.next_range(0.0f, pi);
    const float     away    = 0.3f + (0.5f * random.next_unit());
    const glm::vec3 out     = around_y(azimuth);
    Blade blade;
    blade.stations        = stations;
    blade.detail          = tuft.detail;
    blade.least_stations  = static_cast<int>(stations.size()); // the ear's or the plume's swell
    blade.section         = Blade_section::thick;
    blade.foot            = Blade_foot::on_ground;
    blade.arc.out         = out;
    blade.arc.base        = ((reach * away) * out) + glm::vec3{0.0f, 0.02f * tuft.height, 0.0f};
    blade.arc.length      = tuft.height * grow;
    blade.arc.elevation_0 = (0.5f * pi) - (0.5f * lean);
    blade.arc.elevation_1 = (0.5f * pi) - (bow * lean);
    blade.arc.power       = 2.0f;
    blade.arc.roll        = roll;
    blade.half_width      = half_width;
    blade.slot            = grass_blade_slot;
    blade.tip_slot        = grass_plume_slot;
    blade.tip_from        = 1;
    add_blade(builder, blade);
}

// A reed: an upright cane with a drooping panicle at its top; returns the cane's arc for its leaves.
[[nodiscard]] auto add_reed_cane(Kit_mesh_builder& builder, Kit_random& random, const Tuft& tuft, const int k, const int canes, const float reach) -> Blade_arc
{
    const float     share   = (static_cast<float>(k) + 0.5f) / static_cast<float>(canes);
    const float     azimuth = tuft.turn + (golden_angle * static_cast<float>(k)) + (0.3f * next_signed(random));
    const float     lean    = glm::radians(2.0f + (12.0f * tuft.spread * random.next_unit()));
    const float     grow    = random.next_range(0.72f, 0.86f);
    const float     droop   = random.next_range(0.0f, two_pi);
    const glm::vec3 out     = around_y(azimuth);
    Blade cane;
    cane.stations        = cane_stations;
    cane.detail          = tuft.detail;
    cane.section         = Blade_section::thick;
    cane.foot            = Blade_foot::on_ground;
    cane.arc.out         = out;
    cane.arc.base        = ((reach * std::sqrt(share)) * out) + glm::vec3{0.0f, 0.02f * tuft.height, 0.0f};
    cane.arc.length      = tuft.height * grow;
    cane.arc.elevation_0 = (0.5f * pi) - (0.5f * lean);
    cane.arc.elevation_1 = (0.5f * pi) - (1.5f * lean);
    cane.half_width      = 0.0075f * tuft.height;
    cane.slot            = grass_blade_slot;
    cane.tip_slot        = grass_blade_slot;
    add_blade(builder, cane);

    Blade panicle;
    panicle.stations        = panicle_stations;
    panicle.detail          = tuft.detail;
    panicle.least_stations  = static_cast<int>(panicle_stations.size());
    panicle.section         = Blade_section::thick;
    panicle.arc.out         = around_y(droop);
    panicle.arc.base        = blade_frame(cane.arc, 0.95f).center;
    panicle.arc.length      = 0.17f * tuft.height;
    panicle.arc.elevation_0 = glm::radians(78.0f);
    panicle.arc.elevation_1 = glm::radians(15.0f);
    panicle.half_width      = 0.011f * tuft.height;
    panicle.slot            = grass_plume_slot;
    panicle.tip_slot        = grass_plume_slot;
    add_blade(builder, panicle);
    return cane.arc;
}

void generate_grass_tuft(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const std::string_view form = values.get_enum("form");
    Kit_random random{values.get_seed(), "grass tuft"};
    Tuft tuft;
    tuft.height = values.get_float("height");
    tuft.spread = values.get_float("spread");
    tuft.blades = values.get_int("blades");
    tuft.turn   = random.next_range(0.0f, two_pi);
    tuft.detail = get_kit_detail_factor(values);
    Kit_mesh_builder builder;
    builder.set_slot(grass_blade_slot);
    if (form == "reed") {
        // Canes in a loose stand, each blade a leaf standing off a cane.
        const int   canes  = std::max(2, tuft.blades / 3);
        const int   levels = (tuft.blades + canes - 1) / canes;
        const float reach  = 0.16f * tuft.height * (0.4f + tuft.spread);
        add_stem(builder, tussock_rings, 6, 0.035f * tuft.height, 0.02f * tuft.height, glm::vec3{0.0f}, 0.025f * tuft.height, tuft.detail);
        std::vector<Blade_arc> arcs;
        for (int k = 0; k < canes; ++k) {
            arcs.push_back(add_reed_cane(builder, random, tuft, k, canes, reach));
        }
        for (int k = 0; k < tuft.blades; ++k) {
            const Blade_arc& cane    = arcs[static_cast<std::size_t>(k % canes)];
            const float      level   = (static_cast<float>(k / canes) + 0.5f + (0.3f * next_signed(random))) / static_cast<float>(levels);
            const float      azimuth = (golden_angle * static_cast<float>(k)) + (0.4f * next_signed(random));
            const float      grow    = random.next_range(0.8f, 1.0f);
            Blade leaf;
            leaf.stations        = grass_stations;
            leaf.detail          = tuft.detail;
            leaf.arc.out         = around_y(azimuth);
            leaf.arc.base        = blade_frame(cane, 0.2f + (0.6f * level)).center;
            leaf.arc.length      = 0.36f * tuft.height * grow;
            leaf.arc.elevation_0 = glm::radians(66.0f);
            leaf.arc.elevation_1 = glm::radians(66.0f - (40.0f + (60.0f * tuft.spread)));
            leaf.arc.power       = 1.5f;
            leaf.half_width      = 0.02f * tuft.height;
            leaf.slot            = grass_blade_slot;
            leaf.tip_slot        = grass_blade_slot;
            fit_blade_arc(leaf.arc, 0.03f * tuft.height);
            add_blade(builder, leaf);
        }
        builder.write(mesh);
        return;
    }
    const bool  pampas = (form == "pampas");
    const float reach  = (pampas ? 0.07f : 0.09f) * tuft.height * (0.6f + (0.8f * tuft.spread));
    add_stem(builder, tussock_rings, 6, 1.3f * reach, (pampas ? 0.07f : 0.05f) * tuft.height, glm::vec3{0.0f}, (pampas ? 0.09f : 0.065f) * tuft.height, tuft.detail);
    for (int k = 0; k < tuft.blades; ++k) {
        if (pampas) {
            add_tuft_blade(builder, random, tuft, k, reach, 0.8f * tuft.height, glm::radians(125.0f));
        } else {
            add_tuft_blade(builder, random, tuft, k, reach, 0.9f * tuft.height, glm::radians(40.0f + (80.0f * tuft.spread)));
        }
    }
    const int stalks = pampas ? (2 + (tuft.blades / 6)) : (1 + (tuft.blades / 10));
    for (int k = 0; k < stalks; ++k) {
        if (pampas) {
            add_tuft_stalk(builder, random, tuft, k, reach, 0.0065f * tuft.height, 3.5f, plume_stations);
        } else {
            add_tuft_stalk(builder, random, tuft, k, reach, 0.005f * tuft.height, 1.6f, ear_stations);
        }
    }
    builder.write(mesh);
}

//
// Stones: stacked rings of one convex outline whose edges keep their directions from ring to ring, so every side face
// and every chamfer down a corner is planar.
//

// Edge normals of a convex polygon at rising angles around +Y; corner j lies between edges j and j + 1.
class Stone_outline
{
public:
    explicit Stone_outline(const std::span<const float> angles)
        : m_cos    (angles.size())
        , m_sin    (angles.size())
        , m_cos_gap(angles.size())
        , m_sin_gap(angles.size())
    {
        const std::size_t n = angles.size();
        for (std::size_t j = 0; j < n; ++j) {
            const std::size_t k   = (j + 1) % n;
            const float       gap = (angles[k] - angles[j]) + ((k == 0) ? two_pi : 0.0f);
            m_cos[j]     = std::cos(angles[j]);
            m_sin[j]     = std::sin(angles[j]);
            m_cos_gap[j] = std::cos(gap);
            m_sin_gap[j] = std::sin(gap);
        }
    }

    [[nodiscard]] auto get_edge_length(const std::span<const float> offsets, const std::size_t j) const -> float
    {
        const std::size_t n = m_cos.size();
        const std::size_t k = (j + 1) % n;
        const std::size_t i = (j + n - 1) % n;
        return ((offsets[k] - (offsets[j] * m_cos_gap[j])) / m_sin_gap[j]) + ((offsets[i] - (offsets[j] * m_cos_gap[i])) / m_sin_gap[i]);
    }

    // (x, -z) of corner j.
    [[nodiscard]] auto get_corner(const std::span<const float> offsets, const std::size_t j) const -> glm::vec2
    {
        const std::size_t k = (j + 1) % m_cos.size();
        return glm::vec2{
            ((offsets[j] * m_sin[k]) - (offsets[k] * m_sin[j])) / m_sin_gap[j],
            ((offsets[k] * m_cos[j]) - (offsets[j] * m_cos[k])) / m_sin_gap[j]
        };
    }

    // The direction edge j runs in, counter-clockwise.
    [[nodiscard]] auto get_direction(const std::size_t j) const -> glm::vec2
    {
        return glm::vec2{-m_sin[j], m_cos[j]};
    }

    // Moves wanted toward base by the least share that keeps every edge at least `keep` of the base's: a min of
    // continuous terms, so the polygon stays valid and no decision depends on a float comparison.
    void limit(const std::span<const float> base, const std::span<float> wanted, const float keep) const
    {
        float share = 1.0f;
        for (std::size_t j = 0; j < base.size(); ++j) {
            const float base_length   = get_edge_length(base, j);
            const float wanted_length = get_edge_length(wanted, j);
            share = std::min(share, ((1.0f - keep) * base_length) / std::max(base_length - wanted_length, 1.0e-6f * base_length));
        }
        for (std::size_t j = 0; j < base.size(); ++j) {
            wanted[j] = base[j] + (share * (wanted[j] - base[j]));
        }
    }

private:
    std::vector<float> m_cos;
    std::vector<float> m_sin;
    std::vector<float> m_cos_gap; // of the angle from normal j to normal j + 1
    std::vector<float> m_sin_gap;
};

class Stone_station
{
public:
    float t;     // share of the stone's height
    float scale; // of the footprint
};

constexpr std::array<Stone_station, 4> stone_angular_stations {{{0.28f, 1.00f}, {0.35f, 0.93f}, {0.76f, 0.80f}, {1.00f, 0.52f}}};
constexpr std::array<Stone_station, 4> stone_worn_stations    {{{0.26f, 1.00f}, {0.58f, 0.93f}, {0.86f, 0.72f}, {1.00f, 0.46f}}};
constexpr std::array<Stone_station, 3> stone_stepping_stations{{{0.45f, 1.00f}, {0.80f, 0.99f}, {1.00f, 0.90f}}};

class Stone_form
{
public:
    std::span<const Stone_station> stations;  // the rings above the base ring, bottom to top; the last is the top face
    uint32_t side_min;
    uint32_t side_range;      // sides are side_min + [0, side_range)
    float    depth;           // footprint depth over width
    float    base_scale;      // the base ring's, of the footprint
    float    chamfer_min;     // a corner's chamfer, share of the shorter edge beside it
    float    chamfer_max;
    float    variation;       // the sides' offsets at roughness 1
    float    dip;             // strata dip at roughness 1, rise per meter
    float    lean;            // drift of the top at roughness 1, per meter of height
    float    top_tilt;        // slope of the top face at roughness 1; 0: a level top
    float    crown;           // rise of the top's apex over its ring, a share of the height; 0: a flat top face
    uint32_t fracture_one_in; // one side in this many is a fracture plane
};

const Stone_form stone_angular {stone_angular_stations,  5, 2, 0.72f, 1.05f, 0.05f, 0.14f, 0.30f, 0.40f, 0.30f, 0.80f, 0.00f, 2};
const Stone_form stone_worn    {stone_worn_stations,     5, 3, 0.85f, 0.88f, 0.18f, 0.30f, 0.30f, 0.25f, 0.20f, 0.50f, 0.07f, 4};
const Stone_form stone_stepping{stone_stepping_stations, 7, 1, 0.82f, 0.94f, 0.10f, 0.22f, 0.12f, 0.00f, 0.00f, 0.00f, 0.00f, 4};

enum class Stone_seat : unsigned int {
    ground = 0, // the base ring lies on y = origin.y
    stacked     // the stone is tilted and its lowest point lies at y = origin.y
};

class Stone_placement
{
public:
    glm::vec3  origin      {0.0f};
    float      size        {1.0f}; // width of the footprint before its variation
    float      height      {0.5f};
    float      turn        {0.0f};
    float      roughness   {0.5f};
    uint32_t   sides       {0};    // 0: drawn from the form
    Stone_seat seat        {Stone_seat::ground};
    float      tilt        {0.0f}; // of a stacked stone, radians
    float      tilt_azimuth{0.0f};
};

void add_stone(Kit_mesh_builder& builder, Kit_random& random, const Stone_form& form, const Stone_placement& placement)
{
    const uint32_t    side_draw     = random.next_u32() % form.side_range;
    const std::size_t side_count    = static_cast<std::size_t>((placement.sides != 0u) ? placement.sides : (form.side_min + side_draw));
    const std::size_t station_count = form.stations.size();
    const std::size_t ring_size     = 2 * side_count;
    std::vector<float> angles(side_count);
    for (std::size_t j = 0; j < side_count; ++j) {
        const float offset = random.next_range(-0.28f, 0.28f);
        angles[j] = placement.turn + (two_pi * (static_cast<float>(j) + offset) / static_cast<float>(side_count));
    }
    const Stone_outline outline{angles};

    // The footprint: tangents of an ellipse, each moved in or out.
    const float depth_draw = next_signed(random);
    const float half_x     = 0.5f * placement.size;
    const float half_z     = half_x * form.depth * (1.0f + (0.12f * depth_draw));
    std::vector<float> tangents (side_count);
    std::vector<float> footprint(side_count);
    for (std::size_t j = 0; j < side_count; ++j) {
        const float local = angles[j] - placement.turn;
        const float a     = half_x * std::cos(local);
        const float b     = half_z * std::sin(local);
        const float draw  = next_signed(random);
        tangents[j]  = std::sqrt((a * a) + (b * b));
        footprint[j] = tangents[j] * (1.0f + (form.variation * placement.roughness * draw));
    }
    outline.limit(tangents, footprint, 0.5f);

    std::vector<uint32_t> fracture     (side_count);
    std::vector<float>    fracture_foot(side_count);
    std::vector<float>    fracture_top (side_count);
    std::vector<float>    trend        (side_count); // the other sides stand steeper or slope in earlier
    std::vector<float>    chamfer      (side_count);
    for (std::size_t j = 0; j < side_count; ++j) {
        fracture[j]      = ((random.next_u32() % form.fracture_one_in) == 0u) ? 1u : 0u;
        fracture_foot[j] = random.next_range(0.94f, 1.02f);
        fracture_top[j]  = random.next_range(0.75f, 1.25f);
        trend[j]         = next_signed(random);
        chamfer[j]       = random.next_range(form.chamfer_min, form.chamfer_max);
    }

    // Ring 0 is the base, ring s + 1 station s: per ring the outline's corners, each cut by its chamfer.
    const Stone_station& first_station = form.stations.front();
    const Stone_station& last_station  = form.stations.back();
    std::vector<float>     ring_base(side_count);
    std::vector<float>     ring     (side_count);
    std::vector<glm::vec2> plan     ((station_count + 1) * ring_size);
    std::vector<float>     ring_y   (station_count + 1, 0.0f);
    for (std::size_t r = 0; r <= station_count; ++r) {
        const float t      = (r == 0) ? 0.0f : form.stations[r - 1].t;
        const float scale  = (r == 0) ? form.base_scale : form.stations[r - 1].scale;
        const float along  = std::max(t - first_station.t, 0.0f) / (1.0f - first_station.t);
        const float foot   = form.base_scale + ((first_station.scale - form.base_scale) * std::min(t / first_station.t, 1.0f));
        const float y_draw = next_signed(random);
        ring_y[r] = placement.height * (t + (((r > 0) && (r < station_count)) ? (0.015f * placement.roughness * y_draw) : 0.0f));
        for (std::size_t j = 0; j < side_count; ++j) {
            const float noise    = next_signed(random);
            const float straight = (foot * fracture_foot[j]) + (along * ((last_station.scale * fracture_top[j]) - (foot * fracture_foot[j])));
            const float worn     = scale + (placement.roughness * form.variation * ((1.1f * trend[j] * along) + (0.25f * noise)));
            ring_base[j] = footprint[j] * scale;
            ring[j]      = footprint[j] * ((fracture[j] != 0u) ? straight : worn);
        }
        outline.limit(ring_base, ring, 0.45f);
        for (std::size_t j = 0; j < side_count; ++j) {
            const std::size_t k      = (j + 1) % side_count;
            const glm::vec2   corner = outline.get_corner(ring, j);
            const float       cut    = chamfer[j] * std::min(outline.get_edge_length(ring, j), outline.get_edge_length(ring, k));
            plan[(r * ring_size) + (2 * j)]     = corner - (cut * outline.get_direction(j));
            plan[(r * ring_size) + (2 * j) + 1] = corner + (cut * outline.get_direction(k));
        }
    }

    // The strata dip and the stone leans: one affine map of the rings, so the faces stay planar. Its top face tilts by
    // itself within the room the last band gives it.
    const float     reach      = placement.size;
    const float     dip_angle  = random.next_range(0.0f, two_pi);
    const float     dip_amount = std::min(form.dip * placement.roughness, (0.45f * ring_y[1]) / reach);
    const glm::vec2 dip        = dip_amount * glm::vec2{std::cos(dip_angle), std::sin(dip_angle)};
    const float     lean_angle = random.next_range(0.0f, two_pi);
    const float     lean_draw  = random.next_unit();
    const glm::vec2 lean       = (form.lean * placement.roughness * lean_draw) * glm::vec2{std::cos(lean_angle), std::sin(lean_angle)};
    const float     top_room   = ring_y[station_count] - ring_y[station_count - 1];
    const float     tilt_angle = random.next_range(0.0f, two_pi);
    const float     tilt_draw  = random.next_range(0.5f, 1.0f);
    const float     tilt_slope = std::min(form.top_tilt * placement.roughness * tilt_draw, (0.45f * top_room) / (reach * last_station.scale));
    const glm::vec2 tilt       = tilt_slope * glm::vec2{std::cos(tilt_angle), std::sin(tilt_angle)};
    const float     toward_a   = random.next_range(0.0f, 0.35f);
    const float     toward_b   = random.next_range(0.0f, 0.35f);
    std::vector<glm::vec3> points;
    points.reserve(plan.size() + 1);
    for (std::size_t r = 0; r <= station_count; ++r) {
        for (std::size_t i = 0; i < ring_size; ++i) {
            const glm::vec2 corner = plan[(r * ring_size) + i];
            const float     y      = ring_y[r] + glm::dot(dip, corner) + ((r == station_count) ? glm::dot(tilt, corner) : 0.0f);
            points.push_back(glm::vec3{corner.x + (lean.x * y), y, -(corner.y + (lean.y * y))});
        }
    }
    // The base ring: the lowest band's side planes cut by the ground.
    for (std::size_t i = 0; i < ring_size; ++i) {
        const glm::vec3 above = points[ring_size + i];
        glm::vec3&      base  = points[i];
        base   = base + ((-base.y / (above.y - base.y)) * (above - base));
        base.y = 0.0f;
    }
    const bool crowned = (form.crown > 0.0f);
    if (crowned) {
        // An apex inside the top ring: its corners' mean drawn toward two of them.
        const std::size_t top = station_count * ring_size;
        glm::vec3 mean{0.0f};
        for (std::size_t i = 0; i < ring_size; ++i) {
            mean += points[top + i];
        }
        mean /= static_cast<float>(ring_size);
        points.push_back(mean + (toward_a * (points[top] - mean)) + (toward_b * (points[top + side_count] - mean)) + glm::vec3{0.0f, form.crown * placement.height, 0.0f});
    }

    if (placement.seat == Stone_seat::stacked) {
        // Turned about a level axis, then lifted so its lowest point rests at the origin's height.
        const glm::vec3 axis   = around_y(placement.tilt_azimuth);
        const float     cosine = std::cos(placement.tilt);
        const float     sine   = std::sin(placement.tilt);
        float lowest = 1.0e6f;
        for (glm::vec3& point : points) {
            point  = (cosine * point) + (sine * glm::cross(axis, point)) + (((1.0f - cosine) * glm::dot(axis, point)) * axis);
            lowest = std::min(lowest, point.y);
        }
        for (glm::vec3& point : points) {
            point.y -= lowest;
        }
    }

    uint32_t first = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const uint32_t v = builder.add_vertex(placement.origin + points[i]);
        if (i == 0) {
            first = v;
        }
    }
    const uint32_t n = static_cast<uint32_t>(ring_size);
    std::vector<uint32_t> polygon(ring_size);
    for (uint32_t i = 0; i < n; ++i) {
        polygon[i] = first + (n - 1 - i);
    }
    builder.add_polygon(polygon);
    // The flat base and a flat top keep their outlines sharp; the sides and the chamfers between them shade round.
    for (uint32_t i = 0; i < n; ++i) {
        builder.add_hard_edge(first + i, first + ((i + 1) % n));
    }
    const bool     tilted = (form.top_tilt > 0.0f);
    const uint32_t bands  = static_cast<uint32_t>(station_count);
    for (uint32_t r = 0; r < bands; ++r) {
        const uint32_t lower = first + (r * n);
        const uint32_t upper = lower + n;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t j = (i + 1) % n;
            if (tilted && ((r + 1) == bands)) {
                builder.add_triangle(lower + i, lower + j, upper + j); // under the tilted top the sides are no planes
                builder.add_triangle(lower + i, upper + j, upper + i);
            } else {
                builder.add_quad(lower + i, lower + j, upper + j, upper + i);
            }
        }
    }
    const uint32_t top_ring = first + (bands * n);
    if (crowned) {
        const uint32_t apex = top_ring + n;
        for (uint32_t i = 0; i < n; ++i) {
            builder.add_triangle(top_ring + i, top_ring + ((i + 1) % n), apex);
        }
        return;
    }
    for (uint32_t i = 0; i < n; ++i) {
        polygon[i] = top_ring + i;
        builder.add_hard_edge(top_ring + i, top_ring + ((i + 1) % n));
    }
    builder.add_polygon(polygon);
}

constexpr int cluster_count_max = 10;

void generate_boulder_cluster(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const std::string_view form_name = values.get_enum("form");
    const float            radius    = values.get_float("radius");
    const int              count     = std::clamp(values.get_int("count"), 1, cluster_count_max);
    const float            spread    = values.get_float("size_spread");
    const float            roughness = values.get_float("roughness");
    Kit_random random{values.get_seed(), "boulder cluster"};
    Kit_mesh_builder builder;

    const float largest       = radius * (1.16f - (0.045f * static_cast<float>(count)));
    const float first_azimuth = random.next_range(0.0f, two_pi);
    glm::vec3 heart{0.0f};
    float     heart_height = 0.0f;
    for (int k = 0; k < count; ++k) {
        const uint32_t    form_draw    = random.next_u32() % 2u;
        const float       size_draw    = next_signed(random);
        const float       height_draw  = next_signed(random);
        const float       azimuth_draw = next_signed(random);
        const float       reach_draw   = random.next_unit();
        const float       turn         = random.next_range(0.0f, two_pi);
        const float       tilt         = random.next_range(0.15f, 0.4f);
        const float       tilt_azimuth = random.next_range(0.0f, two_pi);
        const bool        worn         = (form_name == "worn") || ((form_name == "mixed") && (form_draw == 1u));
        const Stone_form& form         = worn ? stone_worn : stone_angular;
        const float       order        = (count > 1) ? (static_cast<float>(k) / static_cast<float>(count - 1)) : 0.0f;
        const float       size         = largest * (1.0f - (0.85f * spread * std::pow(order, 0.7f))) * (1.0f + (0.08f * size_draw));
        const float       azimuth      = first_azimuth + (golden_angle * static_cast<float>(k)) + (0.3f * azimuth_draw);
        const bool        stacked      = (k > 0) && ((k % 3) == 0);
        Stone_placement placement;
        placement.size      = (stacked ? 0.62f : 1.0f) * size;
        placement.height    = (worn ? 0.58f : 0.78f) * placement.size * (1.0f + (0.15f * height_draw));
        placement.turn      = turn;
        placement.roughness = roughness;
        if (k == 0) {
            // The largest stone stands over the origin.
            placement.origin = (0.06f * radius) * around_y(azimuth);
            heart            = placement.origin;
            heart_height     = placement.height;
        } else if (stacked) {
            // Every third stone, a smaller one, lies on the largest one's shoulder.
            placement.seat         = Stone_seat::stacked;
            placement.tilt         = tilt;
            placement.tilt_azimuth = tilt_azimuth;
            placement.origin       = heart + ((0.22f * largest) * around_y(azimuth)) + glm::vec3{0.0f, 0.5f * heart_height, 0.0f};
        } else {
            // The others lean on its flank, out to the footprint's edge.
            const float near_reach = 0.31f * (largest + size);
            const float far_reach  = std::max(radius - (0.5f * size), near_reach);
            placement.origin = heart + ((near_reach + (0.4f * reach_draw * (far_reach - near_reach))) * around_y(azimuth));
        }
        add_stone(builder, random, form, placement);
    }
    builder.write(mesh);
}

void generate_stepping_stone(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    Kit_random random{values.get_seed(), "stepping stone"};
    Kit_mesh_builder builder;
    Stone_placement placement;
    placement.size      = values.get_float("diameter");
    placement.height    = values.get_float("thickness");
    placement.turn      = random.next_range(0.0f, two_pi);
    placement.roughness = 1.0f;
    placement.sides     = static_cast<uint32_t>(values.get_int("sides"));
    add_stone(builder, random, stone_stepping, placement);
    builder.write(mesh);
}

} // anonymous namespace

void add_build_13_16_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.window_wall";
        shape.kit             = "architecture";
        shape.display_name    = "Wall with window opening";
        shape.description     =
            "Wall along X with a window opening at its center: a surround of a splayed reveal, a band and a groove on both "
            "faces over a sloping sill, a flat, round or pointed head, and a casing with mullions and transoms in the "
            "middle of the wall. Ends, top and bottom meet plain walls.";
        shape.tags            = {"obstacle", "wall", "window", "tiles", "size:4x1"};
        // The ranges of the length and the height hold every derived opening; the surround shrinks beside a narrow jamb or header.
        shape.parameters      = {
            derive_float(make_length_parameter("length",         "Size along X",                            4.0f,  Kit_default_scale::modules,      2.0f,  64.0f), 2.5f,  6.0f,  0.5f),
            derive_float(make_length_parameter("height",         "Size along Y",                            1.0f,  Kit_default_scale::wall_heights, 2.5f,  32.0f), 0.85f, 1.25f, 0.05f),
            derive_float(make_length_parameter("thickness",      "Size along Z",                            0.25f, Kit_default_scale::meters,       0.02f, 4.0f),  0.15f, 0.4f,  0.05f),
            derive_float(make_length_parameter("opening_width",  "Clear width of the window",               1.0f,  Kit_default_scale::meters,       0.3f,  4.0f),  0.8f,  1.6f,  0.1f),
            derive_float(make_length_parameter("opening_height", "Clear height of the window to its crown", 1.2f,  Kit_default_scale::meters,       0.3f,  6.0f),  1.0f,  1.3f,  0.05f),
            derive_float(make_length_parameter("sill_height",    "Height of the sill above the base",       0.9f,  Kit_default_scale::meters,       0.1f,  4.0f),  0.7f,  1.0f,  0.05f),
            derive_enum (make_enum_parameter  ("head",           "Flat lintel, round arch or pointed arch", {"flat", "round", "pointed"}, 0)),
            derive_int  (make_count_parameter ("mullions",       "Upright bars in the opening",             1, 0, 4), 0, 2),
            derive_int  (make_count_parameter ("transoms",       "Level bars in the opening",               1, 0, 3), 0, 1),
            make_seed_parameter (),
            make_count_parameter("segments", "Segments along a curved head; an odd count uses one less", 12, 4, 32)
        };
        shape.material_slots    = {"wall", "trim", "wood"};
        shape.normal_style      = Kit_normal_style::faceted;
        shape.triangle_budget   = 160;
        shape.collision         = Kit_collision::triangle_mesh;
        shape.detail_counts     = {{"segments"}};
        shape.generator         = generate_window_wall;
        shape.mirror_planes     = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.check_combination = check_window_wall;
        shape.get_seam_sides    = [](const Kit_parameter_values&) {
            return std::vector<Kit_side>{Kit_side::x_neg, Kit_side::x_pos, Kit_side::y_neg, Kit_side::y_pos};
        };
        // A sill the agent climbs makes the window a doorway, which then has to fit the agent (R2.10).
        shape.check_walkability = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            if (values.get_float("sill_height") > agent.max_climb) {
                return {};
            }
            return check_kit_opening(*values.get_shape(), values.get_float("opening_width"), values.get_float("opening_height"), agent);
        };
        shape.get_connectors    = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float half_l = 0.5f * values.get_float("length");
            const float half_t = 0.5f * values.get_float("thickness");
            add_side_connector(out_connectors, "start", glm::vec3{-half_l, 0.0f, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "end",   glm::vec3{ half_l, 0.0f, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
            add_base_and_top_connectors(out_connectors, values.get_float("height"));
            add_side_connector(out_connectors, "front", glm::vec3{0.0f, 0.0f,  half_t}, glm::vec3{0.0f, 0.0f,  1.0f});
            add_side_connector(out_connectors, "back",  glm::vec3{0.0f, 0.0f, -half_t}, glm::vec3{0.0f, 0.0f, -1.0f});
            type_wall_run_connectors(out_connectors);
        };
        set_wall_run_flow(shape, "thickness");
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.pediment";
        shape.kit             = "architecture";
        shape.display_name    = "Pediment";
        shape.description     =
            "Classical gable along X on an entablature: raking cornices of three steps over a recessed field, upright eave "
            "ends, and plumb dentils under the rakes; both faces alike.";
        shape.tags            = {"obstacle", "gable", "classical", "size:4x0.5"};
        shape.parameters      = {
            derive_float(make_length_parameter("width",   "Size along X",                                4.0f,  Kit_default_scale::modules, 1.0f,  64.0f), 2.0f,  8.0f,  0.5f),
            derive_float(make_angle_parameter ("pitch",   "Slope of the rakes",                          18.0f, 8.0f, 45.0f),                              12.0f, 30.0f, 1.0f),
            derive_float(make_length_parameter("depth",   "Size along Z",                                0.5f,  Kit_default_scale::modules, 0.1f,  8.0f),  0.3f,  0.9f,  0.05f),
            derive_float(make_length_parameter("recess",  "Depth of the field behind the cornice's bed; at most half of what the depth leaves behind the bed", 0.1f, Kit_default_scale::meters, 0.01f, 0.5f), 0.05f, 0.2f, 0.01f),
            derive_int  (make_count_parameter ("dentils", "Dentils under each rake",                     9, 0, pediment_dentil_max), 6, 12),
            make_seed_parameter()
        };
        shape.material_slots  = {"trim", "wall"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 540;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.generator       = generate_pediment;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.get_seam_sides  = [](const Kit_parameter_values&) { return std::vector<Kit_side>{Kit_side::y_neg}; };
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float half = 0.5f * values.get_float("width");
            out_connectors.push_back(Kit_connector{.name = "base", .position = glm::vec3{0.0f}, .outward = glm::vec3{0.0f, -1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
            add_side_connector(out_connectors, "start", glm::vec3{-half, 0.0f, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "end",   glm::vec3{ half, 0.0f, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.stepped_platform";
        shape.kit             = "architecture";
        shape.display_name    = "Stepped platform";
        shape.description     =
            "Tiers of slabs, each inset from the one below, with chamfered edges and a deck inside a rim on top: a platform "
            "with one tier, a stylobate, a stepped pyramid. With a tier rise within the agent's climb every side is stairs.";
        shape.tags            = {"walkable", "obstacle", "platform", "classical", "tiles", "size:4x4"};
        shape.parameters      = {
            derive_float(make_length_parameter("width",      "Size along X",        4.0f, Kit_default_scale::modules, 0.5f,  64.0f), 3.0f,  8.0f,  0.5f),
            derive_float(make_length_parameter("depth",      "Size along Z",        4.0f, Kit_default_scale::modules, 0.5f,  64.0f), 3.0f,  8.0f,  0.5f),
            derive_int  (make_count_parameter ("tiers",      "Slabs",               3, 1, platform_tier_max), 1, 5),
            derive_float(make_length_parameter("tier_rise",  "Height of each tier", 0.2f, Kit_default_scale::meters,  0.05f, 4.0f),  0.15f, 0.35f, 0.05f),
            derive_float(make_length_parameter("tier_inset", "Each tier's step in from the one below; it shrinks where the tiers would leave less than 0.4 of the smaller side", 0.3f, Kit_default_scale::meters, 0.05f, 8.0f), 0.3f, 0.5f, 0.05f),
            make_seed_parameter()
        };
        shape.material_slots  = {"stone", "floor"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 80;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.generator       = generate_stepped_platform;
        // One tier is a slab (U54); more tiers are steps to climb.
        shape.get_collision   = [](const Kit_parameter_values& values) {
            return (values.get_int("tiers") == 1) ? Kit_collision::box : Kit_collision::triangle_mesh;
        };
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.get_seam_sides  = [](const Kit_parameter_values&) { return std::vector<Kit_side>{Kit_side::y_neg}; };
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const Platform_form form{values};
            add_base_and_top_connectors(out_connectors, form.rise * static_cast<float>(form.tiers));
            add_side_connector(out_connectors, "front", glm::vec3{0.0f, 0.0f,  form.half_z}, glm::vec3{ 0.0f, 0.0f,  1.0f});
            add_side_connector(out_connectors, "back",  glm::vec3{0.0f, 0.0f, -form.half_z}, glm::vec3{ 0.0f, 0.0f, -1.0f});
            add_side_connector(out_connectors, "left",  glm::vec3{-form.half_x, 0.0f, 0.0f}, glm::vec3{-1.0f, 0.0f,  0.0f});
            add_side_connector(out_connectors, "right", glm::vec3{ form.half_x, 0.0f, 0.0f}, glm::vec3{ 1.0f, 0.0f,  0.0f});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.succulent";
        shape.foliage_radius  = 0.4f;
        shape.kit             = "nature";
        shape.display_name    = "Succulent";
        shape.description     =
            "Rosette of thick pointed leaves in a spiral at its origin, the old leaves low and wide, the young ones upright "
            "about a bud: agave (broad channelled leaves ending in a dark spine), aloe (tapering leaves curving up, their "
            "ends dry) or yucca (a head of straight swords on a trunk over a skirt of dead leaves). The youngest leaves "
            "are slot shoot; the leaves' points, a yucca's trunk and its dead leaves slot bark.";
        shape.tags            = {"obstacle", "decoration", "tropical", "size:1x1x1"};
        shape.parameters      = {
            derive_enum(make_enum_parameter("form", "Agave, aloe or yucca", {"agave", "aloe", "yucca"}, 0)),
            from_seed_by_default(derive_float(make_length_parameter("size", "Twice the longest leaf: a rosette's width, a yucca's height", 1.0f, Kit_default_scale::meters, 0.2f, 4.0f), 0.4f, 2.5f, 0.1f)),
            from_seed_by_default(derive_int(make_count_parameter("leaves", "Leaves", 16, 6, 32), 8, 24)),
            derive_float(make_angle_parameter("spread", "Angle of the outermost leaves from the upright", 65.0f, 10.0f, 85.0f), 20.0f, 80.0f, 5.0f),
            make_seed_parameter()
        };
        shape.material_slots  = {"foliage", "bark", "shoot"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 600;
        shape.collision       = Kit_collision::convex_hull;
        shape.randomness      = Kit_randomness::seeded;
        shape.detail_response = Kit_detail_response::scaled;
        shape.generator       = generate_succulent;
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.grass_tuft";
        shape.foliage_radius  = 0.15f;
        shape.kit             = "nature";
        shape.display_name    = "Grass tuft";
        shape.description     =
            "Tuft of blades on a tussock at its origin: grass (arching blades and a few ears on stalks), reed (upright "
            "canes with leaves standing off them, each ending in a drooping panicle) or pampas (a fountain of long blades "
            "under tall plumes). Ears, panicles and plumes are slot plume. No body, a walker passes through it.";
        shape.tags            = {"decoration", "grass", "size:1x1x1"};
        shape.parameters      = {
            derive_enum(make_enum_parameter("form", "Grass, reed or pampas", {"grass", "reed", "pampas"}, 0)),
            from_seed_by_default(derive_float(make_length_parameter("height", "Height of the tallest stalk", 0.6f, Kit_default_scale::meters, 0.1f, 3.0f), 0.2f, 2.0f, 0.05f)),
            from_seed_by_default(derive_int(make_count_parameter("blades", "Blades; a reed has a cane for every three", 16, 4, 40), 8, 24)),
            derive_float(make_ratio_parameter("spread", "How far the blades lean and arch out", 0.5f), 0.2f, 0.8f, 0.05f),
            make_seed_parameter()
        };
        shape.material_slots  = {"blade", "plume"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 640;
        shape.collision       = Kit_collision::none;
        shape.randomness      = Kit_randomness::seeded;
        shape.detail_response = Kit_detail_response::scaled;
        shape.generator       = generate_grass_tuft;
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.boulder_cluster";
        shape.kit             = "nature";
        shape.display_name    = "Boulder cluster";
        shape.description     =
            "Heap of boulders in a round footprint: the largest over the origin, smaller ones leaning on its flank out to "
            "the footprint's edge, every third one lying tilted on its shoulder. Each is a stack of convex rings with "
            "planar sides and chamfered corners: angular (few sides, fracture planes, a tilted flat top), worn (more "
            "sides, wide chamfers, a rounded top) or mixed.";
        shape.tags            = {"obstacle", "rock", "size:4x4x2"};
        shape.parameters      = {
            from_seed_by_default(derive_float(make_length_parameter("radius", "Radius of the footprint", 2.0f, Kit_default_scale::modules, 0.3f, 16.0f), 1.0f, 4.0f, 0.25f)),
            from_seed_by_default(derive_int(make_count_parameter("count", "Boulders", 5, 2, cluster_count_max), 3, 9)),
            derive_float(make_ratio_parameter("size_spread", "How much smaller the smallest boulder is than the largest", 0.5f), 0.3f, 0.8f, 0.05f),
            derive_enum (make_enum_parameter ("form",        "Angular, worn or both", {"angular", "worn", "mixed"}, 2)),
            derive_float(make_ratio_parameter("roughness",   "Irregular sides, lean and tilted tops", 0.6f), 0.3f, 0.9f, 0.05f),
            make_seed_parameter(),
            make_shading_parameter(Kit_shading::smooth)
        };
        shape.material_slots  = {"rock"};
        shape.normal_style    = Kit_normal_style::smooth; // the chamfered sides round off; base and flat tops are hard edges
        shape.triangle_budget = 340;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_boulder_cluster;
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.stepping_stone";
        shape.kit             = "nature";
        shape.display_name    = "Stepping stone";
        shape.description     =
            "Flat-topped stone to step on: a convex outline of the sides with chamfered corners, undercut at the ground "
            "and bevelled round its level top.";
        shape.tags            = {"walkable", "rock", "path", "size:1x1"};
        shape.parameters      = {
            from_seed_by_default(derive_float(make_length_parameter("diameter", "Width before the outline's variation", 0.9f, Kit_default_scale::meters, 0.3f, 4.0f), 0.6f, 1.5f, 0.05f)),
            derive_float(make_length_parameter("thickness", "Height of the level top", 0.25f, Kit_default_scale::meters, 0.05f, 0.5f), 0.2f, 0.4f, 0.05f),
            derive_int  (make_count_parameter ("sides",     "Sides of the outline",    7, 5, 9), 5, 9),
            make_seed_parameter(),
            make_shading_parameter(Kit_shading::smooth)
        };
        shape.material_slots  = {"rock"};
        shape.normal_style    = Kit_normal_style::smooth; // the chamfered sides round off; base and top are hard edges
        shape.triangle_budget = 120;
        shape.collision       = Kit_collision::convex_hull;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_stepping_stone;
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            add_base_and_top_connectors(out_connectors, values.get_float("thickness"));
        };
        shapes.push_back(std::move(shape));
    }
}

} // namespace editor
