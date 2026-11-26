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

// The next eight shapes of the build order of doc/plans/level_kit_extra_shapes.md (doc/editor/level_tools.md).
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

// One ring of a tube: where it stands (the placement's y along the tube), its radius and its place along that y.
class Tube_ring
{
public:
    Placement place {};
    float     radius{0.0f};
    float     y     {0.0f};
    uint32_t  slot  {0}; // of the band after the ring
};

// A tube along rings that each have a placement of their own, traced as a lathe's profile, between two polygons.
void add_tube(Kit_mesh_builder& builder, const std::span<const Tube_ring> rings, const int segments, const Lathe_phase phase)
{
    const uint32_t n     = static_cast<uint32_t>(segments);
    const uint32_t count = static_cast<uint32_t>(rings.size());
    const uint32_t first = static_cast<uint32_t>(builder.get_positions().size());
    for (const Tube_ring& ring : rings) {
        for (uint32_t i = 0; i < n; ++i) {
            const glm::vec2 point = get_ring_point(static_cast<int>(i), segments, phase);
            builder.add_vertex(ring.place.at(glm::vec3{ring.radius * point.x, ring.y, ring.radius * point.y}));
        }
    }
    for (uint32_t r = 0; (r + 1) < count; ++r) {
        const uint32_t lower = first + (r * n);
        const uint32_t upper = lower + n;
        builder.set_slot(rings[r].slot);
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t j = (i + 1) % n;
            builder.add_quad(lower + i, lower + j, upper + j, upper + i);
        }
    }
    const uint32_t last = first + ((count - 1) * n);
    std::vector<uint32_t> bottom;
    std::vector<uint32_t> top;
    for (uint32_t i = 0; i < n; ++i) {
        bottom.push_back(first + (n - 1 - i));
        top   .push_back(last + i);
    }
    builder.set_slot(rings.front().slot);
    builder.add_polygon(bottom);
    builder.set_slot(rings[count - 2].slot);
    builder.add_polygon(top);
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

//
// Quay wall: a wall along X, upright toward the land (-Z) and battered toward the water (+Z), under a coping with a
// timber kerb at the water's edge, with timber fenders on the water face.
//

constexpr uint32_t quay_stone_slot = 0;
constexpr uint32_t quay_wood_slot  = 1;
constexpr int      quay_fender_max = 4;

class Quay_form
{
public:
    explicit Quay_form(const Kit_parameter_values& values)
        : half_l {0.5f * values.get_float("length")}
        , height {values.get_float("height")}
        , half_t {0.5f * values.get_float("thickness")}
        , slope  {std::tan(glm::radians(values.get_float("batter")))}
        , fenders{std::clamp(values.get_int("fenders"), 0, quay_fender_max)}
    {
        coping = std::min(0.15f, 0.2f * height);
        kerb   = std::min(0.15f, 0.24f * half_t);
        foot   = half_t + (height * slope);
    }

    // What the agent walks: the top between the land face and the kerb.
    [[nodiscard]] auto get_clear_width() const -> float
    {
        return (2.0f * half_t) - kerb;
    }

    float half_l {0.0f};
    float height {0.0f}; // the walking surface
    float half_t {0.0f}; // of the top
    float slope  {0.0f}; // the water face's run for each meter down
    int   fenders{0};
    float coping {0.0f}; // thickness of the coping
    float kerb   {0.0f}; // width of the kerb
    float foot   {0.0f}; // z of the water face at the base
};

void generate_quay_wall_build(const Kit_parameter_values& values, GEO::Mesh& mesh, const Kit_build build)
{
    const Quay_form form{values};
    const float hl  = form.half_l;
    const float ht  = form.half_t;
    const float top = form.height - form.coping;
    Kit_mesh_builder builder;
    builder.set_slot(quay_stone_slot);
    const float plinth = std::clamp(0.1f * form.height, 0.08f, 0.3f);
    if (build == Kit_build::current) {
        // The wall and the coping, one solid seen from +X: the land face upright to the walking surface, the water
        // face leaning out to its foot under the coping's overhang.
        const float face  = ht + (form.coping * form.slope);
        const float front = ht + 0.06f;
        const std::array<glm::vec2, 8> section{{
            {form.foot, 0.0f}, {-ht, 0.0f}, {-ht, top}, {-ht, form.height}, {face, form.height}, {front, form.height}, {front, top}, {face, top}
        }};
        const std::array<std::vector<uint32_t>, 3> pieces{{{0, 1, 2, 7}, {7, 2, 3, 4}, {6, 7, 4, 5}}};
        builder.add_x_prism(-hl, hl, section, pieces);
        builder.add_box(glm::vec3{-hl, 0.0f, ht - 0.05f}, glm::vec3{hl, plinth, form.foot + 0.06f});
    } else {
        // The wall's section, seen from +X: the land face upright, the water face leaning out to its foot.
        const float face = ht + (form.coping * form.slope);
        const std::array<glm::vec3, 4> section{{
            {hl, 0.0f, -ht}, {hl, top + (0.1f * form.coping), -ht}, {hl, top + (0.1f * form.coping), face - (0.1f * form.coping * form.slope)}, {hl, 0.0f, form.foot}
        }};
        add_prism_between(builder, section, glm::vec3{-2.0f * hl, 0.0f, 0.0f});
        builder.add_box(glm::vec3{-hl, 0.0f, ht - 0.05f}, glm::vec3{hl, plinth, form.foot + 0.06f});
        builder.add_box(glm::vec3{-hl, top, -(ht - 0.01f)}, glm::vec3{hl, form.height, ht + 0.06f});
    }
    builder.set_slot(quay_wood_slot);
    builder.add_box(glm::vec3{-hl, form.height - 0.01f, ht - form.kerb}, glm::vec3{hl, form.height + std::min(0.12f, 0.8f * form.kerb), ht + 0.03f});
    // The fenders lean with the water face, from over the plinth to under the coping.
    const Placement leaning{.origin = glm::vec3{0.0f, 0.0f, form.foot}, .y = glm::vec3{0.0f, 1.0f, -form.slope}};
    const float half = std::min(0.08f, 0.1f * hl);
    for (int k = 0; k < form.fenders; ++k) {
        const float x = -hl + ((2.0f * hl) * (static_cast<float>(k) + 0.5f) / static_cast<float>(form.fenders));
        add_placed_box(builder, leaning, glm::vec3{x - half, 0.6f * plinth, -0.02f}, glm::vec3{x + half, top - (0.3f * form.coping), 0.13f});
    }
    builder.write(mesh);
}

void generate_quay_wall(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    generate_quay_wall_build(values, mesh, Kit_build::current);
}

void generate_quay_wall_old(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    generate_quay_wall_build(values, mesh, Kit_build::former);
}

[[nodiscard]] auto check_quay_walkability(const Kit_parameter_values& values, const Kit_agent& agent) -> std::string
{
    const Quay_form form{values};
    const float min_width = (2.0f * agent.radius) + kit_opening_margin;
    if (form.get_clear_width() < min_width) {
        return fmt::format("{}: the top's clear width {} m is narrower than the agent's {} m", values.get_shape()->id, form.get_clear_width(), min_width);
    }
    return {};
}

//
// Pipe: a pipe lying along X with open ends, straight, bent toward -Z about an upright axis, or with a branch toward -Z.
//

constexpr uint32_t pipe_metal_slot = 0;
constexpr int      pipe_bend_bands = 6;

enum class Pipe_kind : unsigned int { straight = 0, elbow, tee };

[[nodiscard]] auto get_pipe_kind(const std::string_view name) -> Pipe_kind
{
    if (name == "elbow") {
        return Pipe_kind::elbow;
    }
    if (name == "tee") {
        return Pipe_kind::tee;
    }
    return Pipe_kind::straight;
}

class Pipe_form
{
public:
    explicit Pipe_form(const Kit_parameter_values& values)
        : kind    {get_pipe_kind(values.get_enum("form"))}
        , flanged {values.get_enum("flanges") == "ends"}
        , segments{std::max(values.get_int("segments"), 6)}
        , radius  {0.5f * values.get_float("diameter")}
        , half_l  {0.5f * values.get_float("length")}
        , angle   {(kind == Pipe_kind::elbow) ? glm::radians(values.get_float("bend_angle")) : 0.0f}
    {
        flange = 1.35f * radius;
        lip    = 0.2f * radius;
        // The pipe rests on a side of its widest ring.
        axis_y = (flanged ? flange : radius) * get_ring_point(0, segments, Lathe_phase::side).x;
        // The bend: as wide as the legs leave room for, but wider than the pipe.
        const float tangent = std::tan(0.5f * angle);
        bend    = std::max(std::min(3.0f * radius, 0.7f * half_l / std::max(tangent, 0.05f)), 1.5f * radius);
        inset   = bend * tangent;
        out     = glm::vec3{std::cos(angle), 0.0f, -std::sin(angle)};
    }

