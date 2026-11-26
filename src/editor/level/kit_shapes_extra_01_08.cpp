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

// The shapes of build-order lines 1 - 8 of doc/plans/level_kit_extra_shapes.md (doc/editor/level_tools.md).
// A mesh's topology follows from its integer values; floats and the seed only move vertices. Three counts are
// integers taken from lengths: a ladder's rungs, a pier's planks and a pier's bents.

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

//
// Crate: slats on four corner posts over a floor on two skids, under a lid of planks with two battens.
//

constexpr uint32_t crate_wood_slot = 0;
constexpr int      crate_slat_min  = 2;
constexpr int      crate_slat_max  = 5;
constexpr float    crate_lid_open  = 1.2217305f; // 70 degrees up from closed

enum class Crate_lid : unsigned int { closed = 0, open, none };

[[nodiscard]] auto get_crate_lid(const std::string_view name) -> Crate_lid
{
    if (name == "open") {
        return Crate_lid::open;
    }
    if (name == "none") {
        return Crate_lid::none;
    }
    return Crate_lid::closed;
}

class Crate_form
{
public:
    explicit Crate_form(const Kit_parameter_values& values)
        : lid   {get_crate_lid(values.get_enum("lid"))}
        , slats {std::clamp(values.get_int("slats"), crate_slat_min, crate_slat_max)}
        , half_w{0.5f * values.get_float("width")}
        , half_d{0.5f * values.get_float("depth")}
        , height{values.get_float("height")}
    {
        const float least = std::min({2.0f * half_w, 2.0f * half_d, height});
        post  = std::clamp(0.1f * least, 0.03f, 0.09f);
        plank = std::clamp(0.03f * least, 0.012f, 0.03f);
        lip   = 0.3f * post;
        skid  = std::clamp(0.07f * height, 0.02f, 0.07f);
        rim   = (lid == Crate_lid::none) ? height : (height - (2.0f * plank));
    }

    // Where another piece stands on the crate: the lid's battens, or the rim under an open lid.
    [[nodiscard]] auto get_top() const -> float
    {
        return (lid == Crate_lid::open) ? rim : height;
    }

    Crate_lid lid   {Crate_lid::closed};
    int       slats {3};
    float     half_w{0.0f};
    float     half_d{0.0f};
    float     height{0.0f};
    float     post  {0.0f}; // a corner post's width and depth
    float     plank {0.0f}; // thickness of a slat and a lid plank
    float     lip   {0.0f}; // how far the posts stand out of the slats
    float     skid  {0.0f}; // height of the skids
    float     rim   {0.0f}; // the top of the posts
};

void generate_crate(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Crate_form form{values};
    const float hw = form.half_w;
    const float hd = form.half_d;
    Kit_mesh_builder builder;
    builder.set_slot(crate_wood_slot);

    // Skids, floor and posts.
    for (const float side : {-1.0f, 1.0f}) {
        const float x = side * 0.55f * hw;
        builder.add_box(glm::vec3{x - (0.5f * form.post), 0.0f, -(hd - form.lip)}, glm::vec3{x + (0.5f * form.post), form.skid, hd - form.lip});
    }
    {
        const float inset = form.lip + (0.5f * form.plank);
        builder.add_box(glm::vec3{-(hw - inset), form.skid - (0.1f * form.plank), -(hd - inset)}, glm::vec3{hw - inset, form.skid + form.plank, hd - inset});
    }
    for (const float side_x : {-1.0f, 1.0f}) {
        for (const float side_z : {-1.0f, 1.0f}) {
            const float x0 = side_x * (hw - form.post);
            const float x1 = side_x * hw;
            const float z0 = side_z * (hd - form.post);
            const float z1 = side_z * hd;
            builder.add_box(
                glm::vec3{std::min(x0, x1), 0.5f * form.skid, std::min(z0, z1)},
                glm::vec3{std::max(x0, x1), form.rim,         std::max(z0, z1)}
            );
        }
    }
    // The slats of the four sides, their ends inside the posts.
    {
        const float low   = form.skid + form.plank;
        const float high  = form.rim - (0.5f * form.plank);
        const float pitch = (high - low) / static_cast<float>(form.slats);
        const float gap   = std::clamp(0.12f * pitch, 0.006f, 0.025f);
        const float end_x = hw - (0.5f * form.post);
        const float end_z = hd - (0.5f * form.post);
        for (int k = 0; k < form.slats; ++k) {
            const float y0 = low + (pitch * static_cast<float>(k)) + (0.5f * gap);
            const float y1 = low + (pitch * static_cast<float>(k + 1)) - (0.5f * gap);
            for (const float side : {-1.0f, 1.0f}) {
                const float z_out = side * (hd - form.lip);
                const float z_in  = side * (hd - form.lip - form.plank);
                const float x_out = side * (hw - form.lip);
                const float x_in  = side * (hw - form.lip - form.plank);
                builder.add_box(glm::vec3{-end_x, y0, std::min(z_in, z_out)}, glm::vec3{end_x, y1, std::max(z_in, z_out)});
                builder.add_box(glm::vec3{std::min(x_in, x_out), y0, -end_z}, glm::vec3{std::max(x_in, x_out), y1, end_z});
            }
        }
    }
    // The lid: planks along X under two battens, lying on the rim or standing open on its back edge.
    if (form.lid != Crate_lid::none) {
        Placement place{.origin = glm::vec3{0.0f, form.rim, -hd}};
        if (form.lid == Crate_lid::open) {
            const float c = std::cos(crate_lid_open);
            const float s = std::sin(crate_lid_open);
            place.origin.z += 2.0f * form.plank;
            place.y = glm::vec3{0.0f, c, -s};
            place.z = glm::vec3{0.0f, s, c};
        }
        const float pitch  = (2.0f * hd) / static_cast<float>(form.slats);
        const float gap    = std::clamp(0.1f * pitch, 0.006f, 0.02f);
        const float half_x = hw - (0.25f * form.lip);
        for (int k = 0; k < form.slats; ++k) {
            const float z0 = (pitch * static_cast<float>(k)) + (0.5f * gap);
            const float z1 = (pitch * static_cast<float>(k + 1)) - (0.5f * gap);
            add_placed_box(builder, place, glm::vec3{-half_x, 0.0f, z0}, glm::vec3{half_x, form.plank, z1});
        }
        for (const float side : {-1.0f, 1.0f}) {
            const float x = side * 0.55f * hw;
            add_placed_box(
                builder, place,
                glm::vec3{x - (0.5f * form.post), 0.8f * form.plank, form.lip},
                glm::vec3{x + (0.5f * form.post), 2.0f * form.plank, (2.0f * hd) - form.lip}
            );
        }
    }
    builder.write(mesh);
}

//
// Barrel: staves bulging between two recessed heads, bound by hoops; upright, or lying along X between two chocks.
//

constexpr uint32_t barrel_wood_slot  = 0;
constexpr uint32_t barrel_metal_slot = 1;
constexpr int      barrel_hoop_min   = 2;
constexpr int      barrel_hoop_max   = 4;
constexpr float    barrel_hoop_half  = 0.07f; // half a hoop's width in half heights

class Barrel_form
{
public:
    explicit Barrel_form(const Kit_parameter_values& values)
        : height  {values.get_float("height")}
        , radius  {values.get_float("radius")}
        , bulge   {values.get_float("bulge")}
        , hoops   {std::clamp(values.get_int("hoops"), barrel_hoop_min, barrel_hoop_max)}
        , segments{std::max(values.get_int("segments"), 6)}
        , lying   {values.get_enum("pose") == "lying"}
    {
        stave  = std::clamp(0.08f * radius, 0.008f, 0.04f);
        recess = std::min(0.04f * height, 0.05f);
        hoop   = std::clamp(0.03f * radius, 0.003f, 0.012f);
        // The widest ring: the staves' middle, or a hoop on a barrel that hardly bulges.
        widest = radius;
        for (int k = 0; k < hoops; ++k) {
            const float t = get_hoop_center(k);
            widest = std::max(widest, get_radius(std::abs(t) - barrel_hoop_half) + hoop);
        }
        // A lying barrel rests on a side of its widest ring.
        axis_height = widest * get_ring_point(0, segments, Lathe_phase::side).x;
    }

    // The staves' radius at t, -1 to 1 from head to head.
    [[nodiscard]] auto get_radius(const float t) const -> float
    {
        return radius * (1.0f - (bulge * t * t));
    }

    // Hoop k of the count, from the bottom: a pair toward the ends, a second pair or one about the middle.
    [[nodiscard]] auto get_hoop_center(const int k) const -> float
    {
        constexpr std::array<float, 2> two  {-0.78f, 0.78f};
        constexpr std::array<float, 3> three{-0.78f, 0.0f, 0.78f};
        constexpr std::array<float, 4> four {-0.84f, -0.48f, 0.48f, 0.84f};
        const std::size_t index = static_cast<std::size_t>(k);
        return (hoops == 2) ? two[index] : ((hoops == 3) ? three[index] : four[index]);
    }

    // A lying barrel's top is its widest ring's highest point: a side for an even count, a vertex for an odd one.
    [[nodiscard]] auto get_top() const -> float
    {
        if (!lying) {
            return height;
        }
        float lowest_x = 1.0f;
        for (int i = 0; i < segments; ++i) {
            lowest_x = std::min(lowest_x, get_ring_point(i, segments, Lathe_phase::side).x);
        }
        return axis_height - (widest * lowest_x);
    }

