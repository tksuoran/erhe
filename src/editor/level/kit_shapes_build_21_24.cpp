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
#include <cstdlib>
#include <initializer_list>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The shapes of build-order lines 21 - 24 of doc/plans/level_kit_more_shapes.md (doc/editor/level_tools.md).
// Every mesh's topology follows from its integer values and integer draws alone; floats only move vertices.

namespace editor {

namespace {

constexpr float pi           = std::numbers::pi_v<float>;
constexpr float two_pi       = 2.0f * pi;
constexpr float golden_angle = 2.39996323f;
constexpr glm::vec3 x_axis{1.0f, 0.0f, 0.0f};
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

// A box on base_center turned about the upright through it; size holds half the width, the height and half the depth.
void add_turned_box(Kit_mesh_builder& builder, const glm::vec3& base_center, const glm::vec3& size, const float yaw)
{
    const glm::vec3 ax = size.x * around_y(yaw);
    const glm::vec3 az = size.z * glm::vec3{std::sin(yaw), 0.0f, std::cos(yaw)};
    const glm::vec3 up = size.y * y_axis;
    const uint32_t v0 = builder.add_vertex(base_center - ax - az);
    const uint32_t v1 = builder.add_vertex(base_center + ax - az);
    const uint32_t v2 = builder.add_vertex(base_center - ax - az + up);
    const uint32_t v3 = builder.add_vertex(base_center - ax + az);
    const uint32_t v4 = builder.add_vertex(base_center + ax - az + up);
    const uint32_t v5 = builder.add_vertex(base_center + ax + az);
    const uint32_t v6 = builder.add_vertex(base_center - ax + az + up);
    const uint32_t v7 = builder.add_vertex(base_center + ax + az + up);
    builder.add_quad(v1, v4, v7, v5);
    builder.add_quad(v2, v6, v7, v4);
    builder.add_quad(v3, v5, v7, v6);
    builder.add_quad(v0, v3, v6, v2);
    builder.add_quad(v0, v1, v5, v3);
    builder.add_quad(v0, v2, v4, v1);
}

// Facets of a limb's bands that take a slot of their own: the bands from first_band, the strips from first_point.
class Limb_patch
{
public:
    uint32_t first_band {0};
    uint32_t band_count {0};
    uint32_t first_point{0};
    uint32_t point_count{0};
    uint32_t slot       {0};
};

// A closed limb: rings of point_count points along its axis, each counter-clockwise seen from the limb's end, closed by
// fans to the two end points. slots: the start fan's, the band's after each ring but the last, the end fan's.
void add_limb(
    Kit_mesh_builder&                builder,
    const std::span<const glm::vec3> ring_points,
    const int                        point_count,
    const glm::vec3&                 start_point,
    const glm::vec3&                 end_point,
    const std::span<const uint32_t>  slots,
    const Limb_patch&                patch = Limb_patch{}
)
{
    const uint32_t n     = static_cast<uint32_t>(point_count);
    const uint32_t rings = static_cast<uint32_t>(ring_points.size()) / n;
    const uint32_t first = static_cast<uint32_t>(builder.get_positions().size());
    for (const glm::vec3& point : ring_points) {
        builder.add_vertex(point);
    }
    const uint32_t start = builder.add_vertex(start_point);
    const uint32_t end   = builder.add_vertex(end_point);
    builder.set_slot(slots[0]);
    for (uint32_t i = 0; i < n; ++i) {
        builder.add_triangle(start, first + ((i + 1) % n), first + i);
    }
    for (uint32_t r = 0; (r + 1) < rings; ++r) {
        const uint32_t lower = first + (r * n);
        const uint32_t upper = lower + n;
        const bool in_bands = (r >= patch.first_band) && (r < (patch.first_band + patch.band_count));
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t j = (i + 1) % n;
            const bool in_patch = in_bands && (i >= patch.first_point) && (i < (patch.first_point + patch.point_count));
            builder.set_slot(in_patch ? patch.slot : slots[r + 1]);
            builder.add_triangle(lower + i, lower + j, upper + j);
            builder.add_triangle(lower + i, upper + j, upper + i);
        }
    }
    const uint32_t last = first + ((rings - 1) * n);
    builder.set_slot(slots[rings]);
    for (uint32_t i = 0; i < n; ++i) {
        builder.add_triangle(last + i, last + ((i + 1) % n), end);
    }
}

//
// Bridge deck: a slab along X under paving stones between kerbs or parapets, on beams, on piers under beams, or on
// arches between piers. Half piers and half posts stand at the ends: two decks joined make whole ones.
//

constexpr uint32_t bridge_stone_slot = 0;
constexpr uint32_t bridge_floor_slot = 1;
constexpr uint32_t bridge_trim_slot  = 2;
constexpr int      bridge_span_max   = 4;
constexpr int      bridge_course_max = 16;

enum class Bridge_supports : unsigned int { none = 0, piers, arched };
enum class Bridge_parapet  : unsigned int { none = 0, solid, open };

[[nodiscard]] auto get_bridge_supports(const std::string_view name) -> Bridge_supports
{
    if (name == "piers") {
        return Bridge_supports::piers;
    }
    if (name == "arched") {
        return Bridge_supports::arched;
    }
    return Bridge_supports::none;
}

[[nodiscard]] auto get_bridge_parapet(const std::string_view name) -> Bridge_parapet
{
    if (name == "solid") {
        return Bridge_parapet::solid;
    }
    if (name == "open") {
        return Bridge_parapet::open;
    }
    return Bridge_parapet::none;
}

// The deck's sizes at the values; every member shrinks with a short, narrow or low deck.
class Bridge_form
{
public:
    explicit Bridge_form(const Kit_parameter_values& values)
        : supports      {get_bridge_supports(values.get_enum("supports"))}
        , parapet       {get_bridge_parapet(values.get_enum("parapet"))}
        , spans         {std::clamp(values.get_int("spans"), 1, bridge_span_max)}
        , courses       {std::clamp(values.get_int("courses"), 1, bridge_course_max)}
        , length        {values.get_float("length")}
        , half_l        {0.5f * length}
        , half_w        {0.5f * values.get_float("width")}
        , parapet_height{values.get_float("parapet_height")}
    {
        const float thickness = values.get_float("thickness");
        bay  = length / static_cast<float>(spans);
        pier = std::min(std::clamp(0.16f * bay, 0.3f, 0.9f), 0.3f * bay);
        lip  = std::clamp(0.06f * half_w, 0.03f, 0.08f);
        wall = std::clamp(0.15f * half_w, 0.1f, 0.3f);
        if (supports == Bridge_supports::none) {
            beam  = std::clamp(0.8f * thickness, 0.1f, 0.5f);
            slab  = thickness;
            under = beam;
        } else {
            const float height = values.get_float("height");
            slab  = std::min(thickness, 0.4f * height);
            under = height - slab;
            beam  = std::min(0.8f * slab, 0.2f * under);
        }
        top    = under + slab;
        paving = std::min(0.05f, 0.25f * slab);
        inner  = half_w - lip - wall;
    }

    // The upright through the middle of support i, 0 to spans.
    [[nodiscard]] auto get_support_x(const int i) const -> float { return -half_l + (bay * static_cast<float>(i)); }

    // What the agent walks between: the kerbs' outer edges, or the posts of a parapet.
    [[nodiscard]] auto get_clear_width() const -> float
    {
        return (parapet == Bridge_parapet::none) ? (2.0f * half_w) : (2.0f * (inner - lip));
    }

    Bridge_supports supports      {Bridge_supports::arched};
    Bridge_parapet  parapet       {Bridge_parapet::solid};
    int             spans         {1};
    int             courses       {1};
    float           length        {0.0f};
    float           half_l        {0.0f};
    float           half_w        {0.0f};
    float           parapet_height{0.0f};
    float           bay           {0.0f}; // from support to support
    float           pier          {0.0f}; // a whole pier's size along X
    float           lip           {0.0f}; // the step between two members that stand out of one another
    float           wall          {0.0f}; // thickness of a kerb and a parapet
    float           beam          {0.0f}; // height of the beams under the slab
    float           slab          {0.0f};
    float           under         {0.0f}; // the slab's underside
    float           top           {0.0f}; // the walking surface
    float           paving        {0.0f}; // thickness of the paving stones above the slab
    float           inner         {0.0f}; // z of a kerb's inner face
};

// The solid between two polylines from the right foot to the left, `lower` inside `upper`, through z = +-half_z;
// segment i takes slots[i % 2].
void add_arch_band(
    Kit_mesh_builder&                builder,
    const std::span<const glm::vec2> lower,
    const std::span<const glm::vec2> upper,
    const float                      half_z,
    const std::array<uint32_t, 2>&   slots
)
{
    const std::size_t count = lower.size();
    std::vector<uint32_t> fl(count);
    std::vector<uint32_t> fu(count);
    std::vector<uint32_t> bl(count);
    std::vector<uint32_t> bu(count);
    for (std::size_t i = 0; i < count; ++i) {
        fl[i] = builder.add_vertex(glm::vec3{lower[i],  half_z});
        fu[i] = builder.add_vertex(glm::vec3{upper[i],  half_z});
        bl[i] = builder.add_vertex(glm::vec3{lower[i], -half_z});
        bu[i] = builder.add_vertex(glm::vec3{upper[i], -half_z});
    }
    builder.set_slot(slots[0]);
    builder.add_quad(fu[0], fl[0], bl[0], bu[0]);
    for (std::size_t i = 0; (i + 1) < count; ++i) {
        builder.set_slot(slots[i % 2]);
        builder.add_quad(fl[i],     fu[i],     fu[i + 1], fl[i + 1]);
        builder.add_quad(bl[i],     bl[i + 1], bu[i + 1], bu[i]);
        builder.add_quad(fl[i],     fl[i + 1], bl[i + 1], bl[i]);
        builder.add_quad(fu[i + 1], fu[i],     bu[i],     bu[i + 1]);
    }
    builder.set_slot(slots[(count - 2) % 2]);
    builder.add_quad(fl[count - 1], fu[count - 1], bu[count - 1], bl[count - 1]);
}

// The x range of what stands on support i with the given half size, reaching `over` further on the sides that are
// not the deck's ends: half of it at an end.
[[nodiscard]] auto get_bridge_support_range(const Bridge_form& form, const int i, const float half_size, const float over) -> glm::vec2
{
    const float x = form.get_support_x(i);
    return glm::vec2{(i == 0) ? x : (x - half_size - over), (i == form.spans) ? x : (x + half_size + over)};
}

void add_bridge_paving(Kit_mesh_builder& builder, const Bridge_form& form)
{
    builder.set_slot(bridge_floor_slot);
    const float course = form.length / static_cast<float>(form.courses);
    const float gap    = std::min(0.015f, 0.06f * course);
    const float half_p = form.inner - gap;
    const float low    = form.top - (1.5f * form.paving);
    for (int k = 0; k < form.courses; ++k) {
        const float x0 = -form.half_l + (course * static_cast<float>(k)) + (0.5f * gap);
        const float x1 = -form.half_l + (course * static_cast<float>(k + 1)) - (0.5f * gap);
        // Courses of two and of three stones alternate from both ends, so the joints break and the deck mirrors.
        const bool three = (std::min(k, form.courses - 1 - k) % 2) == 1;
        if (three) {
            const float third = half_p / 3.0f;
            builder.add_box(glm::vec3{x0, low, -half_p},               glm::vec3{x1, form.top, -third - (0.5f * gap)});
            builder.add_box(glm::vec3{x0, low, -third + (0.5f * gap)}, glm::vec3{x1, form.top,  third - (0.5f * gap)});
            builder.add_box(glm::vec3{x0, low,  third + (0.5f * gap)}, glm::vec3{x1, form.top,  half_p});
        } else {
            builder.add_box(glm::vec3{x0, low, -half_p},     glm::vec3{x1, form.top, -0.5f * gap});
            builder.add_box(glm::vec3{x0, low,  0.5f * gap}, glm::vec3{x1, form.top,  half_p});
        }
    }
}

// The kerb, wall or curb of the side (+1 or -1) with its posts, copings or rails.
void add_bridge_side(Kit_mesh_builder& builder, const Bridge_form& form, const float side)
{
    const float z_in   = form.inner;
    const float z_out  = form.inner + form.wall;
    const float low    = form.top - (1.5f * form.paving);
    const float height = form.parapet_height;
    const auto add_side_box = [&builder, side](const float x0, const float x1, const float y0, const float y1, const float z0, const float z1) {
        builder.add_box(glm::vec3{x0, y0, std::min(side * z0, side * z1)}, glm::vec3{x1, y1, std::max(side * z0, side * z1)});
    };
    builder.set_slot(bridge_stone_slot);
    if (form.parapet == Bridge_parapet::none) {
        add_side_box(-form.half_l, form.half_l, low, form.top, z_in, z_out);
        return;
    }
    const float coping   = std::min(0.08f, 0.2f * height);
    const float curb     = std::min(0.12f, 0.15f * height);
    const float post     = 0.45f * form.pier; // half a post's size along X
    const float post_top = form.top + height + (0.5f * coping);
    const float post_cap = std::min(0.06f, 0.12f * height);
    const float wall_top = (form.parapet == Bridge_parapet::solid) ? (form.top + height - coping) : (form.top + curb);
    add_side_box(-form.half_l, form.half_l, low, wall_top, z_in, z_out);
    for (int i = 0; i <= form.spans; ++i) {
        const glm::vec2 shaft = get_bridge_support_range(form, i, post, 0.0f);
        const glm::vec2 cap   = get_bridge_support_range(form, i, post, 0.3f * form.lip);
        builder.set_slot(bridge_stone_slot);
        add_side_box(shaft.x, shaft.y, form.top - (1.2f * form.paving), post_top, z_in - form.lip, z_out + (0.6f * form.lip));
        builder.set_slot(bridge_trim_slot);
        add_side_box(cap.x, cap.y, post_top, post_top + post_cap, z_in - (1.3f * form.lip), form.half_w);
    }
    const float embed = 0.2f * post;
    for (int i = 0; i < form.spans; ++i) {
        // What runs between two posts reaches into both.
        const float x0 = get_bridge_support_range(form, i,     post, 0.0f).y - embed;
        const float x1 = get_bridge_support_range(form, i + 1, post, 0.0f).x + embed;
        builder.set_slot(bridge_trim_slot);
        if (form.parapet == Bridge_parapet::solid) {
            add_side_box(x0, x1, form.top + height - coping, form.top + height, z_in - (0.5f * form.lip), z_out + (0.5f * form.lip));
            continue;
        }
        add_side_box(x0, x1, form.top + (0.82f * height), form.top + (0.94f * height), z_in + (0.15f * form.wall), z_out - (0.15f * form.wall));
        add_side_box(x0, x1, form.top + (0.46f * height), form.top + (0.54f * height), z_in + (0.3f * form.wall),  z_out - (0.3f * form.wall));
        const int   balusters = (form.spans <= 2) ? 3 : 2;
        const float half      = std::min(0.22f * form.wall, 0.1f * (x1 - x0));
        const float middle    = 0.5f * (z_in + z_out);
        for (int k = 0; k < balusters; ++k) {
            const float x = x0 + embed + ((x1 - x0 - (2.0f * embed)) * static_cast<float>(k + 1) / static_cast<float>(balusters + 1));
            add_side_box(x - half, x + half, form.top + (0.5f * curb), form.top + (0.86f * height), middle - half, middle + half);
        }
    }
}

// The beams along the deck under its slab.
void add_bridge_beams(Kit_mesh_builder& builder, const Bridge_form& form)
{
    const float half = std::clamp(0.12f * form.half_w, 0.05f, 0.15f);
    builder.set_slot(bridge_trim_slot);
    for (const float side : {-1.0f, 1.0f}) {
        const float z = side * 0.5f * form.half_w;
        builder.add_box(glm::vec3{-form.half_l, form.under - form.beam, z - half}, glm::vec3{form.half_l, form.under, z + half});
    }
}

// Piers on plinths up to shaft_top, each with a band of trim.
void add_bridge_piers(Kit_mesh_builder& builder, const Bridge_form& form, const float shaft_top, const float band_low, const float band_high)
{
    const float plinth = std::min(0.25f, 0.15f * form.under);
    const float over   = std::min(form.lip, 0.2f * form.pier);
    for (int i = 0; i <= form.spans; ++i) {
        const glm::vec2 shaft = get_bridge_support_range(form, i, 0.5f * form.pier, 0.0f);
        const glm::vec2 wide  = get_bridge_support_range(form, i, 0.5f * form.pier, over);
        builder.set_slot(bridge_stone_slot);
        builder.add_box(glm::vec3{wide.x,  0.0f,          -(form.half_w - (0.5f * form.lip))}, glm::vec3{wide.y,  plinth,    form.half_w - (0.5f * form.lip)});
        builder.add_box(glm::vec3{shaft.x, 0.5f * plinth, -(form.half_w - (1.5f * form.lip))}, glm::vec3{shaft.y, shaft_top, form.half_w - (1.5f * form.lip)});
        builder.set_slot(bridge_trim_slot);
        builder.add_box(glm::vec3{wide.x,  band_low,      -(form.half_w - form.lip)},          glm::vec3{wide.y,  band_high, form.half_w - form.lip});
    }
}

// Segments of an arch: the more spans, the fewer; odd, so the voussoirs' slots mirror about the crown.
[[nodiscard]] auto get_bridge_arch_segments(const int spans) -> int
{
    constexpr std::array<int, bridge_span_max> segments{15, 11, 9, 7};
    return segments[static_cast<std::size_t>(spans - 1)];
}

// One span's arch: the opening's half width, the rise, the springing line and the voussoirs' depth.
class Bridge_arch
{
public:
    explicit Bridge_arch(const Bridge_form& form)
        : half  {0.5f * (form.bay - form.pier)}
        , crown {std::min(std::clamp(0.3f * half, 0.12f, 0.5f), 0.3f * form.under)}
        , rise  {std::min(half, (0.85f * form.under) - crown)}
        , spring{form.under - crown - rise}
        , depth {std::min(0.75f * crown, 0.4f * form.pier)}
    {
    }

