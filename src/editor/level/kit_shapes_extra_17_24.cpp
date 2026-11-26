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

// The third eight shapes of the build order of doc/plans/level_kit_extra_shapes.md (doc/editor/level_tools.md).
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

// A ring of `segments` points about the placement's y from y0 up to a cut through y1 on the axis that rises by
// `cut` for each meter along the placement's x and z, between two polygons.
void add_cut_prism(
    Kit_mesh_builder& builder,
    const Placement&  place,
    const int         segments,
    const Lathe_phase phase,
    const float       radius,
    const float       y0,
    const float       y1,
    const glm::vec2   cut
)
{
    const uint32_t n     = static_cast<uint32_t>(segments);
    const uint32_t first = static_cast<uint32_t>(builder.get_positions().size());
    for (uint32_t i = 0; i < n; ++i) {
        const glm::vec2 point = radius * get_ring_point(static_cast<int>(i), segments, phase);
        builder.add_vertex(place.at(glm::vec3{point.x, y0, point.y}));
    }
    for (uint32_t i = 0; i < n; ++i) {
        const glm::vec2 point = radius * get_ring_point(static_cast<int>(i), segments, phase);
        builder.add_vertex(place.at(glm::vec3{point.x, y1 + glm::dot(cut, point), point.y}));
    }
    std::vector<uint32_t> bottom;
    std::vector<uint32_t> top;
    for (uint32_t i = 0; i < n; ++i) {
        bottom.push_back(first + (n - 1 - i));
        top   .push_back(first + n + i);
        const uint32_t j = (i + 1) % n;
        builder.add_quad(first + i, first + j, first + n + j, first + n + i);
    }
    builder.add_polygon(bottom);
    builder.add_polygon(top);
}

//
// Broken pillar: the pillar's stump under a slanting break, and fallen drums lying about it.
//

constexpr uint32_t broken_pillar_slot = 0;
constexpr int      broken_pillar_drums = 3;

void generate_broken_pillar(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const bool  round    = (values.get_enum("profile") == "round");
    const float width    = values.get_float("width");
    const float standing = values.get_float("standing") * values.get_float("height");
    const int   fallen   = std::clamp(values.get_int("fallen"), 0, broken_pillar_drums);
    const int   segments = round ? std::max(values.get_int("segments"), 3) : 4;
    // A square pillar's ring is its four corners.
    const float radius   = round ? (0.5f * width) : (0.5f * width * std::numbers::sqrt2_v<float>);
    Kit_random random{values.get_seed()};
    Kit_mesh_builder builder;
    builder.set_slot(broken_pillar_slot);
    {
        const float steep = random.next_range(0.15f, 0.45f);
        const float turn  = random.next_range(0.0f, two_pi);
        const float slope = std::min(steep, 0.6f * standing / radius);
        add_cut_prism(builder, Placement{}, segments, Lathe_phase::side, radius, 0.0f, standing, slope * glm::vec2{std::cos(turn), std::sin(turn)});
    }
    // Each drum lies on a side in its own sector about the stump, one end broken on a slant.
    const float axis_y = radius * get_ring_point(0, segments, Lathe_phase::side).x;
    const float start  = random.next_range(0.0f, two_pi);
    for (int k = 0; k < fallen; ++k) {
        const float     sway    = next_signed(random);
        const float     away    = random.next_range(1.3f, 2.0f);
        const float     longer  = random.next_range(0.7f, 1.3f);
        const float     heading = random.next_range(0.0f, two_pi);
        const float     steep   = random.next_range(0.1f, 0.35f);
        const float     turn    = random.next_range(0.0f, two_pi);
        const float     length  = longer * width;
        const float     angle   = start + (two_pi * (static_cast<float>(k) + (0.25f * sway)) / static_cast<float>(fallen));
        const glm::vec3 along   = around_y(heading);
        const glm::vec3 center  = ((away * width) + (0.5f * length)) * around_y(angle);
        const Placement place{.origin = center - ((0.5f * length) * along) + (axis_y * y_axis), .x = -y_axis, .y = along, .z = glm::cross(-y_axis, along)};
        const float     slope   = std::min(steep, 0.4f * length / radius);
        add_cut_prism(builder, place, segments, Lathe_phase::side, radius, 0.0f, length, slope * glm::vec2{std::cos(turn), std::sin(turn)});
    }
    builder.write(mesh);
}

//
// Broken arch: the arch's ring from each foot up to a break; the ring between the two breaks is gone.
//

constexpr uint32_t broken_arch_slot = 0;

// One arm of the ring through the angles (0 at the right foot, pi at the left), which rise; it is cut across the
// ring at its broken end.
void add_arch_arm(
    Kit_mesh_builder&            builder,
    const float                  half_span,
    const float                  rise,
    const float                  thickness,
    const float                  half_depth,
    const std::span<const float> angles,
    const bool                   broken_at_start
)
{
    const std::size_t count = angles.size();
    std::vector<uint32_t> inner_front(count);
    std::vector<uint32_t> inner_back (count);
    std::vector<uint32_t> outer_front(count);
    std::vector<uint32_t> outer_back (count);
    for (std::size_t i = 0; i < count; ++i) {
        const bool  is_foot = broken_at_start ? ((i + 1) == count) : (i == 0);
        const float angle   = angles[i];
        // A foot stands on the ground exactly.
        const float side = broken_at_start ? -1.0f : 1.0f;
        const glm::vec2 inner = is_foot ? glm::vec2{side * half_span, 0.0f} : glm::vec2{half_span * std::cos(angle), rise * std::sin(angle)};
        const glm::vec2 outer = is_foot
            ? glm::vec2{side * (half_span + thickness), 0.0f}
            : glm::vec2{(half_span + thickness) * std::cos(angle), (rise + thickness) * std::sin(angle)};
        inner_front[i] = builder.add_vertex(glm::vec3{inner.x, inner.y,  half_depth});
        inner_back [i] = builder.add_vertex(glm::vec3{inner.x, inner.y, -half_depth});
        outer_front[i] = builder.add_vertex(glm::vec3{outer.x, outer.y,  half_depth});
        outer_back [i] = builder.add_vertex(glm::vec3{outer.x, outer.y, -half_depth});
    }
    for (std::size_t i = 0; (i + 1) < count; ++i) {
        const std::size_t j = i + 1;
        builder.add_quad(inner_front[i], outer_front[i], outer_front[j], inner_front[j]);
        builder.add_quad(inner_back[i],  inner_back[j],  outer_back[j],  outer_back[i]);
        builder.add_quad(inner_front[i], inner_front[j], inner_back[j],  inner_back[i]);
        builder.add_quad(outer_front[i], outer_back[i],  outer_back[j],  outer_front[j]);
    }
    const std::size_t last = count - 1;
    builder.add_quad(inner_front[0],    inner_back[0],    outer_back[0],    outer_front[0]);
    builder.add_quad(outer_front[last], outer_back[last], inner_back[last], inner_front[last]);
}

// The angles the two arms end at: the gap's share of the ring about its center, each arm at least 0.08 of the ring.
[[nodiscard]] auto get_arch_breaks(const Kit_parameter_values& values) -> glm::vec2
{
    const float gap    = values.get_float("gap");
    const float center = values.get_float("gap_center");
    const float low    = std::clamp(center - (0.5f * gap), 0.08f, 0.84f);
    const float high   = std::clamp(center + (0.5f * gap), low + 0.08f, 0.92f);
    return glm::vec2{pi * low, pi * high};
}