    float height     {0.0f};
    float radius     {0.0f};
    float bulge      {0.0f};
    int   hoops      {4};
    int   segments   {12};
    bool  lying      {false};
    float stave      {0.0f}; // thickness of the staves at the chime
    float recess     {0.0f}; // how far a head lies inside the chime
    float hoop       {0.0f}; // how far a hoop stands out of the staves
    float widest     {0.0f};
    float axis_height{0.0f}; // of a lying barrel's axis
};

void generate_barrel(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Barrel_form form{values};
    const float half_h = 0.5f * form.height;
    const float inner  = form.get_radius(1.0f) - form.stave;

    // The profile from the lower head to the upper: stations a quarter apart, but where a hoop lies.
    std::vector<Lathe_ring> rings;
    std::vector<uint32_t>   band_slots;
    rings.push_back(Lathe_ring{inner, -half_h + form.recess});
    rings.push_back(Lathe_ring{inner, -half_h});
    band_slots.push_back(barrel_wood_slot);
    band_slots.push_back(barrel_wood_slot);
    rings.push_back(Lathe_ring{form.get_radius(1.0f), -half_h});
    float previous = -1.0f;
    const auto add_staves_until = [&](const float limit) {
        for (int g = -3; g <= 3; ++g) {
            const float t = 0.25f * static_cast<float>(g);
            if ((t > (previous + 0.04f)) && (t < (limit - 0.04f))) {
                band_slots.push_back(barrel_wood_slot);
                rings.push_back(Lathe_ring{form.get_radius(t), half_h * t});
            }
        }
        band_slots.push_back(barrel_wood_slot);
        rings.push_back(Lathe_ring{form.get_radius(limit), half_h * limit});
        previous = limit;
    };
    for (int k = 0; k < form.hoops; ++k) {
        const float low  = form.get_hoop_center(k) - barrel_hoop_half;
        const float high = form.get_hoop_center(k) + barrel_hoop_half;
        add_staves_until(low);
        band_slots.push_back(barrel_metal_slot);
        rings.push_back(Lathe_ring{form.get_radius(low) + form.hoop, half_h * low});
        band_slots.push_back(barrel_metal_slot);
        rings.push_back(Lathe_ring{form.get_radius(high) + form.hoop, half_h * high});
        band_slots.push_back(barrel_metal_slot);
        rings.push_back(Lathe_ring{form.get_radius(high), half_h * high});
        previous = high;
    }
    add_staves_until(1.0f);
    band_slots.push_back(barrel_wood_slot);
    rings.push_back(Lathe_ring{inner, half_h});
    band_slots.push_back(barrel_wood_slot);
    rings.push_back(Lathe_ring{inner, half_h - form.recess});

    Kit_mesh_builder builder;
    Placement place{.origin = glm::vec3{0.0f, half_h, 0.0f}};
    if (form.lying) {
        // The barrel's axis along +X; a side of each ring faces the ground.
        place = Placement{.origin = glm::vec3{0.0f, form.axis_height, 0.0f}, .x = -y_axis, .y = x_axis, .z = z_axis};
    }
    add_lathe(builder, place, Lathe{.rings = rings, .segments = form.segments, .phase = Lathe_phase::side, .band_slots = band_slots});
    if (form.lying) {
        // A chock on each side: its slope toward the barrel.
        const float r      = form.widest;
        const float half_x = 0.18f * form.height;
        builder.set_slot(barrel_wood_slot);
        const std::array<glm::vec3, 3> front{{{half_x, 0.0f, 0.3f * r}, {half_x, 0.3f * r, 0.74f * r}, {half_x, 0.0f, 0.74f * r}}};
        const std::array<glm::vec3, 3> back {{{half_x, 0.0f, -0.3f * r}, {half_x, 0.0f, -0.74f * r}, {half_x, 0.3f * r, -0.74f * r}}};
        add_prism_between(builder, front, glm::vec3{-2.0f * half_x, 0.0f, 0.0f});
        add_prism_between(builder, back,  glm::vec3{-2.0f * half_x, 0.0f, 0.0f});
    }
    builder.write(mesh);
}

//
// Broken wall: a wall along X whose top falls from the full height at its start in columns toward its end.
//

constexpr uint32_t broken_wall_slot     = 0;
constexpr int      broken_wall_step_min = 3;
constexpr int      broken_wall_step_max = 24;

enum class Wall_break : unsigned int { stepped = 0, jagged, slope };

[[nodiscard]] auto get_wall_break(const std::string_view name) -> Wall_break
{
    if (name == "jagged") {
        return Wall_break::jagged;
    }
    if (name == "slope") {
        return Wall_break::slope;
    }
    return Wall_break::stepped;
}

// The top outline: column i from x[i] to x[i + 1], its top from left[i] to right[i]; right[i] is over left[i + 1].
class Broken_wall_outline
{
public:
    explicit Broken_wall_outline(const Kit_parameter_values& values)
    {
        const float      length  = values.get_float("length");
        const float      height  = values.get_float("height");
        const float      intact  = values.get_float("intact");
        const float      course  = values.get_float("course");
        const Wall_break form    = get_wall_break(values.get_enum("break"));
        const int        columns = std::clamp(values.get_int("steps"), broken_wall_step_min, broken_wall_step_max);
        const float      count   = static_cast<float>(columns);
        Kit_random random{values.get_seed()};
        // The share of the height left at u, 0 at the start to 1 at the end: whole, then falling through a break to a
        // low stub; its mean is `intact`.
        constexpr float stub = 0.15f;
        const float center = (intact - stub) / (1.0f - stub);
        const float half   = 0.9f * std::min({center, 1.0f - center, 0.3f});
        const auto get_target = [=](const float u) -> float {
            const float t = std::clamp((u - (center - half)) / (2.0f * half), 0.0f, 1.0f);
            return height * (1.0f - ((1.0f - stub) * t * t * (3.0f - (2.0f * t))));
        };
        // The least fall from a level top to the next, and the size of a tooth.
        const float fall  = std::min(0.12f * course, 0.4f * height / count);
        const float tooth = std::min(course, 0.6f * height / count);
        std::vector<float> u(static_cast<std::size_t>(columns) + 1, 0.0f);
        for (int i = 0; i <= columns; ++i) {
            const float jitter = 0.38f * next_signed(random);
            const bool  inside = (i > 0) && (i < columns);
            u[static_cast<std::size_t>(i)] = (static_cast<float>(i) + (inside ? jitter : 0.0f)) / count;
            x.push_back(length * (u[static_cast<std::size_t>(i)] - 0.5f));
        }
        float previous = height; // the end of the column before
        for (int i = 0; i < columns; ++i) {
            const std::size_t index  = static_cast<std::size_t>(i);
            const float       first  = random.next_unit();
            const float       second = random.next_unit();
            const float       lowest = 0.25f * height * static_cast<float>(columns - i) / count;
            float a = height;
            float b = height;
            if (form == Wall_break::stepped) {
                // Level tops on the courses, each lower than the one before: by a chip where the course is the same.
                const float middle = get_target(0.5f * (u[index] + u[index + 1])) + (course * ((1.6f * first) - 0.8f));
                const float level  = std::min(course * std::round(middle / course), height);
                a = (i == 0) ? height : std::max(std::min(level, previous - fall), lowest);
                b = a;
            } else if (form == Wall_break::jagged) {
                // Each top rises to a tooth over the next column's start.
                const float start = std::min(get_target(u[index]) - (0.8f * first * tooth), previous - (0.5f * tooth));
                a = (i == 0) ? height : std::max(start, 0.03f * height);
                b = std::min(get_target(u[index + 1]) + ((0.5f + (0.7f * second)) * tooth), height);
            } else {
                // Tops along the line, a notch between two.
                a = (i == 0) ? height : std::min(get_target(u[index]) - (0.02f * first * height), previous - (0.012f * height));
                b = std::min(get_target(u[index + 1]) + ((0.01f + (0.03f * second)) * height), height);
            }
            left .push_back(a);
            right.push_back(b);
            previous = b;
        }
    }

    std::vector<float> x;
    std::vector<float> left;
    std::vector<float> right;
};