    [[nodiscard]] auto get_start() const -> glm::vec3 { return glm::vec3{-half_l, axis_y, 0.0f}; }
    [[nodiscard]] auto get_end  () const -> glm::vec3 { return (half_l * out) + (axis_y * y_axis); }
    // The axis and its direction at the angle along the bend, 0 to `angle`.
    [[nodiscard]] auto get_bend_point(const float at) const -> glm::vec3
    {
        return glm::vec3{-inset + (bend * std::sin(at)), axis_y, -bend * (1.0f - std::cos(at))};
    }
    [[nodiscard]] auto get_bend_direction(const float at) const -> glm::vec3
    {
        return glm::vec3{std::cos(at), 0.0f, -std::sin(at)};
    }

    Pipe_kind kind    {Pipe_kind::straight};
    bool      flanged {true};
    int       segments{10};
    float     radius  {0.0f};
    float     half_l  {0.0f};
    float     angle   {0.0f}; // of an elbow's bend
    float     flange  {0.0f}; // radius of a flange
    float     lip     {0.0f}; // width of a flange along the pipe
    float     axis_y  {0.0f};
    float     bend    {0.0f}; // radius of the bend's axis
    float     inset   {0.0f}; // from the corner to where the bend begins
    glm::vec3 out     {1.0f, 0.0f, 0.0f}; // the direction the pipe ends in
};

// The frame of a ring whose tube runs along `direction` (unit, level): a side of the ring faces down.
[[nodiscard]] auto make_pipe_placement(const glm::vec3& origin, const glm::vec3& direction) -> Placement
{
    return Placement{.origin = origin, .x = -y_axis, .y = direction, .z = glm::cross(-y_axis, direction)};
}

// The rings of an open end at `place`, whose y runs into the pipe: the bore's floor, the mouth, and a flange.
void add_pipe_end(std::vector<Tube_ring>& rings, const Pipe_form& form, const Placement& place, const bool at_start)
{
    const float bore  = 0.8f * form.radius;
    const float sign  = at_start ? 1.0f : -1.0f;
    std::vector<Tube_ring> end;
    end.push_back(Tube_ring{place, bore, sign * 0.9f * form.radius});
    end.push_back(Tube_ring{place, bore, 0.0f});
    if (form.flanged) {
        end.push_back(Tube_ring{place, form.flange, 0.0f});
        end.push_back(Tube_ring{place, form.flange, sign * form.lip});
        end.push_back(Tube_ring{place, form.radius, sign * form.lip});
    } else {
        end.push_back(Tube_ring{place, form.radius, 0.0f});
    }
    if (at_start) {
        rings.insert(rings.end(), end.begin(), end.end());
    } else {
        rings.insert(rings.end(), end.rbegin(), end.rend());
    }
}

void generate_pipe(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Pipe_form form{values};
    Kit_mesh_builder builder;
    builder.set_slot(pipe_metal_slot);
    std::vector<Tube_ring> rings;
    add_pipe_end(rings, form, make_pipe_placement(form.get_start(), x_axis), true);
    if (form.kind == Pipe_kind::elbow) {
        for (int k = 0; k <= pipe_bend_bands; ++k) {
            const float at = form.angle * static_cast<float>(k) / static_cast<float>(pipe_bend_bands);
            rings.push_back(Tube_ring{make_pipe_placement(form.get_bend_point(at), form.get_bend_direction(at)), form.radius, 0.0f});
        }
    }
    add_pipe_end(rings, form, make_pipe_placement(form.get_end(), form.out), false);
    add_tube(builder, rings, form.segments, Lathe_phase::side);
    if (form.kind == Pipe_kind::tee) {
        // The branch leaves the run's middle toward -Z; a flanged tee has a collar around its root.
        const glm::vec3 toward{0.0f, 0.0f, -1.0f};
        const Placement root = make_pipe_placement(glm::vec3{0.0f, form.axis_y, -0.3f * form.radius}, toward);
        std::vector<Tube_ring> branch;
        if (form.flanged) {
            branch.push_back(Tube_ring{root, 1.15f * form.radius, 0.0f});
            branch.push_back(Tube_ring{root, 1.15f * form.radius, 0.95f * form.radius});
            branch.push_back(Tube_ring{root, form.radius, 0.95f * form.radius});
        } else {
            branch.push_back(Tube_ring{root, form.radius, 0.0f});
        }
        add_pipe_end(branch, form, make_pipe_placement(glm::vec3{0.0f, form.axis_y, -form.half_l}, toward), false);
        add_tube(builder, branch, form.segments, Lathe_phase::side);
    }
    builder.write(mesh);
}

//
// Pot: a vessel turned about the upright through its origin, hollow at its mouth.
//

constexpr uint32_t pot_stone_slot = 0;

enum class Pot_form : unsigned int { jar = 0, amphora, urn, bowl, planter };

[[nodiscard]] auto get_pot_form(const std::string_view name) -> Pot_form
{
    if (name == "amphora") {
        return Pot_form::amphora;
    }
    if (name == "urn") {
        return Pot_form::urn;
    }
    if (name == "bowl") {
        return Pot_form::bowl;
    }
    if (name == "planter") {
        return Pot_form::planter;
    }
    return Pot_form::jar;
}

// The profile of the form from its foot over its rim down into its hollow: (radius in half widths, height in
// heights); `mouth` is the rim's radius of a jar, an amphora and an urn.
[[nodiscard]] auto get_pot_profile(const Pot_form form, const float mouth) -> std::vector<glm::vec2>
{
    const float m = mouth;
    switch (form) {
        case Pot_form::jar:
            return {
                {0.5f, 0.0f}, {0.8f, 0.12f}, {1.0f, 0.42f}, {0.96f, 0.62f}, {0.9f * m, 0.86f}, {0.9f * m, 0.93f}, {m, 0.96f}, {m, 1.0f},
                {m - 0.1f, 1.0f}, {m - 0.1f, 0.6f}
            };
        case Pot_form::amphora:
            return {
                {0.26f, 0.0f}, {0.26f, 0.03f}, {0.2f, 0.07f}, {0.72f, 0.3f}, {1.0f, 0.52f}, {0.86f, 0.68f}, {0.6f * m, 0.8f}, {0.6f * m, 0.92f},
                {m, 0.96f}, {m, 1.0f}, {(0.6f * m) - 0.07f, 1.0f}, {(0.6f * m) - 0.07f, 0.72f}
            };
        case Pot_form::urn:
            return {
                {0.5f, 0.0f}, {0.5f, 0.05f}, {0.24f, 0.1f}, {0.24f, 0.18f}, {0.72f, 0.32f}, {1.0f, 0.52f}, {1.0f, 0.62f}, {0.9f * m, 0.84f},
                {0.9f * m, 0.93f}, {m, 0.95f}, {m, 1.0f}, {m - 0.1f, 1.0f}, {m - 0.1f, 0.62f}
            };
        case Pot_form::bowl:
            return {{0.35f, 0.0f}, {0.4f, 0.06f}, {0.8f, 0.3f}, {0.98f, 0.65f}, {1.0f, 1.0f}, {0.9f, 1.0f}, {0.86f, 0.65f}, {0.66f, 0.32f}, {0.3f, 0.14f}};
        case Pot_form::planter:
            return {{0.7f, 0.0f}, {0.9f, 0.78f}, {1.0f, 0.78f}, {1.0f, 1.0f}, {0.86f, 1.0f}, {0.84f, 0.86f}};
    }
    return {};
}

void generate_pot(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Pot_form form     = get_pot_form(values.get_enum("form"));
    const float    height   = values.get_float("height");
    const float    half_w   = 0.5f * values.get_float("width");
    const float    mouth    = values.get_float("neck");
    const int      segments = std::max(values.get_int("segments"), 6);
    Kit_mesh_builder builder;
    std::vector<Lathe_ring> rings;
    for (const glm::vec2& point : get_pot_profile(form, mouth)) {
        rings.push_back(Lathe_ring{point.x * half_w, point.y * height});
    }
    add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = segments, .slot = pot_stone_slot});
    if (form == Pot_form::amphora) {
        // Two handles from the neck to the shoulder, toward +X and -X.
        const float thick = 0.12f * half_w;
        builder.set_slot(pot_stone_slot);
        for (const float side : {-1.0f, 1.0f}) {
            const glm::vec3 neck    {side * 0.5f * mouth * half_w, 0.89f * height, 0.0f};
            const glm::vec3 elbow   {side * 0.84f * half_w,        0.87f * height, 0.0f};
            const glm::vec3 shoulder{side * 0.8f * half_w,         0.62f * height, 0.0f};
            builder.add_beam(neck, elbow, thick, 0.8f * thick);
            builder.add_beam(elbow + glm::vec3{0.0f, 0.3f * thick, 0.0f}, shoulder, thick, 0.8f * thick);
        }
    }
    builder.write(mesh);
}