    float half  {0.0f};
    float crown {0.0f}; // what lies over the opening's crown under the slab
    float rise  {0.0f};
    float spring{0.0f};
    float depth {0.0f};
};

void add_bridge_arches(Kit_mesh_builder& builder, const Bridge_form& form)
{
    const Bridge_arch arch{form};
    const int   segments = get_bridge_arch_segments(form.spans);
    const float band     = std::min(0.12f, 0.25f * arch.spring);
    add_bridge_piers(builder, form, form.under, arch.spring - band, arch.spring);
    for (int b = 0; b < form.spans; ++b) {
        const glm::vec2 foot{form.get_support_x(b) + (0.5f * form.bay), arch.spring};
        std::vector<glm::vec2> inner  = make_kit_arch_head_points(Kit_arch_head::round, arch.half,                       arch.rise,                       segments);
        std::vector<glm::vec2> outer  = make_kit_arch_head_points(Kit_arch_head::round, arch.half + arch.depth,          arch.rise + arch.depth,          segments);
        std::vector<glm::vec2> middle = make_kit_arch_head_points(Kit_arch_head::round, arch.half + (0.5f * arch.depth), arch.rise + (0.5f * arch.depth), segments);
        std::vector<glm::vec2> level(middle.size());
        for (std::size_t i = 0; i < inner.size(); ++i) {
            inner [i] += foot;
            outer [i] += foot;
            middle[i] += foot;
            level [i]  = glm::vec2{middle[i].x, form.under};
        }
        // The voussoirs stand out of the spandrel wall, which fills from their middle up to the slab.
        add_arch_band(builder, inner,  outer, form.half_w - (2.0f * form.lip), {bridge_trim_slot, bridge_stone_slot});
        add_arch_band(builder, middle, level, form.half_w - (3.0f * form.lip), {bridge_stone_slot, bridge_stone_slot});
        const float chord  = arch.half * pi / static_cast<float>(segments);
        const float low    = arch.spring + arch.rise - (0.15f * arch.depth);
        const float high   = arch.spring + arch.rise + arch.depth + (0.2f * arch.crown);
        const float narrow = 0.4f * chord;
        const float wide   = narrow + (0.25f * arch.depth);
        const float half_z = form.half_w - (1.7f * form.lip);
        const std::array<glm::vec3, 4> keystone{{
            {foot.x - narrow, low, half_z}, {foot.x + narrow, low, half_z}, {foot.x + wide, high, half_z}, {foot.x - wide, high, half_z}
        }};
        builder.set_slot(bridge_trim_slot);
        add_prism_between(builder, keystone, glm::vec3{0.0f, 0.0f, -2.0f * half_z});
    }
}

void generate_bridge_deck(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Bridge_form form{values};
    Kit_mesh_builder builder;
    builder.set_slot(bridge_trim_slot);
    builder.add_box(glm::vec3{-form.half_l, form.under, -form.half_w}, glm::vec3{form.half_l, form.top - form.paving, form.half_w});
    add_bridge_paving(builder, form);
    add_bridge_side(builder, form,  1.0f);
    add_bridge_side(builder, form, -1.0f);
    if (form.supports == Bridge_supports::none) {
        add_bridge_beams(builder, form);
        // Cross beams under the slab at the supports' places.
        builder.set_slot(bridge_trim_slot);
        for (int i = 0; i <= form.spans; ++i) {
            const glm::vec2 range = get_bridge_support_range(form, i, 0.25f * form.pier, 0.0f);
            builder.add_box(glm::vec3{range.x, 0.35f * form.beam, -(form.half_w - (2.0f * form.lip))}, glm::vec3{range.y, form.under, form.half_w - (2.0f * form.lip)});
        }
    } else if (form.supports == Bridge_supports::piers) {
        const float cap = std::min(0.15f, 0.1f * form.under);
        add_bridge_beams(builder, form);
        add_bridge_piers(builder, form, form.under - form.beam - (0.5f * cap), form.under - form.beam - cap, form.under - form.beam);
    } else {
        add_bridge_arches(builder, form);
    }
    builder.write(mesh);
}

[[nodiscard]] auto check_bridge_walkability(const Kit_parameter_values& values, const Kit_agent& agent) -> std::string
{
    const Bridge_form form{values};
    const float clear     = form.get_clear_width();
    const float min_width = (2.0f * agent.radius) + kit_opening_margin;
    if (clear < min_width) {
        return fmt::format("{}: the deck's clear width {} m is narrower than the agent's {} m", values.get_shape()->id, clear, min_width);
    }
    return {};
}

//
// Buttress: stages of stone against a wall face at z = -depth / 2, each standing less far out than the one below under
// a sloping offset, on a plinth; the last under a slope back to the wall, a gablet or a pinnacle.
//

constexpr uint32_t buttress_stone_slot = 0;
constexpr uint32_t buttress_trim_slot  = 1;
constexpr int      buttress_step_max   = 4;

enum class Buttress_cap : unsigned int { weathered = 0, gabled, pinnacle };

[[nodiscard]] auto get_buttress_cap(const std::string_view name) -> Buttress_cap
{
    if (name == "gabled") {
        return Buttress_cap::gabled;
    }
    if (name == "pinnacle") {
        return Buttress_cap::pinnacle;
    }
    return Buttress_cap::weathered;
}

void generate_buttress(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float        height = values.get_float("height");
    const float        depth  = values.get_float("depth");
    const float        width  = values.get_float("width");
    const int          steps  = std::clamp(values.get_int("steps"), 1, buttress_step_max);
    const Buttress_cap cap    = get_buttress_cap(values.get_enum("cap"));
    Kit_mesh_builder builder;

    const float back    = -0.5f * depth;
    const float chamfer = std::min({0.06f, 0.08f * depth, 0.1f * width});
    const float plinth  = std::min(0.12f * height, 0.35f);
    const float half_w  = (0.5f * width) - chamfer;        // the lowest stage's
    const float narrow  = std::min(0.025f, 0.04f * width); // each stage is this much narrower on both sides
    const float reach   = depth - chamfer;                 // the lowest stage's, from the wall face
    const float drip    = 0.5f * chamfer;

    // The front's z and the half width of stage k.
    const auto get_front = [back, reach, steps](const int k) -> float {
        return back + (reach * (1.0f - (0.55f * static_cast<float>(k) / static_cast<float>(steps))));
    };
    const auto get_half = [half_w, narrow](const int k) -> float {
        return half_w - (narrow * static_cast<float>(k));
    };
    const float last_front = get_front(steps - 1);
    const float last_half  = get_half(steps - 1);
    const float last_reach = last_front - back;
    // What stands over the last stage's front edge.
    float cap_height = 0.0f;
    if (cap == Buttress_cap::weathered) {
        cap_height = std::min(1.3f * last_reach, 0.22f * height);
    } else if (cap == Buttress_cap::gabled) {
        cap_height = std::min(1.9f * last_half, 0.2f * height);
    } else {
        cap_height = std::min(5.0f * last_half, 0.3f * height);
    }
    const float foot  = 0.5f * plinth;
    const float eaves = height - cap_height;
    // The stages' front edges: the lower a stage, the higher it is.
    std::array<float, buttress_step_max + 1> edges{};
    const float weights = 0.5f * static_cast<float>(steps * (steps + 3));
    float sum = 0.0f;
    edges[0] = foot;
    for (int k = 0; k < steps; ++k) {
        sum += static_cast<float>(steps + 1 - k);
        edges[static_cast<std::size_t>(k + 1)] = foot + ((eaves - foot) * sum / weights);
    }

    builder.set_slot(buttress_stone_slot);
    {
        const float x = 0.5f * width;
        const float z = 0.5f * depth;
        const std::array<glm::vec3, 5> outline{{
            {x, 0.0f, back}, {x, plinth, back}, {x, plinth, z - chamfer}, {x, plinth - chamfer, z}, {x, 0.0f, z}
        }};
        add_prism_between(builder, outline, glm::vec3{-width, 0.0f, 0.0f});
    }
    for (int k = 0; k < steps; ++k) {
        const std::size_t index = static_cast<std::size_t>(k);
        const bool  last  = (k + 1) == steps;
        const float x     = get_half(k);
        const float front = get_front(k);
        const float low   = (k == 0) ? foot : (edges[index] - std::min(0.05f, 0.1f * (edges[index] - edges[index - 1])));
        const float high  = edges[index + 1];
        builder.set_slot(buttress_stone_slot);
        if (last) {
            // Under a slope back to the wall, or flat under a gablet or a pinnacle.
            const float top = (cap == Buttress_cap::weathered) ? height : high;
            const std::array<glm::vec3, 4> outline{{{x, low, back}, {x, top, back}, {x, high, front}, {x, low, front}}};
            add_prism_between(builder, outline, glm::vec3{-2.0f * x, 0.0f, 0.0f});
        } else {
            const float next     = get_front(k + 1);
            const float shoulder = high + std::min(1.3f * (front - next), 0.5f * (edges[index + 2] - high));
            const std::array<glm::vec3, 5> outline{{{x, low, back}, {x, shoulder, back}, {x, shoulder, next}, {x, high, front}, {x, low, front}}};
            add_prism_between(builder, outline, glm::vec3{-2.0f * x, 0.0f, 0.0f});
        }
        if (!last || (cap == Buttress_cap::weathered)) {
            // A drip course under the offset.
            const float course = std::min(0.06f, 0.2f * (high - low));
            builder.set_slot(buttress_trim_slot);
            builder.add_box(glm::vec3{-(x + drip), high - course, back}, glm::vec3{x + drip, high, front + drip});
        }
    }
    if (cap == Buttress_cap::gabled) {
        // A gablet over the last stage: its ridge runs out from the wall.
        const float x     = last_half + chamfer;
        const float base  = std::min(0.05f, 0.15f * cap_height);
        const float inset = 0.3f * chamfer;
        const float front = last_front + chamfer - inset;
        builder.set_slot(buttress_trim_slot);
        builder.add_box(glm::vec3{-x, eaves, back}, glm::vec3{x, eaves + base, last_front + chamfer});
        const std::array<glm::vec3, 3> gable{{
            {-(x - inset), eaves + (0.5f * base), front}, {x - inset, eaves + (0.5f * base), front}, {0.0f, height, front}
        }};
        builder.set_slot(buttress_stone_slot);
        add_prism_between(builder, gable, glm::vec3{0.0f, 0.0f, back - front});
    } else if (cap == Buttress_cap::pinnacle) {
        // A square pinnacle at the front of the last stage: a base, a shaft under a band and a spire.
        const float side   = std::min(last_half, 0.5f * last_reach);
        const float center = last_front - side;
        const float base   = std::min(0.06f, 0.1f * cap_height);
        const float shaft  = 0.3f * cap_height;
        const float inside = 0.88f * side;
        const float spire  = 0.8f * side;
        const float low    = eaves + shaft + (0.5f * base);
        builder.set_slot(buttress_trim_slot);
        builder.add_box(glm::vec3{-(side + drip), eaves, center - side - drip}, glm::vec3{side + drip, eaves + base, center + side + drip});
        builder.add_box(glm::vec3{-side, eaves + shaft, center - side}, glm::vec3{side, eaves + shaft + base, center + side});
        builder.set_slot(buttress_stone_slot);
        builder.add_box(glm::vec3{-inside, eaves + (0.5f * base), center - inside}, glm::vec3{inside, eaves + shaft + (0.3f * base), center + inside});
        const uint32_t a    = builder.add_vertex(glm::vec3{ spire, low,    center - spire});
        const uint32_t b    = builder.add_vertex(glm::vec3{-spire, low,    center - spire});
        const uint32_t c    = builder.add_vertex(glm::vec3{-spire, low,    center + spire});
        const uint32_t d    = builder.add_vertex(glm::vec3{ spire, low,    center + spire});
        const uint32_t apex = builder.add_vertex(glm::vec3{0.0f,   height, center});
        builder.add_quad(d, c, b, a);
        builder.add_triangle(a, b, apex);
        builder.add_triangle(b, c, apex);
        builder.add_triangle(c, d, apex);
        builder.add_triangle(d, a, apex);
    }
    builder.write(mesh);
}

//
// Log: a lying trunk with cut ends, a stump on its roots, or a barkless piece of driftwood, with branch stubs. A lying
// trunk rests on its two lowest lines of points, pressed flat into the ground plane.
//

constexpr uint32_t log_bark_slot      = 0;
constexpr uint32_t log_sapwood_slot   = 1;
constexpr uint32_t log_heartwood_slot = 2;
constexpr int      log_stub_max       = 4;
constexpr int      log_stub_sides     = 5;
constexpr int      log_bands          = 8;    // of a lying trunk; even, so a ring lies over the origin
constexpr float    log_stump_share    = 0.4f; // a stump's height in lengths

enum class Log_form : unsigned int { log = 0, stump, driftwood };

[[nodiscard]] auto get_log_form(const std::string_view name) -> Log_form
{
    if (name == "stump") {
        return Log_form::stump;
    }
    if (name == "driftwood") {
        return Log_form::driftwood;
    }
    return Log_form::log;
}

// Point i of n of a ring about an axis along +X, counter-clockwise seen from +X; the ground lies between the last
// point and the first.
[[nodiscard]] auto get_lying_direction(const int i, const int n) -> glm::vec3
{
    const float angle = (-0.5f * pi) + ((static_cast<float>(i) + 0.5f) * two_pi / static_cast<float>(n));
    return glm::vec3{0.0f, std::sin(angle), -std::cos(angle)};
}

// Where a lying trunk's axis rests over the ground: its two lowest lines of points end up in the ground.
[[nodiscard]] auto get_lying_axis_height(const float radius, const int n) -> float
{
    return 0.9f * radius * std::cos(pi / static_cast<float>(n));
}

class Log_values
{
public:
    explicit Log_values(const Kit_parameter_values& values)
        : form  {get_log_form(values.get_enum("form"))}
        , length{values.get_float("length")}
        , radius{0.5f * values.get_float("width")}
        , bend  {values.get_float("bend")}
        , stubs {std::clamp(values.get_int("stubs"), 0, log_stub_max)}
        , sides {std::max(values.get_int("segments"), 6)}
    {
    }

