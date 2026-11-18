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

// The shapes of build-order lines 17 - 20 of doc/plans/level_kit_more_shapes.md (doc/editor/level_tools.md).
// Every mesh's topology follows from its integer values and integer draws alone; floats only move vertices. The step
// count of the two stairs is the one integer taken from floats: Kit_stairs_layout::step_count, as basic.stairs.

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

// A box symmetric about z = 0.
void add_wall_box(Kit_mesh_builder& builder, const float x0, const float x1, const float low, const float high, const float half_z)
{
    builder.add_box(glm::vec3{x0, low, -half_z}, glm::vec3{x1, high, half_z});
}

//
// Timber bay: half posts at the ends, a top beam, a sill, a lintel under a tie rail on both faces and a plaster field
// above it; below the lintel the infill.
//

constexpr uint32_t timber_wood_slot = 0;
constexpr uint32_t timber_wall_slot = 1;
constexpr int      timber_panel_max = 4;
constexpr int      timber_rail_max  = 3;

enum class Timber_infill : unsigned int { open = 0, shoji, plaster, half };

[[nodiscard]] auto get_timber_infill(const std::string_view name) -> Timber_infill
{
    if (name == "shoji") {
        return Timber_infill::shoji;
    }
    if (name == "plaster") {
        return Timber_infill::plaster;
    }
    if (name == "half") {
        return Timber_infill::half;
    }
    return Timber_infill::open;
}

// The bay's sizes at the values; every member shrinks with a short or low bay.
class Timber_form
{
public:
    explicit Timber_form(const Kit_parameter_values& values)
        : infill   {get_timber_infill(values.get_enum("infill"))}
        , panels   {std::clamp(values.get_int("panels"), 1, timber_panel_max)}
        , rails    {std::clamp(values.get_int("rails"), 0, timber_rail_max)}
        , half_l   {0.5f * values.get_float("length")}
        , height   {values.get_float("height")}
        , post     {values.get_float("post_width")}
        , half_post{std::min(0.5f * post, 0.4f * half_l)}
        , clear    {half_l - half_post}
        , embed    {0.3f * half_post}
        , beam     {std::min(1.2f * post, 0.12f * height)}
        , lintel_y {0.62f * height}
        , lintel   {std::min(0.6f * post, 0.045f * height)}
        , tie      {std::min(0.8f * post, 0.055f * height)}
    {
        const bool low_sill = (infill == Timber_infill::open) || (infill == Timber_infill::shoji);
        sill = low_sill ? std::min(0.35f * post, 0.03f * height) : std::min(0.8f * post, 0.06f * height);
    }

    Timber_infill infill   {Timber_infill::shoji};
    int           panels   {2};
    int           rails    {1};
    float         half_l   {0.0f};
    float         height   {0.0f};
    float         post     {0.0f}; // the posts' width and depth
    float         half_post{0.0f}; // an end post's width: half a post
    float         clear    {0.0f}; // half the span between the posts
    float         embed    {0.0f}; // how far a member reaches into a post
    float         beam     {0.0f}; // height of the top beam
    float         lintel_y {0.0f}; // the lintel's underside
    float         lintel   {0.0f};
    float         tie      {0.0f};
    float         sill     {0.0f};
};

// One sliding screen from x0 to x1: a frame, a hip board under a mid rail and a lattice over paper.
void add_shoji_screen(Kit_mesh_builder& builder, const Timber_form& form, const float x0, const float x1, const float foot_y, const float head_y)
{
    const float width    = x1 - x0;
    const float half_z   = 0.16f * form.post;
    const float stile    = std::min(0.04f, 0.14f * width);
    const float bottom   = 1.5f * stile;
    const float hip_y    = foot_y + (0.24f * (head_y - foot_y));
    const float bar      = std::min(0.018f, 0.4f * stile);
    const int   uprights = (form.panels == 1) ? 5 : ((form.panels == 2) ? 3 : 2);
    const int   levels   = 2 + (2 * form.rails);
    builder.set_slot(timber_wood_slot);
    add_wall_box(builder, x0,                  x0 + stile,          foot_y,                  head_y,                 half_z);
    add_wall_box(builder, x1 - stile,          x1,                  foot_y,                  head_y,                 half_z);
    add_wall_box(builder, x0 + (0.6f * stile), x1 - (0.6f * stile), head_y - stile,          head_y,                 0.92f * half_z);
    add_wall_box(builder, x0 + (0.6f * stile), x1 - (0.6f * stile), foot_y,                  foot_y + bottom,        0.92f * half_z);
    add_wall_box(builder, x0 + (0.5f * stile), x1 - (0.5f * stile), foot_y + (0.5f * bottom), hip_y,                 0.45f * half_z);
    add_wall_box(builder, x0 + (0.4f * stile), x1 - (0.4f * stile), hip_y - (0.3f * stile),  hip_y + (0.7f * stile), 0.9f * half_z);
    const float field_x0   = x0 + stile;
    const float field_x1   = x1 - stile;
    const float field_low  = hip_y + (0.7f * stile);
    const float field_high = head_y - stile;
    for (int i = 1; i <= uprights; ++i) {
        const float x = field_x0 + ((field_x1 - field_x0) * static_cast<float>(i) / static_cast<float>(uprights + 1));
        add_wall_box(builder, x - (0.5f * bar), x + (0.5f * bar), field_low - (0.2f * stile), field_high + (0.4f * stile), 0.55f * half_z);
    }
    for (int i = 1; i <= levels; ++i) {
        const float y = field_low + ((field_high - field_low) * static_cast<float>(i) / static_cast<float>(levels + 1));
        add_wall_box(builder, field_x0 - (0.3f * stile), field_x1 + (0.3f * stile), y - (0.5f * bar), y + (0.5f * bar), 0.5f * half_z);
    }
    builder.set_slot(timber_wall_slot);
    add_wall_box(builder, x0 + (0.5f * stile), x1 - (0.5f * stile), hip_y + (0.2f * stile), head_y - (0.5f * stile), 0.2f * half_z);
}

// A plaster field from the sill up to top_y with its rails and studs; it reaches top_embed into what lies on it.
void add_plaster_field(Kit_mesh_builder& builder, const Timber_form& form, const float top_y, const float top_embed)
{
    const float depth = form.post;
    const float rail  = std::min(0.45f * form.post, 0.035f * form.height);
    const float stud  = std::min(0.6f * form.post, 0.3f * form.clear / static_cast<float>(form.panels));
    builder.set_slot(timber_wall_slot);
    add_wall_box(builder, -(form.clear + (0.5f * form.embed)), form.clear + (0.5f * form.embed), 0.5f * form.sill, top_y + (0.5f * top_embed), 0.2f * depth);
    builder.set_slot(timber_wood_slot);
    for (int i = 1; i <= form.rails; ++i) {
        const float y = form.sill + ((top_y - form.sill) * static_cast<float>(i) / static_cast<float>(form.rails + 1));
        add_wall_box(builder, -(form.clear + (0.8f * form.embed)), form.clear + (0.8f * form.embed), y - (0.5f * rail), y + (0.5f * rail), 0.3f * depth);
    }
    for (int i = 1; i < form.panels; ++i) {
        const float x = form.clear * static_cast<float>((2 * i) - form.panels) / static_cast<float>(form.panels);
        add_wall_box(builder, x - (0.5f * stud), x + (0.5f * stud), 0.6f * form.sill, top_y + (0.7f * top_embed), 0.36f * depth);
    }
}

void generate_timber_bay(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Timber_form form{values};
    const float depth = form.post;
    Kit_mesh_builder builder;
    builder.set_slot(timber_wood_slot);
    const float post_top = form.height - (0.5f * form.beam);
    const float tie_y    = form.lintel_y + (0.35f * form.lintel);
    add_wall_box(builder, -form.half_l,               -form.clear,              0.0f,                    post_top,                    0.5f * depth);
    add_wall_box(builder,  form.clear,                 form.half_l,             0.0f,                    post_top,                    0.5f * depth);
    add_wall_box(builder, -form.half_l,                form.half_l,             form.height - form.beam, form.height,                 0.6f * depth);
    add_wall_box(builder, -(form.clear + form.embed),  form.clear + form.embed, 0.0f,                    form.sill,                   0.42f * depth);
    add_wall_box(builder, -(form.clear + form.embed),  form.clear + form.embed, form.lintel_y,           form.lintel_y + form.lintel, 0.45f * depth);
    add_wall_box(builder, -form.half_l,                form.half_l,             tie_y,                   tie_y + form.tie,            0.62f * depth);
    builder.set_slot(timber_wall_slot);
    add_wall_box(builder, -(form.clear + (0.7f * form.embed)), form.clear + (0.7f * form.embed), form.lintel_y + (0.5f * form.lintel), form.height - (0.5f * form.beam), 0.2f * depth);

    if (form.infill == Timber_infill::shoji) {
        const float gap   = std::min(0.006f, 0.02f * form.clear);
        const float width = ((2.0f * form.clear) - (gap * static_cast<float>(form.panels - 1))) / static_cast<float>(form.panels);
        for (int i = 0; i < form.panels; ++i) {
            const float x0 = -form.clear + (static_cast<float>(i) * (width + gap));
            add_shoji_screen(builder, form, x0, x0 + width, 0.7f * form.sill, form.lintel_y + (0.3f * form.lintel));
        }
    } else if (form.infill == Timber_infill::plaster) {
        add_plaster_field(builder, form, form.lintel_y, 0.6f * form.lintel);
    } else if (form.infill == Timber_infill::half) {
        const float cap   = std::min(0.6f * form.post, 0.045f * form.height);
        const float top_y = form.sill + (0.45f * (form.lintel_y - form.sill));
        add_plaster_field(builder, form, top_y, 0.3f * cap);
        builder.set_slot(timber_wood_slot);
        add_wall_box(builder, -(form.clear + (0.6f * form.embed)), form.clear + (0.6f * form.embed), top_y, top_y + cap, 0.48f * depth);
    }
    builder.write(mesh);
}

//
// Railing: half posts at the ends and a post between the bays, each under a pyramid; rails run through the posts.
//

constexpr int railing_bay_max    = 6;
constexpr int railing_infill_max = 8;

enum class Railing_style : unsigned int { rail = 0, picket, balustrade, lattice };