void generate_broken_wall(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Broken_wall_outline outline{values};
    const float       half_t  = 0.5f * values.get_float("thickness");
    const std::size_t columns = outline.left.size();
    Kit_mesh_builder builder;
    builder.set_slot(broken_wall_slot);
    // Front and back points: the base at every column end, and the two ends of every column's top.
    std::array<std::vector<uint32_t>, 2> base;
    std::array<std::vector<uint32_t>, 2> top_left;
    std::array<std::vector<uint32_t>, 2> top_right;
    for (std::size_t side = 0; side < 2; ++side) {
        const float z = (side == 0) ? half_t : -half_t;
        for (std::size_t i = 0; i <= columns; ++i) {
            base[side].push_back(builder.add_vertex(glm::vec3{outline.x[i], 0.0f, z}));
        }
        for (std::size_t i = 0; i < columns; ++i) {
            top_left [side].push_back(builder.add_vertex(glm::vec3{outline.x[i],     outline.left[i],  z}));
            top_right[side].push_back(builder.add_vertex(glm::vec3{outline.x[i + 1], outline.right[i], z}));
        }
    }
    constexpr std::size_t f = 0;
    constexpr std::size_t b = 1;
    for (std::size_t i = 0; i < columns; ++i) {
        const bool last = (i + 1) == columns;
        if (last) {
            builder.add_quad(base[f][i], base[f][i + 1], top_right[f][i], top_left[f][i]);
            builder.add_quad(base[b][i + 1], base[b][i], top_left[b][i], top_right[b][i]);
        } else {
            // The face under the next column's start, and the triangle up to this column's end.
            builder.add_quad(base[f][i], base[f][i + 1], top_left[f][i + 1], top_left[f][i]);
            builder.add_triangle(top_left[f][i], top_left[f][i + 1], top_right[f][i]);
            builder.add_quad(base[b][i + 1], base[b][i], top_left[b][i], top_left[b][i + 1]);
            builder.add_triangle(top_left[b][i + 1], top_left[b][i], top_right[b][i]);
            // The broken face over the next column.
            builder.add_quad(top_left[f][i + 1], top_left[b][i + 1], top_right[b][i], top_right[f][i]);
        }
        builder.add_quad(top_left[f][i], top_right[f][i], top_right[b][i], top_left[b][i]);
        builder.add_quad(base[f][i], base[b][i], base[b][i + 1], base[f][i + 1]);
    }
    builder.add_quad(base[b][0], base[f][0], top_left[f][0], top_left[b][0]);
    builder.add_quad(base[f][columns], base[b][columns], top_right[b][columns - 1], top_right[f][columns - 1]);
    builder.write(mesh);
}

//
// Rubble: tumbled ashlar blocks on a low mound of debris within a round footprint.
//

constexpr uint32_t rubble_stone_slot = 0;
constexpr int      rubble_block_min  = 4;
constexpr int      rubble_block_max  = 48;
constexpr int      rubble_mound      = 8; // points around the mound

// A block: its center and its three half axes.
class Rubble_block
{
public:
    glm::vec3 center{0.0f};
    glm::vec3 x     {0.0f};
    glm::vec3 y     {0.0f};
    glm::vec3 z     {0.0f};
};

void generate_rubble(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float radius = values.get_float("radius");
    const float height = values.get_float("height") * radius;
    const int   blocks = std::clamp(values.get_int("blocks"), rubble_block_min, rubble_block_max);
    const float size   = std::min(values.get_float("block_size"), 0.7f * radius);
    Kit_random random{values.get_seed()};

    // The mound under the blocks: lower than the heap by about a block.
    const float mound_radius = 0.85f * radius;
    const float mound_height = std::max(0.35f * height, height - (0.45f * size));
    const auto get_mound_height = [=](const float distance) -> float {
        const float share = std::min(distance / mound_radius, 1.0f);
        return mound_height * (1.0f - (share * share));
    };

    // Half of the blocks lie flat on the ground around the mound's foot, the others tumbled on the mound.
    const int ground = std::max(4, (blocks + 1) / 2);
    std::vector<Rubble_block> placed;
    for (int i = 0; i < blocks; ++i) {
        const float half_x = 0.5f * size * random.next_range(0.8f, 1.3f);
        const float half_y = 0.5f * size * random.next_range(0.4f, 0.7f);
        const float half_z = 0.5f * size * random.next_range(0.5f, 0.8f);
        const float yaw    = random.next_range(0.0f, two_pi);
        const float turn   = next_signed(random);
        const float out    = random.next_unit();
        const float tilt   = random.next_range(0.15f, 0.55f);
        const float roll   = random.next_range(0.0f, two_pi);
        const float sink   = random.next_unit();
        const float reach  = std::sqrt((half_x * half_x) + (half_z * half_z));
        Rubble_block block;
        block.x = half_x * around_y(yaw);
        block.z = half_z * glm::cross(around_y(yaw), y_axis);
        block.y = half_y * y_axis;
        if (i < ground) {
            const float angle    = two_pi * (static_cast<float>(i) + 0.5f + (0.3f * turn)) / static_cast<float>(ground);
            const float distance = (radius - reach) * (0.5f + (0.5f * out));
            block.center = (distance * around_y(angle)) + block.y;
        } else {
            // Tilted about a level axis, half sunk into the mound.
            const float     angle    = (2.39996323f * static_cast<float>(i)) + turn;
            const float     distance = 0.6f * (radius - reach) * std::sqrt(out);
            const glm::vec3 axis     = around_y(roll);
            const float     c        = std::cos(tilt);
            const float     s        = std::sin(tilt);
            for (glm::vec3* const half : {&block.x, &block.y, &block.z}) {
                *half = (c * (*half)) + (s * glm::cross(axis, *half)) + (((1.0f - c) * glm::dot(axis, *half)) * axis);
            }
            const float drop = std::abs(block.x.y) + std::abs(block.y.y) + std::abs(block.z.y);
            block.center = (distance * around_y(angle)) + ((get_mound_height(distance) + (drop * (0.3f + (0.5f * sink)))) * y_axis);
            // No corner under the ground.
            block.center.y = std::max(block.center.y, 1.05f * drop);
        }
        placed.push_back(block);
    }
    // The heap's top is the height: every point moves in proportion.
    float top = mound_height;
    for (const Rubble_block& block : placed) {
        top = std::max(top, block.center.y + std::abs(block.x.y) + std::abs(block.y.y) + std::abs(block.z.y));
    }
    const float scale = height / top;

    Kit_mesh_builder builder;
    builder.set_slot(rubble_stone_slot);
    const Placement squeezed{.y = glm::vec3{0.0f, scale, 0.0f}};
    {
        const std::array<Lathe_ring, 3> rings{{
            {mound_radius, 0.0f}, {0.75f * mound_radius, get_mound_height(0.75f * mound_radius)}, {0.4f * mound_radius, get_mound_height(0.4f * mound_radius)}
        }};
        add_lathe(builder, squeezed, Lathe{.rings = rings, .segments = rubble_mound, .top = Lathe_cap::point, .top_y = mound_height, .slot = rubble_stone_slot});
    }
    for (const Rubble_block& block : placed) {
        Placement place{.origin = block.center, .x = block.x, .y = block.y, .z = block.z};
        for (glm::vec3* const part : {&place.origin, &place.x, &place.y, &place.z}) {
            part->y *= scale;
        }
        add_placed_box(builder, place, glm::vec3{-1.0f}, glm::vec3{1.0f});
    }
    builder.write(mesh);
}

//
// Ladder: two rails standing on the ground, leaning toward -Z, with level rungs between them.
//

constexpr uint32_t ladder_wood_slot = 0;
constexpr int      ladder_rung_max  = 80;

class Ladder_form
{
public:
    explicit Ladder_form(const Kit_parameter_values& values)
        : height{values.get_float("height")}
        , half_w{0.5f * values.get_float("width")}
    {
        // Equal steps from the ground to the top, each at most the rung spacing.
        const float spacing = values.get_float("rung_spacing");
        steps  = std::clamp(static_cast<int>(std::ceil((height / spacing) - 1.0e-4f)), 1, ladder_rung_max + 1);
        run    = height * std::tan(glm::radians(values.get_float("lean")));
        rail_w = std::clamp(0.09f * (2.0f * half_w), 0.025f, 0.06f);
        rail_d = 1.6f * rail_w;
        rung_h = std::clamp(0.06f * (2.0f * half_w), 0.018f, 0.035f);
    }

    [[nodiscard]] auto get_step_rise() const -> float
    {
        return height / static_cast<float>(steps);
    }

    float height{0.0f};
    float half_w{0.0f};
    int   steps {1};    // the rungs are one less
    float run   {0.0f}; // how far the top stands toward -Z of the foot
    float rail_w{0.0f};
    float rail_d{0.0f};
    float rung_h{0.0f};
};

void generate_ladder(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Ladder_form form{values};
    Kit_mesh_builder builder;
    builder.set_slot(ladder_wood_slot);
    // The rails: their feet and tops are level.
    const Placement leaning{.y = glm::vec3{0.0f, 1.0f, -form.run / form.height}};
    for (const float side : {-1.0f, 1.0f}) {
        const float x0 = side * (form.half_w - form.rail_w);
        const float x1 = side * form.half_w;
        add_placed_box(
            builder, leaning,
            glm::vec3{std::min(x0, x1), 0.0f,        -0.5f * form.rail_d},
            glm::vec3{std::max(x0, x1), form.height,  0.5f * form.rail_d}
        );
    }
    const float half_x = form.half_w - (0.5f * form.rail_w);
    const float half_z = 0.6f * form.rung_h;
    for (int k = 1; k < form.steps; ++k) {
        const float y = form.get_step_rise() * static_cast<float>(k);
        const float z = -form.run * static_cast<float>(k) / static_cast<float>(form.steps);
        builder.add_box(glm::vec3{-half_x, y - (0.5f * form.rung_h), z - half_z}, glm::vec3{half_x, y + (0.5f * form.rung_h), z + half_z});
    }
    builder.write(mesh);
}

//
// Pier: planks across two stringers along X, on bents of two piles under a cap beam, with cross braces.
//

constexpr uint32_t pier_wood_slot  = 0;
constexpr int      pier_plank_max  = 64;
constexpr int      pier_bent_max   = 8;
constexpr float    pier_plank_step = 0.25f; // a plank and its gap along the deck