    // A lying trunk's radius share at s, -0.5 to 0.5 along its length.
    [[nodiscard]] auto get_share(const float s) const -> float
    {
        if (form == Log_form::log) {
            return 1.0f - (0.1f * s);
        }
        // Driftwood: full about the middle, thin toward both ends.
        const float plateau = 1.0f / static_cast<float>(log_bands);
        const float out     = std::max(std::abs(s) - plateau, 0.0f) / (0.5f - plateau);
        return 1.0f - (0.72f * std::pow(out, 1.4f));
    }
    // Its axis there: bowed toward -Z, and driftwood's ends rise.
    [[nodiscard]] auto get_axis(const float s) const -> glm::vec3
    {
        const float plateau = 1.0f / static_cast<float>(log_bands);
        const float out     = std::max(std::abs(s) - plateau, 0.0f);
        const float lift    = (form == Log_form::driftwood) ? ((0.12f + (0.4f * bend)) * length * out * out) : 0.0f;
        return glm::vec3{s * length, get_lying_axis_height(radius, sides) + lift, -0.6f * bend * length * s * s};
    }

    Log_form form  {Log_form::log};
    float    length{0.0f};
    float    radius{0.0f};
    float    bend  {0.0f};
    int      stubs {0};
    int      sides {10};
};

// A branch stub from `root` inside the trunk along `direction`: a collar where it leaves the trunk, then a cut end
// with its bark, or driftwood's broken point.
void add_log_stub(Kit_mesh_builder& builder, const Log_values& log, const glm::vec3& root, const glm::vec3& direction, const float inside, const float radius, const float reach)
{
    const glm::vec3 reference = (std::abs(direction.x) > 0.9f) ? y_axis : x_axis;
    const glm::vec3 u = glm::normalize(glm::cross(direction, reference));
    const glm::vec3 v = glm::cross(direction, u);
    const bool pointed = (log.form == Log_form::driftwood);
    // (distance along the stub, radius share) of the rings; the last is the bark's edge in a cut end.
    const std::array<glm::vec2, 5> stations{{
        {0.0f, 1.4f}, {inside, 1.25f}, {inside + (0.4f * reach), 0.95f}, {inside + reach, pointed ? 0.5f : 0.85f}, {inside + reach, 0.62f}
    }};
    const std::size_t ring_count = pointed ? 4 : 5;
    std::vector<glm::vec3> points;
    for (std::size_t r = 0; r < ring_count; ++r) {
        for (int i = 0; i < log_stub_sides; ++i) {
            const float angle = two_pi * static_cast<float>(i) / static_cast<float>(log_stub_sides);
            points.push_back(root + (stations[r].x * direction) + ((radius * stations[r].y) * ((std::cos(angle) * u) + (std::sin(angle) * v))));
        }
    }
    const glm::vec3 start = root - (std::min(0.1f * inside, 0.25f * root.y) * direction);
    if (pointed) {
        const std::array<uint32_t, 5> slots{log_sapwood_slot, log_sapwood_slot, log_sapwood_slot, log_sapwood_slot, log_heartwood_slot};
        add_limb(builder, points, log_stub_sides, start, root + ((inside + (1.45f * reach)) * direction) + ((0.4f * radius) * u), slots);
    } else {
        const std::array<uint32_t, 6> slots{log_bark_slot, log_bark_slot, log_bark_slot, log_bark_slot, log_bark_slot, log_sapwood_slot};
        add_limb(builder, points, log_stub_sides, start, root + ((inside + reach) * direction), slots);
    }
}

void add_lying_trunk(Kit_mesh_builder& builder, const Log_values& log, Kit_random& random)
{
    const int         n      = log.sides;
    const std::size_t count  = static_cast<std::size_t>(n);
    const bool        is_log = (log.form == Log_form::log);
    // How the two cuts lean from square: the run of x per z.
    const float                cut_start = 0.3f * next_signed(random);
    const float                cut_end   = 0.3f * next_signed(random);
    const std::array<float, 2> cut{cut_start, cut_end};
    std::vector<glm::vec3> outer;  // the rings of the trunk's surface
    std::vector<glm::vec3> centers;
    for (int k = 0; k <= log_bands; ++k) {
        const bool  is_end  = (k == 0) || (k == log_bands);
        const bool  plateau = std::abs((2 * k) - log_bands) <= 2;
        const float s       = (static_cast<float>(k) / static_cast<float>(log_bands)) - 0.5f;
        glm::vec3 center = log.get_axis(s);
        float     share  = log.get_share(s);
        if (!is_end) {
            center.x += 0.2f * (log.length / static_cast<float>(log_bands)) * next_signed(random);
        }
        if (!is_log && !plateau) {
            share    *= 1.0f + (0.18f * next_signed(random));
            center.z += 0.25f * log.radius * next_signed(random);
        }
        centers.push_back(center);
        for (int i = 0; i < n; ++i) {
            const float     radius = log.radius * share * (1.0f + (0.03f * next_signed(random)));
            const glm::vec3 out    = radius * get_lying_direction(i, n);
            glm::vec3 point = center + out;
            if (is_log && is_end) {
                point.x += cut[(k == 0) ? 0 : 1] * out.z;
            }
            point.y = std::max(point.y, 0.0f);
            outer.push_back(point);
        }
    }
    if (!is_log) {
        // Broken ends: a point beyond each, off the axis.
        std::vector<uint32_t> slots(static_cast<std::size_t>(log_bands) + 2, log_sapwood_slot);
        slots.front() = log_heartwood_slot;
        slots.back()  = log_heartwood_slot;
        const float     reach = 0.06f * log.length;
        const float     start_x = -reach * random.next_range(0.6f, 1.2f);
        const float     start_y = 0.3f * log.radius * random.next_unit();
        const float     start_z = 0.3f * log.radius * next_signed(random);
        const float     end_x   = reach * random.next_range(0.6f, 1.2f);
        const float     end_y   = 0.3f * log.radius * random.next_unit();
        const float     end_z   = 0.3f * log.radius * next_signed(random);
        const glm::vec3 start   = centers.front() + glm::vec3{start_x, start_y, start_z};
        const glm::vec3 end     = centers.back()  + glm::vec3{end_x, end_y, end_z};
        // A patch of bark is left on the upper side about the middle.
        const uint32_t quarter = static_cast<uint32_t>(n) / 4;
        const Limb_patch bark{.first_band = (log_bands / 2) - 2, .band_count = 3, .first_point = quarter, .point_count = quarter + 1, .slot = log_bark_slot};
        add_limb(builder, outer, n, start, end, slots, bark);
        return;
    }
    // A cut end: the bark's edge and the heartwood's about the pith, all in the cut's plane through the ring's center.
    std::vector<glm::vec3> points;
    const auto add_cut_ring = [&](const std::size_t ring, const float share, const float irregular) {
        for (std::size_t i = 0; i < count; ++i) {
            const glm::vec3 out = outer[(ring * count) + i] - centers[ring];
            points.push_back(centers[ring] + ((share * (1.0f + (irregular * next_signed(random)))) * out));
        }
    };
    const auto get_pith = [&](const std::size_t ring, const std::size_t which) -> glm::vec3 {
        const float z = 0.12f * log.radius * next_signed(random);
        return centers[ring] + glm::vec3{cut[which] * z, 0.1f * log.radius * next_signed(random), z};
    };
    const std::size_t last = static_cast<std::size_t>(log_bands);
    add_cut_ring(0, 0.5f,  0.12f);
    add_cut_ring(0, 0.86f, 0.0f);
    points.insert(points.end(), outer.begin(), outer.end());
    add_cut_ring(last, 0.86f, 0.0f);
    add_cut_ring(last, 0.5f,  0.12f);
    std::vector<uint32_t> slots(static_cast<std::size_t>(log_bands) + 6, log_bark_slot);
    slots[0] = log_heartwood_slot;
    slots[1] = log_sapwood_slot;
    slots[slots.size() - 2] = log_sapwood_slot;
    slots[slots.size() - 1] = log_heartwood_slot;
    const glm::vec3 start = get_pith(0, 0);
    const glm::vec3 end   = get_pith(last, 1);
    add_limb(builder, points, n, start, end, slots);
}

void add_lying_stubs(Kit_mesh_builder& builder, const Log_values& log, Kit_random& random)
{
    const bool is_log = (log.form == Log_form::log);
    for (int j = 0; j < log.stubs; ++j) {
        // Along the middle of the trunk, on its upper half, to the two sides in turn and toward the trunk's end.
        const float     s      = (0.6f * ((static_cast<float>(j) + 0.5f) / static_cast<float>(log.stubs))) - 0.3f + (0.05f * next_signed(random));
        const float     side   = ((j % 2) == 0) ? 1.0f : -1.0f;
        const float     angle  = (0.5f * pi) + (side * random.next_range(0.35f, 1.1f));
        const glm::vec3 radial {0.0f, std::sin(angle), -std::cos(angle)};
        const float     along  = ((s < 0.0f) ? -1.0f : 1.0f) * random.next_range(0.3f, 0.8f);
        const glm::vec3 out    = glm::normalize(radial + (along * x_axis));
        const float     trunk  = log.radius * log.get_share(s);
        const float     radius = std::min(trunk * random.next_range(0.26f, 0.36f), 0.1f * log.length);
        const float     reach  = trunk * (is_log ? random.next_range(0.5f, 1.0f) : random.next_range(1.2f, 2.4f));
        add_log_stub(builder, log, log.get_axis(s) + ((0.4f * trunk) * radial), out, 0.75f * trunk, radius, reach);
    }
}

// The height of a stump's cut at its middle.
[[nodiscard]] auto get_stump_height(const Log_values& log) -> float
{
    return log_stump_share * log.length;
}

void add_stump(Kit_mesh_builder& builder, const Log_values& log, Kit_random& random)
{
    const int       n      = log.sides;
    const float     height = get_stump_height(log);
    const glm::vec3 lean   = std::min(0.3f * log.bend * height, 0.6f * log.radius) * around_y(random.next_range(0.0f, two_pi));
    const float     phase  = random.next_range(0.0f, two_pi);
    // Every second line of points is a root; a root reaches out by its own share.
    std::vector<float> roots(static_cast<std::size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i) {
        roots[static_cast<std::size_t>(i)] = ((i % 2) == 0) ? random.next_range(0.35f, 1.0f) : random.next_range(0.05f, 0.2f);
    }
    // The cut's rise per run; a low wide stump's cut is nearly level.
    const float     cut_tilt  = std::min(random.next_range(0.05f, 0.2f), 0.25f * height / log.radius);
    const glm::vec3 cut_slope = cut_tilt * around_y(random.next_range(0.0f, two_pi));
    // (height share, radius share, a root's reach in radii, unevenness of the height)
    const std::array<glm::vec4, 5> stations{{
        {0.0f, 1.15f, 1.0f, 0.0f}, {0.12f, 1.06f, 0.45f, 0.03f}, {0.3f, 0.98f, 0.12f, 0.04f}, {0.65f, 0.94f, 0.0f, 0.03f}, {1.0f, 0.92f, 0.0f, 0.0f}
    }};
    std::vector<glm::vec3> points;
    std::vector<glm::vec3> rim; // the top ring's places from the axis
    const auto get_center = [&lean, height](const float share) -> glm::vec3 {
        return ((share * share) * lean) + ((share * height) * y_axis);
    };
    for (std::size_t r = 0; r < stations.size(); ++r) {
        const glm::vec4& station = stations[r];
        const bool       is_top  = ((r + 1) == stations.size());
        const glm::vec3  center  = get_center(station.x);
        for (int i = 0; i < n; ++i) {
            const float     angle  = phase + (two_pi * static_cast<float>(i) / static_cast<float>(n));
            const float     radius = log.radius * (station.y + (station.z * roots[static_cast<std::size_t>(i)])) * (1.0f + (0.04f * next_signed(random)));
            const glm::vec3 out    = radius * around_y(angle);
            glm::vec3 point = center + out;
            if (is_top) {
                point.y += glm::dot(cut_slope, out);
                rim.push_back(out);
            } else {
                point.y += station.w * height * next_signed(random);
            }
            points.push_back(point);
        }
    }
    const glm::vec3 top = get_center(1.0f);
    for (const float share : {0.86f, 0.5f}) {
        for (int i = 0; i < n; ++i) {
            const float     irregular = (share < 0.6f) ? (1.0f + (0.12f * next_signed(random))) : 1.0f;
            const glm::vec3 out       = (share * irregular) * rim[static_cast<std::size_t>(i)];
            points.push_back(top + out + (glm::dot(cut_slope, out) * y_axis));
        }
    }
    const float     pith_x = next_signed(random);
    const float     pith_z = next_signed(random);
    const glm::vec3 pith   = (0.12f * log.radius) * glm::vec3{pith_x, 0.0f, pith_z};
    const std::array<uint32_t, 8> slots{
        log_bark_slot, log_bark_slot, log_bark_slot, log_bark_slot, log_bark_slot, log_bark_slot, log_sapwood_slot, log_heartwood_slot
    };
    add_limb(builder, points, n, glm::vec3{0.0f}, top + pith + (glm::dot(cut_slope, pith) * y_axis), slots);

    for (int j = 0; j < log.stubs; ++j) {
        const float     share  = 0.4f + (0.4f * ((static_cast<float>(j) + 0.5f) / static_cast<float>(log.stubs))) + (0.04f * next_signed(random));
        const float     angle  = phase + (golden_angle * static_cast<float>(j)) + (0.4f * next_signed(random));
        const glm::vec3 radial = around_y(angle);
        const glm::vec3 out    = glm::normalize(radial + (random.next_range(0.4f, 0.9f) * y_axis));
        const float     radius = std::min(log.radius * random.next_range(0.22f, 0.32f), 0.2f * height);
        const float     reach  = log.radius * random.next_range(0.5f, 1.0f);
        add_log_stub(builder, log, get_center(share) + ((0.4f * log.radius) * radial), out, 0.7f * log.radius, radius, reach);
    }
}

void generate_log(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Log_values log{values};
    Kit_random random{values.get_seed(), "log"};
    Kit_mesh_builder builder;
    if (log.form == Log_form::stump) {
        add_stump(builder, log, random);
    } else {
        add_lying_trunk(builder, log, random);
        add_lying_stubs(builder, log, random);
    }
    builder.write(mesh);
}

//
// Shell: a conch lying on its side, a scallop's ribbed valve, or a starfish.
//

constexpr uint32_t shell_shell_slot = 0;
constexpr uint32_t shell_coral_slot = 1;
constexpr int      shell_rib_min    = 5;
constexpr int      shell_rib_max    = 12;

enum class Shell_form : unsigned int { conch = 0, scallop, starfish };

[[nodiscard]] auto get_shell_form(const std::string_view name) -> Shell_form
{
    if (name == "scallop") {
        return Shell_form::scallop;
    }
    if (name == "starfish") {
        return Shell_form::starfish;
    }
    return Shell_form::conch;
}

// A scallop's upper valve over a flat underside: rays from the umbo at the back to the rim, a furrow and a rib in
// turn, between the two ears along the hinge; an ear ends at a short ray beside the fan, which notches the outline.
void add_scallop(Kit_mesh_builder& builder, const float size, const int ribs, Kit_random& random)
{
    constexpr std::size_t stations = 4;
    const int       rays = (2 * ribs) + 5; // an ear, a notch, then a furrow and a rib in turn, a furrow, a notch, an ear
    const glm::vec3 umbo{0.0f, 0.045f * size, -0.46f * size};
    const float     dome = size * random.next_range(0.13f, 0.18f);
    const float     fan  = random.next_range(1.0f, 1.12f); // the outermost furrows' angle from the middle
    std::array<std::vector<uint32_t>, stations> top;
    std::array<std::vector<uint32_t>, stations> bottom;
    for (int j = 0; j < rays; ++j) {
        const bool  is_ear   = (j == 0) || ((j + 1) == rays);
        const bool  is_notch = (j == 1) || ((j + 2) == rays);
        const bool  in_fan   = !is_ear && !is_notch;
        const bool  is_rib   = in_fan && ((j % 2) == 1);
        const float side     = ((2 * j) < rays) ? -1.0f : 1.0f;
        // The valve is a circle through the umbo, so a ray's length falls with the cosine of its angle.
        float angle  = fan * ((2.0f * static_cast<float>(j - 2) / static_cast<float>(rays - 5)) - 1.0f);
        float length = size * std::cos(angle) * (is_rib ? 1.0f : 0.94f) * (1.0f + (0.02f * next_signed(random)));
        float high   = is_rib ? 1.0f : 0.58f;
        if (is_ear) {
            angle  = side * 0.5f * pi;
            length = size * random.next_range(0.25f, 0.3f);
            high   = 0.2f;
        } else if (is_notch) {
            angle  = side * (fan + 0.1f);
            length = size * 0.21f;
            high   = 0.24f;
        }
        const glm::vec3 out{std::sin(angle), 0.0f, std::cos(angle)};
        for (std::size_t k = 0; k < stations; ++k) {
            const float     t      = static_cast<float>(k + 1) / static_cast<float>(stations);
            const float     arch   = std::pow(4.0f * t * (1.0f - t), 0.7f);
            const float     y      = (umbo.y * (1.0f - t)) + (high * ((dome * arch) + (0.03f * size * t)));
            const glm::vec3 ground = umbo + ((length * t) * out);
            top[k].push_back(builder.add_vertex(glm::vec3{ground.x, y, ground.z}));
            bottom[k].push_back(is_ear ? top[k].back() : builder.add_vertex(glm::vec3{ground.x, 0.0f, ground.z}));
        }
    }
    const uint32_t    apex = builder.add_vertex(umbo);
    const std::size_t rim  = stations - 1;
    for (int j = 0; (j + 1) < rays; ++j) {
        const std::size_t a = static_cast<std::size_t>(j);
        const std::size_t b = a + 1;
        // A rib with half the furrow on each side is one band of colour; every third band is coral.
        const int band = j / 2;
        builder.set_slot(((band % 3) == 1) ? shell_coral_slot : shell_shell_slot);
        builder.add_triangle(apex, top[0][a], top[0][b]);
        for (std::size_t k = 0; k < rim; ++k) {
            builder.add_triangle(top[k][a], top[k + 1][a], top[k + 1][b]);
            builder.add_triangle(top[k][a], top[k + 1][b], top[k][b]);
        }
        builder.set_slot(shell_shell_slot);
        builder.add_triangle(apex, bottom[0][b], bottom[0][a]);
        for (std::size_t k = 0; k < rim; ++k) {
            builder.add_triangle(bottom[k][a], bottom[k + 1][b], bottom[k + 1][a]);
            builder.add_triangle(bottom[k][a], bottom[k][b],     bottom[k + 1][b]);
        }
        // The rim between the two; an ear's ray has one point for the upper side and the underside.
        if (j == 0) {
            builder.add_triangle(top[rim][a], bottom[rim][b], top[rim][b]);
        } else if ((j + 2) == rays) {
            builder.add_triangle(top[rim][a], bottom[rim][a], top[rim][b]);
        } else {
            builder.add_triangle(top[rim][a], bottom[rim][a], bottom[rim][b]);
            builder.add_triangle(top[rim][a], bottom[rim][b], top[rim][b]);
        }
    }
}

// A starfish: rings of four points to an arm (the notch before it, a shoulder, the tip, a shoulder) from the flat
// underside up to the arms' ridges, and knobs along the ridges.
void add_starfish(Kit_mesh_builder& builder, const float size, const int arms, Kit_random& random)
{
    const int   n      = 4 * arms;
    const float disc   = 0.5f * size * (0.2f + (0.012f * static_cast<float>(arms)));
    const float phase  = random.next_range(0.0f, two_pi);
    const float sector = two_pi / static_cast<float>(arms);
    // (height in sizes, the shoulders' half width share, the tip's place along the arm, the notch's radius share)
    const std::array<glm::vec4, 4> levels{{
        {0.0f, 0.96f, 0.97f, 0.98f}, {0.03f, 1.0f, 1.0f, 1.0f}, {0.075f, 0.6f, 0.93f, 0.8f}, {0.105f, 0.14f, 0.78f, 0.5f}
    }};
    const float ridge = levels[3].x * size;
    std::vector<glm::vec3> points(static_cast<std::size_t>(n) * levels.size());
    std::vector<glm::vec3> knobs;
    for (int a = 0; a < arms; ++a) {
        const float     angle  = phase + (sector * static_cast<float>(a)) + (0.12f * sector * next_signed(random));
        const float     reach  = 0.5f * size * random.next_range(0.82f, 1.0f);
        const float     curl   = 0.35f * next_signed(random); // the outer arm turns aside
        const float     lift   = 0.03f * size * random.next_unit();
        const glm::vec3 out    = around_y(angle);
        const glm::vec3 across = around_y(angle + (0.5f * pi));
        const glm::vec3 turned = around_y(angle + curl);
        const float     half   = 0.6f * disc * std::sin(0.5f * sector);
        const glm::vec3 notch  = disc * around_y(phase + (sector * (static_cast<float>(a) - 0.5f)));
        const glm::vec3 middle = (0.5f * reach) * out;
        for (std::size_t level = 0; level < levels.size(); ++level) {
            const glm::vec4&  at    = levels[level];
            const float       y     = at.x * size;
            const float       raise = (level == 0) ? 0.0f : lift;
            const std::size_t first = (level * static_cast<std::size_t>(n)) + (static_cast<std::size_t>(a) * 4);
            points[first]     = (at.w * notch) + ((((level == 3) ? 1.25f : 1.0f) * y) * y_axis);
            points[first + 1] = middle - ((at.y * half) * across) + (y * y_axis);
            points[first + 2] = middle + ((at.z * 0.5f * reach) * turned) + (((0.55f * y) + raise) * y_axis);
            points[first + 3] = middle + ((at.y * half) * across) + (y * y_axis);
        }
        knobs.push_back(((0.24f * reach) * out) + ((1.12f * ridge) * y_axis));
        knobs.push_back(middle + (ridge * y_axis));
        knobs.push_back(middle + ((0.2f * reach) * turned) + (((0.8f * ridge) + (0.5f * lift)) * y_axis));
    }
    const glm::vec3 crown{0.0f, 0.14f * size, 0.0f};
    const uint32_t count = static_cast<uint32_t>(n);
    const uint32_t first = static_cast<uint32_t>(builder.get_positions().size());
    for (const glm::vec3& point : points) {
        builder.add_vertex(point);
    }
    const uint32_t under = builder.add_vertex(glm::vec3{0.0f});
    const uint32_t over  = builder.add_vertex(crown);
    const uint32_t top   = first + ((static_cast<uint32_t>(levels.size()) - 1) * count);
    builder.set_slot(shell_coral_slot);
    for (uint32_t lower = first; lower < top; lower += count) {
        const uint32_t upper = lower + count;
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t j = (i + 1) % count;
            builder.add_triangle(lower + i, lower + j, upper + j);
            builder.add_triangle(lower + i, upper + j, upper + i);
        }
    }
    // The upper side and the underside: the disc between the notches and each arm from its notches to its tip, so
    // that no facet crosses an arm that turns aside.
    for (uint32_t a = 0; a < static_cast<uint32_t>(arms); ++a) {
        const uint32_t notch = 4 * a;
        const uint32_t left  = notch + 1;
        const uint32_t tip   = notch + 2;
        const uint32_t right = notch + 3;
        const uint32_t next  = (notch + 4) % count;
        builder.add_triangle(over,        top + notch, top + next);
        builder.add_triangle(top + notch, top + left,  top + right);
        builder.add_triangle(top + notch, top + right, top + next);
        builder.add_triangle(top + left,  top + tip,   top + right);
        builder.add_triangle(under,         first + next,  first + notch);
        builder.add_triangle(first + notch, first + right, first + left);
        builder.add_triangle(first + notch, first + next,  first + right);
        builder.add_triangle(first + left,  first + right, first + tip);
    }