[[nodiscard]] auto get_railing_style(const std::string_view name) -> Railing_style
{
    if (name == "picket") {
        return Railing_style::picket;
    }
    if (name == "balustrade") {
        return Railing_style::balustrade;
    }
    if (name == "lattice") {
        return Railing_style::lattice;
    }
    return Railing_style::rail;
}

class Railing_form
{
public:
    explicit Railing_form(const Kit_parameter_values& values)
        : style  {get_railing_style(values.get_enum("style"))}
        , bays   {std::clamp(values.get_int("bays"), 1, railing_bay_max)}
        , infill {std::clamp(values.get_int("infill"), 1, railing_infill_max)}
        , half_l {0.5f * values.get_float("length")}
        , height {values.get_float("height")}
        , bay    {(2.0f * half_l) / static_cast<float>(bays)}
        , post   {std::min(values.get_float("post_width"), 0.35f * bay)}
    {
        const bool collared = (style == Railing_style::balustrade) || (style == Railing_style::lattice);
        point  = std::min(((style == Railing_style::picket) ? 0.7f : 0.3f) * post, 0.1f * height);
        collar = collared ? std::min(0.2f * post, 0.04f * height) : 0.0f;
    }

    // The clear span of bay i between its posts.
    [[nodiscard]] auto get_bay_start(const int i) const -> float { return -half_l + (bay * static_cast<float>(i)) + (0.5f * post); }
    [[nodiscard]] auto get_bay_end  (const int i) const -> float { return -half_l + (bay * static_cast<float>(i + 1)) - (0.5f * post); }

    Railing_style style {Railing_style::rail};
    int           bays  {1};
    int           infill{1};
    float         half_l{0.0f};
    float         height{0.0f};
    float         bay   {0.0f};
    float         post  {0.0f};
    float         point {0.0f}; // height of a post's pyramid
    float         collar{0.0f}; // height of the slab under it; 0 without one
};

// A post from x0 to x1 on the ground under a pyramid whose apex stands over apex_x.
void add_railing_post(Kit_mesh_builder& builder, const float x0, const float x1, const float apex_x, const float half_z, const float shaft_top, const float apex_y)
{
    std::array<uint32_t, 4> foot{};
    std::array<uint32_t, 4> head{};
    const std::array<glm::vec2, 4> corners{{{x1, -half_z}, {x0, -half_z}, {x0, half_z}, {x1, half_z}}}; // counter-clockwise seen from above
    for (std::size_t i = 0; i < 4; ++i) {
        foot[i] = builder.add_vertex(glm::vec3{corners[i].x, 0.0f,      corners[i].y});
        head[i] = builder.add_vertex(glm::vec3{corners[i].x, shaft_top, corners[i].y});
    }
    const uint32_t apex = builder.add_vertex(glm::vec3{apex_x, apex_y, 0.0f});
    builder.add_quad(foot[3], foot[2], foot[1], foot[0]);
    for (std::size_t i = 0; i < 4; ++i) {
        const std::size_t j = (i + 1) % 4;
        builder.add_quad(foot[i], foot[j], head[j], head[i]);
        builder.add_triangle(head[i], head[j], apex);
    }
}

// A turned baluster about the upright through (x, 0): rings of `sides` points at the profile's heights and radii.
void add_baluster(
    Kit_mesh_builder&                builder,
    const float                      x,
    const std::span<const glm::vec2> profile,
    const float                      foot_y,
    const float                      head_y,
    const float                      radius,
    const int                        sides
)
{
    const uint32_t n     = static_cast<uint32_t>(sides);
    const float    phase = (sides == 6) ? 0.0f : (pi / static_cast<float>(sides));
    const uint32_t first = static_cast<uint32_t>(builder.get_positions().size());
    for (const glm::vec2& station : profile) {
        const float y = foot_y + ((head_y - foot_y) * station.x);
        for (uint32_t i = 0; i < n; ++i) {
            const glm::vec3 out = around_y(phase + (two_pi * static_cast<float>(i) / static_cast<float>(n)));
            builder.add_vertex(glm::vec3{x, y, 0.0f} + ((radius * station.y) * out));
        }
    }
    std::vector<uint32_t> polygon(n);
    for (uint32_t i = 0; i < n; ++i) {
        polygon[i] = first + (n - 1 - i);
    }
    builder.add_polygon(polygon);
    const uint32_t bands = static_cast<uint32_t>(profile.size()) - 1;
    for (uint32_t r = 0; r < bands; ++r) {
        const uint32_t lower = first + (r * n);
        const uint32_t upper = lower + n;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t j = (i + 1) % n;
            builder.add_quad(lower + i, lower + j, upper + j, upper + i);
        }
    }
    for (uint32_t i = 0; i < n; ++i) {
        polygon[i] = first + (bands * n) + i;
    }
    builder.add_polygon(polygon);
}

// (height share, radius share) of a baluster: a foot block, a belly, a neck and a head block.
constexpr std::array<glm::vec2, 8> baluster_profile_fine{{
    {0.0f, 0.66f}, {0.07f, 0.66f}, {0.12f, 0.94f}, {0.30f, 1.0f}, {0.54f, 0.52f}, {0.78f, 0.42f}, {0.90f, 0.64f}, {1.0f, 0.64f}
}};
constexpr std::array<glm::vec2, 6> baluster_profile_medium{{
    {0.0f, 0.66f}, {0.10f, 0.94f}, {0.30f, 1.0f}, {0.60f, 0.46f}, {0.88f, 0.62f}, {1.0f, 0.62f}
}};
constexpr std::array<glm::vec2, 4> baluster_profile_coarse{{
    {0.0f, 0.72f}, {0.28f, 1.0f}, {0.72f, 0.46f}, {1.0f, 0.66f}
}};

void generate_railing(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Railing_form form{values};
    Kit_mesh_builder builder;
    const float half_z    = 0.5f * form.post;
    const float shaft_top = form.height - form.point;
    for (int i = 0; i <= form.bays; ++i) {
        const float x  = -form.half_l + (form.bay * static_cast<float>(i));
        const float x0 = (i == 0) ? x : (x - (0.5f * form.post));
        const float x1 = (i == form.bays) ? x : (x + (0.5f * form.post));
        add_railing_post(builder, x0, x1, x, half_z, shaft_top, form.height);
        if (form.collar > 0.0f) {
            const float over = 0.14f * form.post;
            const float c0   = (i == 0) ? x : (x0 - over);
            const float c1   = (i == form.bays) ? x : (x1 + over);
            add_wall_box(builder, c0, c1, shaft_top - (1.6f * form.collar), shaft_top - (0.6f * form.collar), half_z + over);
        }
    }
    const float span_top = shaft_top - (2.2f * form.collar); // what stands between the posts stays under this
    const float bar      = std::min(0.09f, 0.1f * form.height);

    if (form.style == Railing_style::rail) {
        const int   rails     = (form.infill + 1) / 2;
        const float rail_high = 0.9f * span_top;
        const float rail_low  = 0.3f * form.height;
        for (int k = 0; k < rails; ++k) {
            const float y = (rails == 1) ? rail_high : (rail_low + ((rail_high - rail_low) * static_cast<float>(k) / static_cast<float>(rails - 1)));
            add_wall_box(builder, -form.half_l, form.half_l, y - (0.5f * bar), y + (0.5f * bar), 0.3f * form.post);
        }
    } else if (form.style == Railing_style::picket) {
        const float stringer = 0.8f * bar;
        for (const float share : {0.26f, 0.72f}) {
            add_wall_box(builder, -form.half_l, form.half_l, (share * form.height) - (0.5f * stringer), (share * form.height) + (0.5f * stringer), 0.22f * form.post);
        }
        const float tip_y = 0.97f * form.height;
        for (int i = 0; i < form.bays; ++i) {
            const float start = form.get_bay_start(i);
            const float cell  = (form.get_bay_end(i) - start) / static_cast<float>(form.infill);
            const float half  = 0.5f * std::min(0.08f, 0.62f * cell);
            const float neck  = tip_y - std::min(1.6f * half, 0.2f * form.height);
            for (int k = 0; k < form.infill; ++k) {
                const float x = start + (cell * (static_cast<float>(k) + 0.5f));
                const std::array<glm::vec2, 5> outline{{
                    {x - half, 0.08f * form.height}, {x + half, 0.08f * form.height}, {x + half, neck}, {x, tip_y}, {x - half, neck}
                }};
                add_extrusion(builder, outline, 0.38f * form.post, -0.38f * form.post);
            }
        }
    } else if (form.style == Railing_style::balustrade) {
        const float plinth = 0.1f * form.height;
        const float hand   = span_top - bar;
        add_wall_box(builder, -form.half_l, form.half_l, 0.0f, plinth,   0.42f * form.post);
        add_wall_box(builder, -form.half_l, form.half_l, hand, span_top, 0.47f * form.post);
        // The more balusters the run has, the fewer sides and stations each takes.
        const int total = form.bays * form.infill;
        const int sides = (total <= 6) ? 8 : ((total <= 12) ? 6 : 4);
        const std::span<const glm::vec2> profile =
            (total <= 6) ? std::span<const glm::vec2>{baluster_profile_fine} :
            ((total <= 24) ? std::span<const glm::vec2>{baluster_profile_medium} : std::span<const glm::vec2>{baluster_profile_coarse});
        for (int i = 0; i < form.bays; ++i) {
            const float start  = form.get_bay_start(i);
            const float cell   = (form.get_bay_end(i) - start) / static_cast<float>(form.infill);
            const float radius = 0.5f * std::min(0.8f * form.post, 0.8f * cell);
            for (int k = 0; k < form.infill; ++k) {
                add_baluster(builder, start + (cell * (static_cast<float>(k) + 0.5f)), profile, 0.8f * plinth, hand + (0.2f * bar), radius, sides);
            }
        }
    } else {
        const float low_rail = 0.12f * form.height;
        add_wall_box(builder, -form.half_l, form.half_l, low_rail,       low_rail + bar, 0.3f * form.post);
        add_wall_box(builder, -form.half_l, form.half_l, span_top - bar, span_top,       0.3f * form.post);
        const float slat_low  = low_rail + (0.5f * bar);
        const float slat_high = span_top - (0.5f * bar);
        for (int i = 0; i < form.bays; ++i) {
            const float start = form.get_bay_start(i);
            const float cell  = (form.get_bay_end(i) - start) / static_cast<float>(form.infill);
            const float slat  = std::min(0.05f, 0.3f * cell);
            for (int k = 0; k < form.infill; ++k) {
                const float x0 = start + (cell * (static_cast<float>(k) + 0.04f));
                const float x1 = start + (cell * (static_cast<float>(k) + 0.96f));
                const std::array<glm::vec2, 4> rising {{{x0, slat_low}, {x0 + slat, slat_low}, {x1, slat_high}, {x1 - slat, slat_high}}};
                const std::array<glm::vec2, 4> falling{{{x1 - slat, slat_low}, {x1, slat_low}, {x0 + slat, slat_high}, {x0, slat_high}}};
                add_extrusion(builder, rising,  0.16f * form.post, -0.16f * form.post);
                add_extrusion(builder, falling, 0.16f * form.post, -0.16f * form.post);
            }
        }
    }
    builder.write(mesh);
}