void generate_broken_arch(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float     half_span  = 0.5f * values.get_float("span");
    const float     rise       = values.get_float("rise");
    const float     thickness  = values.get_float("thickness");
    const float     half_depth = 0.5f * values.get_float("depth");
    const int       segments   = std::max(values.get_int("segments"), 2);
    // Each arm has a segment more than half the arch's: an arm may be longer than half the ring.
    const int       bands      = (segments / 2) + 1;
    const glm::vec2 breaks     = get_arch_breaks(values);
    Kit_mesh_builder builder;
    builder.set_slot(broken_arch_slot);
    // The angles of an arm from its foot to its break at `reach`: even, or even up to the crown and one segment
    // beyond it, so that no segment lies across the crown.
    const auto get_angle = [bands](const float reach, const int i) -> float {
        if (reach <= (0.5f * pi)) {
            return reach * static_cast<float>(i) / static_cast<float>(bands);
        }
        return (i == bands) ? reach : ((0.5f * pi) * static_cast<float>(i) / static_cast<float>(bands - 1));
    };
    std::vector<float> right;
    std::vector<float> left;
    for (int i = 0; i <= bands; ++i) {
        right.push_back(get_angle(breaks.x, i));
        left .push_back(pi - get_angle(pi - breaks.y, bands - i));
    }
    add_arch_arm(builder, half_span, rise, thickness, half_depth, right, false);
    add_arch_arm(builder, half_span, rise, thickness, half_depth, left, true);
    builder.write(mesh);
}

//
// Lantern: a cage of bars around a glowing core between a plate and a roof; standing, hanging on a chain, or
// hanging from a bracket on a wall behind it (-Z).
//

constexpr uint32_t lantern_metal_slot = 0;
constexpr uint32_t lantern_glow_slot  = 1;
constexpr int      lantern_side_min   = 4;
constexpr int      lantern_side_max   = 8;
constexpr int      lantern_links      = 5;
constexpr float    lantern_body       = 1.7f;  // the body's height in sizes
constexpr float    lantern_reach      = 1.3f;  // from the wall to the lantern's axis in sizes
constexpr float    lantern_light      = 0.93f; // the light's height in sizes: over the core, under the eaves

enum class Lantern_mount : unsigned int { standing = 0, hanging, bracket };

[[nodiscard]] auto get_lantern_mount(const std::string_view name) -> Lantern_mount
{
    if (name == "hanging") {
        return Lantern_mount::hanging;
    }
    if (name == "bracket") {
        return Lantern_mount::bracket;
    }
    return Lantern_mount::standing;
}

void generate_lantern(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Lantern_mount mount = get_lantern_mount(values.get_enum("mount"));
    const float         s     = values.get_float("size");
    const float         chain = values.get_float("chain_length");
    const int           sides = std::clamp(values.get_int("sides"), lantern_side_min, lantern_side_max);
    const Placement     place{};
    Kit_mesh_builder builder;
    {
        const std::array<Lathe_ring, 3> rings{{{0.42f * s, 0.0f}, {0.5f * s, 0.06f * s}, {0.5f * s, 0.12f * s}}};
        add_lathe(builder, place, Lathe{.rings = rings, .segments = sides, .slot = lantern_metal_slot});
    }
    {
        const std::array<Lathe_ring, 2> rings{{{0.3f * s, 0.1f * s}, {0.4f * s, 0.85f * s}}};
        add_lathe(builder, place, Lathe{.rings = rings, .segments = sides, .slot = lantern_glow_slot});
    }
    builder.set_slot(lantern_metal_slot);
    for (int i = 0; i < sides; ++i) {
        const glm::vec2 point = get_ring_point(i, sides, Lathe_phase::point);
        builder.add_beam(
            glm::vec3{0.45f * s * point.x, 0.1f * s,  0.45f * s * point.y},
            glm::vec3{0.53f * s * point.x, 1.04f * s, 0.53f * s * point.y},
            0.05f * s, 0.05f * s
        );
    }
    {
        const std::array<Lathe_ring, 2> rings{{{0.6f * s, 1.0f * s}, {0.62f * s, 1.08f * s}}};
        add_lathe(builder, place, Lathe{.rings = rings, .segments = sides, .slot = lantern_metal_slot});
    }
    {
        const std::array<Lathe_ring, 3> rings{{{0.57f * s, 1.06f * s}, {0.2f * s, 1.38f * s}, {0.11f * s, 1.47f * s}}};
        add_lathe(builder, place, Lathe{.rings = rings, .segments = sides, .top = Lathe_cap::point, .top_y = 1.56f * s, .slot = lantern_metal_slot});
    }
    const float body = lantern_body * s;
    builder.set_slot(lantern_metal_slot);
    builder.add_box(glm::vec3{-0.05f * s, 1.5f * s, -0.018f * s}, glm::vec3{0.05f * s, body, 0.018f * s});
    if (mount == Lantern_mount::hanging) {
        // A chain of links, each turned a quarter against the one below.
        const float link = chain / static_cast<float>(lantern_links);
        for (int k = 0; k < lantern_links; ++k) {
            const float y0   = body + (link * (static_cast<float>(k) - 0.3f));
            const float y1   = body + (link * static_cast<float>(k + 1));
            const float wide = ((k % 2) == 0) ? (0.014f * s) : (0.04f * s);
            const float thin = ((k % 2) == 0) ? (0.04f * s) : (0.014f * s);
            builder.add_box(glm::vec3{-wide, y0, -thin}, glm::vec3{wide, y1, thin});
        }
    } else if (mount == Lantern_mount::bracket) {
        // A link up to an arm from a plate on the wall, over a strut.
        const float reach = lantern_reach * s;
        const float arm   = body + (0.25f * s);
        builder.add_box(glm::vec3{-0.014f * s, body - (0.04f * s), -0.04f * s}, glm::vec3{0.014f * s, arm + (0.02f * s), 0.04f * s});
        builder.add_box(glm::vec3{-0.04f * s, arm, -reach + (0.02f * s)}, glm::vec3{0.04f * s, arm + (0.08f * s), 0.12f * s});
        builder.add_box(glm::vec3{-0.13f * s, arm - (0.7f * s), -reach}, glm::vec3{0.13f * s, arm + (0.2f * s), -reach + (0.05f * s)});
        builder.add_beam(
            glm::vec3{0.0f, arm - (0.6f * s), -reach + (0.03f * s)},
            glm::vec3{0.0f, arm + (0.03f * s), -0.35f * reach},
            0.05f * s, 0.05f * s
        );
    }
    builder.write(mesh);
}

// The lantern's top, where a chain or a bracket's link takes it.
[[nodiscard]] auto get_lantern_top(const Kit_parameter_values& values) -> float
{
    const float body = lantern_body * values.get_float("size");
    return (values.get_enum("mount") == "hanging") ? (body + values.get_float("chain_length")) : body;
}

//
// Fire: a brazier on three legs, a torch on a wall behind it (-Z), or a campfire in a ring of stones under a
// tripod; each with a seeded flame over glowing embers.
//

constexpr uint32_t fire_metal_slot = 0;
constexpr uint32_t fire_glow_slot  = 1;
constexpr uint32_t fire_stone_slot = 2;
constexpr int      fire_flame      = 6; // points around the flame
constexpr int      fire_round      = 8; // points around the bowl, the plinth and the embers
constexpr int      fire_stones     = 7;

enum class Fire_form : unsigned int { brazier = 0, torch, campfire };

[[nodiscard]] auto get_fire_form(const std::string_view name) -> Fire_form
{
    if (name == "torch") {
        return Fire_form::torch;
    }
    if (name == "campfire") {
        return Fire_form::campfire;
    }
    return Fire_form::brazier;
}