class Pier_form
{
public:
    explicit Pier_form(const Kit_parameter_values& values)
        : length  {values.get_float("length")}
        , half_w  {0.5f * values.get_float("width")}
        , deck    {values.get_float("pile_depth")}
        , jitter  {values.get_float("plank_jitter")}
        , braced  {values.get_enum("braces") == "cross"}
        , segments{std::max(values.get_int("segments"), 5)}
    {
        planks   = std::clamp(static_cast<int>(std::lround(length / pier_plank_step)), 1, pier_plank_max);
        bents    = std::clamp(static_cast<int>(std::lround(length / values.get_float("pile_spacing"))), 1, pier_bent_max);
        pitch    = length / static_cast<float>(planks);
        gap      = std::min(0.02f, 0.1f * pitch);
        plank    = std::min(0.05f, 0.1f * deck);
        stringer = std::min(0.14f, 0.25f * deck);
        cap      = std::min(0.14f, 0.2f * deck);
        pile     = std::clamp(0.06f * (2.0f * half_w), 0.07f, 0.14f);
        pile_z   = 0.72f * half_w;
        cap_low  = deck - plank - stringer - cap;
    }

    float length  {0.0f};
    float half_w  {0.0f};
    float deck    {0.0f}; // the walking surface over the piles' feet
    float jitter  {0.0f};
    bool  braced  {true};
    int   segments{8};
    int   planks  {1};
    int   bents   {1};
    float pitch   {0.0f}; // a plank and its gap
    float gap     {0.0f};
    float plank   {0.0f}; // thickness of the planks
    float stringer{0.0f}; // height of the stringers
    float cap     {0.0f}; // height of the cap beams
    float pile    {0.0f}; // radius of a pile at the cap
    float pile_z  {0.0f};
    float cap_low {0.0f}; // the cap beams' underside
};

void generate_pier(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Pier_form form{values};
    const float half_l = 0.5f * form.length;
    Kit_random random{values.get_seed()};
    Kit_mesh_builder builder;
    builder.set_slot(pier_wood_slot);
    // The planks: with jitter their ends stand back unevenly and their tops lie a little low.
    for (int k = 0; k < form.planks; ++k) {
        const float front = random.next_unit();
        const float back  = random.next_unit();
        const float sag   = random.next_unit();
        const float x0    = -half_l + (form.pitch * static_cast<float>(k)) + (0.5f * form.gap);
        const float x1    = -half_l + (form.pitch * static_cast<float>(k + 1)) - (0.5f * form.gap);
        const float top   = form.deck - (0.008f * form.jitter * sag);
        builder.add_box(
            glm::vec3{x0, form.deck - form.plank, -(form.half_w - (0.01f + (0.1f * form.jitter * back)))},
            glm::vec3{x1, top,                      form.half_w - (0.01f + (0.1f * form.jitter * front))}
        );
    }
    {
        const float half = std::min(0.05f, 0.06f * form.half_w);
        const float low  = form.deck - form.plank - form.stringer;
        for (const float side : {-1.0f, 1.0f}) {
            const float z = side * 0.56f * form.half_w;
            builder.add_box(glm::vec3{-half_l, low, z - half}, glm::vec3{half_l, form.deck - (0.8f * form.plank), z + half});
        }
    }
    const float pile_top = form.cap_low + (0.3f * form.cap);
    for (int i = 0; i < form.bents; ++i) {
        const float x = -half_l + (form.length * (static_cast<float>(i) + 0.5f) / static_cast<float>(form.bents));
        builder.add_box(
            glm::vec3{x - (0.8f * form.pile), form.cap_low,                  -form.half_w},
            glm::vec3{x + (0.8f * form.pile), form.cap_low + (1.1f * form.cap), form.half_w}
        );
        for (const float side : {-1.0f, 1.0f}) {
            const std::array<Lathe_ring, 2> rings{{{0.85f * form.pile, 0.0f}, {form.pile, pile_top}}};
            const Placement place{.origin = glm::vec3{x, 0.0f, side * form.pile_z}};
            add_lathe(builder, place, Lathe{.rings = rings, .segments = form.segments, .slot = pier_wood_slot});
        }
        if (form.braced) {
            // Two braces across the bent, one on each face of the piles.
            const float offset = 0.9f * form.pile;
            const float low    = 0.2f * pile_top;
            const float high   = 0.88f * pile_top;
            const float width  = std::min(0.04f, 0.4f * form.pile);
            const float depth  = std::min(0.11f, 0.12f * pile_top);
            builder.set_slot(pier_wood_slot);
            builder.add_beam(glm::vec3{x + offset, low, -form.pile_z}, glm::vec3{x + offset, high,  form.pile_z}, width, depth);
            builder.add_beam(glm::vec3{x - offset, low,  form.pile_z}, glm::vec3{x - offset, high, -form.pile_z}, width, depth);
        }
    }
    builder.write(mesh);
}

[[nodiscard]] auto check_pier_walkability(const Kit_parameter_values& values, const Kit_agent& agent) -> std::string
{
    const Pier_form form{values};
    const float min_width = (2.0f * agent.radius) + kit_opening_margin;
    if ((2.0f * form.half_w) < min_width) {
        return fmt::format("{}: the deck's width {} m is narrower than the agent's {} m", values.get_shape()->id, 2.0f * form.half_w, min_width);
    }
    const float max_gap = 0.1f * agent.radius;
    if (form.gap > max_gap) {
        return fmt::format("{}: the gap between planks {} m is wider than {} m", values.get_shape()->id, form.gap, max_gap);
    }
    return {};
}

//
// Mooring: a bollard or a cleat on a timber pad, a banded post, or three piles bound by a hoop; with a rope coil.
//

constexpr uint32_t mooring_metal_slot = 0;
constexpr uint32_t mooring_wood_slot  = 1;
constexpr int      mooring_rope_turns = 3;
constexpr int      mooring_cluster    = 12; // points around a pile cluster's hoop: four to a pile

enum class Mooring_form : unsigned int { bollard = 0, cleat, post, pile_cluster };

[[nodiscard]] auto get_mooring_form(const std::string_view name) -> Mooring_form
{
    if (name == "cleat") {
        return Mooring_form::cleat;
    }
    if (name == "post") {
        return Mooring_form::post;
    }
    if (name == "pile_cluster") {
        return Mooring_form::pile_cluster;
    }
    return Mooring_form::bollard;
}

// The heights of a post and a pile cluster in sizes.
constexpr float mooring_post_height    = 2.4f;
constexpr float mooring_cluster_height = 4.0f;

// Turns of rope around the upright through `origin`, each a ring of a square section on its corner.
void add_rope_coil(
    Kit_mesh_builder&            builder,
    Kit_random&                  random,
    const Placement&             place,
    const float                  radius,
    const float                  rope,
    const float                  first_y,
    const int                    segments,
    const std::span<const float> scales = {}
)
{
    for (int turn = 0; turn < mooring_rope_turns; ++turn) {
        const float tilt_x = 0.07f * next_signed(random);
        const float tilt_z = 0.07f * next_signed(random);
        const float wide   = 1.0f + (0.06f * random.next_unit());
        const float y      = first_y + (2.0f * rope * static_cast<float>(turn));
        const float r      = radius * wide;
        Placement turned = place;
        turned.origin = place.at(glm::vec3{0.0f, y, 0.0f});
        turned.x      = place.x + (tilt_x * place.y);
        turned.z      = place.z + (tilt_z * place.y);
        const std::array<Lathe_ring, 4> rings{{{r + rope, 0.0f}, {r, rope}, {r - rope, 0.0f}, {r, -rope}}};
        add_lathe(builder, turned, Lathe{
            .rings = rings, .segments = segments, .bottom = Lathe_cap::loop, .top = Lathe_cap::loop, .slot = mooring_wood_slot, .scales = scales
        });
    }
}