//
// Table: furniture of boards on legs within its width, depth and height: a table, a bench with a back, a stool, a shelf.
//

constexpr uint32_t table_wood_slot = 0;
constexpr int      table_shelves   = 4;

enum class Table_form : unsigned int { table = 0, bench, stool, shelf };

[[nodiscard]] auto get_table_form(const std::string_view name) -> Table_form
{
    if (name == "bench") {
        return Table_form::bench;
    }
    if (name == "stool") {
        return Table_form::stool;
    }
    if (name == "shelf") {
        return Table_form::shelf;
    }
    return Table_form::table;
}

class Table_values
{
public:
    explicit Table_values(const Kit_parameter_values& values)
        : form    {get_table_form(values.get_enum("form"))}
        , trestles{values.get_enum("legs") == "trestles"}
        , half_w  {0.5f * values.get_float("width")}
        , half_d  {0.5f * values.get_float("depth")}
        , height  {values.get_float("height")}
    {
        const float least = std::min({2.0f * half_w, 2.0f * half_d, height});
        board = std::clamp(0.05f * least, 0.015f, 0.045f);
        leg   = std::clamp(0.08f * least, 0.025f, 0.07f);
        // A bench's seat lies under its back.
        seat  = (form == Table_form::bench) ? (0.55f * height) : height;
    }

    Table_form form    {Table_form::table};
    bool       trestles{false};
    float      half_w  {0.0f};
    float      half_d  {0.0f};
    float      height  {0.0f};
    float      board   {0.0f}; // thickness of a board
    float      leg     {0.0f}; // width of a leg
    float      seat    {0.0f}; // the top of the board on the legs
};

// A leg with a level foot and a level head: from `foot` on the ground to under `head`, both the leg's middles.
void add_table_leg(Kit_mesh_builder& builder, const glm::vec3& foot, const glm::vec3& head, const float half)
{
    const glm::vec3 run = head - foot;
    const Placement place{.origin = foot, .y = glm::vec3{run.x / run.y, 1.0f, run.z / run.y}};
    add_placed_box(builder, place, glm::vec3{-half, 0.0f, -half}, glm::vec3{half, run.y, half});
}

// What carries a board at `top` (its upper face) over the half sizes about z = center_z: four posts under aprons, or
// two trestles of splayed legs under a tie, joined by a stretcher.
void add_table_legs(Kit_mesh_builder& builder, const Table_values& table, const float half_w, const float half_d, const float top, const float center_z)
{
    const float     under = top - table.board;
    const float     half  = 0.5f * table.leg;
    const glm::vec3 shift{0.0f, 0.0f, center_z};
    if (!table.trestles) {
        const float x     = half_w - (1.6f * table.leg);
        const float z     = half_d - (1.6f * table.leg);
        const float apron = std::min(2.0f * table.leg, 0.3f * under);
        for (const float side_x : {-1.0f, 1.0f}) {
            for (const float side_z : {-1.0f, 1.0f}) {
                const glm::vec3 foot = shift + glm::vec3{side_x * x, 0.0f, side_z * z};
                add_table_leg(builder, foot, foot + glm::vec3{0.0f, under + (0.3f * table.board), 0.0f}, half);
            }
        }
        const float thin = 0.3f * table.leg;
        for (const float side : {-1.0f, 1.0f}) {
            builder.add_box(shift + glm::vec3{-x, under - apron, (side * z) - thin}, shift + glm::vec3{x, under + (0.2f * table.board), (side * z) + thin});
            builder.add_box(shift + glm::vec3{(side * x) - thin, under - apron, -z}, shift + glm::vec3{(side * x) + thin, under + (0.2f * table.board), z});
        }
        return;
    }
    const float x = 0.7f * half_w;
    for (const float side_x : {-1.0f, 1.0f}) {
        for (const float side_z : {-1.0f, 1.0f}) {
            add_table_leg(
                builder,
                shift + glm::vec3{side_x * x, 0.0f, side_z * (half_d - half)},
                shift + glm::vec3{side_x * x, under + (0.3f * table.board), side_z * 0.3f * half_d},
                half
            );
        }
        // The tie across the trestle under the board.
        builder.add_box(
            shift + glm::vec3{(side_x * x) - (0.7f * half), under - table.leg, -0.5f * half_d},
            shift + glm::vec3{(side_x * x) + (0.7f * half), under + (0.2f * table.board), 0.5f * half_d}
        );
    }
    builder.add_box(shift + glm::vec3{-x, 0.45f * under, -0.6f * half}, shift + glm::vec3{x, (0.45f * under) + table.leg, 0.6f * half});
}

void generate_table(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Table_values table{values};
    const float hw = table.half_w;
    const float hd = table.half_d;
    const float h  = table.height;
    Kit_mesh_builder builder;
    builder.set_slot(table_wood_slot);
    if (table.form == Table_form::table) {
        builder.add_box(glm::vec3{-hw, h - table.board, -hd}, glm::vec3{hw, h, hd});
        add_table_legs(builder, table, hw, hd, h, 0.0f);
    } else if (table.form == Table_form::bench) {
        // A seat of two planks toward +Z, and a back of two uprights and two slats at -Z.
        const float back = 2.0f * table.leg;
        const float gap  = std::min(0.012f, 0.05f * hd);
        const float mid  = 0.5f * back;
        builder.add_box(glm::vec3{-hw, table.seat - table.board, -hd + back}, glm::vec3{hw, table.seat, mid - gap});
        builder.add_box(glm::vec3{-hw, table.seat - table.board, mid + gap}, glm::vec3{hw, table.seat, hd});
        // The legs stand under the seat, in front of the back.
        add_table_legs(builder, table, hw, hd - (0.5f * back), table.seat, 0.5f * back);
        const float x = hw - (1.6f * table.leg);
        for (const float side : {-1.0f, 1.0f}) {
            builder.add_box(glm::vec3{(side * x) - (0.5f * table.leg), 0.0f, -hd + (0.25f * back)}, glm::vec3{(side * x) + (0.5f * table.leg), h, -hd + (0.75f * back)});
        }
        const float rise = h - table.seat;
        for (const float share : {0.45f, 0.82f}) {
            const float y = table.seat + (share * rise);
            builder.add_box(glm::vec3{-hw, y - (0.14f * rise), -hd}, glm::vec3{hw, y + (0.14f * rise), -hd + (0.3f * back)});
        }
    } else if (table.form == Table_form::stool) {
        builder.add_box(glm::vec3{-hw, h - table.board, -hd}, glm::vec3{hw, h, hd});
        const float under = h - table.board;
        if (table.trestles) {
            // Two boards under the seat's ends and a stretcher between them.
            for (const float side : {-1.0f, 1.0f}) {
                const float x = side * 0.78f * hw;
                builder.add_box(glm::vec3{x - (0.5f * table.board), 0.0f, -0.85f * hd}, glm::vec3{x + (0.5f * table.board), under + (0.2f * table.board), 0.85f * hd});
            }
            builder.add_box(glm::vec3{-0.78f * hw, 0.4f * under, -0.5f * table.leg}, glm::vec3{0.78f * hw, (0.4f * under) + table.leg, 0.5f * table.leg});
        } else {
            // Four splayed legs and a rung between each two.
            const float half = 0.5f * table.leg;
            for (const float side_x : {-1.0f, 1.0f}) {
                for (const float side_z : {-1.0f, 1.0f}) {
                    add_table_leg(
                        builder,
                        glm::vec3{side_x * (hw - half), 0.0f, side_z * (hd - half)},
                        glm::vec3{side_x * 0.62f * hw, under + (0.3f * table.board), side_z * 0.62f * hd},
                        half
                    );
                }
            }
            const float y    = 0.35f * under;
            const float x    = 0.86f * (hw - half);
            const float z    = 0.86f * (hd - half);
            const float thin = 0.3f * table.leg;
            for (const float side : {-1.0f, 1.0f}) {
                builder.add_box(glm::vec3{-x, y - thin, (side * z) - thin}, glm::vec3{x, y + thin, (side * z) + thin});
                builder.add_box(glm::vec3{(side * x) - thin, y + (2.5f * thin), -z}, glm::vec3{(side * x) + thin, y + (4.5f * thin), z});
            }
        }
    } else {
        // Shelves between four posts, or between two side boards before a back board.
        const float inset = table.trestles ? table.board : (0.5f * table.leg);
        for (int k = 0; k < table_shelves; ++k) {
            const float top = (k == (table_shelves - 1)) ? h : ((0.08f * h) + table.board + (((0.92f * h) - table.board) * static_cast<float>(k) / static_cast<float>(table_shelves - 1)));
            builder.add_box(glm::vec3{-(hw - (0.5f * inset)), top - table.board, -(hd - (0.3f * table.board))}, glm::vec3{hw - (0.5f * inset), top, hd - (0.3f * table.board)});
        }
        const float top = h - (0.5f * table.board);
        if (table.trestles) {
            for (const float side : {-1.0f, 1.0f}) {
                const float x0 = side * (hw - table.board);
                const float x1 = side * hw;
                builder.add_box(glm::vec3{std::min(x0, x1), 0.0f, -hd}, glm::vec3{std::max(x0, x1), top, hd});
            }
            builder.add_box(glm::vec3{-(hw - (0.4f * table.board)), 0.04f * h, -hd + (0.1f * table.board)}, glm::vec3{hw - (0.4f * table.board), top - (0.2f * table.board), -hd + (0.6f * table.board)});
        } else {
            for (const float side_x : {-1.0f, 1.0f}) {
                for (const float side_z : {-1.0f, 1.0f}) {
                    const float x0 = side_x * (hw - table.leg);
                    const float x1 = side_x * hw;
                    const float z0 = side_z * (hd - table.leg);
                    const float z1 = side_z * hd;
                    builder.add_box(glm::vec3{std::min(x0, x1), 0.0f, std::min(z0, z1)}, glm::vec3{std::max(x0, x1), top, std::max(z0, z1)});
                }
            }
        }
    }
    builder.write(mesh);
}