// Where the flame's foot stands and how wide the flame is, in sizes.
[[nodiscard]] auto get_fire_root(const Fire_form form, const float size) -> glm::vec4
{
    switch (form) {
        case Fire_form::brazier:  return glm::vec4{0.0f, 0.95f * size, 0.0f, 0.24f * size};
        case Fire_form::torch:    return glm::vec4{0.0f, 0.97f * size, 0.32f * size, 0.11f * size};
        case Fire_form::campfire: return glm::vec4{0.0f, 0.1f * size, 0.0f, 0.3f * size};
    }
    return glm::vec4{0.0f};
}

// The light: over the flame's tip.
[[nodiscard]] auto get_fire_light(const Kit_parameter_values& values) -> glm::vec3
{
    const glm::vec4 root = get_fire_root(get_fire_form(values.get_enum("form")), values.get_float("size"));
    return glm::vec3{root.x, root.y + values.get_float("flame") + 0.06f, root.z};
}

void generate_fire_build(const Kit_parameter_values& values, GEO::Mesh& mesh, const Kit_build build)
{
    const Fire_form form  = get_fire_form(values.get_enum("form"));
    const float     s     = values.get_float("size");
    const float     flame = values.get_float("flame");
    Kit_random random{values.get_seed()};
    Kit_mesh_builder builder;
    if (form == Fire_form::brazier) {
        // A bowl on three legs on a plinth of stone.
        {
            const std::array<Lathe_ring, 3> rings{{{0.56f * s, 0.0f}, {0.56f * s, 0.07f * s}, {0.5f * s, 0.1f * s}}};
            add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = fire_round, .slot = fire_stone_slot});
        }
        builder.set_slot(fire_metal_slot);
        for (int k = 0; k < 3; ++k) {
            const glm::vec2 point = get_kit_circle_point(k, 3);
            const glm::vec3 out{point.x, 0.0f, point.y};
            const Placement leg{.origin = ((0.42f * s) * out) + ((0.08f * s) * y_axis), .y = glm::vec3{-0.3f * out.x, 1.0f, -0.3f * out.z}};
            add_placed_box(builder, leg, glm::vec3{-0.03f * s, 0.0f, -0.03f * s}, glm::vec3{0.03f * s, 0.72f * s, 0.03f * s});
        }
        {
            const std::array<Lathe_ring, 5> rings{{{0.16f * s, 0.7f * s}, {0.45f * s, 0.85f * s}, {0.5f * s, 1.0f * s}, {0.44f * s, 1.0f * s}, {0.38f * s, 0.9f * s}}};
            add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = fire_round, .slot = fire_metal_slot});
        }
        {
            const std::array<Lathe_ring, 2> rings{{{0.36f * s, 0.88f * s}, {0.28f * s, 0.97f * s}}};
            add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = fire_round, .slot = fire_glow_slot});
        }
    } else if (form == Fire_form::torch) {
        // A shaft leaning out of a ring on an arm from a stone on the wall, under a cup.
        builder.set_slot(fire_stone_slot);
        builder.add_box(glm::vec3{-0.15f * s, 0.22f * s, -0.12f * s}, glm::vec3{0.15f * s, 0.7f * s, -0.05f * s});
        builder.set_slot(fire_metal_slot);
        const Placement shaft{.y = glm::vec3{0.0f, 1.0f, 0.34f}};
        add_placed_box(builder, shaft, glm::vec3{-0.035f * s, 0.0f, -0.035f * s}, glm::vec3{0.035f * s, 0.9f * s, 0.035f * s});
        builder.add_box(glm::vec3{-0.03f * s, 0.42f * s, -0.07f * s}, glm::vec3{0.03f * s, 0.48f * s, 0.19f * s});
        {
            const Placement cup{.origin = glm::vec3{0.0f, 0.0f, 0.3f * s}};
            const std::array<Lathe_ring, 2> rings{{{0.06f * s, 0.84f * s}, {0.13f * s, 0.98f * s}}};
            add_lathe(builder, cup, Lathe{.rings = rings, .segments = fire_flame, .slot = fire_metal_slot});
        }
    } else {
        // Stones around embers under a tripod of three rods.
        builder.set_slot(fire_stone_slot);
        for (int k = 0; k < fire_stones; ++k) {
            const float turn   = 0.3f * next_signed(random);
            const float longer = random.next_range(0.14f, 0.2f);
            const float higher = random.next_range(0.12f, 0.2f);
            const float deeper = random.next_range(0.1f, 0.14f);
            const float angle  = two_pi * (static_cast<float>(k) + (0.5f * turn)) / static_cast<float>(fire_stones);
            // A stone lies across its radius.
            const glm::vec3 out   = around_y(angle);
            const Placement stone = make_turned_placement((0.75f * s) * out, glm::cross(y_axis, out));
            add_placed_box(builder, stone, glm::vec3{-longer * s, 0.0f, -deeper * s}, glm::vec3{longer * s, higher * s, deeper * s});
        }
        {
            const std::array<Lathe_ring, 3> rings{{{0.52f * s, 0.0f}, {0.4f * s, 0.08f * s}, {0.16f * s, 0.14f * s}}};
            add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = fire_round, .slot = fire_glow_slot});
        }
        builder.set_slot(fire_metal_slot);
        for (int k = 0; k < 3; ++k) {
            const glm::vec2 point = get_kit_circle_point(k, 3);
            const glm::vec3 out{point.x, 0.0f, point.y};
            const float     high = (1.5f * s) + flame;
            // The rods cross on the axis under their heads (the former build's heads stood a little apart).
            const float     lean = (build == Kit_build::current) ? ((1.05f * s) / (0.85f * high)) : ((1.0f * s) / high);
            const Placement rod{.origin = (1.05f * s) * out, .y = glm::vec3{-lean * out.x, 1.0f, -lean * out.z}};
            add_placed_box(builder, rod, glm::vec3{-0.02f * s, 0.0f, -0.02f * s}, glm::vec3{0.02f * s, high, 0.02f * s});
        }
    }
    // The flame: rings that wander with the seed, up to a point.
    {
        const glm::vec4 root = get_fire_root(form, s);
        std::array<float, fire_flame> scales{};
        for (float& scale : scales) {
            scale = random.next_range(0.75f, 1.2f);
        }
        const float lean_x = 0.5f * next_signed(random);
        const float lean_z = 0.5f * next_signed(random);
        // The tip stands off the foot by up to half the flame's width.
        const Placement place = make_leaning_placement(glm::vec3{root.x, root.y, root.z}, (root.w / flame) * glm::vec2{lean_x, lean_z});
        const std::array<Lathe_ring, 4> rings{{{0.55f * root.w, 0.0f}, {root.w, 0.22f * flame}, {0.78f * root.w, 0.5f * flame}, {0.4f * root.w, 0.78f * flame}}};
        add_lathe(builder, place, Lathe{
            .rings = rings, .segments = fire_flame, .top = Lathe_cap::point, .top_y = flame, .slot = fire_glow_slot, .scales = scales
        });
    }
    builder.write(mesh);
}

void generate_fire(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    generate_fire_build(values, mesh, Kit_build::current);
}

void generate_fire_old(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    generate_fire_build(values, mesh, Kit_build::former);
}

//
// Buoy: a float of bands under a mast with a lamp, leaning in a seeded direction.
//

constexpr uint32_t buoy_metal_slot = 0;
constexpr uint32_t buoy_trim_slot  = 1;
constexpr uint32_t buoy_glow_slot  = 2;
constexpr int      buoy_rings      = 12; // bands up the float
constexpr int      buoy_band_max   = 3;