    // A knob: a small pyramid standing in the ridge.
    builder.set_slot(shell_shell_slot);
    const float knob = 0.016f * size;
    const auto add_knob = [&builder, knob](const glm::vec3& at, const float turn) {
        std::array<uint32_t, 4> foot{};
        for (std::size_t i = 0; i < 4; ++i) {
            foot[i] = builder.add_vertex(at + (knob * around_y(turn + (0.5f * pi * static_cast<float>(i)))) - ((1.2f * knob) * y_axis));
        }
        const uint32_t apex = builder.add_vertex(at + ((1.5f * knob) * y_axis));
        builder.add_quad(foot[3], foot[2], foot[1], foot[0]);
        for (std::size_t i = 0; i < 4; ++i) {
            builder.add_triangle(foot[i], foot[(i + 1) % 4], apex);
        }
    };
    for (const glm::vec3& at : knobs) {
        add_knob(at, random.next_range(0.0f, two_pi));
    }
    add_knob(crown, random.next_range(0.0f, two_pi));
}

// A conch along X, its spire toward -X: rings about the axis from the spire's point over three stepped whorls with
// knobs on their shoulders to the body whorl and the canal's point. The outer lip flares toward +Z over the coral
// aperture; the body rests on the ground on its two lowest lines of points.
void add_conch(Kit_mesh_builder& builder, const float size, const int ribs, Kit_random& random)
{
    const int   n     = 2 * ribs;
    const float along = 0.92f * size;
    // (place along the shell, radius, knobs' height share, the lip's flare share); the origin lies between the two
    // widest rings.
    const std::array<glm::vec4, 10> stations{{
        {-0.36f, 0.035f, 0.0f, 0.0f},  {-0.31f, 0.075f, 0.3f,  0.0f},
        {-0.27f, 0.07f,  0.0f, 0.0f},  {-0.2f,  0.135f, 0.35f, 0.0f},
        {-0.16f, 0.125f, 0.0f, 0.1f},  {-0.05f, 0.21f,  0.32f, 0.6f},
        { 0.1f,  0.215f, 0.0f, 1.0f},  { 0.26f, 0.165f, 0.0f,  0.8f},
        { 0.42f, 0.085f, 0.0f, 0.35f}, { 0.54f, 0.04f,  0.0f,  0.0f}
    }};
    const float axis_y = size * get_lying_axis_height(0.21f, n);
    const float flare  = random.next_range(0.5f, 0.75f);
    const float lip_at = pi * random.next_range(0.66f, 0.74f); // the lip's angle: up and toward +Z
    const float lean   = random.next_range(0.2f, 0.35f);     // the whorls lean: the run of x per rise
    const int   first_aperture = (2 * n) / 3;                // the first line of the aperture's points
    std::vector<glm::vec3> points;
    for (const glm::vec4& station : stations) {
        for (int i = 0; i < n; ++i) {
            const float     angle  = (-0.5f * pi) + ((static_cast<float>(i) + 0.5f) * two_pi / static_cast<float>(n));
            const float     upper  = std::max(std::sin(angle), 0.0f); // knobs stand on the upper half
            const float     to_lip = std::max(1.0f - (std::abs(angle - lip_at) / 0.8f), 0.0f);
            const float     knob   = (((i % 2) == 1) ? 1.0f : -0.12f) * station.z * upper * (1.0f - to_lip) * (1.0f + (0.3f * next_signed(random)));
            const bool      inside = (i > first_aperture) && ((i + 1) < n); // the aperture's hollow
            const float     hollow = inside ? (1.0f - (0.35f * station.w)) : 1.0f;
            const float     radius = size * station.y * (1.0f + knob + (flare * station.w * to_lip * to_lip)) * hollow;
            const glm::vec3 out    = radius * get_lying_direction(i, n);
            glm::vec3 point = glm::vec3{along * station.x, axis_y, 0.0f} + out;
            point.x += lean * out.y;
            point.y  = std::max(point.y, 0.0f);
            points.push_back(point);
        }
    }
    const uint32_t first = static_cast<uint32_t>(builder.get_positions().size());
    const uint32_t count = static_cast<uint32_t>(n);
    const uint32_t rings = static_cast<uint32_t>(stations.size());
    for (const glm::vec3& point : points) {
        builder.add_vertex(point);
    }
    const uint32_t start = builder.add_vertex(glm::vec3{along * -0.47f, axis_y + (0.01f * size), 0.0f});
    const uint32_t end   = builder.add_vertex(glm::vec3{along *  0.62f, axis_y - (0.02f * size), 0.02f * size});
    const uint32_t last  = first + ((rings - 1) * count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t j = (i + 1) % count;
        // The aperture: the strips of the body whorl from the lip down to the ground.
        const bool aperture = (i >= static_cast<uint32_t>(first_aperture)) && ((i + 2) <= count);
        builder.set_slot(shell_shell_slot);
        builder.add_triangle(start, first + j, first + i);
        for (uint32_t r = 0; (r + 1) < rings; ++r) {
            const uint32_t lower = first + (r * count);
            const uint32_t upper = lower + count;
            builder.set_slot((aperture && (r >= 4) && (r <= 7)) ? shell_coral_slot : shell_shell_slot);
            builder.add_triangle(lower + i, lower + j, upper + j);
            builder.add_triangle(lower + i, upper + j, upper + i);
        }
        builder.set_slot(aperture ? shell_coral_slot : shell_shell_slot);
        builder.add_triangle(last + i, last + j, end);
    }
}