//
// Market stall: a counter toward +Z between four posts under an awning of cloth with a valance.
//

constexpr uint32_t stall_wood_slot  = 0;
constexpr uint32_t stall_cloth_slot = 1;

enum class Stall_awning : unsigned int { flat = 0, sloped, peaked };

[[nodiscard]] auto get_stall_awning(const std::string_view name) -> Stall_awning
{
    if (name == "sloped") {
        return Stall_awning::sloped;
    }
    if (name == "peaked") {
        return Stall_awning::peaked;
    }
    return Stall_awning::flat;
}

class Stall_form
{
public:
    explicit Stall_form(const Kit_parameter_values& values)
        : awning  {get_stall_awning(values.get_enum("awning"))}
        , half_w  {0.5f * values.get_float("width")}
        , half_d  {0.5f * values.get_float("depth")}
        , height  {values.get_float("height")}
        , overhang{values.get_float("overhang")}
    {
        post    = std::clamp(0.05f * (2.0f * half_w), 0.06f, 0.1f);
        cloth   = 0.03f;
        counter = std::min(0.9f, 0.45f * height);
        reach   = half_d + overhang;
        // The fall of the cloth from its highest line to its eaves.
        fall    = (awning == Stall_awning::flat) ? 0.0f : (((awning == Stall_awning::sloped) ? 0.2f : 0.18f) * height);
    }

    // The cloth's upper face over z.
    [[nodiscard]] auto get_cloth_top(const float z) const -> float
    {
        if (awning == Stall_awning::sloped) {
            return height - (fall * (z + reach) / (2.0f * reach));
        }
        if (awning == Stall_awning::peaked) {
            return height - (fall * std::abs(z) / reach);
        }
        return height;
    }

    Stall_awning awning  {Stall_awning::flat};
    float        half_w  {0.0f};
    float        half_d  {0.0f};
    float        height  {0.0f};
    float        overhang{0.0f};
    float        post    {0.0f};
    float        cloth   {0.0f}; // thickness of the cloth
    float        counter {0.0f}; // height of the counter
    float        reach   {0.0f}; // from the middle to the cloth's front and back edges
    float        fall    {0.0f};
};

void generate_market_stall_build(const Kit_parameter_values& values, GEO::Mesh& mesh, const Kit_build build)
{
    const Stall_form form{values};
    const float hw = form.half_w;
    const float hd = form.half_d;
    Kit_mesh_builder builder;
    builder.set_slot(stall_wood_slot);
    // The posts reach into the cloth; a rail joins the front two and the back two under it.
    for (const float side_z : {-1.0f, 1.0f}) {
        const float z0   = side_z * (hd - form.post);
        const float z1   = side_z * hd;
        const float top  = std::min(form.get_cloth_top(z0), form.get_cloth_top(z1)) - (0.5f * form.cloth);
        for (const float side_x : {-1.0f, 1.0f}) {
            const float x0 = side_x * (hw - form.post);
            const float x1 = side_x * hw;
            builder.add_box(glm::vec3{std::min(x0, x1), 0.0f, std::min(z0, z1)}, glm::vec3{std::max(x0, x1), top, std::max(z0, z1)});
        }
        const float rail_z = side_z * (hd - (0.5f * form.post));
        builder.add_box(
            glm::vec3{-(hw - (0.5f * form.post)), top - (1.6f * form.post), rail_z - (0.3f * form.post)},
            glm::vec3{  hw - (0.5f * form.post),  top - (0.6f * form.post), rail_z + (0.3f * form.post)}
        );
    }
    // The counter: a top on a front and two sides of boards.
    {
        const float deep  = std::min(0.55f, 0.9f * hd);
        const float board = 0.03f;
        const float inner = hw - (0.4f * form.post);
        builder.add_box(glm::vec3{-inner, form.counter - 0.05f, hd - deep}, glm::vec3{inner, form.counter, hd + std::min(0.08f, 0.5f * form.overhang)});
        builder.add_box(glm::vec3{-(hw - (0.6f * form.post)), 0.08f, hd - (0.7f * form.post)}, glm::vec3{hw - (0.6f * form.post), form.counter - 0.04f, hd - (0.7f * form.post) + board});
        for (const float side : {-1.0f, 1.0f}) {
            const float x0 = side * (hw - (0.7f * form.post));
            const float x1 = side * (hw - (0.7f * form.post) - board);
            builder.add_box(glm::vec3{std::min(x0, x1), 0.08f, hd - (0.92f * deep)}, glm::vec3{std::max(x0, x1), form.counter - 0.04f, hd - (0.8f * form.post)});
        }
    }
    builder.set_slot(stall_cloth_slot);
    if (build == Kit_build::current) {
        // The cloth and the valance hanging flush from its front edge, one solid: a level slab, a slab falling toward
        // the front, or two slopes sharing a ridge along X.
        const float     edge     = form.get_cloth_top(form.reach);
        const float     valance  = edge - std::min(0.22f, 0.1f * form.height);
        constexpr float hem      = 0.016f; // the valance's depth
        const auto      under    = [&form](const float z) -> glm::vec2 { return glm::vec2{z, form.get_cloth_top(z) - form.cloth}; };
        const auto      over     = [&form](const float z) -> glm::vec2 { return glm::vec2{z, form.get_cloth_top(z)}; };
        const float     r        = form.reach;
        std::vector<glm::vec2>             section{glm::vec2{r, valance}, glm::vec2{r - hem, valance}, under(r - hem)};
        std::vector<std::vector<uint32_t>> pieces;
        if (form.awning == Stall_awning::peaked) {
            section.insert(section.end(), {under(0.0f), under(-r), over(-r), over(0.0f), over(r)});
            pieces = {{0, 1, 2, 7}, {2, 3, 6, 7}, {3, 4, 5, 6}};
        } else {
            section.insert(section.end(), {under(-r), over(-r), over(r)});
            pieces = {{0, 1, 2, 5}, {2, 3, 4, 5}};
        }
        builder.add_x_prism(-hw, hw, section, pieces);
        builder.write(mesh);
        return;
    }
    // The cloth: a level slab, a slab falling toward the front, or two falling from a ridge along X.
    const float slope = form.fall / ((form.awning == Stall_awning::sloped) ? (2.0f * form.reach) : form.reach);
    if (form.awning == Stall_awning::peaked) {
        const Placement front = make_sloping_placement(glm::vec3{0.0f, form.height - form.cloth, 0.0f}, -slope);
        const Placement back  = make_sloping_placement(glm::vec3{0.0f, form.height - form.cloth, 0.0f}, slope);
        add_placed_box(builder, front, glm::vec3{-hw, 0.0f, 0.0f}, glm::vec3{hw, form.cloth, form.reach});
        add_placed_box(builder, back,  glm::vec3{-(hw - 0.004f), -0.002f, -form.reach}, glm::vec3{hw - 0.004f, form.cloth - 0.002f, 0.002f});
    } else {
        const Placement place = make_sloping_placement(glm::vec3{0.0f, form.get_cloth_top(0.0f) - form.cloth, 0.0f}, -slope);
        add_placed_box(builder, place, glm::vec3{-hw, 0.0f, -form.reach}, glm::vec3{hw, form.cloth, form.reach});
    }
    {
        // The valance hangs from the front edge.
        const float edge = form.get_cloth_top(form.reach);
        const float drop = std::min(0.22f, 0.1f * form.height);
        builder.add_box(glm::vec3{-(hw - 0.006f), edge - drop, form.reach - 0.02f}, glm::vec3{hw - 0.006f, edge - (0.4f * form.cloth), form.reach - 0.004f});
    }
    builder.write(mesh);
}