//
// Bamboo: canes of level rings, a dark band at every node, and leaf sprays at the upper nodes and the tips.
//

constexpr uint32_t bamboo_cane_slot    = 0;
constexpr uint32_t bamboo_bark_slot    = 1; // the nodes
constexpr uint32_t bamboo_foliage_slot = 2;
constexpr int      bamboo_culm_max     = 16;

// What a cane is made of: the more canes, the fewer sides, nodes and leaves each takes.
class Bamboo_detail
{
public:
    int sides     {5};
    int internodes{6};
    int sprays    {4}; // one at the tip, then two at each node below it
    int leaves    {3}; // in a spray
};

[[nodiscard]] auto get_bamboo_detail(const int culms) -> Bamboo_detail
{
    if (culms <= 4) {
        return Bamboo_detail{.sides = 6, .internodes = 8, .sprays = 7, .leaves = 4};
    }
    if (culms <= 8) {
        return Bamboo_detail{.sides = 5, .internodes = 6, .sprays = 5, .leaves = 3};
    }
    if (culms <= 12) {
        return Bamboo_detail{.sides = 4, .internodes = 5, .sprays = 3, .leaves = 3};
    }
    return Bamboo_detail{.sides = 3, .internodes = 5, .sprays = 3, .leaves = 3};
}

// One cane's line: its axis leans and arches along `out`.
class Bamboo_cane
{
public:
    [[nodiscard]] auto get_center(const float t) const -> glm::vec3
    {
        return base + ((height * t) * y_axis) + ((height * ((lean * t) + (arch * t * t))) * out);
    }
    [[nodiscard]] auto get_radius(const float t) const -> float
    {
        return radius * (1.0f - (0.7f * t));
    }

    glm::vec3 base  {0.0f};
    glm::vec3 out   {1.0f, 0.0f, 0.0f};
    float     height{1.0f};
    float     radius{0.05f};
    float     lean  {0.0f}; // run per rise at the foot
    float     arch  {0.0f}; // the tip's further run per height
    float     phase {0.0f};
};

// A leaf from the base along `out`: a flat top over a keel, pointed at both ends.
void add_bamboo_leaf(Kit_mesh_builder& builder, const glm::vec3& base, const glm::vec3& out, const float length, const float width, const float lift, const float droop)
{
    const glm::vec3 left   = glm::normalize(glm::cross(y_axis, out));
    const glm::vec3 middle = base + ((0.42f * length) * out) + ((lift * length) * y_axis);
    const uint32_t b = builder.add_vertex(base);
    const uint32_t l = builder.add_vertex(middle + ((0.5f * width) * left));
    const uint32_t r = builder.add_vertex(middle - ((0.5f * width) * left));
    const uint32_t k = builder.add_vertex(middle - ((0.22f * width) * y_axis));
    const uint32_t t = builder.add_vertex(base + (length * out) + (((lift - droop) * length) * y_axis));
    builder.add_triangle(b, r, l);
    builder.add_triangle(t, l, r);
    builder.add_triangle(b, l, k);
    builder.add_triangle(b, k, r);
    builder.add_triangle(t, k, l);
    builder.add_triangle(t, r, k);
}

// A twig from the anchor to the tip: a level triangle under a point.
void add_bamboo_twig(Kit_mesh_builder& builder, const glm::vec3& anchor, const glm::vec3& tip, const float radius, const float turn)
{
    std::array<uint32_t, 3> foot{};
    for (std::size_t i = 0; i < 3; ++i) {
        foot[i] = builder.add_vertex(anchor + (radius * around_y(turn + (two_pi * static_cast<float>(i) / 3.0f))));
    }
    const uint32_t apex = builder.add_vertex(tip);
    builder.add_triangle(foot[2], foot[1], foot[0]);
    for (std::size_t i = 0; i < 3; ++i) {
        builder.add_triangle(foot[i], foot[(i + 1) % 3], apex);
    }
}

void add_bamboo_cane(Kit_mesh_builder& builder, const Bamboo_cane& cane, const Bamboo_detail& detail, Kit_random& random)
{
    const uint32_t n = static_cast<uint32_t>(detail.sides);
    const int      m = detail.internodes;
    // A node's band is about a cane's width high.
    const float band = std::min((2.4f * cane.radius) / cane.height, 0.3f / static_cast<float>(m));

    // (t, radius share) of the rings: the foot, under and over every node, the tip.
    std::vector<glm::vec2> stations;
    stations.push_back(glm::vec2{0.0f, 1.0f});
    for (int k = 1; k < m; ++k) {
        const float t = static_cast<float>(k) / static_cast<float>(m);
        stations.push_back(glm::vec2{t - (0.5f * band), 1.22f});
        stations.push_back(glm::vec2{t + (0.5f * band), 0.96f});
    }
    stations.push_back(glm::vec2{1.0f, 0.6f});

    const uint32_t first = static_cast<uint32_t>(builder.get_positions().size());
    for (const glm::vec2& station : stations) {
        const glm::vec3 center      = cane.get_center(station.x);
        const float     ring_radius = cane.get_radius(station.x) * station.y;
        for (uint32_t i = 0; i < n; ++i) {
            builder.add_vertex(center + (ring_radius * around_y(cane.phase + (two_pi * static_cast<float>(i) / static_cast<float>(n)))));
        }
    }
    std::vector<uint32_t> polygon(n);
    for (uint32_t i = 0; i < n; ++i) {
        polygon[i] = first + (n - 1 - i);
    }
    builder.set_slot(bamboo_cane_slot);
    builder.add_polygon(polygon);
    const uint32_t bands = static_cast<uint32_t>(stations.size()) - 1;
    for (uint32_t r = 0; r < bands; ++r) {
        const uint32_t lower = first + (r * n);
        const uint32_t upper = lower + n;
        builder.set_slot(((r % 2) == 1) ? bamboo_bark_slot : bamboo_cane_slot); // every second band lies over a node
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t j = (i + 1) % n;
            builder.add_quad(lower + i, lower + j, upper + j, upper + i);
        }
    }
    for (uint32_t i = 0; i < n; ++i) {
        polygon[i] = first + (bands * n) + i;
    }
    builder.set_slot(bamboo_cane_slot);
    builder.add_polygon(polygon);

    // The fewer leaves a spray has, the longer they are.
    const float leaf_length = std::min(std::clamp(0.12f * cane.height, 0.3f, 0.85f), 0.2f * cane.height) * ((detail.leaves < 4) ? 1.25f : 1.0f);
    for (int s = 0; s < detail.sprays; ++s) {
        // Spray 0 stands on the tip; the others leave the nodes below it two to a node, on opposite sides.
        const bool      at_tip    = (s == 0);
        const int       node      = at_tip ? m : std::max(m - 1 - ((s - 1) / 2), 1);
        const float     t         = static_cast<float>(node) / static_cast<float>(m);
        const float     azimuth   = cane.phase + (golden_angle * static_cast<float>(node)) + (((s % 2) == 0) ? 0.0f : pi) + (0.6f * next_signed(random));
        const float     elevation = at_tip ? random.next_range(1.0f, 1.3f) : random.next_range(0.45f, 0.9f);
        const float     reach     = leaf_length * random.next_range(0.7f, 1.1f);
        const glm::vec3 anchor    = cane.get_center(t - (at_tip ? (0.3f * band) : 0.0f));
        const glm::vec3 level     = around_y(azimuth);
        const glm::vec3 tip       = anchor + (reach * ((std::cos(elevation) * level) + (std::sin(elevation) * y_axis)));
        builder.set_slot(bamboo_cane_slot);
        add_bamboo_twig(builder, anchor, tip, std::max(0.3f * cane.get_radius(t), 0.004f), azimuth);
        builder.set_slot(bamboo_foliage_slot);
        for (int k = 0; k < detail.leaves; ++k) {
            // The last leaf leaves the twig's tip forward; the others leave it along its length to the left and the right.
            const bool      last   = ((k + 1) == detail.leaves);
            const float     share  = last ? 0.97f : (0.45f + (0.5f * static_cast<float>(k) / static_cast<float>(detail.leaves - 1)));
            const float     turn   = last ? (0.3f * next_signed(random)) : ((((k % 2) == 0) ? 1.0f : -1.0f) * random.next_range(0.6f, 1.1f));
            const glm::vec3 out    = around_y(azimuth + turn);
            const float     length = leaf_length * random.next_range(0.75f, 1.15f);
            const float     lift   = random.next_range(0.0f, 0.25f);
            const float     droop  = random.next_range(0.25f, 0.55f);
            add_bamboo_leaf(builder, anchor + (share * (tip - anchor)), out, length, 0.24f * length, lift, droop);
        }
    }
}

void generate_bamboo(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float height     = values.get_float("height");
    const int   culms      = std::clamp(values.get_int("culms"), 1, bamboo_culm_max);
    const float radius     = values.get_float("radius");
    const float lean       = std::tan(glm::radians(values.get_float("lean")));
    const float cane_width = values.get_float("cane_width");
    const Bamboo_detail detail = get_bamboo_detail(culms);
    Kit_random random{values.get_seed(), "bamboo"};
    Kit_mesh_builder builder;
    const float first_azimuth = random.next_range(0.0f, two_pi);
    for (int c = 0; c < culms; ++c) {
        // The tallest cane stands at the origin, the others around it out to the radius.
        const float share   = std::sqrt(static_cast<float>(c) / static_cast<float>(std::max(culms - 1, 1)));
        const float azimuth = first_azimuth + (golden_angle * static_cast<float>(c)) + (0.3f * next_signed(random));
        const float reach   = radius * share * random.next_range(0.8f, 1.0f);
        const float size    = (c == 0) ? 1.0f : random.next_range(0.55f, 0.95f);
        Bamboo_cane cane;
        cane.base   = reach * around_y(azimuth);
        cane.out    = around_y(azimuth + (0.5f * next_signed(random)));
        cane.height = height * size;
        cane.radius = 0.5f * cane_width * (0.55f + (0.45f * size)) * random.next_range(0.85f, 1.0f);
        cane.lean   = lean * (0.35f + (0.65f * share)) * random.next_range(0.6f, 1.0f);
        cane.arch   = (0.04f + (1.4f * lean)) * random.next_range(0.5f, 1.0f);
        cane.phase  = random.next_range(0.0f, two_pi);
        add_bamboo_cane(builder, cane, detail, random);
    }
    builder.write(mesh);
}