void generate_shell(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Shell_form form = get_shell_form(values.get_enum("form"));
    const float      size = values.get_float("size");
    const int        ribs = std::clamp(values.get_int("ribs"), shell_rib_min, shell_rib_max);
    Kit_random random{values.get_seed(), "shell"};
    Kit_mesh_builder builder;
    if (form == Shell_form::scallop) {
        add_scallop(builder, size, ribs, random);
    } else if (form == Shell_form::starfish) {
        add_starfish(builder, size, ribs, random);
    } else {
        add_conch(builder, size, ribs, random);
    }
    builder.write(mesh);
}

//
// Blocky tree: a square trunk on a root block under canopy blocks, each a little turned, with a lighter block on its
// top and small blocks standing out of its sides.
//

constexpr uint32_t blocky_bark_slot    = 0;
constexpr uint32_t blocky_foliage_slot = 1;
constexpr uint32_t blocky_shoot_slot   = 2;
constexpr int      blocky_block_max    = 4;
constexpr int      blocky_bud_count    = 2; // small blocks on a canopy block

enum class Blocky_form : unsigned int { cube = 0, tiers, cluster };

[[nodiscard]] auto get_blocky_form(const std::string_view name) -> Blocky_form
{
    if (name == "tiers") {
        return Blocky_form::tiers;
    }
    if (name == "cluster") {
        return Blocky_form::cluster;
    }
    return Blocky_form::cube;
}

// A canopy block on base_center (size: half its width, its height, half its depth): its lighter top block toward one
// side, and its buds.
void add_canopy_block(Kit_mesh_builder& builder, const glm::vec3& base_center, const glm::vec3& size, const float yaw, Kit_random& random)
{
    builder.set_slot(blocky_foliage_slot);
    add_turned_box(builder, base_center, size, yaw);
    const glm::vec3 ax = around_y(yaw);
    const glm::vec3 az{std::sin(yaw), 0.0f, std::cos(yaw)};
    const float     least = std::min({size.x, 0.5f * size.y, size.z});
    {
        const float     share  = random.next_range(0.42f, 0.6f);
        const float     toward = 0.5f * pi * static_cast<float>(random.next_u32() % 4u);
        const glm::vec3 offset = ((0.3f * size.x * std::cos(toward)) * ax) + ((0.3f * size.z * std::sin(toward)) * az);
        const float     height = least * random.next_range(0.3f, 0.5f);
        builder.set_slot(blocky_shoot_slot);
        add_turned_box(builder, base_center + offset + ((size.y - (0.3f * height)) * y_axis), glm::vec3{share * size.x, 1.3f * height, share * size.z}, yaw);
    }
    builder.set_slot(blocky_foliage_slot);
    const uint32_t first_face = random.next_u32() % 4u;
    for (int k = 0; k < blocky_bud_count; ++k) {
        // On two faces next to one another, half in the block.
        const uint32_t  face   = (first_face + static_cast<uint32_t>(k)) % 4u;
        const float     sign   = (face < 2u) ? 1.0f : -1.0f;
        const bool      on_x   = (face % 2u) == 0u;
        const float     bud    = least * random.next_range(0.3f, 0.45f);
        const float     slide  = 0.55f * next_signed(random);
        const float     up     = random.next_range(0.1f, 0.85f) * (size.y - (2.0f * bud));
        const glm::vec3 normal = on_x ? ((sign * size.x) * ax) : ((sign * size.z) * az);
        const glm::vec3 aside  = on_x ? ((slide * size.z) * az) : ((slide * size.x) * ax);
        add_turned_box(builder, base_center + normal + aside + (up * y_axis), glm::vec3{bud, 2.0f * bud, bud}, yaw);
    }
}