enum class Buoy_form : unsigned int { can = 0, cone, sphere, spar };

[[nodiscard]] auto get_buoy_form(const std::string_view name) -> Buoy_form
{
    if (name == "cone") {
        return Buoy_form::cone;
    }
    if (name == "sphere") {
        return Buoy_form::sphere;
    }
    if (name == "spar") {
        return Buoy_form::spar;
    }
    return Buoy_form::can;
}

// The float's height in sizes.
[[nodiscard]] auto get_buoy_height(const Buoy_form form) -> float
{
    switch (form) {
        case Buoy_form::can:    return 1.0f;
        case Buoy_form::cone:   return 1.25f;
        case Buoy_form::sphere: return 1.0f;
        case Buoy_form::spar:   return 2.6f;
    }
    return 1.0f;
}

// The float's radius in sizes at v, 0 at its foot to 1 at its top.
[[nodiscard]] auto get_buoy_radius(const Buoy_form form, const float v) -> float
{
    const float foot = 0.7f + (0.3f * std::min(v / 0.08f, 1.0f));
    switch (form) {
        case Buoy_form::can:    return 0.35f * foot * (1.0f - (0.25f * std::max((v - 0.92f) / 0.08f, 0.0f)));
        case Buoy_form::cone:   return 0.35f * foot * (1.0f - (0.8f * std::max((v - 0.5f) / 0.5f, 0.0f)));
        case Buoy_form::sphere: return 0.5f * std::max(std::sqrt(std::max(1.0f - (((2.0f * v) - 1.0f) * ((2.0f * v) - 1.0f)), 0.0f)), 0.3f);
        case Buoy_form::spar:   return ((v > 0.2f) && (v < 0.45f)) ? 0.26f : 0.12f;
    }
    return 0.35f;
}

class Buoy_form_values
{
public:
    explicit Buoy_form_values(const Kit_parameter_values& values)
        : form  {get_buoy_form(values.get_enum("form"))}
        , size  {values.get_float("size")}
        , height{get_buoy_height(form) * size}
    {
        Kit_random random{values.get_seed()};
        const float heading = random.next_range(0.0f, two_pi);
        const float slope   = std::tan(glm::radians(values.get_float("tilt")));
        place = make_leaning_placement(glm::vec3{0.0f}, slope * glm::vec2{std::cos(heading), -std::sin(heading)});
    }

    // The light: over the lamp on the mast.
    [[nodiscard]] auto get_light() const -> glm::vec3
    {
        return place.at(glm::vec3{0.0f, height + (0.5f * size), 0.0f});
    }

    Buoy_form form  {Buoy_form::can};
    float     size  {0.0f};
    float     height{0.0f}; // of the float
    Placement place {};
};

void generate_buoy(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Buoy_form_values buoy{values};
    const float s        = buoy.size;
    const int   bands    = std::clamp(values.get_int("bands"), 1, buoy_band_max);
    const int   segments = std::max(values.get_int("segments"), 6);
    Kit_mesh_builder builder;
    {
        // Stripes up the float: metal, and a band of trim between each two.
        std::array<Lathe_ring, buoy_rings + 1> rings{};
        std::array<uint32_t, buoy_rings>       slots{};
        for (int k = 0; k <= buoy_rings; ++k) {
            const float v = static_cast<float>(k) / static_cast<float>(buoy_rings);
            rings[static_cast<std::size_t>(k)] = Lathe_ring{get_buoy_radius(buoy.form, v) * s, v * buoy.height};
        }
        for (int k = 0; k < buoy_rings; ++k) {
            const int stripe = (k * ((2 * bands) + 1)) / buoy_rings;
            slots[static_cast<std::size_t>(k)] = ((stripe % 2) == 1) ? buoy_trim_slot : buoy_metal_slot;
        }
        add_lathe(builder, buoy.place, Lathe{.rings = rings, .segments = segments, .band_slots = slots});
    }
    // The mast, its cross piece and the lamp.
    builder.set_slot(buoy_metal_slot);
    add_placed_box(builder, buoy.place, glm::vec3{-0.03f * s, buoy.height - (0.05f * s), -0.03f * s}, glm::vec3{0.03f * s, buoy.height + (0.3f * s), 0.03f * s});
    add_placed_box(builder, buoy.place, glm::vec3{-0.14f * s, buoy.height + (0.14f * s), -0.02f * s}, glm::vec3{0.14f * s, buoy.height + (0.19f * s), 0.02f * s});
    {
        const std::array<Lathe_ring, 3> rings{{{0.06f * s, buoy.height + (0.28f * s)}, {0.085f * s, buoy.height + (0.36f * s)}, {0.05f * s, buoy.height + (0.43f * s)}}};
        add_lathe(builder, buoy.place, Lathe{.rings = rings, .segments = 6, .slot = buoy_glow_slot});
    }
    builder.write(mesh);
}

//
// Trestle: two legs splayed along Z on a sill, under a cap beam, tied by braces.
//

constexpr uint32_t trestle_wood_slot = 0;
constexpr int      trestle_brace_max = 3;

void generate_trestle(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float height = values.get_float("height");
    const float half_w = 0.5f * values.get_float("width");
    const float splay  = std::tan(glm::radians(values.get_float("splay")));
    const int   braces = std::clamp(values.get_int("braces"), 0, trestle_brace_max);
    const float leg    = std::clamp(0.16f * half_w, 0.06f, 0.18f);
    const float low    = 0.3f * leg;
    const float high   = height - (0.7f * leg);
    const float head_z = half_w - (0.8f * leg);
    const float foot_z = head_z + ((high - low) * splay);
    // A leg's middle at the height y.
    const auto get_leg_z = [=](const float y) -> float {
        return foot_z - ((y - low) * splay);
    };
    Kit_mesh_builder builder;
    builder.set_slot(trestle_wood_slot);
    builder.add_box(glm::vec3{-0.7f * leg, 0.0f, -(foot_z + leg)}, glm::vec3{0.7f * leg, 0.6f * leg, foot_z + leg});
    builder.add_box(glm::vec3{-0.7f * leg, height - leg, -half_w}, glm::vec3{0.7f * leg, height, half_w});
    for (const float side : {-1.0f, 1.0f}) {
        const Placement place{.origin = glm::vec3{0.0f, low, side * foot_z}, .y = glm::vec3{0.0f, 1.0f, -side * splay}};
        add_placed_box(builder, place, glm::vec3{-0.5f * leg, 0.0f, -0.5f * leg}, glm::vec3{0.5f * leg, high - low, 0.5f * leg});
    }
    // The braces cross the bays between the sill and the cap, each the other way from the one below.
    const float bottom = 1.2f * leg;
    const float top    = height - (1.6f * leg);
    for (int k = 0; k < braces; ++k) {
        const float y0    = bottom + ((top - bottom) * static_cast<float>(k) / static_cast<float>(braces));
        const float y1    = bottom + ((top - bottom) * static_cast<float>(k + 1) / static_cast<float>(braces));
        const float mid   = 0.5f * (y0 + y1);
        const float reach = get_leg_z(mid);
        const float way   = ((k % 2) == 0) ? 1.0f : -1.0f;
        const float slope = way * 0.7f * (y1 - y0) / (2.0f * reach);
        add_placed_box(
            builder, make_sloping_placement(glm::vec3{0.0f, mid, 0.0f}, slope),
            glm::vec3{-0.22f * leg, -0.35f * leg, -reach}, glm::vec3{0.22f * leg, 0.35f * leg, reach}
        );
    }
    builder.write(mesh);
}

//
// Scaffold: one bay of four poles on base plates with a deck of planks on ledgers at every level, braces on its
// back face (-Z) and a guard rail over the top deck.
//