//
// Stairs: flights of stone steps under tread slabs between side walls with copings, piers at every wall end.
//

constexpr uint32_t stairs_stone_slot = 0;
constexpr uint32_t stairs_trim_slot  = 1;
constexpr int      stairs_step_max   = 64;

enum class Stairs_form : unsigned int { walled = 0, l_landing, u_landing };

[[nodiscard]] auto get_stairs_form(const std::string_view name) -> Stairs_form
{
    if (name == "l_landing") {
        return Stairs_form::l_landing;
    }
    if (name == "u_landing") {
        return Stairs_form::u_landing;
    }
    return Stairs_form::walled;
}

// A flight's own coordinates (s, y, q): s to the right of the ascent, q against it; (right, up, back) is right-handed.
class Stairs_frame
{
public:
    [[nodiscard]] auto at(const float s, const float y, const float q) const -> glm::vec3
    {
        return origin + (s * right) + (y * y_axis) + (q * back);
    }

    glm::vec3 origin{0.0f};
    glm::vec3 right {1.0f, 0.0f, 0.0f};
    glm::vec3 back  {0.0f, 0.0f, 1.0f};
};

// A box between two corners given as (s, y, q).
void add_frame_box(Kit_mesh_builder& builder, const Stairs_frame& frame, const glm::vec3& low, const glm::vec3& high)
{
    const glm::vec3 a = frame.at(low.x,  low.y,  low.z);
    const glm::vec3 b = frame.at(high.x, high.y, high.z);
    builder.add_box(glm::min(a, b), glm::max(a, b));
}

// A station of a wall or a coping: its underside and its top at q.
class Stairs_station
{
public:
    float q   {0.0f};
    float low {0.0f};
    float high{0.0f};
};

// The solid between s0 and s1 under the stations' tops and over their undersides; q falls along the stations.
void add_strip_prism(Kit_mesh_builder& builder, const Stairs_frame& frame, const std::span<const Stairs_station> stations, const float s0, const float s1)
{
    const std::size_t count = stations.size();
    std::vector<uint32_t> l0(count);
    std::vector<uint32_t> h0(count);
    std::vector<uint32_t> l1(count);
    std::vector<uint32_t> h1(count);
    for (std::size_t i = 0; i < count; ++i) {
        l0[i] = builder.add_vertex(frame.at(s0, stations[i].low,  stations[i].q));
        h0[i] = builder.add_vertex(frame.at(s0, stations[i].high, stations[i].q));
        l1[i] = builder.add_vertex(frame.at(s1, stations[i].low,  stations[i].q));
        h1[i] = builder.add_vertex(frame.at(s1, stations[i].high, stations[i].q));
    }
    for (std::size_t i = 0; (i + 1) < count; ++i) {
        builder.add_quad(l0[i + 1], l0[i],     h0[i],     h0[i + 1]);
        builder.add_quad(l1[i],     l1[i + 1], h1[i + 1], h1[i]);
        builder.add_quad(h0[i],     h1[i],     h1[i + 1], h0[i + 1]);
        builder.add_quad(l0[i],     l0[i + 1], l1[i + 1], l1[i]);
    }
    builder.add_quad(l0[0],         l1[0],         h1[0],         h0[0]);
    builder.add_quad(l1[count - 1], l0[count - 1], h0[count - 1], h1[count - 1]);
}

// The stations moved in from both ends along their end segments, by at most 0.4 of a segment.
[[nodiscard]] auto shorten_stations(std::vector<Stairs_station> stations, const float distance) -> std::vector<Stairs_station>
{
    const auto move_in = [distance](Stairs_station& end, const Stairs_station& next) {
        const float share = std::min(distance / std::abs(next.q - end.q), 0.4f);
        end.q    += share * (next.q - end.q);
        end.low  += share * (next.low - end.low);
        end.high += share * (next.high - end.high);
    };
    const Stairs_station second = stations[1];
    const Stairs_station before = stations[stations.size() - 2];
    move_in(stations.front(), second);
    move_in(stations.back(),  before);
    return stations;
}

// What the stairs are at the values; construction coordinates have the first riser's foot on the X axis.
class Stairs_plan
{
public:
    explicit Stairs_plan(const Kit_parameter_values& values);

    [[nodiscard]] auto get_world_frame () const -> Stairs_frame; // the first flight's, and the construction coordinates'
    [[nodiscard]] auto get_second_frame() const -> Stairs_frame; // the second flight's, its first riser at q = 0

    Stairs_form form       {Stairs_form::walled};
    int         steps      {1};
    int         first      {1};    // risers up to the landing (walled: every riser)
    int         second     {0};    // risers of the second flight
    float       rise       {0.0f};
    float       step_rise  {0.0f};
    float       tread      {0.0f};
    float       width      {0.0f};
    float       wall       {0.0f}; // thickness of the side walls
    float       wall_height{0.0f};
    float       slab       {0.0f}; // thickness of a tread slab
    float       nosing     {0.0f};
    float       landing_y  {0.0f};
    float       landing_z  {0.0f}; // the landing's front edge
    float       second_x   {0.0f}; // u_landing: the second flight's center line
    glm::vec3   center     {0.0f}; // of the steps' and the landing's footprint
};

Stairs_plan::Stairs_plan(const Kit_parameter_values& values)
    : form       {get_stairs_form(values.get_enum("form"))}
    , rise       {values.get_float("rise")}
    , width      {values.get_float("width")}
    , wall_height{values.get_float("wall_height")}
{
    const Kit_stairs_layout layout = get_stairs_layout(values);
    steps = std::clamp(layout.step_count, 1, stairs_step_max);
    if (steps < 2) {
        form = Stairs_form::walled; // check_combination refuses a landing without a step on each side
    }
    first     = (form == Stairs_form::walled) ? steps : (steps / 2);
    second    = steps - first;
    step_rise = rise / static_cast<float>(steps);
    tread     = kit_stairs_tread_per_rise * step_rise;
    wall      = std::clamp(0.15f * width, 0.1f, 0.35f);
    slab      = std::min(0.05f, 0.3f * step_rise);
    nosing    = std::min(0.03f, 0.15f * tread);
    landing_y = step_rise * static_cast<float>(first);
    landing_z = -tread * static_cast<float>(first - 1);
    second_x  = width + (1.6f * wall);
    if (form == Stairs_form::walled) {
        center = glm::vec3{0.0f, 0.0f, -0.5f * tread * static_cast<float>(steps)};
    } else if (form == Stairs_form::l_landing) {
        center = glm::vec3{0.5f * tread * static_cast<float>(second), 0.0f, 0.5f * (landing_z - width)};
    } else {
        center = glm::vec3{0.5f * second_x, 0.0f, landing_z + (0.5f * ((tread * static_cast<float>(second)) - width))};
    }
}

auto Stairs_plan::get_world_frame() const -> Stairs_frame
{
    return Stairs_frame{.origin = -center, .right = glm::vec3{1.0f, 0.0f, 0.0f}, .back = glm::vec3{0.0f, 0.0f, 1.0f}};
}

auto Stairs_plan::get_second_frame() const -> Stairs_frame
{
    if (form == Stairs_form::l_landing) {
        // Toward +X from the landing's right edge.
        return Stairs_frame{.origin = glm::vec3{0.5f * width, 0.0f, landing_z - (0.5f * width)} - center, .right = glm::vec3{0.0f, 0.0f, 1.0f}, .back = glm::vec3{-1.0f, 0.0f, 0.0f}};
    }
    // Back toward +Z from the landing's front edge.
    return Stairs_frame{.origin = glm::vec3{second_x, 0.0f, landing_z} - center, .right = glm::vec3{-1.0f, 0.0f, 0.0f}, .back = glm::vec3{0.0f, 0.0f, -1.0f}};
}

// The stepped block of a flight from the ground: riser i stands at -i treads, tread i lies at first_top + i step rises.
void add_flight_body(Kit_mesh_builder& builder, const Stairs_frame& frame, const Stairs_plan& plan, const int steps, const float first_top, const float half_width)
{
    if (steps < 1) {
        return;
    }
    std::vector<glm::vec2> profile; // (q, y): the front's foot, every riser's top and tread's end, the back's foot
    profile.push_back(glm::vec2{0.0f, 0.0f});
    for (int i = 0; i < steps; ++i) {
        const float y = first_top + (plan.step_rise * static_cast<float>(i));
        profile.push_back(glm::vec2{-plan.tread * static_cast<float>(i),     y});
        profile.push_back(glm::vec2{-plan.tread * static_cast<float>(i + 1), y});
    }
    profile.push_back(glm::vec2{-plan.tread * static_cast<float>(steps), 0.0f});
    const uint32_t count = static_cast<uint32_t>(profile.size());
    const uint32_t left  = static_cast<uint32_t>(builder.get_positions().size());
    for (const glm::vec2& p : profile) {
        builder.add_vertex(frame.at(-half_width, p.y, p.x));
    }
    const uint32_t right = static_cast<uint32_t>(builder.get_positions().size());
    for (const glm::vec2& p : profile) {
        builder.add_vertex(frame.at(half_width, p.y, p.x));
    }
    const uint32_t apex = count - 1; // fans from the back's foot cover the stepped sides
    for (uint32_t k = 0; (k + 2) < count; ++k) {
        builder.add_triangle(left + apex,  left + k,      left + k + 1);
        builder.add_triangle(right + apex, right + k + 1, right + k);
    }
    for (uint32_t i = 0; i < static_cast<uint32_t>(steps); ++i) {
        const uint32_t riser_bottom = 2 * i;
        const uint32_t riser_top    = (2 * i) + 1;
        const uint32_t tread_end    = (2 * i) + 2;
        builder.add_quad(left + riser_bottom, right + riser_bottom, right + riser_top, left + riser_top);
        builder.add_quad(left + riser_top,    right + riser_top,    right + tread_end, left + tread_end);
    }
    const uint32_t last = count - 2;
    builder.add_quad(left + apex, left + last, right + last, right + apex);
    builder.add_quad(left + 0,    left + apex, right + apex, right + 0);
}