void generate_market_stall(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    generate_market_stall_build(values, mesh, Kit_build::current);
}

void generate_market_stall_old(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    generate_market_stall_build(values, mesh, Kit_build::former);
}

//
// Cart: a hand cart along Z on two wheels about the origin: a bed on two shafts that reach forward (+Z) as handles,
// tilted toward them and standing on two legs under its front.
//

constexpr uint32_t cart_wood_slot  = 0;
constexpr uint32_t cart_metal_slot = 1;

enum class Cart_sides : unsigned int { open = 0, rails, boards };

[[nodiscard]] auto get_cart_sides(const std::string_view name) -> Cart_sides
{
    if (name == "rails") {
        return Cart_sides::rails;
    }
    if (name == "boards") {
        return Cart_sides::boards;
    }
    return Cart_sides::open;
}

class Cart_form
{
public:
    explicit Cart_form(const Kit_parameter_values& values)
        : sides   {get_cart_sides(values.get_enum("sides"))}
        , segments{std::max(values.get_int("segments"), 6)}
        , length  {values.get_float("length")}
        , half_w  {0.5f * values.get_float("width")}
        , wheel   {values.get_float("wheel_radius")}
    {
        thick  = std::clamp(0.12f * wheel, 0.04f, 0.08f);
        axle_y = wheel * get_ring_point(0, segments, Lathe_phase::side).x;
        bed    = half_w - thick - 0.04f;
        back   = -0.3f * length;
        front  = 0.3f * length;
        tips   = 0.7f * length;
        // The handles' ends stay over the ground.
        const float most = std::asin(std::min(0.8f * axle_y / tips, 1.0f));
        tilt = std::min(glm::radians(values.get_float("tilt")), most);
    }

    // The bed's frame: its origin on the axle, its z along the shafts.
    [[nodiscard]] auto get_placement() const -> Placement
    {
        const float c = std::cos(tilt);
        const float s = std::sin(tilt);
        return Placement{.origin = glm::vec3{0.0f, axle_y, 0.0f}, .x = x_axis, .y = glm::vec3{0.0f, c, s}, .z = glm::vec3{0.0f, -s, c}};
    }

    Cart_sides sides   {Cart_sides::open};
    int        segments{12};
    float      length  {0.0f};
    float      half_w  {0.0f};
    float      wheel   {0.0f};
    float      thick   {0.0f}; // of a wheel
    float      axle_y  {0.0f};
    float      bed     {0.0f}; // half the bed's width
    float      back    {0.0f}; // the bed's ends and the handles' along the shafts, from the axle
    float      front   {0.0f};
    float      tips    {0.0f};
    float      tilt    {0.0f};
};

void generate_cart(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Cart_form form{values};
    const Placement place = form.get_placement();
    const float shaft = std::clamp(0.05f * form.length, 0.03f, 0.06f);
    const float deck  = 0.6f * shaft;
    Kit_mesh_builder builder;
    builder.set_slot(cart_wood_slot);
    // Two shafts under the deck, reaching forward as handles.
    for (const float side : {-1.0f, 1.0f}) {
        const float x = side * (form.bed - (0.7f * shaft));
        add_placed_box(builder, place, glm::vec3{x - (0.5f * shaft), 0.0f, form.back}, glm::vec3{x + (0.5f * shaft), shaft, form.tips});
    }
    add_placed_box(builder, place, glm::vec3{-form.bed, 0.9f * shaft, form.back + 0.01f}, glm::vec3{form.bed, shaft + deck, form.front});
    // Two legs from the ground up into the deck's middle under the bed's front; the deck's tilt stays within its thickness.
    {
        const glm::vec3 into = place.at(glm::vec3{0.0f, (0.9f * shaft) + (0.5f * ((0.1f * shaft) + deck)), 0.85f * form.front});
        const float     half = 0.5f * shaft;
        for (const float side : {-1.0f, 1.0f}) {
            const float x = side * (form.bed - (2.2f * shaft));
            builder.add_box(glm::vec3{x - half, 0.0f, into.z - half}, glm::vec3{x + half, into.y, into.z + half});
        }
    }
    const float top = shaft + deck;
    if (form.sides == Cart_sides::rails) {
        // Two posts and a rail on each side.
        const float high = std::min(0.35f, 0.3f * form.length);
        for (const float side : {-1.0f, 1.0f}) {
            const float x = side * (form.bed - (0.5f * shaft));
            for (const float z : {form.back + shaft, form.front - shaft}) {
                add_placed_box(builder, place, glm::vec3{x - (0.4f * shaft), top - (0.2f * deck), z - (0.4f * shaft)}, glm::vec3{x + (0.4f * shaft), top + high, z + (0.4f * shaft)});
            }
            add_placed_box(builder, place, glm::vec3{x - (0.3f * shaft), top + high - (0.2f * shaft), form.back + 0.02f}, glm::vec3{x + (0.3f * shaft), top + high + (0.5f * shaft), form.front - 0.02f});
        }
    } else if (form.sides == Cart_sides::boards) {
        // A board on each side and across both ends.
        const float high  = std::min(0.3f, 0.25f * form.length);
        const float board = 0.5f * shaft;
        for (const float side : {-1.0f, 1.0f}) {
            const float x0 = side * (form.bed - board);
            const float x1 = side * form.bed;
            add_placed_box(builder, place, glm::vec3{std::min(x0, x1), top - (0.2f * deck), form.back + 0.02f}, glm::vec3{std::max(x0, x1), top + high, form.front - 0.01f});
            const float z0 = (side < 0.0f) ? (form.back + 0.03f) : (form.front - 0.02f - board);
            add_placed_box(builder, place, glm::vec3{-(form.bed - (0.5f * board)), top - (0.1f * deck), z0}, glm::vec3{form.bed - (0.5f * board), top + (0.95f * high), z0 + board});
        }
    }
    // The axle and the wheels: a disc of wood between a hub and an iron tyre.
    builder.set_slot(cart_metal_slot);
    builder.add_box(glm::vec3{-(form.half_w - (0.5f * form.thick)), form.axle_y - (0.4f * shaft), -0.4f * shaft}, glm::vec3{form.half_w - (0.5f * form.thick), form.axle_y + (0.4f * shaft), 0.4f * shaft});
    for (const float side : {-1.0f, 1.0f}) {
        const float a    = 0.42f * form.thick;
        const float b    = 0.5f * form.thick;
        const float hub  = 0.2f * form.wheel;
        const float tyre = form.wheel - std::min(0.03f, 0.08f * form.wheel);
        const std::array<Lathe_ring, 8> rings{{
            {hub, -b}, {hub, -a}, {tyre, -a}, {form.wheel, -a}, {form.wheel, a}, {tyre, a}, {hub, a}, {hub, b}
        }};
        constexpr std::array<uint32_t, 7> band_slots{
            cart_metal_slot, cart_wood_slot, cart_metal_slot, cart_metal_slot, cart_metal_slot, cart_wood_slot, cart_metal_slot
        };
        const Placement turned{.origin = glm::vec3{side * (form.half_w - b), form.axle_y, 0.0f}, .x = -y_axis, .y = x_axis, .z = z_axis};
        add_lathe(builder, turned, Lathe{.rings = rings, .segments = form.segments, .phase = Lathe_phase::side, .band_slots = band_slots});
    }
    builder.write(mesh);
}

//
// Well: a ring wall under a coping around a shaft, with a bucket on its rim; two posts beside it carry a windlass
// under a crossbar or a gable roof.
//

constexpr uint32_t well_stone_slot = 0;
constexpr uint32_t well_wood_slot  = 1;
constexpr int      well_round      = 8; // points around the bucket and the windlass

enum class Well_roof : unsigned int { none = 0, crossbar, gable };