void generate_mooring(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Mooring_form form     = get_mooring_form(values.get_enum("form"));
    const float        s        = values.get_float("size");
    const bool         coil     = (values.get_enum("rope") == "coil");
    const int          segments = std::max(values.get_int("segments"), 6);
    const float        rope     = 0.04f * s;
    Kit_random random{values.get_seed()};
    const float     heading = random.next_range(0.0f, two_pi);
    const float     slope   = std::tan(glm::radians(values.get_float("lean")));
    const glm::vec2 lean{slope * std::cos(heading), -slope * std::sin(heading)};
    Kit_mesh_builder builder;

    if (form == Mooring_form::bollard) {
        // A cast bollard on a timber pad: a flange, a waist and a head.
        builder.set_slot(mooring_wood_slot);
        builder.add_box(glm::vec3{-0.55f * s, 0.0f, -0.55f * s}, glm::vec3{0.55f * s, 0.08f * s, 0.55f * s});
        const Placement place = make_leaning_placement(glm::vec3{0.0f, 0.06f * s, 0.0f}, lean);
        const std::array<Lathe_ring, 9> rings{{
            {0.38f * s, 0.0f}, {0.38f * s, 0.07f * s}, {0.25f * s, 0.14f * s}, {0.2f * s, 0.46f * s}, {0.22f * s, 0.6f * s},
            {0.36f * s, 0.69f * s}, {0.38f * s, 0.8f * s}, {0.3f * s, 0.9f * s}, {0.12f * s, 0.94f * s}
        }};
        add_lathe(builder, place, Lathe{.rings = rings, .segments = segments, .slot = mooring_metal_slot});
        if (coil) {
            add_rope_coil(builder, random, place, 0.25f * s, rope, 0.2f * s, segments);
        }
    } else if (form == Mooring_form::cleat) {
        // A horn cleat along X on a timber pad: two legs under a bar thinning to both ends.
        builder.set_slot(mooring_wood_slot);
        builder.add_box(glm::vec3{-0.6f * s, 0.0f, -0.3f * s}, glm::vec3{0.6f * s, 0.1f * s, 0.3f * s});
        const Placement place = make_leaning_placement(glm::vec3{0.0f, 0.08f * s, 0.0f}, lean);
        builder.set_slot(mooring_metal_slot);
        for (const float side : {-1.0f, 1.0f}) {
            const float x = side * 0.2f * s;
            add_placed_box(builder, place, glm::vec3{x - (0.05f * s), 0.0f, -0.07f * s}, glm::vec3{x + (0.05f * s), 0.24f * s, 0.07f * s});
        }
        // The bar: a lathe along X at the legs' heads.
        const Placement bar{.origin = place.at(glm::vec3{0.0f, 0.27f * s, 0.0f}), .x = -place.y, .y = x_axis, .z = z_axis};
        const std::array<Lathe_ring, 5> rings{{
            {0.04f * s, -0.44f * s}, {0.075f * s, -0.22f * s}, {0.085f * s, 0.0f}, {0.075f * s, 0.22f * s}, {0.04f * s, 0.44f * s}
        }};
        add_lathe(builder, bar, Lathe{
            .rings = rings, .segments = 6, .phase = Lathe_phase::side, .bottom = Lathe_cap::point, .top = Lathe_cap::point,
            .bottom_y = -0.5f * s, .top_y = 0.5f * s, .slot = mooring_metal_slot
        });
        if (coil) {
            add_rope_coil(builder, random, place, 0.31f * s, rope, (0.02f * s) + rope, segments);
        }
    } else if (form == Mooring_form::post) {
        // A timber post under an iron band and an iron cap.
        const float r    = 0.2f * s;
        const float band = 0.018f * s;
        const Placement place = make_leaning_placement(glm::vec3{0.0f}, lean);
        const std::array<Lathe_ring, 9> rings{{
            {r, 0.0f}, {r, 1.7f * s}, {r + band, 1.7f * s}, {r + band, 1.9f * s}, {r, 1.9f * s},
            {r, 2.2f * s}, {r + band, 2.2f * s}, {r + band, 2.32f * s}, {0.7f * r, mooring_post_height * s}
        }};
        constexpr std::array<uint32_t, 8> band_slots{
            mooring_wood_slot, mooring_metal_slot, mooring_metal_slot, mooring_metal_slot, mooring_wood_slot,
            mooring_metal_slot, mooring_metal_slot, mooring_metal_slot
        };
        add_lathe(builder, place, Lathe{.rings = rings, .segments = segments, .band_slots = band_slots});
        if (coil) {
            add_rope_coil(builder, random, place, r + (0.6f * rope), rope, 0.9f * s, segments);
        }
    } else {
        // Three piles leaning together, bound by an iron hoop.
        const float     r      = 0.2f * s;
        const float     spread = 0.34f * s;
        const float     top    = mooring_cluster_height * s;
        const float     turn   = random.next_range(0.0f, two_pi);
        const glm::vec3 ahead  = around_y(turn);
        for (int k = 0; k < 3; ++k) {
            const float     short_by = 0.1f * random.next_unit();
            const float     inward   = 0.55f + (0.1f * next_signed(random));
            const glm::vec3 out      = around_y(turn + (two_pi * static_cast<float>(k) / 3.0f));
            const float     pile_top = top * (1.0f - ((k == 0) ? 0.0f : short_by));
            const glm::vec2 pile_lean = lean - ((inward * spread / top) * glm::vec2{out.x, out.z});
            const Placement place = make_leaning_placement(spread * out, pile_lean);
            const std::array<Lathe_ring, 3> rings{{{r, 0.0f}, {0.92f * r, pile_top - (0.2f * s)}, {0.6f * r, pile_top}}};
            add_lathe(builder, place, Lathe{.rings = rings, .segments = segments, .slot = mooring_wood_slot});
        }
        // The hoop and the rope follow the three piles: wide at a pile, close between two.
        constexpr std::array<float, mooring_cluster> scales{1.0f, 0.88f, 0.78f, 0.88f, 1.0f, 0.88f, 0.78f, 0.88f, 1.0f, 0.88f, 0.78f, 0.88f};
        Placement place = make_leaning_placement(glm::vec3{0.0f}, lean);
        place.x = ahead;
        place.z = glm::cross(ahead, y_axis);
        const auto get_reach = [=](const float y) -> float {
            return (spread * (1.0f - (0.55f * y / top))) + (0.95f * r);
        };
        {
            const float low   = 3.0f * s;
            const float high  = 3.22f * s;
            const float outer = get_reach(low) + (0.03f * s);
            const float inner = get_reach(high) - (0.06f * s);
            const std::array<Lathe_ring, 4> rings{{{outer, low}, {outer, high}, {inner, high}, {inner, low}}};
            add_lathe(builder, place, Lathe{
                .rings = rings, .segments = mooring_cluster, .bottom = Lathe_cap::loop, .top = Lathe_cap::loop, .slot = mooring_metal_slot, .scales = scales
            });
        }
        if (coil) {
            add_rope_coil(builder, random, place, get_reach(2.3f * s) + (0.2f * rope), rope, 2.2f * s, mooring_cluster, scales);
        }
    }
    builder.write(mesh);
}

//
// Lamp post: a post on a plinth with a head on its top, or under a finial with a head on each of its arms.
//

constexpr uint32_t lamp_metal_slot    = 0;
constexpr uint32_t lamp_glow_slot     = 1;
constexpr int      lamp_arm_max       = 4;
constexpr int      lamp_globe         = 8;    // points around a globe
constexpr float    lamp_collar_radius = 1.2f; // the post's widest by the arms, in units
constexpr std::array<const char*, lamp_arm_max> c_lamp_light_names{"light_1", "light_2", "light_3", "light_4"};

enum class Lamp_head : unsigned int { lantern = 0, globe, hood };

[[nodiscard]] auto get_lamp_head(const std::string_view name) -> Lamp_head
{
    if (name == "globe") {
        return Lamp_head::globe;
    }
    if (name == "hood") {
        return Lamp_head::hood;
    }
    return Lamp_head::lantern;
}

// The lamp's sizes at the values; `unit` is the post's radius at its foot, and the heads are sized in it.
class Lamp_form
{
public:
    explicit Lamp_form(const Kit_parameter_values& values)
        : head      {get_lamp_head(values.get_enum("head"))}
        , arms      {std::clamp(values.get_int("arms"), 0, lamp_arm_max)}
        , segments  {std::max(values.get_int("segments"), 5)}
        , height    {values.get_float("height")}
        , arm_length{values.get_float("arm_length")}
    {
        unit      = std::clamp(0.028f * height, 0.045f, 0.16f);
        head_unit = unit;
        if (arms > 0) {
            // A head on an arm keeps clear of the post's collar: smaller heads on short arms of thick posts.
            head_unit = std::min(unit, (arm_length - (lamp_collar_radius * unit) - 0.01f) / get_head_radius());
        }
        if (arms == 0) {
            shaft_top = height - (get_head_height() * unit);
            arm_y     = shaft_top;
        } else {
            shaft_top = height - (3.0f * unit);
            arm_y     = shaft_top - (2.5f * unit);
        }
    }

    // A head on the post's top, in units: the lantern's point, the globe's top, the hood's point.
    [[nodiscard]] auto get_head_height() const -> float
    {
        return (head == Lamp_head::lantern) ? 4.6f : ((head == Lamp_head::globe) ? 3.95f : 3.6f);
    }
    // How far a head on an arm reaches from its anchor toward the post, in head units.
    [[nodiscard]] auto get_head_radius() const -> float
    {
        return (head == Lamp_head::lantern) ? 1.65f : ((head == Lamp_head::globe) ? 1.6f : 2.5f);
    }
    [[nodiscard]] auto get_head_count() const -> int
    {
        return std::max(arms, 1);
    }
    // The level direction of arm i; +X for the head on the post.
    [[nodiscard]] auto get_direction(const int i) const -> glm::vec3
    {
        if (arms == 0) {
            return x_axis;
        }
        const glm::vec2 point = get_kit_circle_point(i, arms);
        return glm::vec3{point.x, 0.0f, point.y};
    }
    [[nodiscard]] auto hangs() const -> bool
    {
        return (head == Lamp_head::hood) && (arms > 0);
    }
    // Where head i stands: on the post, on the arm's end, or (a hood) where it hangs from the arm's end.
    [[nodiscard]] auto get_anchor(const int i) const -> glm::vec3
    {
        if (arms == 0) {
            return glm::vec3{0.0f, shaft_top, 0.0f};
        }
        const float y = hangs() ? (arm_y - (2.0f * head_unit)) : (arm_y + (0.3f * unit));
        return (arm_length * get_direction(i)) + (y * y_axis);
    }
    // The light of head i: in the open between the glowing part and what covers it, under a hanging hood, over a globe.
    [[nodiscard]] auto get_light_position(const int i) const -> glm::vec3
    {
        float y = 0.0f;
        if (head == Lamp_head::lantern) {
            y = 2.72f;
        } else if (head == Lamp_head::globe) {
            y = get_head_height() + 0.3f;
        } else {
            y = hangs() ? -0.55f : 1.85f;
        }
        return get_anchor(i) + ((y * head_unit) * y_axis);
    }