constexpr uint32_t scaffold_wood_slot  = 0;
constexpr uint32_t scaffold_metal_slot = 1;
constexpr int      scaffold_level_max  = 6;
constexpr int      scaffold_planks     = 3;
constexpr float    scaffold_guard      = 1.0f;  // the guard rail's height over the top deck
constexpr float    scaffold_ledger     = 0.12f; // what hangs under a deck's walking surface

enum class Scaffold_braces : unsigned int { none = 0, single, cross };

[[nodiscard]] auto get_scaffold_braces(const std::string_view name) -> Scaffold_braces
{
    if (name == "single") {
        return Scaffold_braces::single;
    }
    if (name == "cross") {
        return Scaffold_braces::cross;
    }
    return Scaffold_braces::none;
}

class Scaffold_form
{
public:
    explicit Scaffold_form(const Kit_parameter_values& values)
        : braces{get_scaffold_braces(values.get_enum("braces"))}
        , levels{std::clamp(values.get_int("levels"), 1, scaffold_level_max)}
        , half_w{0.5f * values.get_float("width")}
        , half_d{0.5f * values.get_float("depth")}
        , level {values.get_float("level_height")}
    {
        pole  = std::clamp(0.03f * (2.0f * half_w), 0.05f, 0.07f);
        inset = 0.02f;
    }

    [[nodiscard]] auto get_top() const -> float { return level * static_cast<float>(levels); }
    // What the agent walks between the poles of the two faces.
    [[nodiscard]] auto get_clear_depth() const -> float { return 2.0f * (half_d - inset - pole); }

    Scaffold_braces braces{Scaffold_braces::cross};
    int             levels{2};
    float           half_w{0.0f};
    float           half_d{0.0f};
    float           level {0.0f};
    float           pole  {0.0f}; // width of a pole
    float           inset {0.0f}; // from the bay's side to a pole
};

void generate_scaffold(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Scaffold_form form{values};
    const float p     = form.pole;
    const float top   = form.get_top();
    const float x_mid = form.half_w - form.inset - (0.5f * p); // a pole's middle
    const float z_mid = form.half_d - form.inset - (0.5f * p);
    Kit_mesh_builder builder;
    for (const float side_x : {-1.0f, 1.0f}) {
        for (const float side_z : {-1.0f, 1.0f}) {
            const float x = side_x * x_mid;
            const float z = side_z * z_mid;
            builder.set_slot(scaffold_metal_slot);
            builder.add_box(glm::vec3{x - (0.5f * p) - form.inset, 0.0f, z - (0.5f * p) - form.inset}, glm::vec3{x + (0.5f * p) + form.inset, 0.03f, z + (0.5f * p) + form.inset});
            builder.set_slot(scaffold_wood_slot);
            builder.add_box(glm::vec3{x - (0.5f * p), 0.02f, z - (0.5f * p)}, glm::vec3{x + (0.5f * p), top + scaffold_guard, z + (0.5f * p)});
        }
    }
    const float inner_z = z_mid - (0.5f * p) - 0.005f;
    for (int level = 1; level <= form.levels; ++level) {
        const float deck = form.level * static_cast<float>(level);
        builder.set_slot(scaffold_wood_slot);
        // Planks along X over two ledgers along X and two transoms along Z.
        const float pitch = (2.0f * inner_z) / static_cast<float>(scaffold_planks);
        for (int k = 0; k < scaffold_planks; ++k) {
            const float z0 = -inner_z + (pitch * static_cast<float>(k)) + 0.005f;
            const float z1 = -inner_z + (pitch * static_cast<float>(k + 1)) - 0.005f;
            builder.add_box(glm::vec3{-(form.half_w - 0.004f), deck - 0.04f, z0}, glm::vec3{form.half_w - 0.004f, deck, z1});
        }
        for (const float side : {-1.0f, 1.0f}) {
            builder.add_box(glm::vec3{-x_mid, deck - 0.1f, (side * z_mid) - (0.3f * p)}, glm::vec3{x_mid, deck - 0.035f, (side * z_mid) + (0.3f * p)});
            builder.add_box(glm::vec3{(side * x_mid) - (0.3f * p), deck - scaffold_ledger, -z_mid}, glm::vec3{(side * x_mid) + (0.3f * p), deck - 0.06f, z_mid});
        }
        // Braces across the back face under the deck.
        if (form.braces != Scaffold_braces::none) {
            const float low   = deck - form.level + 0.15f;
            const float high  = deck - 0.25f;
            const float slope = (high - low) / (2.0f * x_mid);
            const float way   = ((level % 2) == 0) ? -1.0f : 1.0f;
            builder.set_slot(scaffold_metal_slot);
            Placement place{.origin = glm::vec3{0.0f, 0.5f * (low + high), -z_mid - (0.25f * p)}, .x = glm::vec3{1.0f, way * slope, 0.0f}};
            add_placed_box(builder, place, glm::vec3{-x_mid, -0.02f, -0.012f}, glm::vec3{x_mid, 0.02f, 0.012f});
            if (form.braces == Scaffold_braces::cross) {
                place.origin.z = -z_mid + (0.25f * p);
                place.x        = glm::vec3{1.0f, -way * slope, 0.0f};
                add_placed_box(builder, place, glm::vec3{-x_mid, -0.02f, -0.012f}, glm::vec3{x_mid, 0.02f, 0.012f});
            }
        }
    }
    // The guard rail: along the back and the two sides.
    builder.set_slot(scaffold_wood_slot);
    const float rail = top + scaffold_guard - 0.02f;
    builder.add_box(glm::vec3{-x_mid, rail - 0.06f, -z_mid - (0.25f * p)}, glm::vec3{x_mid, rail, -z_mid + (0.25f * p)});
    for (const float side : {-1.0f, 1.0f}) {
        builder.add_box(glm::vec3{(side * x_mid) - (0.25f * p), rail - 0.08f, -z_mid}, glm::vec3{(side * x_mid) + (0.25f * p), rail - 0.02f, z_mid});
    }
    builder.write(mesh);
}

[[nodiscard]] auto check_scaffold_walkability(const Kit_parameter_values& values, const Kit_agent& agent) -> std::string
{
    const Scaffold_form form{values};
    const float min_width = (2.0f * agent.radius) + kit_opening_margin;
    if (form.get_clear_depth() < min_width) {
        return fmt::format("{}: the decks' clear depth {} m is narrower than the agent's {} m", values.get_shape()->id, form.get_clear_depth(), min_width);
    }
    const float headroom = form.level - scaffold_ledger;
    if (headroom < (agent.height + kit_opening_margin)) {
        return fmt::format("{}: the headroom {} m under a deck is lower than the agent's {} m", values.get_shape()->id, headroom, agent.height + kit_opening_margin);
    }
    return {};
}

//
// Tunnel: a block along X with a bore through it over a floor: jambs under a round or a pointed head, or under a
// lintel.
//

constexpr uint32_t tunnel_stone_slot = 0;
constexpr uint32_t tunnel_floor_slot = 1;
constexpr float    tunnel_floor      = 0.1f; // the walking surface over the base