[[nodiscard]] auto get_well_roof(const std::string_view name) -> Well_roof
{
    if (name == "crossbar") {
        return Well_roof::crossbar;
    }
    if (name == "gable") {
        return Well_roof::gable;
    }
    return Well_roof::none;
}

class Well_form
{
public:
    explicit Well_form(const Kit_parameter_values& values)
        : roof    {get_well_roof(values.get_enum("roof"))}
        , segments{std::max(values.get_int("segments"), 6)}
        , radius  {values.get_float("radius")}
        , wall    {values.get_float("wall_height")}
    {
        thick  = std::clamp(0.24f * radius, 0.1f, 0.35f);
        lip    = std::min(0.04f, 0.2f * thick);
        post   = std::clamp(0.14f * radius, 0.07f, 0.14f);
        post_x = radius + lip + (0.6f * post);
        beam_y = wall + std::clamp(1.4f * radius, 0.9f, 2.2f);
    }

    // The highest point: the wall and its bucket, the crossbar, or the roof's ridge.
    [[nodiscard]] auto get_height() const -> float
    {
        if (roof == Well_roof::none) {
            return wall + get_bucket_height();
        }
        return (roof == Well_roof::crossbar) ? beam_y : (beam_y + get_roof_rise());
    }
    [[nodiscard]] auto get_bucket_radius() const -> float { return std::min(0.14f, 0.6f * thick); }
    [[nodiscard]] auto get_bucket_height() const -> float { return 2.2f * get_bucket_radius(); }
    [[nodiscard]] auto get_roof_rise    () const -> float { return 0.5f * (radius + (2.0f * post)); }

    Well_roof roof    {Well_roof::crossbar};
    int       segments{12};
    float     radius  {0.0f}; // of the wall's outer face
    float     wall    {0.0f}; // height of the wall
    float     thick   {0.0f}; // of the wall
    float     lip     {0.0f}; // how far the coping stands out
    float     post    {0.0f};
    float     post_x  {0.0f};
    float     beam_y  {0.0f}; // the top of the posts' beam
};

void generate_well_build(const Kit_parameter_values& values, GEO::Mesh& mesh, const Kit_build build)
{
    const Well_form form{values};
    const float r      = form.radius;
    const float coping = std::min(0.1f, 0.2f * form.wall);
    Kit_mesh_builder builder;
    {
        // The wall from its foot over the coping down the shaft to the water's level.
        const float inner = r - form.thick;
        const std::array<Lathe_ring, 7> rings{{
            {r, 0.0f}, {r, form.wall - coping}, {r + form.lip, form.wall - coping}, {r + form.lip, form.wall}, {inner - form.lip, form.wall},
            {inner, form.wall - coping}, {inner, 0.15f * form.wall}
        }};
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = form.segments, .slot = well_stone_slot});
    }
    {
        // The bucket stands on the coping toward +Z.
        const float b      = form.get_bucket_radius();
        const float high   = form.get_bucket_height();
        const float turn   = 0.35f;
        const Placement place{.origin = glm::vec3{(r - (0.5f * form.thick)) * std::sin(turn), form.wall, (r - (0.5f * form.thick)) * std::cos(turn)}};
        const std::array<Lathe_ring, 5> rings{{{0.78f * b, 0.0f}, {b, high}, {0.84f * b, high}, {0.7f * b, 0.5f * high}, {0.68f * b, 0.2f * high}}};
        add_lathe(builder, place, Lathe{.rings = rings, .segments = well_round, .slot = well_wood_slot});
    }
    if (form.roof != Well_roof::none) {
        const float p = form.post;
        builder.set_slot(well_wood_slot);
        for (const float side : {-1.0f, 1.0f}) {
            const float x = side * form.post_x;
            builder.add_box(glm::vec3{x - (0.5f * p), 0.0f, -0.5f * p}, glm::vec3{x + (0.5f * p), form.beam_y - (0.5f * p), 0.5f * p});
        }
        builder.add_box(glm::vec3{-(form.post_x + p), form.beam_y - p, -0.4f * p}, glm::vec3{form.post_x + p, form.beam_y, 0.4f * p});
        // The windlass between the posts, with a crank beyond the post at +X.
        const float axle_y = form.wall + (0.55f * (form.beam_y - form.wall));
        const float drum   = 0.6f * p;
        {
            const Placement turned{.origin = glm::vec3{0.0f, axle_y, 0.0f}, .x = -y_axis, .y = x_axis, .z = z_axis};
            const float reach = form.post_x + p;
            const std::array<Lathe_ring, 6> rings{{
                {0.4f * drum, -reach}, {0.4f * drum, -0.6f * r}, {drum, -0.6f * r}, {drum, 0.6f * r}, {0.4f * drum, 0.6f * r}, {0.4f * drum, reach}
            }};
            add_lathe(builder, turned, Lathe{.rings = rings, .segments = well_round, .phase = Lathe_phase::side, .slot = well_wood_slot});
            builder.set_slot(well_wood_slot);
            builder.add_box(glm::vec3{reach - (0.3f * p), axle_y - (0.25f * p), -0.25f * p}, glm::vec3{reach, axle_y + (2.2f * p), 0.25f * p});
            builder.add_box(glm::vec3{reach - (0.1f * p), axle_y + (1.8f * p), -0.2f * p}, glm::vec3{reach + (1.4f * p), axle_y + (2.15f * p), 0.2f * p});
        }
        if (form.roof == Well_roof::gable) {
            const float rise  = form.get_roof_rise();
            const float run   = 1.15f * (r + p);
            const float slab  = 0.35f * p;
            const float half  = form.post_x + (2.0f * p);
            const float ridge = form.beam_y + rise - slab;
            if (build == Kit_build::current) {
                // Two slopes sharing the ridge along X, one solid.
                const std::array<glm::vec2, 6> section{{
                    {run, ridge - rise}, {0.0f, ridge}, {-run, ridge - rise}, {-run, ridge - rise + slab}, {0.0f, ridge + slab}, {run, ridge - rise + slab}
                }};
                const std::array<std::vector<uint32_t>, 2> pieces{{{0, 1, 4, 5}, {1, 2, 3, 4}}};
                builder.add_x_prism(-half, half, section, pieces);
            } else {
                add_placed_box(builder, make_sloping_placement(glm::vec3{0.0f, ridge, 0.0f}, -rise / run), glm::vec3{-half, 0.0f, 0.0f}, glm::vec3{half, slab, run});
                add_placed_box(builder, make_sloping_placement(glm::vec3{0.0f, ridge, 0.0f},  rise / run), glm::vec3{-(half - 0.004f), -0.002f, -run}, glm::vec3{half - 0.004f, slab - 0.002f, 0.002f});
            }
            // A king post on the beam under the ridge at each end.
            for (const float side : {-1.0f, 1.0f}) {
                const float x = side * form.post_x;
                builder.add_box(glm::vec3{x - (0.35f * p), form.beam_y - (0.2f * p), -0.35f * p}, glm::vec3{x + (0.35f * p), ridge + (0.2f * slab), 0.35f * p});
            }
        }
    }
    builder.write(mesh);
}

void generate_well(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    generate_well_build(values, mesh, Kit_build::current);
}

void generate_well_old(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    generate_well_build(values, mesh, Kit_build::former);
}

//
// Signpost: a leaning post on a foot block with arrow boards at seeded headings and heights.
//

constexpr uint32_t sign_wood_slot = 0;
constexpr int      sign_arm_max   = 4;