    Lamp_head head      {Lamp_head::lantern};
    int       arms      {0};
    int       segments  {8};
    float     height    {0.0f};
    float     arm_length{0.0f};
    float     unit      {0.0f};
    float     head_unit {0.0f}; // the heads' unit: `unit`, or less where an arm is too short for a head of it
    float     shaft_top {0.0f}; // the post's top under a head or the finial
    float     arm_y     {0.0f}; // the arms' axis
};

// Rings of a lathe from (radius, height) pairs in units.
[[nodiscard]] auto make_lamp_rings(const std::span<const glm::vec2> profile, const float unit) -> std::vector<Lathe_ring>
{
    std::vector<Lathe_ring> rings;
    for (const glm::vec2& point : profile) {
        rings.push_back(Lathe_ring{point.x * unit, point.y * unit});
    }
    return rings;
}

// A four-sided lantern: a neck and a plate, a glowing core between four bars, eaves and a roof to a point.
void add_lamp_lantern(Kit_mesh_builder& builder, const Placement& place, const float m)
{
    const float root_2 = std::numbers::sqrt2_v<float>;
    builder.set_slot(lamp_metal_slot);
    add_placed_box(builder, place, glm::vec3{-0.5f * m, 0.0f, -0.5f * m}, glm::vec3{0.5f * m, 0.55f * m, 0.5f * m});
    add_placed_box(builder, place, glm::vec3{-1.15f * m, 0.5f * m, -1.15f * m}, glm::vec3{1.15f * m, 0.8f * m, 1.15f * m});
    {
        const std::array<Lathe_ring, 2> rings{{{0.72f * root_2 * m, 0.75f * m}, {1.05f * root_2 * m, 2.4f * m}}};
        add_lathe(builder, place, Lathe{.rings = rings, .segments = 4, .phase = Lathe_phase::side, .slot = lamp_glow_slot});
    }
    builder.set_slot(lamp_metal_slot);
    for (const float side_x : {-1.0f, 1.0f}) {
        for (const float side_z : {-1.0f, 1.0f}) {
            const glm::vec3 foot = place.at(glm::vec3{side_x * 0.98f * m, 0.7f * m, side_z * 0.98f * m});
            const glm::vec3 head = place.at(glm::vec3{side_x * 1.42f * m, 3.1f * m, side_z * 1.42f * m});
            builder.add_beam(foot, head, 0.2f * m, 0.2f * m);
        }
    }
    add_placed_box(builder, place, glm::vec3{-1.65f * m, 3.0f * m, -1.65f * m}, glm::vec3{1.65f * m, 3.22f * m, 1.65f * m});
    {
        const std::array<Lathe_ring, 3> rings{{{1.5f * root_2 * m, 3.2f * m}, {0.42f * root_2 * m, 4.02f * m}, {0.24f * root_2 * m, 4.22f * m}}};
        add_lathe(builder, place, Lathe{
            .rings = rings, .segments = 4, .phase = Lathe_phase::side, .top = Lathe_cap::point, .top_y = 4.6f * m, .slot = lamp_metal_slot
        });
    }
}

// A glowing globe in a cup.
void add_lamp_globe(Kit_mesh_builder& builder, const Placement& place, const float m, const int segments)
{
    {
        constexpr std::array<glm::vec2, 4> profile{{{0.7f, 0.0f}, {0.75f, 0.25f}, {1.15f, 0.7f}, {1.15f, 0.95f}}};
        const std::vector<Lathe_ring> rings = make_lamp_rings(profile, m);
        add_lathe(builder, place, Lathe{.rings = rings, .segments = segments, .slot = lamp_metal_slot});
    }
    {
        // Rings of a sphere of 1.6 units on the cup, between its two poles.
        const float radius = 1.6f * m;
        const float center = (0.75f * m) + radius;
        std::array<Lathe_ring, 5> rings{};
        for (std::size_t k = 0; k < rings.size(); ++k) {
            const float angle = pi * (static_cast<float>(k + 1) / static_cast<float>(rings.size() + 1));
            rings[k] = Lathe_ring{radius * std::sin(angle), center - (radius * std::cos(angle))};
        }
        add_lathe(builder, place, Lathe{
            .rings = rings, .segments = lamp_globe, .bottom = Lathe_cap::point, .top = Lathe_cap::point,
            .bottom_y = center - radius, .top_y = center + radius, .slot = lamp_glow_slot
        });
    }
}

// A hood on the post: a glowing bulb on a plate under a wide hat on four rods.
void add_lamp_hood_on_post(Kit_mesh_builder& builder, const Placement& place, const float m, const int segments)
{
    {
        constexpr std::array<glm::vec2, 3> profile{{{0.7f, 0.0f}, {1.2f, 0.25f}, {1.2f, 0.45f}}};
        const std::vector<Lathe_ring> rings = make_lamp_rings(profile, m);
        add_lathe(builder, place, Lathe{.rings = rings, .segments = segments, .slot = lamp_metal_slot});
    }
    {
        constexpr std::array<glm::vec2, 4> profile{{{0.55f, 0.4f}, {0.8f, 0.75f}, {0.8f, 1.15f}, {0.5f, 1.5f}}};
        const std::vector<Lathe_ring> rings = make_lamp_rings(profile, m);
        add_lathe(builder, place, Lathe{.rings = rings, .segments = segments, .slot = lamp_glow_slot});
    }
    builder.set_slot(lamp_metal_slot);
    for (const float side_x : {-1.0f, 1.0f}) {
        for (const float side_z : {-1.0f, 1.0f}) {
            const glm::vec3 foot = place.at(glm::vec3{side_x * 0.7f * m, 0.35f * m, side_z * 0.7f * m});
            const glm::vec3 head = place.at(glm::vec3{side_x * 1.3f * m, 2.3f * m, side_z * 1.3f * m});
            builder.add_beam(foot, head, 0.14f * m, 0.14f * m);
        }
    }
    {
        constexpr std::array<glm::vec2, 3> profile{{{2.6f, 2.2f}, {2.7f, 2.36f}, {0.5f, 3.3f}}};
        const std::vector<Lathe_ring> rings = make_lamp_rings(profile, m);
        add_lathe(builder, place, Lathe{.rings = rings, .segments = segments, .top = Lathe_cap::point, .top_y = 3.6f * m, .slot = lamp_metal_slot});
    }
}

// A hood hanging from an arm's end 2 units over the placement's origin: a stem, a shade and a glowing disc under it.
void add_lamp_hood_hanging(Kit_mesh_builder& builder, const Placement& place, const float m, const int segments)
{
    builder.set_slot(lamp_metal_slot);
    add_placed_box(builder, place, glm::vec3{-0.22f * m, 0.9f * m, -0.22f * m}, glm::vec3{0.22f * m, 2.2f * m, 0.22f * m});
    {
        constexpr std::array<glm::vec2, 4> profile{{{2.4f, 0.0f}, {2.5f, 0.16f}, {1.0f, 0.9f}, {0.45f, 1.05f}}};
        const std::vector<Lathe_ring> rings = make_lamp_rings(profile, m);
        add_lathe(builder, place, Lathe{.rings = rings, .segments = segments, .top = Lathe_cap::point, .top_y = 1.25f * m, .slot = lamp_metal_slot});
    }
    {
        constexpr std::array<glm::vec2, 2> profile{{{1.5f, -0.25f}, {1.75f, 0.05f}}};
        const std::vector<Lathe_ring> rings = make_lamp_rings(profile, m);
        add_lathe(builder, place, Lathe{.rings = rings, .segments = segments, .slot = lamp_glow_slot});
    }
}