void generate_blocky_tree(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Blocky_form form         = get_blocky_form(values.get_enum("form"));
    const float       trunk_height = values.get_float("trunk_height");
    const float       canopy       = values.get_float("canopy_size");
    const int         blocks       = std::clamp(values.get_int("blocks"), 1, blocky_block_max);
    Kit_random random{values.get_seed(), "blocky tree"};
    Kit_mesh_builder builder;

    const float trunk = std::min(std::max(0.13f * canopy, 0.1f), 0.25f * trunk_height); // half the trunk's width
    const float root  = std::min(0.14f * trunk_height, 1.2f * trunk);
    const float turn  = 0.2f * next_signed(random);
    builder.set_slot(blocky_bark_slot);
    add_turned_box(builder, glm::vec3{0.0f},                    glm::vec3{1.5f * trunk, root, 1.5f * trunk}, turn);
    add_turned_box(builder, glm::vec3{0.0f, 0.5f * root, 0.0f}, glm::vec3{trunk, trunk_height + (0.25f * canopy) - (0.5f * root), trunk}, turn);
    {
        // A root block against one side of the foot.
        const float toward = turn + (0.5f * pi * static_cast<float>(random.next_u32() % 4u));
        const float size   = trunk * random.next_range(0.5f, 0.7f);
        add_turned_box(builder, (1.5f * trunk) * around_y(toward), glm::vec3{size, 0.7f * root, size}, turn + 0.3f);
    }

    const float yaw_range = 0.22f;
    if (form == Blocky_form::cube) {
        // A cube on the trunk and smaller ones around it, each higher than the one before.
        const float     main = canopy * ((blocks == 1) ? 0.5f : 0.3f); // half the main cube
        const float     yaw  = turn + (yaw_range * next_signed(random));
        const glm::vec3 center{0.0f, trunk_height, 0.0f};
        add_canopy_block(builder, center, glm::vec3{main, 2.0f * main, main}, yaw, random);
        const float first = random.next_range(0.0f, two_pi);
        for (int k = 1; k < blocks; ++k) {
            const float     half   = main * (0.78f - (0.08f * static_cast<float>(k))) * random.next_range(0.9f, 1.05f);
            const float     angle  = first + (two_pi * static_cast<float>(k) / 3.0f) + (0.3f * next_signed(random));
            const float     rise   = (2.0f * main) * ((0.3f * static_cast<float>(k - 1)) - 0.12f + (0.06f * next_signed(random)));
            const glm::vec3 offset = (1.15f * main) * around_y(angle);
            // The lowest hangs under the main cube's foot, but stays off the ground.
            add_canopy_block(builder, center + offset + (std::max(rise, -0.5f * trunk_height) * y_axis), glm::vec3{half, 2.0f * half, half}, yaw + (0.5f * next_signed(random)), random);
        }
    } else if (form == Blocky_form::tiers) {
        // Slabs on one another, each smaller than the one below and turned against it.
        const float total = canopy * (0.45f + (0.2f * static_cast<float>(blocks)));
        const float tier  = total / static_cast<float>(blocks);
        for (int k = 0; k < blocks; ++k) {
            const float     half   = 0.5f * canopy * (1.0f - (0.21f * static_cast<float>(k))) * ((blocks == 1) ? 0.8f : 1.0f);
            const float     yaw    = turn + ((((k % 2) == 0) ? 1.0f : -1.0f) * random.next_range(0.08f, yaw_range));
            const float     offset_length = 0.06f * canopy * random.next_unit();
            const glm::vec3 offset        = offset_length * around_y(random.next_range(0.0f, two_pi));
            const float     low    = trunk_height + (tier * static_cast<float>(k)) - ((k == 0) ? 0.0f : (0.12f * tier));
            const float     high   = trunk_height + (tier * static_cast<float>(k + 1));
            add_canopy_block(builder, glm::vec3{offset.x, low, offset.z}, glm::vec3{half, high - low, half}, yaw, random);
        }
    } else {
        // A cube on the trunk and one on a branch for each further block.
        const float     top = 0.3f * canopy; // half the top cube
        const glm::vec3 center{0.0f, trunk_height, 0.0f};
        add_canopy_block(builder, center, glm::vec3{top, 2.0f * top, top}, turn + (yaw_range * next_signed(random)), random);
        const float first = random.next_range(0.0f, two_pi);
        for (int k = 1; k < blocks; ++k) {
            const float     half  = canopy * random.next_range(0.17f, 0.23f);
            const float     angle = first + (two_pi * static_cast<float>(k) / static_cast<float>(blocks - 1)) + (0.35f * next_signed(random));
            const float     reach = (0.5f * canopy) - half + (0.1f * canopy * random.next_unit());
            const float     low   = trunk_height * random.next_range(0.5f, 0.85f);
            const glm::vec3 out   = around_y(angle);
            const glm::vec3 base  = (reach * out) + (low * y_axis);
            add_canopy_block(builder, base, glm::vec3{half, 2.0f * half, half}, angle + (yaw_range * next_signed(random)), random);
            builder.set_slot(blocky_bark_slot);
            const float from = low * random.next_range(0.55f, 0.75f);
            builder.add_beam((from * y_axis) + ((0.5f * trunk) * out), base + ((0.6f * half) * y_axis), 1.1f * trunk, 1.1f * trunk);
        }
    }
    builder.write(mesh);
}

//
// Stone lantern: a stepped pedestal, a shaft with a belt, a flaring platform, a light box of window panels between
// corner posts around a glowing flame, a broad cap with a curved slope and upturned corners, and a jewel.
//

constexpr uint32_t lantern_stone_slot = 0;
constexpr uint32_t lantern_glow_slot  = 1;
constexpr int      lantern_round      = 12; // points around the shaft of a hexagonal lantern
constexpr int      lantern_jewel      = 8;  // points around the jewel and its collar
constexpr float    lantern_flame_low  = 0.604f; // the flame's foot, just inside the sill, in shares of the height
constexpr float    lantern_flame_high = 0.646f; // its tip, under the light frame

enum class Lantern_cap : unsigned int { square = 0, hexagonal };

[[nodiscard]] auto get_lantern_cap(const std::string_view name) -> Lantern_cap
{
    return (name == "square") ? Lantern_cap::square : Lantern_cap::hexagonal;
}

// Corner j of the plan in half widths, counter-clockwise seen from above; mirror images are equal to the bit.
[[nodiscard]] auto get_lantern_corner(const Lantern_cap cap, const int j) -> glm::vec2
{
    if (cap == Lantern_cap::hexagonal) {
        return get_kit_circle_point(j % 6, 6);
    }
    constexpr std::array<glm::vec2, 4> corners{{{1.0f, -1.0f}, {-1.0f, -1.0f}, {-1.0f, 1.0f}, {1.0f, 1.0f}}};
    return corners[static_cast<std::size_t>(j % 4)];
}

// The lantern's sizes at the values: heights in shares of the height, plan sizes in shares of the half width.
class Lantern_form
{
public:
    explicit Lantern_form(const Kit_parameter_values& values)
        : cap    {get_lantern_cap(values.get_enum("cap"))}
        , height {values.get_float("height")}
        , half   {0.5f * values.get_float("width")}
        , curve  {values.get_float("cap_curve")}
        , corners{(cap == Lantern_cap::hexagonal) ? 6 : 4}
        , run    {(cap == Lantern_cap::hexagonal) ? 6 : 8}
        , apothem{(cap == Lantern_cap::hexagonal) ? (0.5f * std::sqrt(3.0f)) : 1.0f}
        , box_low  {0.600f * height}
        , box_high {0.745f * height}
        , panel_out{0.50f * apothem * half}
        , panel    {0.05f * half}
    {
    }

    [[nodiscard]] auto get_light_height() const -> float
    {
        return 0.5f * (box_low + box_high);
    }

    Lantern_cap cap;
    float       height;
    float       half;      // half the width: the plan's unit
    float       curve;
    int         corners;
    int         run;       // points to a side of the cap
    float       apothem;   // of the plan, in half widths
    float       box_low;   // the windows' sill
    float       box_high;  // the windows' head
    float       panel_out; // the window panels' outer faces from the axis
    float       panel;     // their thickness
};

// A ring of the plan at the scale (in half widths) and height: `run` points to a side. Toward the corners the ring
// rises by up to `lift`, and its sides bow inward by `bow` of the scale at their middles.
void add_lantern_ring(std::vector<glm::vec3>& points, const Lantern_form& form, const int run, const float scale, const float y, const float lift = 0.0f, const float bow = 0.0f)
{
    for (int j = 0; j < form.corners; ++j) {
        const glm::vec2 a = get_lantern_corner(form.cap, j);
        const glm::vec2 b = get_lantern_corner(form.cap, j + 1);
        for (int k = 0; k < run; ++k) {
            // Weights and the distance from the side's middle from integers: a side's two halves mirror to the bit.
            const float     wa     = static_cast<float>(run - k) / static_cast<float>(run);
            const float     wb     = static_cast<float>(k) / static_cast<float>(run);
            const float     corner = static_cast<float>(std::abs(run - (2 * k))) / static_cast<float>(run);
            const glm::vec2 p      = ((wa * a) + (wb * b)) * (form.half * scale * (1.0f - (bow * (1.0f - (corner * corner)))));
            points.push_back(glm::vec3{p.x, y + (lift * corner * corner), p.y});
        }
    }
}

void add_round_ring(std::vector<glm::vec3>& points, const int count, const float radius, const float y)
{
    for (int i = 0; i < count; ++i) {
        const glm::vec2 p = radius * get_kit_circle_point(i, count);
        points.push_back(glm::vec3{p.x, y, p.y});
    }
}

// A solid of rings bottom to top, `count` points each, counter-clockwise seen from above, closed by fans to the two
// cap points on the axis. run == 1: the bands are planar quads; else each ring is sides of `run` points and a band's
// quads are split by the diagonal that leaves the side's middle, so a side's two halves are mirror images.
void add_lantern_solid(Kit_mesh_builder& builder, const std::span<const glm::vec3> points, const int count, const int run, const float bottom_y, const float top_y)
{
    const uint32_t n     = static_cast<uint32_t>(count);
    const uint32_t rings = static_cast<uint32_t>(points.size()) / n;
    const uint32_t first = static_cast<uint32_t>(builder.get_positions().size());
    for (const glm::vec3& point : points) {
        builder.add_vertex(point);
    }
    const uint32_t bottom = builder.add_vertex(glm::vec3{0.0f, bottom_y, 0.0f});
    const uint32_t top    = builder.add_vertex(glm::vec3{0.0f, top_y, 0.0f});
    const uint32_t last   = first + ((rings - 1) * n);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t j = (i + 1) % n;
        builder.add_triangle(bottom, first + j, first + i);
        builder.add_triangle(last + i, last + j, top);
    }
    for (uint32_t r = 0; (r + 1) < rings; ++r) {
        const uint32_t a = first + (r * n);
        const uint32_t b = a + n;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t j = (i + 1) % n;
            if (run == 1) {
                builder.add_quad(a + i, a + j, b + j, b + i);
            } else if ((2u * (i % static_cast<uint32_t>(run))) < static_cast<uint32_t>(run)) {
                builder.add_triangle(a + i, a + j, b + i);
                builder.add_triangle(a + j, b + j, b + i);
            } else {
                builder.add_triangle(a + i, a + j, b + j);
                builder.add_triangle(a + i, b + j, b + i);
            }
        }
    }
}

// Scale and height of one ring of a profile.
class Lantern_step
{
public:
    float scale;
    float y;
};

// A solid of plan rings with a point to a corner, from the profile's steps (heights in shares of the height).
void add_lantern_profile(Kit_mesh_builder& builder, const Lantern_form& form, const std::span<const Lantern_step> steps)
{
    std::vector<glm::vec3> points;
    for (const Lantern_step& step : steps) {
        add_lantern_ring(points, form, 1, step.scale, step.y * form.height);
    }
    add_lantern_solid(builder, points, form.corners, 1, steps.front().y * form.height, steps.back().y * form.height);
}