void generate_signpost(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float height = values.get_float("height");
    const float length = values.get_float("arm_length");
    const int   arms   = std::clamp(values.get_int("arms"), 1, sign_arm_max);
    Kit_random random{values.get_seed()};
    const float     heading = random.next_range(0.0f, two_pi);
    const float     slope   = std::tan(glm::radians(values.get_float("lean")));
    const glm::vec2 lean{slope * std::cos(heading), -slope * std::sin(heading)};
    const float     half    = std::clamp(0.022f * height, 0.035f, 0.07f);
    const float     root_2  = std::numbers::sqrt2_v<float>;
    Kit_mesh_builder builder;
    builder.set_slot(sign_wood_slot);
    builder.add_box(glm::vec3{-2.2f * half, 0.0f, -2.2f * half}, glm::vec3{2.2f * half, 1.2f * half, 2.2f * half});
    const Placement post = make_leaning_placement(glm::vec3{0.0f}, lean);
    {
        // A square post under a point.
        const std::array<Lathe_ring, 2> rings{{{half * root_2, 0.4f * half}, {half * root_2, height - (2.0f * half)}}};
        add_lathe(builder, post, Lathe{.rings = rings, .segments = 4, .phase = Lathe_phase::side, .top = Lathe_cap::point, .top_y = height, .slot = sign_wood_slot});
    }
    const float board = std::clamp(0.28f * length, 0.1f, 0.26f); // a board's height
    const float thick = std::min(0.03f, 0.25f * board);
    const float first = height - (3.0f * half) - (0.6f * board);
    const float pitch = std::min(1.25f * board, (first - (0.35f * height)) / static_cast<float>(sign_arm_max));
    for (int i = 0; i < arms; ++i) {
        const float turn  = random.next_range(0.0f, two_pi);
        const float shift = 0.1f * next_signed(random);
        const float y     = first - (pitch * (static_cast<float>(i) + shift));
        // The board lies against the post's side, its point ahead.
        const glm::vec3 ahead = around_y(turn);
        const glm::vec3 aside = glm::cross(ahead, y_axis);
        // The square post's support distance along the board's side keeps the board off its corners.
        const float     reach = half * (std::abs(aside.x) + std::abs(aside.z));
        const glm::vec3 at    = post.at(glm::vec3{0.0f, y, 0.0f}) + ((reach + thick + 0.004f) * aside);
        const float     x0    = -0.22f * length;
        const float     x1    = 0.78f * length;
        const float     neck  = x1 - (0.8f * board);
        const std::array<glm::vec3, 5> outline{{
            at + (x0 * ahead) - ((0.5f * board) * y_axis), at + (neck * ahead) - ((0.5f * board) * y_axis), at + (x1 * ahead),
            at + (neck * ahead) + ((0.5f * board) * y_axis), at + (x0 * ahead) + ((0.5f * board) * y_axis)
        }};
        add_prism_between(builder, outline, -thick * aside);
    }
    builder.write(mesh);
}

} // anonymous namespace