void generate_lamp_post(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Lamp_form form{values};
    const float m = form.unit;
    const float h = form.height;
    Kit_mesh_builder builder;
    // The post: a plinth, a pedestal, a tapering shaft and a collar.
    {
        const std::array<Lathe_ring, 9> rings{{
            {2.0f * m, 0.0f}, {2.0f * m, 0.05f * h}, {1.4f * m, 0.07f * h}, {1.4f * m, 0.2f * h}, {1.0f * m, 0.22f * h},
            {0.75f * m, form.shaft_top - (1.2f * m)}, {lamp_collar_radius * m, form.shaft_top - (1.0f * m)}, {lamp_collar_radius * m, form.shaft_top - (0.4f * m)},
            {0.8f * m, form.shaft_top}
        }};
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = form.segments, .slot = lamp_metal_slot});
    }
    if (form.arms > 0) {
        // A finial on the post, and to every arm a beam and a strut under it.
        const std::array<Lathe_ring, 4> rings{{
            {0.6f * m, form.shaft_top - (0.1f * m)}, {0.95f * m, form.shaft_top + (0.9f * m)}, {0.95f * m, form.shaft_top + (1.5f * m)},
            {0.35f * m, form.shaft_top + (2.5f * m)}
        }};
        add_lathe(builder, Placement{}, Lathe{.rings = rings, .segments = form.segments, .top = Lathe_cap::point, .top_y = h, .slot = lamp_metal_slot});
        builder.set_slot(lamp_metal_slot);
        for (int i = 0; i < form.arms; ++i) {
            const glm::vec3 out  = form.get_direction(i);
            const glm::vec3 axis = form.arm_y * y_axis;
            const float     drop = std::min(0.5f * form.arm_length, 5.0f * m);
            builder.add_beam(axis + ((0.2f * m) * out), axis + ((form.arm_length + (0.5f * m)) * out), 0.5f * m, 0.6f * m);
            builder.add_beam(axis + ((0.6f * m) * out) - (drop * y_axis), axis + ((0.62f * form.arm_length) * out) - ((0.2f * m) * y_axis), 0.3f * m, 0.4f * m);
        }
    }
    for (int i = 0; i < form.get_head_count(); ++i) {
        const Placement place = make_turned_placement(form.get_anchor(i), form.get_direction(i));
        // A round head keeps its points on the axes: the lamp is its own mirror image about z = 0.
        const Placement upright{.origin = place.origin};
        const float     hm = form.head_unit;
        if (form.head == Lamp_head::lantern) {
            add_lamp_lantern(builder, place, hm);
        } else if (form.head == Lamp_head::globe) {
            add_lamp_globe(builder, upright, hm, form.segments);
        } else if (form.hangs()) {
            add_lamp_hood_hanging(builder, upright, hm, form.segments);
        } else {
            add_lamp_hood_on_post(builder, upright, hm, form.segments);
        }
    }
    builder.write(mesh);
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

} // anonymous namespace