class Tunnel_form
{
public:
    explicit Tunnel_form(const Kit_parameter_values& values)
        : half_l{0.5f * values.get_float("length")}
        , half  {0.5f * values.get_float("bore_width")}
        , bore  {values.get_float("bore_height")}
        , wall  {values.get_float("wall")}
    {
        const std::string_view profile = values.get_enum("profile");
        const int segments = std::max(values.get_int("segments"), 2);
        // The bore's outline from its right foot (+Z) over its head to its left foot, in (z, y).
        outline.push_back(glm::vec2{half, tunnel_floor});
        if (profile == "square") {
            outline.push_back(glm::vec2{half, tunnel_floor + bore});
            outline.push_back(glm::vec2{-half, tunnel_floor + bore});
        } else {
            const bool  pointed = (profile == "pointed");
            const float rise    = std::min(pointed ? (1.3f * half) : half, 0.6f * bore);
            const std::vector<glm::vec2> head = make_kit_arch_head_points(pointed ? Kit_arch_head::pointed : Kit_arch_head::round, half, rise, segments);
            for (const glm::vec2& point : head) {
                outline.push_back(point + glm::vec2{0.0f, tunnel_floor + bore - rise});
            }
        }
        outline.push_back(glm::vec2{-half, tunnel_floor});
        outer  = half + wall;
        height = tunnel_floor + bore + wall;
    }

    // The bore's clear width at the height over the walking surface.
    [[nodiscard]] auto get_clear_width(const float over) const -> float
    {
        const float y = tunnel_floor + over;
        float clear = 2.0f * half;
        for (std::size_t i = 0; (i + 1) < outline.size(); ++i) {
            const glm::vec2 a = outline[i];
            const glm::vec2 b = outline[i + 1];
            if ((a.x <= 0.0f) || (b.y <= a.y) || (a.y > y) || (b.y < y)) {
                continue;
            }
            clear = std::min(clear, 2.0f * std::max(a.x + ((b.x - a.x) * (y - a.y) / (b.y - a.y)), 0.0f));
        }
        return (y > (tunnel_floor + bore)) ? 0.0f : clear;
    }

    float                  half_l{0.0f};
    float                  half  {0.0f}; // of the bore's width
    float                  bore  {0.0f}; // the bore's height
    float                  wall  {0.0f};
    float                  outer {0.0f}; // half the block's width
    float                  height{0.0f}; // of the block
    std::vector<glm::vec2> outline;
};

void generate_tunnel(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Tunnel_form form{values};
    // The section's points in (z, y): the block's six base and floor points, its two top corners, and the bore.
    std::vector<glm::vec2> points{
        {-form.outer, 0.0f}, {-form.half, 0.0f}, {form.half, 0.0f}, {form.outer, 0.0f},
        {form.outer, tunnel_floor}, {form.outer, form.height}, {-form.outer, form.height}, {-form.outer, tunnel_floor}
    };
    const uint32_t bore  = static_cast<uint32_t>(points.size());
    const uint32_t count = static_cast<uint32_t>(form.outline.size());
    points.insert(points.end(), form.outline.begin(), form.outline.end());
    const uint32_t right = bore;
    const uint32_t left  = bore + count - 1;
    const uint32_t crown = bore + (count / 2);
    // The section's facets, counter-clockwise in (z, y): three under the floor line, and fans from the block's four
    // upper points to the bore.
    std::vector<std::array<uint32_t, 4>> quads{{0, 1, left, 7}, {1, 2, right, left}, {2, 3, 4, right}};
    std::vector<std::array<uint32_t, 3>> triangles;
    triangles.push_back({4, bore + 1, right});
    triangles.push_back({4, 5, bore + 1});
    for (uint32_t i = bore + 1; i < crown; ++i) {
        triangles.push_back({5, i + 1, i});
    }
    triangles.push_back({5, 6, crown});
    for (uint32_t i = crown; i < (left - 1); ++i) {
        triangles.push_back({6, i + 1, i});
    }
    triangles.push_back({6, 7, left - 1});
    triangles.push_back({7, left, left - 1});

    Kit_mesh_builder builder;
    std::vector<uint32_t> start; // at -X
    std::vector<uint32_t> end;
    for (const glm::vec2& point : points) {
        start.push_back(builder.add_vertex(glm::vec3{-form.half_l, point.y, point.x}));
        end  .push_back(builder.add_vertex(glm::vec3{ form.half_l, point.y, point.x}));
    }
    builder.set_slot(tunnel_stone_slot);
    for (const std::array<uint32_t, 4>& quad : quads) {
        builder.add_quad(start[quad[0]], start[quad[1]], start[quad[2]], start[quad[3]]);
        builder.add_quad(end[quad[3]], end[quad[2]], end[quad[1]], end[quad[0]]);
    }
    for (const std::array<uint32_t, 3>& triangle : triangles) {
        builder.add_triangle(start[triangle[0]], start[triangle[1]], start[triangle[2]]);
        builder.add_triangle(end[triangle[2]], end[triangle[1]], end[triangle[0]]);
    }
    // The faces along X: from p to q with the stone on the left, seen from -X.
    const auto add_face = [&](const uint32_t p, const uint32_t q) {
        builder.add_quad(start[q], start[p], end[p], end[q]);
    };
    for (uint32_t i = 0; i < 8; ++i) {
        add_face(i, (i + 1) % 8);
    }
    for (uint32_t i = left; i > right; --i) {
        add_face(i, i - 1);
    }
    builder.set_slot(tunnel_floor_slot);
    add_face(right, left);
    builder.write(mesh);
}

[[nodiscard]] auto check_tunnel_walkability(const Kit_parameter_values& values, const Kit_agent& agent) -> std::string
{
    const Tunnel_form form{values};
    return check_kit_opening(*values.get_shape(), form.get_clear_width(agent.height + kit_opening_margin), form.bore, agent);
}

} // anonymous namespace