// A solid of round rings from the profile's steps (radii in half widths, heights in shares of the height), ending in
// a point at top_y where that is over the last ring.
void add_lantern_lathe(Kit_mesh_builder& builder, const Lantern_form& form, const int count, const std::span<const Lantern_step> steps, const float top_y)
{
    std::vector<glm::vec3> points;
    for (const Lantern_step& step : steps) {
        add_round_ring(points, count, step.scale * form.half, step.y * form.height);
    }
    add_lantern_solid(builder, points, count, 1, steps.front().y * form.height, top_y * form.height);
}

// Points of a window panel's outline and of its opening, in units of the half sizes: unit square points and unit
// circle points at the same angles, counter-clockwise from the first; mirror images are equal to the bit.
constexpr float lantern_tan_15 = 0.26794919f;
constexpr float lantern_cos_15 = 0.96592583f;
constexpr float lantern_sin_15 = 0.25881905f;
constexpr float lantern_cos_45 = 0.70710678f;
constexpr std::array<glm::vec2, 8> lantern_square_8{{
    {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}, {-1.0f, 1.0f}, {-1.0f, 0.0f}, {-1.0f, -1.0f}, {0.0f, -1.0f}, {1.0f, -1.0f}
}};
constexpr std::array<glm::vec2, 12> lantern_square_12{{
    {1.0f, lantern_tan_15},   {1.0f, 1.0f},   {lantern_tan_15, 1.0f},   {-lantern_tan_15, 1.0f},  {-1.0f, 1.0f},  {-1.0f, lantern_tan_15},
    {-1.0f, -lantern_tan_15}, {-1.0f, -1.0f}, {-lantern_tan_15, -1.0f}, {lantern_tan_15, -1.0f},  {1.0f, -1.0f},  {1.0f, -lantern_tan_15}
}};
constexpr std::array<glm::vec2, 12> lantern_circle_12{{
    {lantern_cos_15, lantern_sin_15},   {lantern_cos_45, lantern_cos_45},   {lantern_sin_15, lantern_cos_15},
    {-lantern_sin_15, lantern_cos_15},  {-lantern_cos_45, lantern_cos_45},  {-lantern_cos_15, lantern_sin_15},
    {-lantern_cos_15, -lantern_sin_15}, {-lantern_cos_45, -lantern_cos_45}, {-lantern_sin_15, -lantern_cos_15},
    {lantern_sin_15, -lantern_cos_15},  {lantern_cos_45, -lantern_cos_45},  {lantern_cos_15, -lantern_sin_15}
}};

enum class Lantern_window : unsigned int { open = 0, moon };

// A window panel: a slab of the thickness behind `center` on its outer face, half_size wide and high, around an
// opening: a rectangle inside a frame, or a round moon window.
void add_lantern_panel(Kit_mesh_builder& builder, const glm::vec3& center, const glm::vec3& normal, const glm::vec2 half_size, const float thickness, const Lantern_window window)
{
    const glm::vec3 right{normal.z, 0.0f, -normal.x}; // seen from outside
    const float     least  = std::min(half_size.x, half_size.y);
    const bool      moon   = window == Lantern_window::moon;
    const uint32_t  count  = moon ? 12u : 8u;
    const glm::vec2 inside = moon ? glm::vec2{0.64f * least} : (half_size - glm::vec2{0.26f * least});
    const uint32_t  first  = static_cast<uint32_t>(builder.get_positions().size());
    // Per point: the outline's front and back, the opening's front and back.
    for (uint32_t i = 0; i < count; ++i) {
        const glm::vec2 outer = half_size * (moon ? lantern_square_12[i] : lantern_square_8[i]);
        const glm::vec2 inner = inside * (moon ? lantern_circle_12[i] : lantern_square_8[i]);
        const glm::vec3 back  = -thickness * normal;
        const glm::vec3 outer_point = center + (outer.x * right) + (outer.y * y_axis);
        const glm::vec3 inner_point = center + (inner.x * right) + (inner.y * y_axis);
        builder.add_vertex(outer_point);
        builder.add_vertex(outer_point + back);
        builder.add_vertex(inner_point);
        builder.add_vertex(inner_point + back);
    }
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t a = first + (4u * i);
        const uint32_t b = first + (4u * ((i + 1u) % count));
        builder.add_quad(a,      b,      b + 2u, a + 2u); // front
        builder.add_quad(b + 1u, a + 1u, a + 3u, b + 3u); // back
        builder.add_quad(a,      a + 1u, b + 1u, b);      // outline
        builder.add_quad(a + 2u, b + 2u, b + 3u, a + 3u); // opening
    }
}

void generate_stone_lantern(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Lantern_form form{values};
    const bool         hexagonal = form.cap == Lantern_cap::hexagonal;
    const float        h         = form.height;
    const float        r         = form.half;
    Kit_mesh_builder builder;
    builder.set_slot(lantern_stone_slot);

    // The pedestal: a ground slab, a step and a shoulder falling away from the shaft's foot.
    {
        constexpr std::array<Lantern_step, 8> steps{{
            {0.86f, 0.000f}, {0.86f, 0.034f}, {0.78f, 0.041f}, {0.78f, 0.060f}, {0.70f, 0.070f}, {0.58f, 0.104f}, {0.42f, 0.128f}, {0.36f, 0.136f}
        }};
        add_lantern_profile(builder, form, steps);
    }
    // The shaft: round under a hexagonal cap, square under a square one, with rings at its foot, middle and head.
    {
        const float wide = hexagonal ? 0.285f : 0.25f;
        const float thin = hexagonal ? 0.225f : 0.195f;
        const std::array<Lantern_step, 10> steps{{
            {wide, 0.128f}, {wide, 0.150f}, {thin, 0.157f}, {thin, 0.298f}, {wide, 0.304f}, {wide, 0.330f},
            {thin, 0.336f}, {thin, 0.478f}, {wide, 0.485f}, {wide, 0.500f}
        }};
        if (hexagonal) {
            add_lantern_lathe(builder, form, lantern_round, steps, steps.back().y);
        } else {
            add_lantern_profile(builder, form, steps);
        }
    }
    // The platform: it flares from the shaft's head to a slab under the light box.
    {
        constexpr std::array<Lantern_step, 6> steps{{
            {0.30f, 0.495f}, {0.38f, 0.512f}, {0.62f, 0.548f}, {0.72f, 0.560f}, {0.72f, 0.584f}, {0.66f, 0.592f}
        }};
        add_lantern_profile(builder, form, steps);
    }
    // The light box: a sill and a head, posts on the corners and a window panel to a side around the glowing flame.
    // The open windows face +Z and -Z, the moon windows the other sides.
    {
        constexpr std::array<Lantern_step, 2> sill{{{0.57f, 0.584f}, {0.57f, 0.606f}}};
        constexpr std::array<Lantern_step, 2> head{{{0.57f, 0.739f}, {0.57f, 0.762f}}};
        add_lantern_profile(builder, form, sill);
        add_lantern_profile(builder, form, head);
        const float     low       = form.box_low  - (0.004f * h);
        const float     high      = form.box_high + (0.004f * h);
        const float     post      = 0.05f * r;
        // A little short of the corner, behind the post: two panels share no point.
        const float     half_side = 0.97f * form.panel_out * (hexagonal ? (1.0f / std::sqrt(3.0f)) : 1.0f);
        for (int j = 0; j < form.corners; ++j) {
            const glm::vec2 a      = get_lantern_corner(form.cap, j);
            const glm::vec2 b      = get_lantern_corner(form.cap, j + 1);
            const glm::vec2 out    = glm::normalize(a + b);
            const glm::vec3 normal{out.x, 0.0f, out.y};
            const bool      open   = std::abs(out.y) > 0.9f;
            const glm::vec3 center = (form.panel_out * normal) + ((0.5f * (low + high)) * y_axis);
            add_lantern_panel(builder, center, normal, glm::vec2{half_side, 0.5f * (high - low)}, form.panel, open ? Lantern_window::open : Lantern_window::moon);
            // The post on corner j: a box along the corner's radius.
            const glm::vec2 radial  = glm::normalize(a);
            const glm::vec2 aside{-radial.y, radial.x};
            const glm::vec2 corner  = a * (form.panel_out / form.apothem);
            const std::array<glm::vec2, 4> signs{{{1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}, {-1.0f, -1.0f}}};
            std::array<uint32_t, 8> v{};
            for (std::size_t k = 0; k < 4; ++k) {
                const glm::vec2 p = corner + ((signs[k].x * post) * radial) + ((signs[k].y * post) * aside);
                v[k]     = builder.add_vertex(glm::vec3{p.x, low, p.y});
                v[k + 4] = builder.add_vertex(glm::vec3{p.x, high, p.y});
            }
            builder.add_quad(v[0], v[1], v[2], v[3]);
            builder.add_quad(v[7], v[6], v[5], v[4]);
            for (std::size_t k = 0; k < 4; ++k) {
                const std::size_t next = (k + 1) % 4;
                builder.add_quad(v[k], v[k + 4], v[next + 4], v[next]);
            }
        }
        // The glowing core is a low flame on the sill, under the light's frame, so the light is enclosed by no geometry and
        // reaches out through the windows with shadow casting on.
        const float core = (form.panel_out - form.panel - (0.03f * r)) / (form.apothem * r);
        const std::array<Lantern_step, 3> steps{{{0.30f * core, lantern_flame_low}, {0.36f * core, 0.624f}, {0.12f * core, lantern_flame_high}}};
        builder.set_slot(lantern_glow_slot);
        add_lantern_profile(builder, form, steps);
        builder.set_slot(lantern_stone_slot);
    }
    // The cap: from its underside out to the eaves, up their edge and up the slope, steep at the top and level at the
    // eaves. The corners turn up and the sides bow in with the curve.
    {
        const float rise    = 0.108f * h;
        const float eaves   = 0.780f * h; // the eaves' lower edge at a side's middle
        const float edge    = 0.022f * h;
        const float lift    = form.curve * std::min(0.36f * r, 0.08f * h);
        const float bow     = 0.07f * form.curve;
        std::vector<glm::vec3> points;
        add_lantern_ring(points, form, form.run, 0.60f, 0.757f * h);
        add_lantern_ring(points, form, form.run, 1.0f, eaves, lift, bow);
        add_lantern_ring(points, form, form.run, 1.0f, eaves + edge, lift, bow);
        constexpr std::array<float, 4> shares{0.76f, 0.54f, 0.35f, 0.20f};
        for (const float share : shares) {
            const float steep = (1.0f - share) / (1.0f - shares.back());
            add_lantern_ring(points, form, form.run, share, eaves + edge + (rise * steep * std::sqrt(steep)), lift * share * share * share, bow * share);
        }
        const int count = form.corners * form.run;
        add_lantern_solid(builder, points, count, form.run, 0.757f * h, eaves + edge + rise);
        // A ridge down the slope to every corner: a three-sided beam over the corner's points from the eaves up.
        const float       ridge_w = 0.045f * r;
        const float       ridge_h = 0.014f * h;
        const std::size_t rings   = shares.size() + 1;
        for (int j = 0; j < form.corners; ++j) {
            const glm::vec2 radial = glm::normalize(get_lantern_corner(form.cap, j));
            const glm::vec3 aside{-ridge_w * radial.y, 0.0f, ridge_w * radial.x};
            // The foot lies under the slope and inside the corner: the cap's corners stay the bounds.
            const glm::vec3 inward{1.25f * ridge_w * radial.x, std::min(1.5f * ridge_w, 0.9f * edge), 1.25f * ridge_w * radial.y};
            std::array<uint32_t, 3 * 5> v{};
            for (std::size_t ring = 0; ring < rings; ++ring) {
                const glm::vec3 p = points[((ring + 2) * static_cast<std::size_t>(count)) + static_cast<std::size_t>(j * form.run)];
                v[(3 * ring)]     = builder.add_vertex(p - inward - aside);
                v[(3 * ring) + 1] = builder.add_vertex(p - inward + aside);
                v[(3 * ring) + 2] = builder.add_vertex(p + (ridge_h * y_axis));
            }
            const std::size_t last = 3 * (rings - 1);
            builder.add_triangle(v[1], v[0], v[2]);
            builder.add_triangle(v[last], v[last + 1], v[last + 2]);
            for (std::size_t ring = 0; (ring + 1) < rings; ++ring) {
                const std::size_t a = 3 * ring;
                const std::size_t b = a + 3;
                builder.add_quad(v[a], v[a + 1], v[b + 1], v[b]);
                builder.add_triangle(v[a + 2], v[a], v[b + 2]);
                builder.add_triangle(v[b + 2], v[a], v[b]);
                builder.add_triangle(v[a + 1], v[a + 2], v[b + 2]);
                builder.add_triangle(v[b + 1], v[a + 1], v[b + 2]);
            }
        }
    }
    // The jewel on its collar.
    {
        constexpr std::array<Lantern_step, 4> collar{{{0.17f, 0.895f}, {0.25f, 0.918f}, {0.25f, 0.928f}, {0.14f, 0.934f}}};
        constexpr std::array<Lantern_step, 4> jewel {{{0.10f, 0.928f}, {0.185f, 0.948f}, {0.195f, 0.962f}, {0.13f, 0.982f}}};
        add_lantern_lathe(builder, form, lantern_jewel, collar, collar.back().y);
        add_lantern_lathe(builder, form, lantern_jewel, jewel, 1.0f);
    }
    builder.write(mesh);
}

} // anonymous namespace