void add_extra_01_08_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape;
        shape.id              = "prop.crate";
        shape.kit             = "prop";
        shape.display_name    = "Crate";
        shape.description     =
            "Slatted wooden crate at its origin: slats on the four sides between corner posts that stand out of them, a "
            "floor on two skids, and a lid of planks along X under two battens. The lid lies on the crate, stands open "
            "on its back edge (-Z), or is left out. Crates stack base on top.";
        shape.tags            = {"obstacle", "decoration", "prop", "crate", "stack", "size:0.75x0.75"};
        shape.parameters      = {
            from_seed_by_default(derive_float(make_length_parameter("width",  "Size along X", 0.75f, Kit_default_scale::modules, 0.3f, 4.0f), 0.5f, 1.25f, 0.25f)),
            from_seed_by_default(derive_float(make_length_parameter("height", "Size along Y; an open lid stands over it", 0.75f, Kit_default_scale::modules, 0.3f, 4.0f), 0.5f, 1.25f, 0.25f)),
            from_seed_by_default(derive_float(make_length_parameter("depth",  "Size along Z", 0.75f, Kit_default_scale::modules, 0.3f, 4.0f), 0.5f, 1.25f, 0.25f)),
            derive_int (make_count_parameter("slats", "Slats on a side and planks in the lid", 3, crate_slat_min, crate_slat_max), 2, 5),
            derive_enum(make_enum_parameter ("lid",   "The lid lies on the crate, stands open, or is left out", {"closed", "open", "none"}, 0)),
            make_seed_parameter()
        };
        shape.material_slots  = {"wood"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 300;
        shape.collision       = Kit_collision::convex_hull;
        shape.generator       = generate_crate;
        // A closed or missing lid leaves the box (U54); an open lid stands over its back edge.
        shape.get_collision   = [](const Kit_parameter_values& values) {
            return (values.get_enum("lid") == "open") ? Kit_collision::convex_hull : Kit_collision::box;
        };
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x};
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            add_base_and_top_connectors(out_connectors, Crate_form{values}.get_top());
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "prop.barrel";
        shape.kit             = "prop";
        shape.display_name    = "Barrel";
        shape.description     =
            "Wooden barrel at its origin: staves bulging between two heads that lie inside the chime, bound by iron "
            "hoops (slot metal). It stands upright, or lies along X on a side of its widest ring between two chocks.";
        shape.tags            = {"obstacle", "decoration", "prop", "barrel", "size:0.6x0.9"};
        shape.parameters      = {
            from_seed_by_default(derive_float(make_length_parameter("height", "From head to head", 0.9f, Kit_default_scale::meters, 0.4f, 2.0f), 0.7f, 1.2f, 0.05f)),
            from_seed_by_default(derive_float(make_length_parameter("radius", "Radius of the staves at the middle", 0.3f, Kit_default_scale::meters, 0.15f, 1.0f), 0.25f, 0.4f, 0.05f)),
            derive_float(make_share_parameter("bulge", "How much narrower the ends are than the middle", 0.12f, 0.0f, 0.2f), 0.06f, 0.18f, 0.02f),
            derive_int  (make_count_parameter("hoops", "Iron hoops", 4, barrel_hoop_min, barrel_hoop_max), 2, 4),
            derive_enum (make_enum_parameter ("pose",  "Standing, or lying along X between two chocks", {"upright", "lying"}, 0)),
            make_seed_parameter (),
            make_count_parameter("segments", "Staves around the barrel", 12, 8, 24)
        };
        shape.material_slots  = {"wood", "metal"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 620;
        shape.collision       = Kit_collision::convex_hull;
        shape.detail_counts   = {{"segments"}};
        shape.generator       = generate_barrel;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::z};
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            add_base_and_top_connectors(out_connectors, Barrel_form{values}.get_top());
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "ruin.broken_wall";
        shape.kit             = "ruin";
        shape.display_name    = "Broken wall";
        shape.description     =
            "Ruined wall along X with the parameters of the wall: full height at its start (-X), where it meets an intact "
            "wall, its top falling in columns toward its end (+X). The break is stepped (level tops in courses), "
            "jagged (teeth) or a slope with notches; intact is the share of the wall's face left standing.";
        shape.tags            = {"obstacle", "wall", "ruin", "tiles", "size:4x1"};
        shape.parameters      = {
            derive_float(make_length_parameter("length",    "Size along X", 4.0f,  Kit_default_scale::modules,      0.1f,  64.0f), 1.0f,  6.0f,  0.5f),
            derive_float(make_length_parameter("height",    "Size along Y at the start", 1.0f,  Kit_default_scale::wall_heights, 0.1f,  32.0f), 0.75f, 1.25f, 0.05f),
            derive_float(make_length_parameter("thickness", "Size along Z", 0.25f, Kit_default_scale::meters,       0.02f, 4.0f),  0.15f, 0.4f,  0.05f),
            from_seed_by_default(derive_float(make_share_parameter("intact", "Share of the wall's face left standing", 0.55f, 0.3f, 0.8f), 0.3f, 0.8f, 0.05f)),
            derive_enum (make_enum_parameter  ("break",  "Level tops in courses, teeth, or a slope with notches", {"stepped", "jagged", "slope"}, 0)),
            derive_float(make_length_parameter("course", "Height of a course: the steps of a stepped break, the teeth of a jagged one", 0.25f, Kit_default_scale::meters, 0.05f, 1.0f), 0.2f, 0.3f, 0.05f),
            derive_int  (make_count_parameter ("steps",  "Columns the top falls in", 8, broken_wall_step_min, broken_wall_step_max), 6, 12),
            make_seed_parameter()
        };
        shape.material_slots  = {"wall"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 100;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_broken_wall;
        shape.get_seam_sides  = [](const Kit_parameter_values&) { return std::vector<Kit_side>{Kit_side::x_neg, Kit_side::y_neg}; };
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float half_l = 0.5f * values.get_float("length");
            const float half_t = 0.5f * values.get_float("thickness");
            add_side_connector(out_connectors, "start", glm::vec3{-half_l, 0.0f, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "end",   glm::vec3{ half_l, 0.0f, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
            out_connectors.push_back(make_base_connector());
            add_side_connector(out_connectors, "front", glm::vec3{0.0f, 0.0f,  half_t}, glm::vec3{0.0f, 0.0f,  1.0f});
            add_side_connector(out_connectors, "back",  glm::vec3{0.0f, 0.0f, -half_t}, glm::vec3{0.0f, 0.0f, -1.0f});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "ruin.rubble";
        shape.kit             = "ruin";
        shape.display_name    = "Rubble";
        shape.description     =
            "Heap of tumbled ashlar blocks about its origin within a round footprint: half of the blocks lie flat "
            "around the foot of a low mound of debris, the others lie tilted on it. The heap's top is the height, a "
            "share of the radius.";
        shape.tags            = {"obstacle", "decoration", "ruin", "rubble", "size:2x0.4"};
        shape.parameters      = {
            from_seed_by_default(derive_float(make_length_parameter("radius", "Radius of the footprint", 1.0f, Kit_default_scale::modules, 0.3f, 8.0f), 0.5f, 2.0f, 0.25f)),
            from_seed_by_default(derive_float(make_share_parameter("height", "Height of the heap in radii", 0.4f, 0.2f, 0.6f), 0.2f, 0.6f, 0.05f)),
            derive_int  (make_count_parameter ("blocks",     "Blocks", 16, rubble_block_min, rubble_block_max), 8, 24),
            derive_float(make_length_parameter("block_size", "Length of a block; at most 0.7 of the radius", 0.4f, Kit_default_scale::meters, 0.1f, 1.5f), 0.25f, 0.5f, 0.05f),
            make_seed_parameter()
        };
        shape.material_slots  = {"stone"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 240;
        shape.collision       = Kit_collision::convex_hull;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_rubble;
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "traversal.ladder";
        shape.kit             = "traversal";
        shape.display_name    = "Ladder";
        shape.description     =
            "Ladder standing on its origin: two rails with level feet and tops, and rungs in equal steps of at most "
            "the rung spacing. It stands upright or leans toward -Z, its top connector on the rails' tops facing -Z.";
        shape.tags            = {"traversal", "ladder", "climb", "size:0.5x3"};
        shape.parameters      = {
            derive_float(make_length_parameter("height", "Size along Y", 1.0f, Kit_default_scale::wall_heights, 0.5f, 12.0f), 0.75f, 1.25f, 0.05f),
            derive_float(make_length_parameter("width",  "Size along X", 0.5f, Kit_default_scale::modules,      0.3f, 1.5f),  0.4f,  0.6f,  0.05f),
            // In meters and not derived: what climbs the ladder fixes the step at every level length (R2.10).
            make_length_parameter("rung_spacing", "The most from a rung to the next", 0.3f, Kit_default_scale::meters, 0.15f, 0.5f),
            derive_float(make_angle_parameter("lean", "How far the ladder leans toward -Z from upright", 10.0f, 0.0f, 20.0f), 0.0f, 20.0f, 5.0f),
            make_seed_parameter()
        };
        shape.material_slots  = {"wood"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 140;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.generator       = generate_ladder;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x};
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const Ladder_form form{values};
            out_connectors.push_back(Kit_connector{.name = "bottom", .position = glm::vec3{0.0f}, .outward = glm::vec3{0.0f, -1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
            add_side_connector(out_connectors, "top", glm::vec3{0.0f, form.height, -form.run}, glm::vec3{0.0f, 0.0f, -1.0f});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "harbour.pier";
        shape.kit             = "harbour";
        shape.display_name    = "Pier";
        shape.description     =
            "Timber pier along X: planks across two stringers, on bents of two round piles under a cap beam, a bent "
            "to every pile spacing. The piles stand on the base and reach pile_depth up to the walking surface, so "
            "the piece is sunk into the water by what the piles stand in. Cross braces tie the piles of a bent. With "
            "plank_jitter the planks' ends stand back unevenly.";
        shape.tags            = {"walkable", "pier", "harbour", "floor", "tiles", "size:2x2"};
        shape.parameters      = {
            derive_float(make_length_parameter("length", "Size along X", 2.0f, Kit_default_scale::modules, 1.0f, 16.0f), 2.0f, 6.0f, 1.0f),
            // In meters and not derived: the agent fixes the least width at every level length (R2.10).
            make_length_parameter("width", "Size along Z", 2.0f, Kit_default_scale::meters, 1.2f, 8.0f),
            derive_float(make_length_parameter("pile_depth",   "Height of the walking surface over the piles' feet", 1.0f, Kit_default_scale::wall_heights, 0.5f, 16.0f), 0.5f, 1.25f, 0.25f),
            derive_float(make_length_parameter("pile_spacing", "From a bent of piles to the next", 2.0f, Kit_default_scale::meters, 1.0f, 8.0f), 1.5f, 3.0f, 0.5f),
            derive_enum (make_enum_parameter  ("braces",       "Cross braces between the piles of a bent", {"none", "cross"}, 1)),
            from_seed_by_default(derive_float(make_ratio_parameter("plank_jitter", "How unevenly the planks' ends stand back", 0.4f), 0.2f, 1.0f, 0.1f)),
            make_seed_parameter (),
            make_count_parameter("segments", "Points around a pile", 8, 6, 12)
        };
        shape.material_slots    = {"wood"};
        shape.normal_style      = Kit_normal_style::faceted;
        shape.triangle_budget   = 220;
        shape.collision         = Kit_collision::triangle_mesh;
        shape.randomness        = Kit_randomness::seeded;
        shape.detail_counts     = {{"segments"}};
        shape.generator         = generate_pier;
        shape.check_walkability = check_pier_walkability;
        shape.get_connectors    = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const Pier_form form{values};
            add_side_connector(out_connectors, "start", glm::vec3{-0.5f * form.length, form.deck, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "end",   glm::vec3{ 0.5f * form.length, form.deck, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "left",  glm::vec3{0.0f, form.deck, -form.half_w}, glm::vec3{0.0f, 0.0f, -1.0f});
            add_side_connector(out_connectors, "right", glm::vec3{0.0f, form.deck,  form.half_w}, glm::vec3{0.0f, 0.0f,  1.0f});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "harbour.mooring";
        shape.kit             = "harbour";
        shape.display_name    = "Mooring";
        shape.description     =
            "Mooring fixture at its origin. Bollard: a cast bollard one size high on a timber pad. Cleat: a horn "
            "cleat one size long along X on a timber pad. Post: a timber post 2.4 sizes high under an iron band and "
            "cap. Pile cluster: three piles 4 sizes high leaning together, bound by an iron hoop. Each leans in a "
            "seeded direction, and takes three turns of rope (slot wood).";
        shape.tags            = {"obstacle", "decoration", "harbour", "mooring", "size:0.5x0.5"};
        shape.parameters      = {
            derive_enum(make_enum_parameter("form", "Bollard, cleat, post or pile cluster", {"bollard", "cleat", "post", "pile_cluster"}, 0)),
            derive_float(make_length_parameter("size", "A bollard's height and a cleat's length; a post is 2.4 and a pile cluster 4 of it high", 0.5f, Kit_default_scale::meters, 0.2f, 2.0f), 0.4f, 0.6f, 0.05f),
            from_seed_by_default(derive_float(make_angle_parameter("lean", "How far it leans from upright", 3.0f, 0.0f, 8.0f), 1.0f, 8.0f, 1.0f)),
            derive_enum(make_enum_parameter("rope", "Three turns of rope around it", {"none", "coil"}, 0)),
            make_seed_parameter (),
            make_count_parameter("segments", "Points around the round parts", 10, 6, 16)
        };
        shape.material_slots  = {"metal", "wood"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 200;
        shape.collision       = Kit_collision::convex_hull;
        shape.randomness      = Kit_randomness::seeded;
        shape.detail_counts   = {{"segments"}};
        shape.generator       = generate_mooring;
        shape.get_connectors  = [](const Kit_parameter_values&, std::vector<Kit_connector>& out_connectors) {
            out_connectors.push_back(make_base_connector());
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "light.lamp_post";
        shape.kit             = "light";
        shape.display_name    = "Lamp post";
        shape.description     =
            "Lamp post at its origin: a post on a plinth with a head on its top, or under a finial with a head on "
            "each of one to four arms, the first toward +X. Lantern: a glowing core between four bars under a "
            "pointed roof. Globe: a glowing globe in a cup. Hood: a glowing bulb under a wide hat on the post, a "
            "shade over a glowing disc hanging from an arm. Each head carries one light: a point light, a spot "
            "shining down under a hood.";
        shape.tags            = {"decoration", "obstacle", "light", "lamp", "size:0.4x3.6"};
        shape.parameters      = {
            derive_float(make_length_parameter("height", "Size along Y", 1.2f, Kit_default_scale::wall_heights, 1.5f, 12.0f), 1.0f, 1.4f, 0.1f),
            derive_int  (make_count_parameter ("arms",   "Arms with a head each; none: one head on the post", 0, 0, lamp_arm_max), 0, 4),
            derive_float(make_length_parameter("arm_length", "From the post's axis to a head on an arm", 0.6f, Kit_default_scale::meters, 0.3f, 2.0f), 0.5f, 0.9f, 0.1f),
            derive_enum (make_enum_parameter  ("head",   "Lantern, globe or hood", {"lantern", "globe", "hood"}, 0)),
            make_seed_parameter (),
            make_count_parameter("segments", "Points around the post", 8, 6, 16)
        };
        shape.material_slots   = {"metal", "glow"};
        shape.normal_style     = Kit_normal_style::faceted;
        shape.triangle_budget  = 270;
        shape.collision        = Kit_collision::convex_hull;
        shape.detail_counts    = {{"segments"}};
        shape.generator        = generate_lamp_post;
        shape.mirror_planes    = std::vector<Kit_mirror_plane>{Kit_mirror_plane::z};
        shape.get_connectors   = [](const Kit_parameter_values&, std::vector<Kit_connector>& out_connectors) {
            out_connectors.push_back(make_base_connector());
        };
        shape.get_light_frames = [](const Kit_parameter_values& values, std::vector<Kit_light_frame>& out_frames) {
            const Lamp_form form{values};
            Kit_light_start start{};
            if (form.head == Lamp_head::hood) {
                start.type             = Kit_light_type::spot;
                start.inner_spot_angle = 1.2f;
                start.outer_spot_angle = 2.0f;
            }
            for (int i = 0; i < form.get_head_count(); ++i) {
                out_frames.push_back(Kit_light_frame{
                    .name      = c_lamp_light_names[static_cast<std::size_t>(i)],
                    .position  = form.get_light_position(i),
                    .direction = glm::vec3{0.0f, -1.0f, 0.0f},
                    .start     = start
                });
            }
        };
        shapes.push_back(std::move(shape));
    }
}

} // namespace editor