void add_extra_17_24_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape;
        shape.id              = "ruin.broken_pillar";
        shape.kit             = "ruin";
        shape.display_name    = "Broken pillar";
        shape.description     =
            "Ruined pillar at its origin with the parameters of the pillar: its stump, standing a share of the height "
            "under a slanting break, and fallen drums lying on their sides about it, each with one slanting end.";
        shape.tags            = {"obstacle", "pillar", "ruin", "size:0.5x1"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("profile",  "Cross section",         {"square", "round"}, 0)),
            derive_float(make_length_parameter("width",    "Width or diameter",     0.5f, Kit_default_scale::modules,      0.05f, 16.0f), 0.3f, 1.0f, 0.05f),
            derive_float(make_length_parameter("height",   "Height of the whole pillar", 1.0f, Kit_default_scale::wall_heights, 0.1f,  64.0f), 0.5f, 1.5f, 0.05f),
            make_count_parameter ("segments", "Sides of a round pillar", 12, 3, 64),
            from_seed_by_default(derive_float(make_share_parameter("standing", "Height of the stump in heights", 0.4f, 0.2f, 0.7f), 0.2f, 0.7f, 0.05f)),
            from_seed_by_default(derive_int(make_count_parameter("fallen", "Fallen drums", 2, 0, broken_pillar_drums), 0, 3)),
            make_seed_parameter  ()
        };
        shape.material_slots  = {"trim"};
        shape.normal_style    = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = 45.0f;
        shape.triangle_budget = 60;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.randomness      = Kit_randomness::seeded;
        shape.detail_counts   = {{"segments"}};
        shape.generator       = generate_broken_pillar;
        shape.get_connectors  = [](const Kit_parameter_values&, std::vector<Kit_connector>& out_connectors) {
            out_connectors.push_back(make_base_connector());
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "ruin.broken_arch";
        shape.kit             = "ruin";
        shape.display_name    = "Broken arch";
        shape.description     =
            "Ruined arch across X with the parameters of the arch: its ring stands from each foot up to a break, and "
            "the gap's share of the ring about the gap's center is gone, cut across the ring at both breaks.";
        shape.tags            = {"obstacle", "arch", "ruin", "size:2x1"};
        shape.parameters      = {
            derive_float(make_length_parameter("span",      "Clear width between the feet", 2.0f, Kit_default_scale::modules,      0.3f,  32.0f), 2.0f, 4.0f, 0.25f),
            derive_float(make_length_parameter("rise",      "Height of the whole ring's opening", 0.8f, Kit_default_scale::wall_heights, 0.2f,  32.0f), 0.8f, 1.2f, 0.05f),
            derive_float(make_length_parameter("depth",     "Size along Z",                 0.5f, Kit_default_scale::modules,      0.05f, 16.0f), 0.3f, 1.0f, 0.05f),
            derive_float(make_length_parameter("thickness", "Thickness of the ring",        0.3f, Kit_default_scale::meters,       0.05f, 8.0f),  0.2f, 0.5f, 0.05f),
            make_count_parameter ("segments",  "Segments along the whole ring; each arm has one more than half of them", 12, 2, 48),
            from_seed_by_default(derive_float(make_share_parameter("gap", "Share of the ring that is gone", 0.3f, 0.15f, 0.5f), 0.15f, 0.5f, 0.05f)),
            from_seed_by_default(derive_float(make_share_parameter("gap_center", "Where the gap's middle lies along the ring, from the foot at +X", 0.5f, 0.25f, 0.75f), 0.25f, 0.75f, 0.05f)),
            make_seed_parameter  ()
        };
        shape.material_slots  = {"trim"};
        shape.normal_style    = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = 45.0f;
        shape.triangle_budget = 130;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.origin_rule     = Kit_origin_rule::base_contains_origin; // the two feet stand apart
        shape.detail_counts   = {{"segments"}};
        shape.generator       = generate_broken_arch;
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float half = (0.5f * values.get_float("span")) + values.get_float("thickness");
            add_side_connector(out_connectors, "start", glm::vec3{-half, 0.0f, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "end",   glm::vec3{ half, 0.0f, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
            out_connectors.push_back(make_base_connector());
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "light.lantern";
        shape.kit             = "light";
        shape.display_name    = "Lantern";
        shape.description     =
            "Cage lantern at its origin: bars around a glowing core between a plate and a pointed roof under a loop, "
            "1.7 sizes high. It stands on its plate, hangs on a chain of five links, or hangs from an arm on a plate "
            "on the wall behind it (-Z, 1.3 sizes from its axis). It carries one point light inside its cage.";
        shape.tags            = {"decoration", "light", "lantern", "size:0.3x0.5"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("mount", "Standing, hanging on a chain, or on a wall bracket", {"standing", "hanging", "bracket"}, 0)),
            derive_float(make_length_parameter("size",  "Width of the lantern", 0.3f, Kit_default_scale::meters, 0.15f, 1.5f), 0.25f, 0.4f, 0.05f),
            derive_float(make_length_parameter("chain_length", "Length of a hanging lantern's chain", 0.5f, Kit_default_scale::meters, 0.1f, 4.0f), 0.3f, 1.0f, 0.1f),
            derive_int  (make_count_parameter ("sides", "Sides of the cage", 6, lantern_side_min, lantern_side_max), 4, 8),
            make_seed_parameter()
        };
        shape.material_slots   = {"metal", "glow"};
        shape.normal_style     = Kit_normal_style::faceted;
        shape.triangle_budget  = 200;
        shape.collision        = Kit_collision::convex_hull;
        shape.generator        = generate_lantern;
        shape.get_connectors   = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float s = values.get_float("size");
            add_base_and_top_connectors(out_connectors, get_lantern_top(values));
            // The wall a bracket stands on; else the lantern's back.
            if (values.get_enum("mount") == "bracket") {
                add_side_connector(out_connectors, "back", glm::vec3{0.0f, (lantern_body + 0.25f) * s, -lantern_reach * s}, glm::vec3{0.0f, 0.0f, -1.0f});
            } else {
                add_side_connector(out_connectors, "back", glm::vec3{0.0f, 0.5f * lantern_body * s, -0.5f * s}, glm::vec3{0.0f, 0.0f, -1.0f});
            }
        };
        shape.get_light_frames = [](const Kit_parameter_values& values, std::vector<Kit_light_frame>& out_frames) {
            out_frames.push_back(Kit_light_frame{
                .name      = "light",
                .position  = glm::vec3{0.0f, lantern_light * values.get_float("size"), 0.0f},
                .direction = glm::vec3{0.0f, -1.0f, 0.0f}
            });
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "light.fire";
        shape.kit             = "light";
        shape.display_name    = "Fire";
        shape.description     =
            "Open flame at its origin over glowing embers. Brazier: a bowl one size high on three legs on a plinth of "
            "stone. Torch: a shaft leaning out of a ring on an arm from a stone on the wall behind it (-Z), under a "
            "cup. Campfire: embers in a ring of seven stones 1.5 sizes wide under a tripod of three rods. The flame is "
            "seeded; one point light stands over its tip.";
        shape.tags            = {"decoration", "light", "fire", "size:0.6x1"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("form",  "Brazier, torch or campfire", {"brazier", "torch", "campfire"}, 0)),
            derive_float(make_length_parameter("size",  "A brazier's width and height, a torch's length, half a campfire's width", 0.6f, Kit_default_scale::meters, 0.3f, 2.0f), 0.5f, 0.8f, 0.1f),
            from_seed_by_default(derive_float(make_length_parameter("flame", "Height of the flame", 0.4f, Kit_default_scale::meters, 0.1f, 1.5f), 0.3f, 0.6f, 0.05f)),
            make_seed_parameter()
        };
        shape.material_slots   = {"metal", "glow", "stone"};
        shape.normal_style     = Kit_normal_style::faceted;
        shape.triangle_budget  = 240;
        shape.collision        = Kit_collision::convex_hull;
        shape.randomness       = Kit_randomness::seeded;
        shape.generator        = generate_fire;
        shape.get_connectors   = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const Fire_form form = get_fire_form(values.get_enum("form"));
            const float     s    = values.get_float("size");
            out_connectors.push_back(make_base_connector());
            // The wall a torch stands on; else the back of the brazier's plinth or of the campfire's ring.
            if (form == Fire_form::torch) {
                add_side_connector(out_connectors, "back", glm::vec3{0.0f, 0.46f * s, -0.12f * s}, glm::vec3{0.0f, 0.0f, -1.0f});
            } else {
                add_side_connector(out_connectors, "back", glm::vec3{0.0f, 0.0f, ((form == Fire_form::brazier) ? -0.56f : -0.95f) * s}, glm::vec3{0.0f, 0.0f, -1.0f});
            }
        };
        shape.get_light_frames = [](const Kit_parameter_values& values, std::vector<Kit_light_frame>& out_frames) {
            out_frames.push_back(Kit_light_frame{
                .name      = "light",
                .position  = get_fire_light(values),
                .direction = glm::vec3{0.0f, -1.0f, 0.0f},
                .start     = Kit_light_start{.color = glm::vec3{1.0f, 0.62f, 0.3f}}
            });
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "harbour.buoy";
        shape.kit             = "harbour";
        shape.display_name    = "Buoy";
        shape.description     =
            "Buoy at its origin, standing on its foot: a float in stripes of metal and trim under a mast with a cross "
            "piece and a glowing lamp, leaning in a seeded direction. Can: a drum one size high. Cone: a drum under a "
            "cone. Sphere: a ball one size wide. Spar: a pole 2.6 sizes high with a float collar. It carries one point "
            "light over its lamp.";
        shape.tags            = {"decoration", "obstacle", "harbour", "buoy", "light", "size:0.7x1.4"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("form",  "Can, cone, sphere or spar", {"can", "cone", "sphere", "spar"}, 0)),
            derive_float(make_length_parameter("size",  "A can's height and a sphere's width", 1.0f, Kit_default_scale::meters, 0.4f, 3.0f), 0.8f, 1.2f, 0.1f),
            from_seed_by_default(derive_float(make_angle_parameter("tilt", "How far it leans from upright", 4.0f, 0.0f, 12.0f), 1.0f, 12.0f, 1.0f)),
            derive_int  (make_count_parameter ("bands", "Bands of trim around the float", 1, 1, buoy_band_max), 1, 3),
            make_seed_parameter (),
            make_count_parameter("segments", "Points around the float", 10, 8, 16)
        };
        shape.material_slots   = {"metal", "trim", "glow"};
        shape.normal_style     = Kit_normal_style::faceted;
        shape.triangle_budget  = 320;
        shape.collision        = Kit_collision::convex_hull;
        shape.randomness       = Kit_randomness::seeded;
        shape.detail_counts    = {{"segments"}};
        shape.generator        = generate_buoy;
        shape.get_connectors   = [](const Kit_parameter_values&, std::vector<Kit_connector>& out_connectors) {
            out_connectors.push_back(make_base_connector());
        };
        shape.get_light_frames = [](const Kit_parameter_values& values, std::vector<Kit_light_frame>& out_frames) {
            out_frames.push_back(Kit_light_frame{
                .name      = "light",
                .position  = Buoy_form_values{values}.get_light(),
                .direction = glm::vec3{0.0f, -1.0f, 0.0f}
            });
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "traversal.trestle";
        shape.kit             = "traversal";
        shape.display_name    = "Trestle";
        shape.description     =
            "Support under a deck at its origin: two legs across Z, splayed outward toward their feet, on a sill "
            "under a cap beam the width long, whose top is the height. Braces cross the bays between the legs, each "
            "the other way from the one below.";
        shape.tags            = {"obstacle", "traversal", "trestle", "support", "size:1x3"};
        shape.parameters      = {
            derive_float(make_length_parameter("height", "Height of the cap beam's top", 1.0f, Kit_default_scale::wall_heights, 0.5f, 16.0f), 0.5f, 1.25f, 0.25f),
            derive_float(make_length_parameter("width",  "Length of the cap beam along Z", 1.0f, Kit_default_scale::modules, 0.5f, 8.0f), 1.0f, 2.0f, 0.5f),
            derive_float(make_angle_parameter ("splay",  "How far each leg leans outward toward its foot", 8.0f, 0.0f, 15.0f), 0.0f, 15.0f, 2.5f),
            derive_int  (make_count_parameter ("braces", "Braces between the legs", 2, 0, trestle_brace_max), 0, 3),
            make_seed_parameter()
        };
        shape.material_slots  = {"wood"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 80;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.generator       = generate_trestle;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x};
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            add_base_and_top_connectors(out_connectors, values.get_float("height"));
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "traversal.scaffold";
        shape.kit             = "traversal";
        shape.display_name    = "Scaffold";
        shape.description     =
            "One scaffold bay at its origin: four poles on base plates (slot metal), and at every level a walkable "
            "deck of three planks along X on ledgers. Braces (slot metal) cross the back face (-Z) under every deck, "
            "one or two to a level, and a guard rail runs along the back and the sides 1 m over the top deck. Bays "
            "join side to side and back to front.";
        shape.tags            = {"walkable", "traversal", "scaffold", "tiles", "size:2x1x6"};
        shape.parameters      = {
            derive_float(make_length_parameter("width",  "Size along X", 2.0f, Kit_default_scale::modules, 1.0f, 8.0f), 2.0f, 3.0f, 0.5f),
            derive_float(make_length_parameter("depth",  "Size along Z; the agent fixes the least", 1.0f, Kit_default_scale::modules, 1.0f, 4.0f), 1.0f, 1.5f, 0.25f),
            derive_int  (make_count_parameter ("levels", "Decks", 2, 1, scaffold_level_max), 1, 3),
            // In meters and not derived: the agent's headroom under a deck fixes the least at every level length (R2.10).
            make_length_parameter("level_height", "From a deck to the next", 2.4f, Kit_default_scale::meters, 2.2f, 6.0f),
            derive_enum (make_enum_parameter  ("braces", "Braces across the back face under every deck", {"none", "single", "cross"}, 2)),
            make_seed_parameter()
        };
        shape.material_slots    = {"wood", "metal"};
        shape.normal_style      = Kit_normal_style::faceted;
        shape.triangle_budget   = 400;
        shape.collision         = Kit_collision::triangle_mesh;
        shape.generator         = generate_scaffold;
        shape.check_walkability = check_scaffold_walkability;
        shape.get_connectors    = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const Scaffold_form form{values};
            add_base_and_top_connectors(out_connectors, form.get_top());
            add_side_connector(out_connectors, "left",  glm::vec3{-form.half_w, 0.0f, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "right", glm::vec3{ form.half_w, 0.0f, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "front", glm::vec3{0.0f, 0.0f,  form.half_d}, glm::vec3{0.0f, 0.0f,  1.0f});
            add_side_connector(out_connectors, "back",  glm::vec3{0.0f, 0.0f, -form.half_d}, glm::vec3{0.0f, 0.0f, -1.0f});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.tunnel";
        shape.kit             = "architecture";
        shape.display_name    = "Tunnel";
        shape.description     =
            "Tunnel segment along X: a block of stone with a bore through it over a flat floor (slot floor) 0.1 m over "
            "the base. The bore has jambs under a round head, a pointed head, or a lintel. Segments join end to "
            "start.";
        shape.tags            = {"walkable", "opening", "tunnel", "tiles", "size:2x2.6x2.8"};
        shape.parameters      = {
            derive_float(make_length_parameter("length", "Size along X", 2.0f, Kit_default_scale::modules, 1.0f, 32.0f), 2.0f, 6.0f, 1.0f),
            // In meters and not derived: the agent fixes the least bore at every level length (R2.10).
            make_length_parameter("bore_width",  "Clear width of the bore",  2.0f, Kit_default_scale::meters, 1.2f, 8.0f),
            make_length_parameter("bore_height", "Clear height of the bore", 2.4f, Kit_default_scale::meters, 2.3f, 8.0f),
            derive_float(make_length_parameter("wall", "Thickness of the stone beside and over the bore", 0.3f, Kit_default_scale::meters, 0.1f, 2.0f), 0.2f, 0.5f, 0.05f),
            derive_enum (make_enum_parameter  ("profile", "A round head, a pointed head, or a lintel", {"round", "pointed", "square"}, 0)),
            make_seed_parameter (),
            make_count_parameter("segments", "Segments along a round or a pointed head", 12, 2, 48)
        };
        shape.material_slots    = {"stone", "floor"};
        shape.normal_style      = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = 45.0f;
        shape.triangle_budget   = 100;
        shape.collision         = Kit_collision::triangle_mesh;
        shape.detail_counts     = {{"segments"}};
        shape.generator         = generate_tunnel;
        shape.mirror_planes     = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x};
        shape.check_walkability = check_tunnel_walkability;
        shape.get_connectors    = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float half_l = 0.5f * values.get_float("length");
            add_side_connector(out_connectors, "start", glm::vec3{-half_l, tunnel_floor, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "end",   glm::vec3{ half_l, tunnel_floor, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
            out_connectors.push_back(make_base_connector());
        };
        shapes.push_back(std::move(shape));
    }
    add_former_build(shapes, "light.fire", generate_fire_old);
}

} // namespace editor