enum class Stairs_top_tread : unsigned int { none = 0, slab };

// A flight's body and tread slabs: `steps` risers from base_y; without a top tread the last riser is the landing's.
void add_flight(Kit_mesh_builder& builder, const Stairs_frame& frame, const Stairs_plan& plan, const int steps, const float base_y, const Stairs_top_tread top_tread)
{
    const int   treads = (top_tread == Stairs_top_tread::slab) ? steps : (steps - 1);
    const float half   = 0.5f * plan.width;
    builder.set_slot(stairs_stone_slot);
    add_flight_body(builder, frame, plan, treads, base_y + plan.step_rise - (0.5f * plan.slab), half + (0.5f * plan.wall));
    builder.set_slot(stairs_trim_slot);
    for (int i = 0; i < treads; ++i) {
        const float y     = base_y + (plan.step_rise * static_cast<float>(i + 1));
        const float front = (-plan.tread * static_cast<float>(i)) + plan.nosing;
        // The last tread of the stairs ends just inside the back; the others reach under the next riser.
        const bool  last  = (top_tread == Stairs_top_tread::slab) && ((i + 1) == treads);
        const float back  = (-plan.tread * static_cast<float>(i + 1)) + (last ? (0.15f * plan.nosing) : (-0.4f * plan.nosing));
        add_frame_box(builder, frame, glm::vec3{-(half + (0.2f * plan.wall)), y - plan.slab, back}, glm::vec3{half + (0.2f * plan.wall), y, front});
    }
}

// A wall from the ground under the stations' tops, s0 to s1, with its coping.
void add_stairs_wall(Kit_mesh_builder& builder, const Stairs_frame& frame, const Stairs_plan& plan, const std::vector<Stairs_station>& stations, const float s0, const float s1)
{
    if (stations.size() < 2) {
        return;
    }
    builder.set_slot(stairs_stone_slot);
    add_strip_prism(builder, frame, stations, s0, s1);
    std::vector<Stairs_station> coping = shorten_stations(stations, 0.36f * plan.wall);
    for (Stairs_station& station : coping) {
        station.low  = station.high - (0.1f * plan.wall);
        station.high = station.high + (0.3f * plan.wall);
    }
    // A string course on both faces at the height of the steps' edges.
    const float band = std::min(0.3f * plan.wall, 0.5f * plan.step_rise);
    std::vector<Stairs_station> course = shorten_stations(stations, 0.3f * plan.wall);
    for (Stairs_station& station : course) {
        station.high = station.high - plan.wall_height - (0.5f * plan.step_rise) + (0.5f * band);
        station.low  = station.high - band;
    }
    builder.set_slot(stairs_trim_slot);
    add_strip_prism(builder, frame, coping, s0 - (0.15f * plan.wall), s1 + (0.15f * plan.wall));
    add_strip_prism(builder, frame, course, s0 - (0.08f * plan.wall), s1 + (0.08f * plan.wall));
}

// How far a wall reaches into the pier at a landing, so that no two walls end on one line.
[[nodiscard]] auto get_wall_lap(const Stairs_plan& plan) -> float
{
    return 0.18f * plan.wall;
}

// The tops of a flight's wall: from `lead` before the first riser on the steps' slope, over the last riser, and level
// to the end of a top tread or a little onto the landing.
[[nodiscard]] auto make_flight_stations(const Stairs_plan& plan, const int steps, const float base_y, const Stairs_top_tread top_tread, const float lead) -> std::vector<Stairs_station>
{
    std::vector<Stairs_station> stations;
    const float top   = base_y + (plan.step_rise * static_cast<float>(steps)) + plan.wall_height;
    const float last  = -plan.tread * static_cast<float>(steps - 1);
    const float slope = (steps >= 2) ? (plan.step_rise / plan.tread) : 0.0f;
    stations.push_back(Stairs_station{.q = lead, .low = 0.0f, .high = base_y + plan.step_rise + plan.wall_height - (slope * lead)});
    if (steps >= 2) {
        stations.push_back(Stairs_station{.q = last, .low = 0.0f, .high = top});
    }
    const float end = (top_tread == Stairs_top_tread::slab) ? (last - plan.tread) : (last - get_wall_lap(plan));
    stations.push_back(Stairs_station{.q = end, .low = 0.0f, .high = top});
    return stations;
}

// A pier about (s, q) of the frame to a little over `top`, under a cap; size: its half width in wall thicknesses.
void add_stairs_pier(Kit_mesh_builder& builder, const Stairs_frame& frame, const Stairs_plan& plan, const float s, const float q, const float top, const float size)
{
    const float half = size * plan.wall;
    const float over = 0.12f * plan.wall;
    const float head = top + (0.5f * plan.wall);
    builder.set_slot(stairs_stone_slot);
    add_frame_box(builder, frame, glm::vec3{s - half, 0.0f, q - half}, glm::vec3{s + half, head, q + half});
    builder.set_slot(stairs_trim_slot);
    add_frame_box(builder, frame, glm::vec3{s - half - over, head - (0.1f * plan.wall), q - half - over}, glm::vec3{s + half + over, head + (0.3f * plan.wall), q + half + over});
}

void generate_stairs(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Stairs_plan  plan{values};
    const Stairs_frame world = plan.get_world_frame();
    const float        half  = 0.5f * plan.width;
    const float        side  = half + (0.5f * plan.wall);         // a wall's center line
    const float        foot  = plan.step_rise + plan.wall_height; // a wall's top over the first riser
    Kit_mesh_builder builder;

    if (plan.form == Stairs_form::walled) {
        add_flight(builder, world, plan, plan.steps, 0.0f, Stairs_top_tread::slab);
        const std::vector<Stairs_station> stations = make_flight_stations(plan, plan.steps, 0.0f, Stairs_top_tread::slab, 0.0f);
        add_stairs_wall(builder, world, plan, stations, -(half + plan.wall), -half);
        add_stairs_wall(builder, world, plan, stations, half, half + plan.wall);
        const float end = (-plan.tread * static_cast<float>(plan.steps)) + (0.76f * plan.wall);
        for (const float s : {-side, side}) {
            add_stairs_pier(builder, world, plan, s, 0.0f, foot,                         0.72f);
            add_stairs_pier(builder, world, plan, s, end,  plan.rise + plan.wall_height, 0.76f);
        }
        builder.write(mesh);
        return;
    }

    const Stairs_frame second    = plan.get_second_frame();
    const float        z         = plan.landing_z;
    const float        y         = plan.landing_y;
    const float        level     = y + plan.wall_height;   // the landing walls' top
    const float        high      = level + plan.step_rise; // the second flight's walls at its foot
    const bool         u_form    = (plan.form == Stairs_form::u_landing);
    const float        landing_x = u_form ? (plan.second_x + half) : half; // the landing's right edge

    // The first flight; its last riser is the landing's front.
    add_flight(builder, world, plan, plan.first, 0.0f, Stairs_top_tread::none);
    const std::vector<Stairs_station> first_stations = make_flight_stations(plan, plan.first, 0.0f, Stairs_top_tread::none, 0.0f);
    add_stairs_wall(builder, world, plan, first_stations, -(half + plan.wall), -half);
    add_stairs_wall(builder, world, plan, first_stations, half, half + plan.wall);

    builder.set_slot(stairs_stone_slot);
    add_frame_box(
        builder, world,
        glm::vec3{-(half + (0.45f * plan.wall)), 0.0f, z - plan.width - (0.45f * plan.wall)},
        glm::vec3{landing_x + (0.45f * plan.wall), y - (0.45f * plan.slab), z}
    );
    builder.set_slot(stairs_trim_slot);
    add_frame_box(
        builder, world,
        glm::vec3{-(half + (0.2f * plan.wall)), y - plan.slab, z - plan.width - (0.2f * plan.wall)},
        glm::vec3{landing_x + (0.25f * plan.wall), y, z + plan.nosing}
    );

    add_flight(builder, second, plan, plan.second, y, Stairs_top_tread::slab);
    const std::vector<Stairs_station> second_stations = make_flight_stations(plan, plan.second, y, Stairs_top_tread::slab, 0.6f * get_wall_lap(plan));
    add_stairs_wall(builder, second, plan, second_stations, -(half + plan.wall), -half);
    add_stairs_wall(builder, second, plan, second_stations, half, half + plan.wall);

    // The landing's walls start and end inside the piers, a little into the flights' walls.
    const float lap  = get_wall_lap(plan);
    const float back = z - plan.width - (0.5f * plan.wall); // the back wall's center line
    const std::vector<Stairs_station> west{
        Stairs_station{.q = z + lap, .low = 0.0f, .high = level}, Stairs_station{.q = back, .low = 0.0f, .high = level}
    };
    add_stairs_wall(builder, world, plan, west, -(half + plan.wall), -half);
    if (u_form) {
        // The back wall runs toward +X, the right wall forward to the second flight's left wall.
        const Stairs_frame along_x{.origin = glm::vec3{0.0f, 0.0f, back} - plan.center, .right = glm::vec3{0.0f, 0.0f, 1.0f}, .back = glm::vec3{-1.0f, 0.0f, 0.0f}};
        const std::vector<Stairs_station> north{
            Stairs_station{.q = side, .low = 0.0f, .high = level}, Stairs_station{.q = -(landing_x + (0.5f * plan.wall)), .low = 0.0f, .high = level}
        };
        add_stairs_wall(builder, along_x, plan, north, -0.5f * plan.wall, 0.5f * plan.wall);
        const std::vector<Stairs_station> east{
            Stairs_station{.q = plan.width + (0.5f * plan.wall), .low = 0.0f, .high = level}, Stairs_station{.q = -lap, .low = 0.0f, .high = level}
        };
        add_stairs_wall(builder, second, plan, east, -(half + plan.wall), -half);
    } else {
        const std::vector<Stairs_station> north{
            Stairs_station{.q = plan.width + (0.5f * plan.wall), .low = 0.0f, .high = level}, Stairs_station{.q = -lap, .low = 0.0f, .high = level}
        };
        add_stairs_wall(builder, second, plan, north, -(half + plan.wall), -half);
    }

    if (plan.first >= 2) {
        add_stairs_pier(builder, world, plan, -side, 0.0f, foot, 0.72f);
        add_stairs_pier(builder, world, plan,  side, 0.0f, foot, 0.72f);
    }
    add_stairs_pier(builder, world, plan, -side, z,    level, 0.70f);
    add_stairs_pier(builder, world, plan, -side, back, level, 0.78f);
    if (u_form) {
        add_stairs_pier(builder, world, plan, landing_x + (0.5f * plan.wall), back, level, 0.74f);
        add_stairs_pier(builder, world, plan, landing_x + (0.5f * plan.wall), z,    high,  0.71f);
        add_stairs_pier(builder, world, plan, half + (0.8f * plan.wall),      z,    high,  1.0f); // over both inner walls
    } else {
        add_stairs_pier(builder, world, plan, half, back,                   high, 0.74f);
        add_stairs_pier(builder, world, plan, side, z + (0.5f * plan.wall), high, 0.80f);
    }
    const float end = (-plan.tread * static_cast<float>(plan.second)) + (0.76f * plan.wall);
    add_stairs_pier(builder, second, plan, -side, end, plan.rise + plan.wall_height, 0.76f);
    add_stairs_pier(builder, second, plan,  side, end, plan.rise + plan.wall_height, 0.76f);
    builder.write(mesh);
}