void add_extra_09_16_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape;
        shape.id              = "harbour.quay_wall";
        shape.kit             = "harbour";
        shape.display_name    = "Quay wall";
        shape.description     =
            "Quay segment along X, walkable on top: a stone wall upright toward the land (-Z) and battered toward the "
            "water (+Z), where its foot stands out by the batter, on a plinth under a coping. A timber kerb runs along "
            "the water's edge and timber fenders lean on the water face. The origin is under the middle of the top.";
        shape.tags            = {"walkable", "quay", "harbour", "wall", "tiles", "size:2x1x3"};
        shape.parameters      = {
            derive_float(make_length_parameter("length",    "Size along X", 2.0f, Kit_default_scale::modules, 1.0f, 32.0f), 2.0f, 6.0f, 1.0f),
            derive_float(make_length_parameter("height",    "Height of the walking surface", 1.0f, Kit_default_scale::wall_heights, 0.5f, 16.0f), 0.5f, 1.25f, 0.25f),
            derive_float(make_length_parameter("thickness", "Size of the top along Z; the agent fixes the least", 1.0f, Kit_default_scale::modules, 1.0f, 8.0f), 1.0f, 2.0f, 0.5f),
            derive_float(make_angle_parameter ("batter",    "How far the water face leans from upright", 5.0f, 0.0f, 10.0f), 0.0f, 10.0f, 2.5f),
            derive_int  (make_count_parameter ("fenders",   "Timber fenders on the water face", 2, 0, quay_fender_max), 1, 3),
            make_seed_parameter()
        };
        shape.material_slots    = {"stone", "wood"};
        shape.normal_style      = Kit_normal_style::faceted;
        shape.triangle_budget   = 80;
        shape.collision         = Kit_collision::triangle_mesh;
        shape.origin_rule       = Kit_origin_rule::base_contains_origin;
        shape.generator         = generate_quay_wall;
        // An upright face without fenders is a box (U54).
        shape.get_collision     = [](const Kit_parameter_values& values) {
            return ((values.get_int("fenders") == 0) && (values.get_float("batter") == 0.0f)) ? Kit_collision::box : Kit_collision::triangle_mesh;
        };
        shape.mirror_planes     = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x};
        shape.check_walkability = check_quay_walkability;
        shape.get_connectors    = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const Quay_form form{values};
            add_side_connector(out_connectors, "start", glm::vec3{-form.half_l, form.height, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "end",   glm::vec3{ form.half_l, form.height, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
            add_base_and_top_connectors(out_connectors, form.height);
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "industrial.pipe";
        shape.kit             = "industrial";
        shape.display_name    = "Pipe";
        shape.description     =
            "Pipe piece lying on the ground from its start at -X, with open ends: straight along X, an elbow that "
            "turns at the origin toward -Z by the bend angle, or a tee with a branch from its middle toward -Z. "
            "With flanges a flange stands at every end and the pipe rests on them. Pieces chain end to start.";
        shape.tags            = {"obstacle", "decoration", "industrial", "pipe", "size:2x0.4"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("form",       "Straight, an elbow or a tee", {"straight", "elbow", "tee"}, 0)),
            derive_float(make_length_parameter("diameter",   "Outer diameter of the pipe", 0.4f, Kit_default_scale::meters, 0.1f, 0.5f), 0.3f, 0.5f, 0.1f),
            derive_float(make_length_parameter("length",     "From start to end along the pipe's legs; a tee's branch is half of it", 2.0f, Kit_default_scale::modules, 1.0f, 16.0f), 2.0f, 4.0f, 1.0f),
            derive_float(make_angle_parameter ("bend_angle", "How far an elbow turns", 90.0f, 15.0f, 90.0f), 45.0f, 90.0f, 45.0f),
            derive_enum (make_enum_parameter  ("flanges",    "A flange at every end", {"none", "ends"}, 1)),
            make_seed_parameter (),
            make_count_parameter("segments", "Points around the pipe", 10, 6, 16)
        };
        shape.material_slots  = {"metal"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 210;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.origin_rule     = Kit_origin_rule::base_contains_origin;
        shape.generator       = generate_pipe;
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const Pipe_form form{values};
            add_side_connector(out_connectors, "start", form.get_start(), glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "end",   form.get_end(),   form.out);
            // Where a branch leaves: a tee's branch end, a straight pipe's side, the outside of an elbow's bend.
            if (form.kind == Pipe_kind::tee) {
                add_side_connector(out_connectors, "branch", glm::vec3{0.0f, form.axis_y, -form.half_l}, glm::vec3{0.0f, 0.0f, -1.0f});
            } else if (form.kind == Pipe_kind::elbow) {
                const glm::vec3 outward = glm::normalize(x_axis - form.out);
                add_side_connector(out_connectors, "branch", form.get_bend_point(0.5f * form.angle) + (form.radius * outward), outward);
            } else {
                add_side_connector(out_connectors, "branch", glm::vec3{0.0f, form.axis_y, -form.radius}, glm::vec3{0.0f, 0.0f, -1.0f});
            }
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "prop.pot";
        shape.kit             = "prop";
        shape.display_name    = "Pot";
        shape.description     =
            "Vessel at its origin, hollow at its mouth. Jar: a belly under a short neck. Amphora: a belly on a small "
            "foot under a long neck with two handles toward +X and -X. Urn: a belly on a pedestal foot under a rim. "
            "Bowl: wide and open. Planter: a tapered tub with a rim band, filled to under the rim. The neck is the "
            "mouth's width in widths for a jar, an amphora and an urn.";
        shape.tags            = {"obstacle", "decoration", "prop", "pot", "size:0.5x0.6"};
        shape.parameters      = {
            derive_enum(make_enum_parameter("form", "Jar, amphora, urn, bowl or planter", {"jar", "amphora", "urn", "bowl", "planter"}, 0)),
            from_seed_by_default(derive_float(make_length_parameter("height", "Size along Y", 0.6f, Kit_default_scale::meters, 0.15f, 3.0f), 0.3f, 1.2f, 0.1f)),
            from_seed_by_default(derive_float(make_length_parameter("width",  "The widest diameter", 0.5f, Kit_default_scale::meters, 0.15f, 3.0f), 0.3f, 0.8f, 0.1f)),
            derive_float(make_share_parameter("neck", "The mouth's width in widths, of a jar, an amphora and an urn", 0.6f, 0.3f, 0.9f), 0.3f, 0.9f, 0.1f),
            make_seed_parameter (),
            make_count_parameter("segments", "Points around the vessel", 12, 8, 20)
        };
        shape.material_slots  = {"stone"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 250;
        shape.collision       = Kit_collision::convex_hull;
        shape.generator       = generate_pot;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::z};
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            add_base_and_top_connectors(out_connectors, values.get_float("height"));
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "prop.table";
        shape.kit             = "prop";
        shape.display_name    = "Table";
        shape.description     =
            "Wooden furniture at its origin, within its width, depth and height. Table: a top on legs. Bench: a seat "
            "of two planks at 0.55 of the height before a back of two uprights and two slats at -Z. Stool: a seat on "
            "splayed legs with rungs, or on two boards. Shelf: four shelves between four posts, or between two side "
            "boards before a back board. Legs: four posts under aprons, or two trestles joined by a stretcher.";
        shape.tags            = {"obstacle", "decoration", "prop", "furniture", "table", "size:1.4x0.8x0.75"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("form",   "Table, bench, stool or shelf", {"table", "bench", "stool", "shelf"}, 0)),
            derive_float(make_length_parameter("width",  "Size along X", 1.4f,  Kit_default_scale::meters, 0.3f, 4.0f), 1.0f, 1.8f, 0.2f),
            derive_float(make_length_parameter("depth",  "Size along Z", 0.8f,  Kit_default_scale::meters, 0.3f, 2.0f), 0.6f, 0.9f, 0.1f),
            derive_float(make_length_parameter("height", "Size along Y", 0.75f, Kit_default_scale::meters, 0.3f, 3.0f), 0.7f, 0.8f, 0.05f),
            derive_enum (make_enum_parameter  ("legs",   "Posts, or trestles (boards under a stool and beside a shelf)", {"posts", "trestles"}, 0)),
            make_seed_parameter()
        };
        shape.material_slots  = {"wood"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 110;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.origin_rule     = Kit_origin_rule::base_contains_origin; // a bench's back stands behind its legs
        shape.generator       = generate_table;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x};
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            add_base_and_top_connectors(out_connectors, values.get_float("height"));
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "prop.market_stall";
        shape.kit             = "prop";
        shape.display_name    = "Market stall";
        shape.description     =
            "Market stall at its origin, its counter toward +Z: four posts joined by rails, a counter top on boards, "
            "and an awning of cloth with a valance at its front: level, falling toward the front, or falling both ways "
            "from a ridge along X. The awning reaches the overhang beyond the posts at the front and the back; stalls "
            "join side to side by start and end.";
        shape.tags            = {"obstacle", "decoration", "prop", "market", "stall", "size:2x1x2.4"};
        shape.parameters      = {
            derive_float(make_length_parameter("width",  "Size along X", 2.0f, Kit_default_scale::modules,      1.2f, 8.0f), 2.0f, 3.0f, 0.5f),
            derive_float(make_length_parameter("depth",  "Size along Z between the posts", 1.0f, Kit_default_scale::modules, 0.6f, 4.0f), 1.0f, 1.5f, 0.25f),
            derive_float(make_length_parameter("height", "Size along Y", 0.8f, Kit_default_scale::wall_heights, 1.8f, 6.0f), 0.75f, 0.9f, 0.05f),
            derive_enum (make_enum_parameter  ("awning", "Level, falling toward the front, or from a ridge", {"flat", "sloped", "peaked"}, 1)),
            from_seed_by_default(derive_float(make_length_parameter("overhang", "How far the awning reaches beyond the posts at the front and the back", 0.3f, Kit_default_scale::meters, 0.0f, 0.8f), 0.2f, 0.5f, 0.1f)),
            make_seed_parameter()
        };
        shape.material_slots  = {"wood", "cloth"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 150;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.generator       = generate_market_stall;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x};
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float half_w = 0.5f * values.get_float("width");
            out_connectors.push_back(make_base_connector());
            add_side_connector(out_connectors, "start", glm::vec3{-half_w, 0.0f, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "end",   glm::vec3{ half_w, 0.0f, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "prop.cart";
        shape.kit             = "prop";
        shape.display_name    = "Cart";
        shape.description     =
            "Two-wheel hand cart along Z, its axle over the origin: a bed on two shafts that reach forward (+Z) as "
            "handles, tilted toward them and standing on two legs under its front. The wheels are discs of wood "
            "between an iron hub and an iron tyre (slot metal) on an iron axle. The bed is open, has two posts and a "
            "rail on each side, or boards around it.";
        shape.tags            = {"obstacle", "decoration", "prop", "cart", "size:1x1.5"};
        shape.parameters      = {
            derive_float(make_length_parameter("length",       "From the bed's back to the handles' ends", 1.5f, Kit_default_scale::modules, 0.8f, 4.0f), 1.5f, 2.0f, 0.25f),
            derive_float(make_length_parameter("width",        "Size along X over the wheels", 1.0f, Kit_default_scale::modules, 0.5f, 2.5f), 1.0f, 1.25f, 0.25f),
            derive_float(make_length_parameter("wheel_radius", "Radius of the wheels", 0.4f, Kit_default_scale::meters, 0.2f, 0.8f), 0.3f, 0.5f, 0.05f),
            from_seed_by_default(derive_float(make_angle_parameter("tilt", "How far the bed tilts toward the handles; at most what keeps their ends over the ground", 8.0f, 0.0f, 15.0f), 0.0f, 15.0f, 1.0f)),
            derive_enum (make_enum_parameter  ("sides", "An open bed, rails or boards", {"open", "rails", "boards"}, 1)),
            make_seed_parameter (),
            make_count_parameter("segments", "Points around a wheel", 12, 8, 16)
        };
        shape.material_slots  = {"wood", "metal"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 540;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.origin_rule     = Kit_origin_rule::base_contains_origin;
        shape.generator       = generate_cart;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x};
        shape.get_connectors  = [](const Kit_parameter_values&, std::vector<Kit_connector>& out_connectors) {
            out_connectors.push_back(make_base_connector());
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "prop.well";
        shape.kit             = "prop";
        shape.display_name    = "Well";
        shape.description     =
            "Well at its origin: a ring wall of stone under a coping around a shaft, with a wooden bucket on its rim "
            "toward +Z. With a crossbar two posts at +X and -X carry a beam over a windlass with a crank; with a gable "
            "a roof of two slabs stands on them, its ridge along X.";
        shape.tags            = {"obstacle", "decoration", "prop", "well", "size:1.5x2"};
        shape.parameters      = {
            derive_float(make_length_parameter("radius",      "Radius of the wall's outer face", 0.75f, Kit_default_scale::modules, 0.4f, 3.0f), 0.6f, 1.0f, 0.05f),
            derive_float(make_length_parameter("wall_height", "Height of the ring wall", 0.9f, Kit_default_scale::meters, 0.4f, 1.5f), 0.7f, 1.0f, 0.1f),
            derive_enum (make_enum_parameter  ("roof",        "The wall alone, a crossbar or a gable roof", {"none", "crossbar", "gable"}, 1)),
            make_seed_parameter (),
            make_count_parameter("segments", "Points around the wall", 12, 8, 20)
        };
        shape.material_slots  = {"stone", "wood"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 400;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.origin_rule     = Kit_origin_rule::base_contains_origin;
        shape.generator       = generate_well;
        shape.get_connectors  = [](const Kit_parameter_values&, std::vector<Kit_connector>& out_connectors) {
            out_connectors.push_back(make_base_connector());
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "prop.signpost";
        shape.kit             = "prop";
        shape.display_name    = "Signpost";
        shape.description     =
            "Signpost at its origin: a square post under a point, on a foot block, leaning in a seeded direction, "
            "with one to four arrow boards against its sides at seeded headings, one under the other.";
        shape.tags            = {"decoration", "obstacle", "prop", "sign", "size:0.6x2.4"};
        shape.parameters      = {
            derive_float(make_length_parameter("height", "Size along Y", 0.8f, Kit_default_scale::wall_heights, 1.0f, 6.0f), 0.7f, 0.9f, 0.05f),
            from_seed_by_default(derive_int(make_count_parameter("arms", "Arrow boards", 2, 1, sign_arm_max), 1, 4)),
            derive_float(make_length_parameter("arm_length", "Length of a board", 0.6f, Kit_default_scale::meters, 0.3f, 1.5f), 0.5f, 0.8f, 0.1f),
            from_seed_by_default(derive_float(make_angle_parameter("lean", "How far the post leans from upright", 2.0f, 0.0f, 6.0f), 0.0f, 6.0f, 1.0f)),
            make_seed_parameter()
        };
        shape.material_slots  = {"wood"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 100;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_signpost;
        shape.get_connectors  = [](const Kit_parameter_values&, std::vector<Kit_connector>& out_connectors) {
            out_connectors.push_back(make_base_connector());
        };
        shapes.push_back(std::move(shape));
    }
    add_former_build(shapes, "harbour.quay_wall", generate_quay_wall_old);
    add_former_build(shapes, "prop.market_stall", generate_market_stall_old);
    add_former_build(shapes, "prop.well",         generate_well_old);
}

} // namespace editor