void add_build_21_24_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.bridge_deck";
        shape.kit             = "architecture";
        shape.display_name    = "Bridge deck";
        shape.description     =
            "Walkable deck along X between its two ends: a slab under paving stones in broken courses between two kerbs, "
            "walls under copings or open railings, with posts over the supports. It lies on two beams and cross beams, "
            "stands on piers under beams, or on arches of voussoirs with a keystone between piers on plinths. Half piers "
            "and half posts stand at the ends, so two decks joined make whole ones.";
        shape.tags            = {"walkable", "bridge", "floor", "tiles", "size:4x2"};
        shape.parameters      = {
            derive_float(make_length_parameter("length",         "Size along X",                                     4.0f, Kit_default_scale::modules, 1.0f, 32.0f), 3.0f, 8.0f, 1.0f),
            // In meters and not derived: the agent fixes the least width at every level length (R2.10).
            make_length_parameter("width", "Size along Z; a parapet's walls and posts take about a third of it", 2.0f, Kit_default_scale::meters, 1.2f, 8.0f),
            derive_float(make_length_parameter("thickness",      "Thickness of the slab; at most 0.4 of the height", 0.3f, Kit_default_scale::meters,  0.1f, 1.0f),  0.2f, 0.4f, 0.05f),
            derive_float(make_length_parameter("height",         "Height of the walking surface on piers or arches; a deck without supports lies on its beams", 0.75f, Kit_default_scale::wall_heights, 0.5f, 16.0f), 0.5f, 1.25f, 0.05f),
            derive_enum (make_enum_parameter  ("supports",       "Beams alone, piers under beams, or arches between piers", {"none", "piers", "arched"}, 2)),
            derive_int  (make_count_parameter ("spans",          "Bays between supports",                            1, 1, bridge_span_max), 1, 2),
            derive_enum (make_enum_parameter  ("parapet",        "Kerbs alone, walls under copings, or railings",    {"none", "solid", "open"}, 1)),
            derive_float(make_length_parameter("parapet_height", "Height of the walls or railings over the walking surface", 0.7f, Kit_default_scale::meters, 0.2f, 1.5f), 0.5f, 1.0f, 0.1f),
            derive_int  (make_count_parameter ("courses",        "Courses of paving stones along the deck",          8, 1, bridge_course_max), 6, 10),
            make_seed_parameter()
        };
        shape.material_slots    = {"stone", "floor", "trim"};
        shape.normal_style      = Kit_normal_style::faceted;
        shape.triangle_budget   = 760;
        shape.collision         = Kit_collision::triangle_mesh;
        shape.generator         = generate_bridge_deck;
        shape.mirror_planes     = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.check_walkability = check_bridge_walkability;
        shape.get_connectors    = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const Bridge_form form{values};
            add_side_connector(out_connectors, "start", glm::vec3{-form.half_l, form.top, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "end",   glm::vec3{ form.half_l, form.top, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
            out_connectors.push_back(Kit_connector{.name = "base", .position = glm::vec3{0.0f}, .outward = glm::vec3{0.0f, -1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.buttress";
        shape.kit             = "architecture";
        shape.display_name    = "Buttress";
        shape.description     =
            "Medieval buttress against a wall face at its back: stages of stone on a chamfered plinth, each standing less "
            "far out and a little narrower than the one below, under a sloping offset over a drip course. The last stage "
            "ends under a slope back to the wall, a gablet, or a pinnacle with a spire.";
        shape.tags            = {"obstacle", "buttress", "medieval", "size:0.5x1"};
        shape.parameters      = {
            derive_float(make_length_parameter("height", "Size along Y",                                          1.0f, Kit_default_scale::wall_heights, 1.0f, 16.0f), 0.8f,  1.2f, 0.05f),
            derive_float(make_length_parameter("depth",  "Size along Z: how far the foot stands out of the wall", 1.0f, Kit_default_scale::modules,      0.3f, 6.0f),  0.75f, 1.5f, 0.25f),
            derive_float(make_length_parameter("width",  "Size along X",                                          0.5f, Kit_default_scale::modules,      0.2f, 3.0f),  0.4f,  0.7f, 0.05f),
            derive_int  (make_count_parameter ("steps",  "Stages",                                                3, 1, buttress_step_max), 1, 4),
            derive_enum (make_enum_parameter  ("cap",    "A slope back to the wall, a gablet or a pinnacle",      {"weathered", "gabled", "pinnacle"}, 1)),
            make_seed_parameter()
        };
        shape.material_slots  = {"stone", "trim"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 110;
        shape.collision       = Kit_collision::convex_hull;
        shape.generator       = generate_buttress;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x};
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            add_side_connector(out_connectors, "back", glm::vec3{0.0f, 0.0f, -0.5f * values.get_float("depth")}, glm::vec3{0.0f, 0.0f, -1.0f});
            out_connectors.push_back(Kit_connector{.name = "base", .position = glm::vec3{0.0f}, .outward = glm::vec3{0.0f, -1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.log";
        shape.kit             = "nature";
        shape.display_name    = "Log";
        shape.description     =
            "Dead wood at its origin. Log: a trunk lying along X, pressed a little into the ground, its two ends cut on a "
            "slant and showing bark, sapwood and heartwood. Stump: a trunk 0.4 of the length high on spreading roots, cut "
            "on a slant. Driftwood: a barkless trunk thinning to broken points that rise off the ground. Branch stubs "
            "leave the trunk: cut short, or long and pointed on driftwood.";
        shape.tags            = {"obstacle", "decoration", "wood", "size:2x0.4"};
        shape.parameters      = {
            derive_enum(make_enum_parameter("form", "A lying log, a stump or driftwood", {"log", "stump", "driftwood"}, 0)),
            from_seed_by_default(derive_float(make_length_parameter("length", "Size along X; a stump is 0.4 of it high", 2.0f, Kit_default_scale::meters, 0.3f, 8.0f), 0.5f, 4.0f, 0.25f)),
            from_seed_by_default(derive_float(make_length_parameter("width",  "Width of the trunk",                      0.4f, Kit_default_scale::meters, 0.1f, 2.0f), 0.2f, 0.8f, 0.05f)),
            from_seed_by_default(derive_float(make_ratio_parameter("bend", "How far a lying trunk bows toward -Z at its ends, and a stump leans", 0.3f), 0.0f, 0.6f, 0.05f)),
            derive_int(make_count_parameter("stubs", "Branch stubs", 2, 0, log_stub_max), 0, 4),
            make_seed_parameter (),
            make_count_parameter("segments", "Points around the trunk", 10, 6, 14)
        };
        shape.material_slots  = {"bark", "sapwood", "heartwood"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 380;
        shape.collision       = Kit_collision::convex_hull;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_log;
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const Log_values log{values};
            const float top = (log.form == Log_form::stump) ? get_stump_height(log) : (get_lying_axis_height(log.radius, log.sides) + log.radius);
            add_base_and_top_connectors(out_connectors, top);
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.shell";
        shape.kit             = "nature";
        shape.display_name    = "Shell";
        shape.description     =
            "Sea life left on the sand at its origin. Conch: a spire of stepped whorls with knobs on their shoulders, a "
            "body whorl and a canal along X, the outer lip flaring over the coral aperture. Scallop: a round ribbed valve "
            "with two ears at its hinge, bands of colour along its ribs. Starfish: arms about a disc, each turning aside "
            "toward its tip, with knobs along the ridges.";
        shape.tags            = {"decoration", "beach", "size:0.3x0.3"};
        shape.parameters      = {
            derive_enum(make_enum_parameter("form", "Conch, scallop or starfish", {"conch", "scallop", "starfish"}, 0)),
            from_seed_by_default(derive_float(make_length_parameter("size", "A conch's length, a scallop's width, a starfish's width", 0.3f, Kit_default_scale::meters, 0.05f, 2.0f), 0.1f, 0.6f, 0.05f)),
            derive_int(make_count_parameter("ribs", "A scallop's ribs, a starfish's arms, the knobs around a conch's whorl", 8, shell_rib_min, shell_rib_max), 5, 12),
            make_seed_parameter()
        };
        shape.material_slots  = {"shell", "coral"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 340;
        shape.collision       = Kit_collision::none;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_shell;
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.blocky_tree";
        shape.foliage_radius  = 1.5f;
        shape.kit             = "nature";
        shape.display_name    = "Blocky tree";
        shape.description     =
            "Tree of boxes at its origin: a square trunk on a root block under one to four canopy blocks, each a little "
            "turned, with a lighter block on its top (slot shoot) and small blocks standing out of its sides. Cube: a "
            "cube on the trunk and smaller ones around it. Tiers: slabs on one another, each smaller than the one below. "
            "Cluster: a cube on the trunk and one on a branch for each further block.";
        shape.tags            = {"obstacle", "decoration", "tree", "size:2x2x4"};
        shape.parameters      = {
            derive_enum(make_enum_parameter("form", "A cube with smaller ones, tiers, or cubes on branches", {"cube", "tiers", "cluster"}, 0)),
            from_seed_by_default(derive_float(make_length_parameter("trunk_height", "Height of the trunk under the canopy", 0.6f, Kit_default_scale::wall_heights, 0.3f, 8.0f), 0.4f, 0.8f, 0.05f)),
            from_seed_by_default(derive_float(make_length_parameter("canopy_size",  "Width of the canopy",                  2.0f, Kit_default_scale::modules,      0.5f, 8.0f), 1.5f, 3.0f, 0.25f)),
            from_seed_by_default(derive_int(make_count_parameter("blocks", "Canopy blocks", 3, 1, blocky_block_max), 1, 4)),
            make_seed_parameter()
        };
        shape.material_slots  = {"bark", "foliage", "shoot"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 280;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_blocky_tree;
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.stone_lantern";
        shape.kit             = "architecture";
        shape.display_name    = "Stone lantern";
        shape.description     =
            "Japanese stone lantern at its origin: a stepped pedestal, a shaft with a belt, a flaring platform, a light "
            "box and a broad cap under a jewel. The light box has open windows toward +Z and -Z and round moon windows "
            "on its other sides, around a glowing flame (slot glow) under its one point light. The cap's slope is "
            "steep at the top and level at the eaves; its corners turn up with cap_curve. A hexagonal cap stands on a "
            "round shaft, a square one on a square shaft.";
        shape.tags            = {"decoration", "obstacle", "light", "lantern", "japanese", "size:0.6x1.5"};
        shape.parameters      = {
            derive_float(make_length_parameter("height", "Size along Y", 1.5f, Kit_default_scale::meters, 0.6f, 4.0f), 1.2f, 2.1f, 0.1f),
            derive_float(make_length_parameter("width",  "Width of the cap along X; a hexagonal cap has two corners on X", 0.6f, Kit_default_scale::meters, 0.3f, 2.0f), 0.5f, 0.8f, 0.05f),
            derive_enum (make_enum_parameter  ("cap",    "The plan of the cap, the light box, the platform and the pedestal", {"square", "hexagonal"}, 1)),
            from_seed_by_default(derive_float(make_ratio_parameter("cap_curve", "How far the cap's corners turn up and its sides bow in", 0.5f), 0.2f, 0.9f, 0.05f)),
            make_seed_parameter()
        };
        shape.material_slots   = {"stone", "glow"};
        shape.normal_style     = Kit_normal_style::faceted;
        shape.triangle_budget  = 1880;
        shape.collision        = Kit_collision::convex_hull;
        shape.generator        = generate_stone_lantern;
        shape.mirror_planes    = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.get_connectors   = [](const Kit_parameter_values&, std::vector<Kit_connector>& out_connectors) {
            out_connectors.push_back(Kit_connector{.name = "base", .position = glm::vec3{0.0f}, .outward = glm::vec3{0.0f, -1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
        };
        shape.get_light_frames = [](const Kit_parameter_values& values, std::vector<Kit_light_frame>& out_frames) {
            const Lantern_form form{values};
            // A flame behind stone: dimmer and shorter of reach than a lamp.
            out_frames.push_back(Kit_light_frame{
                .name      = "light",
                .position  = glm::vec3{0.0f, form.get_light_height(), 0.0f},
                .direction = glm::vec3{0.0f, -1.0f, 0.0f},
                .start     = Kit_light_start{.color = glm::vec3{1.0f, 0.72f, 0.42f}, .intensity = 12.0f, .range = 6.0f}
            });
        };
        shapes.push_back(std::move(shape));
    }
}

} // namespace editor