[[nodiscard]] auto check_stairs_combination(const Kit_parameter_values& values) -> std::string
{
    const Kit_stairs_layout layout = get_stairs_layout(values);
    if (layout.step_count > stairs_step_max) {
        return fmt::format(
            "{}: 'rise' {} m over 'step_rise_limit' {} m makes {} steps, more than {}",
            values.get_shape()->id, values.get_float("rise"), values.get_float("step_rise_limit"), layout.step_count, stairs_step_max
        );
    }
    if ((layout.step_count < 2) && (values.get_enum("form") != "walled")) {
        return fmt::format("{}: a landing needs a step on each side, 'rise' {} m makes one step", values.get_shape()->id, values.get_float("rise"));
    }
    return {};
}

void get_stairs_connectors(const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors)
{
    const Stairs_plan  plan{values};
    const Stairs_frame world = plan.get_world_frame();
    add_side_connector(out_connectors, "bottom", world.at(0.0f, 0.0f, 0.0f), world.back);
    if (plan.form == Stairs_form::walled) {
        add_side_connector(out_connectors, "top", world.at(0.0f, plan.rise, -plan.tread * static_cast<float>(plan.steps)), -world.back);
    } else {
        const Stairs_frame second = plan.get_second_frame();
        add_side_connector(out_connectors, "top", second.at(0.0f, plan.rise, -plan.tread * static_cast<float>(plan.second)), -second.back);
    }
    out_connectors.push_back(Kit_connector{.name = "base", .position = glm::vec3{0.0f}, .outward = glm::vec3{0.0f, -1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
}

//
// Spiral stairs: a wedge of stone per step, each lapping the one below, about a newel.
//

constexpr uint32_t spiral_stone_slot  = 0;
constexpr uint32_t spiral_trim_slot   = 1;    // the treads
constexpr int      spiral_step_max    = 96;
constexpr int      spiral_newel_sides = 10;
constexpr float    spiral_pitch       = 2.4f; // meters climbed in a turn: the headroom and a step's thickness

class Spiral_plan
{
public:
    explicit Spiral_plan(const Kit_parameter_values& values)
        : rise  {values.get_float("rise")}
        , radius{values.get_float("radius")}
        , newel {0.5f * values.get_float("newel_width")}
        , side  {(values.get_enum("turn") == "right") ? -1.0f : 1.0f}
    {
        const Kit_stairs_layout layout = get_stairs_layout(values);
        steps     = std::clamp(layout.step_count, 1, spiral_step_max);
        step_rise = rise / static_cast<float>(steps);
        angle     = two_pi * step_rise / spiral_pitch;
        thickness = 1.35f * step_rise;
    }

    // The point at the radius and the angle of a left turn; a right turn is its mirror image across x = 0.
    [[nodiscard]] auto at(const float r, const float a, const float y) const -> glm::vec3
    {
        return glm::vec3{side * r * std::cos(a), y, -r * std::sin(a)};
    }

    float rise     {0.0f};
    float radius   {0.0f};
    float newel    {0.0f}; // radius
    float side     {1.0f};
    int   steps    {1};
    float step_rise{0.0f};
    float angle    {0.0f}; // of a step
    float thickness{0.0f};
};

void generate_spiral_stairs(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Spiral_plan plan{values};
    Kit_mesh_builder builder;
    for (int i = 0; i < plan.steps; ++i) {
        const float front  = (static_cast<float>(i) - 0.12f) * plan.angle;
        const float back   = (static_cast<float>(i) + 1.10f) * plan.angle;
        const float top    = plan.step_rise * static_cast<float>(i + 1);
        const float bottom = std::max(top - plan.thickness, 0.0f);
        // (radius, angle) of the outline counter-clockwise seen from above; a right turn's mirror image turns the order.
        std::array<glm::vec2, 5> outline{{
            {0.5f * plan.newel, front}, {plan.radius, front}, {plan.radius, 0.5f * (front + back)}, {plan.radius, back}, {0.5f * plan.newel, back}
        }};
        if (plan.side < 0.0f) {
            std::reverse(outline.begin(), outline.end());
        }
        std::array<uint32_t, 5> upper{};
        std::array<uint32_t, 5> lower{};
        for (std::size_t k = 0; k < outline.size(); ++k) {
            upper[k] = builder.add_vertex(plan.at(outline[k].x, outline[k].y, top));
            lower[k] = builder.add_vertex(plan.at(outline[k].x, outline[k].y, bottom));
        }
        builder.set_slot(spiral_trim_slot);
        builder.add_polygon(upper);
        builder.set_slot(spiral_stone_slot);
        const std::array<uint32_t, 5> reversed{lower[4], lower[3], lower[2], lower[1], lower[0]};
        builder.add_polygon(reversed);
        for (std::size_t k = 0; k < outline.size(); ++k) {
            const std::size_t j = (k + 1) % outline.size();
            builder.add_quad(lower[k], lower[j], upper[j], upper[k]);
        }
    }
    builder.set_slot(spiral_stone_slot);
    const float plinth = std::min(0.12f, 0.3f * plan.rise);
    const float collar = std::min(0.10f, 0.25f * plan.rise);
    builder.add_prism(spiral_newel_sides, plan.newel,         0.0f,               plan.rise - (0.5f * collar), 0.0f);
    builder.add_prism(spiral_newel_sides, 1.3f * plan.newel,  0.0f,               plinth,                      pi / static_cast<float>(spiral_newel_sides));
    builder.add_prism(spiral_newel_sides, 1.22f * plan.newel, plan.rise - collar, plan.rise,                   pi / static_cast<float>(spiral_newel_sides));
    builder.write(mesh);
}

[[nodiscard]] auto check_spiral_combination(const Kit_parameter_values& values) -> std::string
{
    const Kit_stairs_layout layout = get_stairs_layout(values);
    if (layout.step_count > spiral_step_max) {
        return fmt::format(
            "{}: 'rise' {} m over 'step_rise_limit' {} m makes {} steps, more than {}",
            values.get_shape()->id, values.get_float("rise"), values.get_float("step_rise_limit"), layout.step_count, spiral_step_max
        );
    }
    if (values.get_float("newel_width") > values.get_float("radius")) {
        return fmt::format(
            "{}: 'newel_width' {} m is more than 'radius' {} m, which leaves less than half the radius for the steps",
            values.get_shape()->id, values.get_float("newel_width"), values.get_float("radius")
        );
    }
    return {};
}

// R2.10 on the line the agent's center walks closest to the newel: the steps' slope there, the clear width and the headroom.
[[nodiscard]] auto check_spiral_walkability(const Kit_parameter_values& values, const Kit_agent& agent) -> std::string
{
    const Spiral_plan plan{values};
    const float tread = plan.angle * (plan.newel + agent.radius);
    std::string problem = check_stairs_walkability(values.get_shape()->id, plan.step_rise, tread, plan.radius - plan.newel, agent);
    if (!problem.empty()) {
        return problem;
    }
    const float headroom = spiral_pitch - plan.thickness;
    if (headroom < (agent.height + kit_opening_margin)) {
        return fmt::format("{}: headroom {} m under the turn above is lower than the agent's {} m", values.get_shape()->id, headroom, agent.height + kit_opening_margin);
    }
    return {};
}

void get_spiral_connectors(const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors)
{
    const Spiral_plan plan{values};
    const float middle = 0.5f * (plan.newel + plan.radius);
    const float turn   = plan.angle * static_cast<float>(plan.steps);
    add_side_connector(out_connectors, "bottom", plan.at(middle, 0.0f, 0.0f), glm::vec3{0.0f, 0.0f, 1.0f});
    add_side_connector(out_connectors, "top",    plan.at(middle, turn, plan.rise), glm::vec3{-plan.side * std::sin(turn), 0.0f, -std::cos(turn)});
    out_connectors.push_back(Kit_connector{.name = "base", .position = glm::vec3{0.0f}, .outward = glm::vec3{0.0f, -1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
}

//
// Rock arch: squarish sections fanned about the opening's outline from foot to foot, and loose rocks at the feet.
//

constexpr int rock_arch_section = 10; // points of a section
constexpr int rock_arch_talus   = 4;

// The opening is at least the stated ellipse: its outline is drawn outside it by the jitter and the chords' sag.
[[nodiscard]] auto get_rock_arch_scale(const float jitter, const int segments) -> float
{
    return 1.0f / ((1.0f - jitter) * std::cos(pi / static_cast<float>(segments)));
}

[[nodiscard]] auto get_rock_arch_jitter(const Kit_parameter_values& values) -> float
{
    return 0.07f * values.get_float("roughness");
}

// A loose rock on the ground: two rings of six under a point.
void add_talus_rock(Kit_mesh_builder& builder, const glm::vec3& center, const float radius, Kit_random& random)
{
    constexpr int sides = 6;
    std::array<glm::vec3, 2 * sides> points{};
    const float     turn   = random.next_range(0.0f, two_pi);
    const float     height = radius * random.next_range(0.7f, 1.1f);
    const glm::vec3 lean   = (0.25f * radius) * around_y(random.next_range(0.0f, two_pi));
    for (int i = 0; i < sides; ++i) {
        const glm::vec3 out = around_y(turn + (two_pi * static_cast<float>(i) / static_cast<float>(sides)));
        points[static_cast<std::size_t>(i)]         = center + ((radius * random.next_range(0.75f, 1.1f)) * out);
        const float top_radius = radius * random.next_range(0.5f, 0.8f);
        const float top_height = height * random.next_range(0.5f, 0.7f);
        points[static_cast<std::size_t>(sides + i)] = center + lean + (top_radius * out) + (top_height * y_axis);
    }
    builder.add_ring_solid(points, sides, center, center + (1.6f * lean) + (height * y_axis));
}

// One side's outer outline: a squarish quarter from the foot's outer edge to the crown's top.
class Rock_arch_side
{
public:
    float reach {0.0f}; // of the foot's outer edge from the origin
    float corner{0.6f}; // below 1 the shoulder stands out square
    float top   {0.0f}; // height of the outline over the origin's side of the crown
};

// The opening's outline and the two sides (0 toward +X, 1 toward -X): the first draws of the arch's random.
class Rock_arch_frame
{
public:
    std::vector<glm::vec2>        inner;
    std::array<Rock_arch_side, 2> sides{};
};

[[nodiscard]] auto make_rock_arch_frame(const Kit_parameter_values& values, Kit_random& random) -> Rock_arch_frame
{
    const float span      = values.get_float("span");
    const float rise      = values.get_float("rise");
    const float thickness = values.get_float("thickness");
    const int   segments  = std::max(values.get_int("segments"), 4);
    const float jitter    = get_rock_arch_jitter(values);
    const float scale     = get_rock_arch_scale(jitter, segments);
    Rock_arch_frame frame;
    frame.inner = make_kit_arch_head_points(Kit_arch_head::round, 0.5f * span * scale, rise * scale, segments, jitter, &random);
    // What makes this arch its own: a heavy leg and a light one, a square shoulder and a worn one.
    const std::size_t heavy = static_cast<std::size_t>(random.next_u32() % 2u);
    const std::size_t worn  = static_cast<std::size_t>(random.next_u32() % 2u);
    for (std::size_t i = 0; i < frame.sides.size(); ++i) {
        const float leg = thickness * ((i == heavy) ? random.next_range(1.5f, 2.0f) : random.next_range(0.9f, 1.3f));
        Rock_arch_side& side = frame.sides[i];
        side.reach  = (0.5f * span * scale) + leg;
        side.corner = (i == worn) ? random.next_range(0.7f, 0.85f) : random.next_range(0.5f, 0.62f);
        side.top    = (rise * scale) + thickness;
    }
    return frame;
}

void generate_rock_arch(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float span      = values.get_float("span");
    const float thickness = values.get_float("thickness");
    const float depth     = values.get_float("depth");
    const float roughness = values.get_float("roughness");
    const int   segments  = std::max(values.get_int("segments"), 4);
    Kit_random random{values.get_seed(), "rock arch"};
    Kit_mesh_builder builder;

    const float scale = get_rock_arch_scale(get_rock_arch_jitter(values), segments);
    const Rock_arch_frame                frame = make_rock_arch_frame(values, random);
    const std::vector<glm::vec2>&        inner = frame.inner;
    const std::array<Rock_arch_side, 2>& sides = frame.sides;
    // A slow sway across the opening.
    const float tilt       = 0.12f * roughness * next_signed(random); // one shoulder stands higher
    const float sway_phase = random.next_range(0.0f, two_pi);
    const float sway_turns = random.next_range(0.7f, 1.4f);
    std::array<float, rock_arch_section> ridge{}; // each line of points along the arch stands in or out
    for (float& factor : ridge) {
        factor = 1.0f + (0.3f * roughness * next_signed(random));
    }

    constexpr std::size_t section = rock_arch_section;
    std::vector<glm::vec3> points;
    std::array<glm::vec3, 2> centers{};
    for (int j = 0; j <= segments; ++j) {
        const float       angle   = pi * static_cast<float>(j) / static_cast<float>(segments);
        const std::size_t half    = ((2 * j) < segments) ? 0 : 1; // the right or the left side
        const bool        is_foot = (j == 0) || (j == segments);
        const float       c       = std::cos(angle);
        const float       s       = std::sin(angle);
        const float       low     = std::abs(c);
        const Rock_arch_side& side = sides[half];
        // The section spans from the opening's outline to the outer outline; a foot's lies on the ground.
        const glm::vec2 from  = inner[static_cast<std::size_t>(j)];
        const float     out_x = ((c < 0.0f) ? -1.0f : 1.0f) * side.reach * std::pow(low, side.corner);
        const float     out_y = is_foot ? 0.0f : (side.top * std::pow(s, side.corner) * (1.0f + (tilt * c)));
        const float     noise = is_foot ? 0.0f : (0.2f * roughness * next_signed(random));
        const glm::vec2 to    = from + ((1.0f + noise) * (glm::vec2{out_x, out_y} - from));
        const float     deep  = depth * (0.8f + (0.45f * low * low) + (0.14f * roughness * next_signed(random)));
        const float     sway  = 0.12f * depth * roughness * std::sin(sway_phase + (sway_turns * angle));
        const glm::vec3 center{0.5f * (from + to), sway};
        const glm::vec3 radial{0.5f * (to - from), 0.0f};
        if (is_foot) {
            centers[(j == 0) ? 0 : 1] = center;
        }
        const float twist = is_foot ? 0.0f : (0.2f * roughness * next_signed(random));
        for (std::size_t a = 0; a < section; ++a) {
            // From the opening's side over +Z to the outside and back over -Z; a squarish outline.
            const float phi    = pi - (two_pi * static_cast<float>(a) / static_cast<float>(section)) + ((a == 0) ? 0.0f : twist);
            const float cp     = std::cos(phi);
            const float sp     = std::sin(phi);
            const float square = std::pow(std::pow(std::abs(cp), 3.2f) + std::pow(std::abs(sp), 3.2f), -1.0f / 3.2f);
            // The point on the opening's outline stays; its neighbours stand back from the opening.
            const float grain  = (a == 0) ? 1.0f : (ridge[a] * (1.0f + (0.24f * roughness * next_signed(random))));
            const float along  = (cp < 0.0f) ? std::min(grain, 1.0f) : grain;
            points.push_back(center + ((cp * square * along) * radial) + glm::vec3{0.0f, 0.0f, 0.5f * deep * sp * square * grain});
        }
    }
    builder.add_ring_solid(points, rock_arch_section, centers[0], centers[1]);

    // Loose rocks before and behind the feet.
    for (int i = 0; i < rock_arch_talus; ++i) {
        const std::size_t half   = static_cast<std::size_t>(i % 2);
        const float       x_side = (half == 0) ? 1.0f : -1.0f;
        const float       z_side = (i < 2) ? 1.0f : -1.0f;
        const float       radius = thickness * random.next_range(0.3f, 0.55f);
        const float       x      = x_side * ((0.5f * span * scale) + radius + ((sides[half].reach - (0.5f * span * scale)) * random.next_range(0.2f, 0.9f)));
        const float       z      = z_side * ((0.55f * depth) + (radius * random.next_range(0.3f, 0.9f)));
        add_talus_rock(builder, glm::vec3{x, 0.0f, z}, radius, random);
    }
    builder.write(mesh);
}

// The clear width of the stated ellipse at the height.
[[nodiscard]] auto get_rock_arch_clear_width(const Kit_parameter_values& values, const float height) -> float
{
    const float share = height / values.get_float("rise");
    return (share >= 1.0f) ? 0.0f : (values.get_float("span") * std::sqrt(1.0f - (share * share)));
}

} // anonymous namespace

void add_build_17_20_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.timber_bay";
        shape.kit             = "architecture";
        shape.display_name    = "Timber bay";
        shape.description     =
            "Japanese post-and-beam wall bay along X: half posts at its ends (two bays joined make a whole post), a top "
            "beam, a sill, a lintel under a tie rail on both faces and a plaster field above it. Below the lintel it is "
            "open, closed by sliding shoji screens (a lattice over paper above a hip board), plastered with rails and "
            "studs, or half: a plastered wall under a cap rail, open above.";
        shape.tags            = {"obstacle", "wall", "japanese", "tiles", "size:2x1"};
        shape.parameters      = {
            derive_float(make_length_parameter("length",     "Size along X",                    2.0f,  Kit_default_scale::modules,      0.5f,  16.0f), 1.5f,  4.0f,  0.5f),
            derive_float(make_length_parameter("height",     "Size along Y",                    1.0f,  Kit_default_scale::wall_heights, 1.0f,  8.0f),  0.85f, 1.25f, 0.05f),
            derive_float(make_length_parameter("post_width", "Width and depth of a whole post", 0.15f, Kit_default_scale::meters,       0.06f, 0.4f),  0.12f, 0.24f, 0.03f),
            derive_enum (make_enum_parameter  ("infill",     "Open, shoji screens, plaster, or a plastered half wall", {"open", "shoji", "plaster", "half"}, 1)),
            derive_int  (make_count_parameter ("rails",      "Rails across a plaster field; a shoji screen has two more lattice bars for each", 1, 0, timber_rail_max), 0, 2),
            derive_int  (make_count_parameter ("panels",     "Shoji screens; a plaster field has a stud between two", 2, 1, timber_panel_max), 1, 3),
            make_seed_parameter()
        };
        shape.material_slots  = {"wood", "wall"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 440;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.generator       = generate_timber_bay;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float half_l = 0.5f * values.get_float("length");
            const float half_t = 0.5f * values.get_float("post_width");
            add_side_connector(out_connectors, "start", glm::vec3{-half_l, 0.0f, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "end",   glm::vec3{ half_l, 0.0f, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
            add_base_and_top_connectors(out_connectors, values.get_float("height"));
            add_side_connector(out_connectors, "front", glm::vec3{0.0f, 0.0f,  half_t}, glm::vec3{0.0f, 0.0f,  1.0f});
            add_side_connector(out_connectors, "back",  glm::vec3{0.0f, 0.0f, -half_t}, glm::vec3{0.0f, 0.0f, -1.0f});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.railing";
        shape.kit             = "architecture";
        shape.display_name    = "Railing";
        shape.description     =
            "Railing or fence run along X in equal bays: half posts at its ends (two runs joined make a whole post) and a "
            "post between two bays, each under a pyramid. Between the posts: level rails, pointed pickets on two "
            "stringers, turned balusters between a plinth and a handrail, or crossed slats between two rails.";
        shape.tags            = {"obstacle", "railing", "fence", "tiles", "size:2x1"};
        shape.parameters      = {
            derive_float(make_length_parameter("length",     "Size along X",        2.0f, Kit_default_scale::modules, 0.5f, 16.0f), 1.5f, 4.0f, 0.5f),
            derive_float(make_length_parameter("height",     "Height of the posts", 1.0f, Kit_default_scale::meters,  0.4f, 2.0f),  0.9f, 1.2f, 0.05f),
            derive_int  (make_count_parameter ("bays",       "Bays between posts",  2, 1, railing_bay_max), 1, 3),
            derive_float(make_length_parameter("post_width", "Width and depth of a whole post; at most 0.35 of a bay", 0.12f, Kit_default_scale::meters, 0.04f, 0.3f), 0.1f, 0.16f, 0.02f),
            derive_enum (make_enum_parameter  ("style",      "Rails, pickets, balusters or crossed slats", {"rail", "picket", "balustrade", "lattice"}, 2)),
            derive_int  (make_count_parameter ("infill",     "Pickets, balusters or crosses in a bay; the rail style has a rail for every two", 5, 1, railing_infill_max), 3, 6),
            make_seed_parameter()
        };
        shape.material_slots  = {"trim"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 800;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.generator       = generate_railing;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float half_l = 0.5f * values.get_float("length");
            add_side_connector(out_connectors, "start", glm::vec3{-half_l, 0.0f, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "end",   glm::vec3{ half_l, 0.0f, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
            out_connectors.push_back(Kit_connector{.name = "base", .position = glm::vec3{0.0f}, .outward = glm::vec3{0.0f, -1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.bamboo";
        shape.kit             = "nature";
        shape.display_name    = "Bamboo";
        shape.description     =
            "Clump of jointed canes, the tallest at its origin and the others around it, leaning and arching outward, a "
            "dark band at every node (slot bark) and sprays of pointed leaves at the tips and the upper nodes. The more "
            "canes, the fewer sides, nodes and leaves each has.";
        shape.tags            = {"obstacle", "decoration", "tropical", "size:1x1x6"};
        shape.parameters      = {
            from_seed_by_default(derive_float(make_length_parameter("height", "Height of the tallest cane", 1.5f, Kit_default_scale::wall_heights, 1.0f, 16.0f), 1.0f, 4.0f, 0.25f)),
            from_seed_by_default(derive_int(make_count_parameter("culms", "Canes", 6, 3, bamboo_culm_max), 3, 16)),
            derive_float(make_length_parameter("radius", "Radius of the canes' feet", 0.5f, Kit_default_scale::modules, 0.1f, 4.0f), 0.3f, 1.0f, 0.1f),
            from_seed_by_default(derive_float(make_angle_parameter("lean", "Outward lean of the outermost canes", 5.0f, 0.0f, 20.0f), 0.0f, 10.0f, 1.0f)),
            derive_float(make_length_parameter("cane_width", "Width of the thickest cane at its foot", 0.1f, Kit_default_scale::meters, 0.03f, 0.3f), 0.07f, 0.14f, 0.01f),
            make_seed_parameter()
        };
        shape.material_slots  = {"cane", "bark", "foliage"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 1200;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_bamboo;
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.stairs";
        shape.kit             = "architecture";
        shape.display_name    = "Stairs with side walls";
        shape.description     =
            "Stone stairs between side walls: steps under tread slabs with a nosing (slot trim), walls that follow the "
            "flight under a coping, and a pier with a cap at every wall end. Walled: one straight flight rising toward -Z. "
            "L landing: half the steps up to a square landing, the rest to the right of it. U landing: the second flight "
            "comes back beside the first. Steps follow the rules of the straight stairs.";
        shape.tags            = {"walkable", "stairs", "size:1xNx1"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("form",        "One flight, a quarter turn or a half turn at a landing", {"walled", "l_landing", "u_landing"}, 0)),
            derive_float(make_length_parameter("rise",        "Total height climbed",          1.0f, Kit_default_scale::wall_heights, 0.2f, 12.0f), 0.25f, 1.5f, 0.05f),
            // In meters: the agent fixes the least width at every level length (R2.10).
            derive_float(make_length_parameter("width",       "Clear width between the walls", 1.2f, Kit_default_scale::meters,       0.3f, 8.0f),  1.0f,  2.0f, 0.1f),
            make_length_parameter("step_rise_limit", "Highest allowed single step", 0.2f, Kit_default_scale::meters, 0.05f, 1.0f),
            derive_float(make_length_parameter("wall_height", "Height of the walls above the steps' edges", 0.9f, Kit_default_scale::meters, 0.05f, 1.5f), 0.3f, 1.1f, 0.1f),
            make_seed_parameter()
        };
        shape.material_slots    = {"stone", "trim"};
        shape.normal_style      = Kit_normal_style::faceted;
        shape.triangle_budget   = 540;
        shape.collision         = Kit_collision::triangle_mesh;
        shape.origin_rule       = Kit_origin_rule::base_contains_origin; // a landing's footprint has no center of its own
        shape.generator         = generate_stairs;
        shape.check_combination = check_stairs_combination;
        shape.check_walkability = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            const Kit_stairs_layout layout = get_stairs_layout(values);
            return check_stairs_walkability(values.get_shape()->id, layout.step_rise, layout.tread_depth, values.get_float("width"), agent);
        };
        shape.get_connectors    = get_stairs_connectors;
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.spiral_stairs";
        shape.kit             = "architecture";
        shape.display_name    = "Spiral stairs";
        shape.description     =
            "Medieval spiral stairs about a newel at the origin: a wedge of stone for each step, lapping the one below, "
            "the treads slot trim; the newel stands on a plinth under a collar. The first riser lies along X and the steps "
            "rise toward -Z, turning left or right, 2.4 m to the turn at every step rise.";
        shape.tags            = {"walkable", "stairs", "medieval", "size:3xNx3"};
        shape.parameters      = {
            derive_float(make_length_parameter("rise",        "Total height climbed",            1.0f,  Kit_default_scale::wall_heights, 0.2f, 12.0f), 0.5f,  1.5f, 0.05f),
            // In meters: the agent fixes the least radius and newel at every level length (R2.10).
            derive_float(make_length_parameter("radius",      "Radius of the steps' outer edge", 1.25f, Kit_default_scale::meters,       0.6f, 4.0f),  1.15f, 1.6f, 0.05f),
            make_length_parameter("step_rise_limit", "Highest allowed single step", 0.2f, Kit_default_scale::meters, 0.05f, 0.4f),
            derive_enum (make_enum_parameter  ("turn",        "The way the stairs turn going up", {"left", "right"}, 0)),
            derive_float(make_length_parameter("newel_width", "Width of the newel",              0.3f,  Kit_default_scale::meters,       0.1f, 1.0f),  0.25f, 0.4f, 0.05f),
            make_seed_parameter()
        };
        shape.material_slots    = {"stone", "trim"};
        shape.normal_style      = Kit_normal_style::faceted;
        shape.triangle_budget   = 360;
        shape.collision         = Kit_collision::triangle_mesh;
        shape.origin_rule       = Kit_origin_rule::base_contains_origin; // the first step lies on the ground at one side
        shape.generator         = generate_spiral_stairs;
        shape.check_combination = check_spiral_combination;
        shape.check_walkability = check_spiral_walkability;
        shape.get_connectors    = get_spiral_connectors;
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.rock_arch";
        shape.kit             = "nature";
        shape.display_name    = "Rock arch";
        shape.description     =
            "Natural arch across X over its origin, walked through along Z: a band of rock of squarish sections from foot "
            "to foot, heavy at the feet and the shoulders, with loose rocks beside the feet. The opening is at least the "
            "half ellipse of the span and the rise.";
        shape.tags            = {"obstacle", "rock", "opening", "size:6x4x2"};
        // In meters: the agent fixes the least opening at every level length (R2.10).
        shape.parameters      = {
            derive_float(make_length_parameter("span",      "Clear width of the opening at the ground",  3.0f, Kit_default_scale::meters, 1.0f, 12.0f), 2.5f, 5.0f, 0.25f),
            derive_float(make_length_parameter("rise",      "Clear height of the opening at its center", 3.0f, Kit_default_scale::meters, 1.0f, 10.0f), 2.6f, 4.0f, 0.2f),
            from_seed_by_default(derive_float(make_length_parameter("thickness", "Thickness of the rock over the crown", 1.2f, Kit_default_scale::meters, 0.4f, 4.0f), 0.8f, 2.0f, 0.1f)),
            derive_float(make_length_parameter("depth",     "Size along Z at the crown",                 1.6f, Kit_default_scale::meters, 0.5f, 6.0f),  1.2f, 2.6f, 0.2f),
            derive_float(make_ratio_parameter ("roughness", "Irregular outline and sections",            0.6f), 0.3f, 0.9f, 0.05f),
            make_seed_parameter (),
            make_count_parameter("segments", "Stations along the arch", 14, 8, 24)
        };
        shape.material_slots    = {"rock"};
        shape.normal_style      = Kit_normal_style::faceted;
        shape.triangle_budget   = 420;
        shape.collision         = Kit_collision::triangle_mesh;
        shape.randomness        = Kit_randomness::seeded;
        shape.generator         = generate_rock_arch;
        shape.check_walkability = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            const float clear = get_rock_arch_clear_width(values, agent.height + kit_opening_margin);
            return check_kit_opening(*values.get_shape(), clear, values.get_float("rise"), agent);
        };
        shape.get_connectors    = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            // Each side connector on its own foot's outer edge, drawn as the generator draws it.
            Kit_random            random{values.get_seed(), "rock arch"};
            const Rock_arch_frame frame = make_rock_arch_frame(values, random);
            add_side_connector(out_connectors, "start", glm::vec3{-frame.sides[1].reach, 0.0f, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "end",   glm::vec3{ frame.sides[0].reach, 0.0f, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
            out_connectors.push_back(Kit_connector{.name = "base", .position = glm::vec3{0.0f}, .outward = glm::vec3{0.0f, -1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
        };
        shapes.push_back(std::move(shape));
    }
}

} // namespace editor
