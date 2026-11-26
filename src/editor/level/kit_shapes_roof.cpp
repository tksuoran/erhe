#include "level/kit_mesh_builder.hpp"
#include "level/kit_shapes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <vector>

namespace editor {

namespace {

constexpr uint32_t roof_tile_slot   = 0;     // the slab, the tile courses and the caps
constexpr uint32_t roof_trim_slot   = 1;     // the body under the slab: the gable ends and the closed underside
constexpr float    roof_thickness   = 0.15f; // the slab's, vertical
constexpr float    roof_course      = 0.32f; // a tile course's length along the slope, about
constexpr int      roof_max_courses = 14;    // per slope: keeps the roof within the kit's ceiling at every value
constexpr float    roof_body_inset  = 0.02f; // the body stands this far inside the wall lines, so it meets no slab vertex
constexpr float    roof_butt        = 0.05f; // a course's lower edge stands this far above the slab
constexpr float    roof_lap         = 0.012f; // and its upper edge this far
constexpr float    roof_embed       = 0.01f; // a course's underside lies this far inside the slab
constexpr float    roof_pullback    = 0.02f; // a course ends this far short of the next station along the slope
constexpr float    roof_hip_gap     = 0.015f; // and this far from a hip line along its edge
constexpr float    roof_least_ridge = 0.05f; // a hip roof's ridge shorter than twice this ends in a point

enum class Roof_form : unsigned int {
    gable = 0,
    hip,
    shed,
    pyramid
};

[[nodiscard]] auto get_roof_form(const std::string_view form) -> Roof_form
{
    if (form == "hip") {
        return Roof_form::hip;
    }
    if (form == "shed") {
        return Roof_form::shed;
    }
    if (form == "pyramid") {
        return Roof_form::pyramid;
    }
    return Roof_form::gable;
}

// The roof's sizes from its values (doc/editor/level_kit_shapes.md "Roof").
class Roof_frame
{
public:
    explicit Roof_frame(const Kit_parameter_values& values)
        : form    {get_roof_form(values.get_enum("form"))}
        , width   {values.get_float("width")}
        , depth   {values.get_float("depth")}
        , overhang{values.get_float("overhang")}
        , pitch   {glm::radians(values.get_float("pitch"))}
        , curve   {2.5f * values.get_float("curve")}
    {
        outer_x = (0.5f * width) + overhang;
        outer_z = (0.5f * depth) + overhang;
        switch (form) {
            case Roof_form::gable: run = outer_z; break;
            case Roof_form::shed:  run = 2.0f * outer_z; break;
            default:               run = std::min(outer_x, outer_z); break;
        }
        rise    = run * std::tan(pitch);
        courses = std::clamp(static_cast<int>(std::lround((run / std::cos(pitch)) / roof_course)), 2, roof_max_courses);
    }

    // Height of the slab's underside at s along the run (0 at the eave, 1 at the ridge): the curve flattens the eaves.
    [[nodiscard]] auto underside(const float s) const -> float
    {
        return rise * (s - (curve * s * (1.0f - s)));
    }

    Roof_form form;
    float     width;
    float     depth;
    float     overhang;
    float     pitch;
    float     curve;
    float     outer_x{0.0f};
    float     outer_z{0.0f};
    float     run    {0.0f};
    float     rise   {0.0f};
    int       courses{2};
};

// One tile course on the slab's top: lower edge l0 - l1, upper edge u0 - u1 (equal for a hip end's last course), with
// (l1 - l0) x (u0 - l0) pointing up; the butt stands out at the lower edge.
void add_roof_course(Kit_mesh_builder& builder, const glm::vec3& l0, const glm::vec3& l1, const glm::vec3& u0, const glm::vec3& u1)
{
    const glm::vec3 butt {0.0f, roof_butt, 0.0f};
    const glm::vec3 lap  {0.0f, roof_lap, 0.0f};
    const glm::vec3 embed{0.0f, roof_embed, 0.0f};
    const uint32_t b_l0 = builder.add_vertex(l0 - embed);
    const uint32_t b_l1 = builder.add_vertex(l1 - embed);
    const uint32_t t_l0 = builder.add_vertex(l0 + butt);
    const uint32_t t_l1 = builder.add_vertex(l1 + butt);
    builder.add_quad(b_l0, b_l1, t_l1, t_l0); // the butt
    if (u0 == u1) {
        const uint32_t b_u = builder.add_vertex(u0 - embed);
        const uint32_t t_u = builder.add_vertex(u0 + lap);
        builder.add_triangle(t_l0, t_l1, t_u);
        builder.add_triangle(b_l0, b_u, b_l1);
        builder.add_quad(b_l0, t_l0, t_u, b_u);
        builder.add_quad(b_l1, b_u, t_u, t_l1);
        return;
    }
    const uint32_t b_u0 = builder.add_vertex(u0 - embed);
    const uint32_t b_u1 = builder.add_vertex(u1 - embed);
    const uint32_t t_u0 = builder.add_vertex(u0 + lap);
    const uint32_t t_u1 = builder.add_vertex(u1 + lap);
    builder.add_quad(t_l0, t_l1, t_u1, t_u0);
    builder.add_quad(b_l0, b_u0, b_u1, b_l1);
    builder.add_quad(b_u1, b_u0, t_u0, t_u1);
    builder.add_quad(b_l0, t_l0, t_u0, b_u0);
    builder.add_quad(b_l1, b_u1, t_u1, t_l1);
}

// The upper edge pulled back toward the lower one by roof_pullback, so it meets no vertex of the next course.
[[nodiscard]] auto pull_back(const glm::vec3& upper, const glm::vec3& lower) -> glm::vec3
{
    const float length = glm::length(lower - upper);
    return upper + (std::min(0.25f, roof_pullback / length) * (lower - upper));
}

// The gable and the shed: a section along Z extruded along X. Chain points carry z and s along the run.
class Roof_station
{
public:
    float z;
    float s;
};

void make_roof_chain(const Roof_frame& frame, std::vector<Roof_station>& out_chain)
{
    out_chain.clear();
    const int n = frame.courses;
    for (int i = 0; i <= n; ++i) {
        const float s = static_cast<float>(i) / static_cast<float>(n);
        out_chain.push_back(Roof_station{.z = (frame.form == Roof_form::shed) ? (frame.outer_z - (2.0f * frame.outer_z * s)) : (frame.outer_z * (1.0f - s)), .s = s});
    }
    if (frame.form == Roof_form::gable) {
        for (int i = n - 1; i >= 0; --i) {
            const float s = static_cast<float>(i) / static_cast<float>(n);
            out_chain.push_back(Roof_station{.z = -frame.outer_z * (1.0f - s), .s = s});
        }
    }
}

// A solid of the chain's points between heights low(c) and high(c), from x0 to x1: the slab or the body.
template <typename Low, typename High>
void add_extruded_section(Kit_mesh_builder& builder, const std::vector<Roof_station>& chain, const float x0, const float x1, const Low& low, const High& high)
{
    const std::size_t count = chain.size();
    std::vector<uint32_t> top_0(count);
    std::vector<uint32_t> top_1(count);
    std::vector<uint32_t> bottom_0(count);
    std::vector<uint32_t> bottom_1(count);
    for (std::size_t c = 0; c < count; ++c) {
        top_0   [c] = builder.add_vertex(glm::vec3{x0, high(chain[c]), chain[c].z});
        top_1   [c] = builder.add_vertex(glm::vec3{x1, high(chain[c]), chain[c].z});
        bottom_0[c] = builder.add_vertex(glm::vec3{x0, low (chain[c]), chain[c].z});
        bottom_1[c] = builder.add_vertex(glm::vec3{x1, low (chain[c]), chain[c].z});
    }
    for (std::size_t c = 0; c + 1 < count; ++c) {
        builder.add_quad(top_0[c], top_1[c], top_1[c + 1], top_0[c + 1]);
        builder.add_quad(bottom_0[c], bottom_0[c + 1], bottom_1[c + 1], bottom_1[c]);
        builder.add_quad(bottom_1[c], bottom_1[c + 1], top_1[c + 1], top_1[c]);
        builder.add_quad(bottom_0[c], top_0[c], top_0[c + 1], bottom_0[c + 1]);
    }
    const std::size_t last = count - 1;
    builder.add_quad(bottom_0[0], bottom_1[0], top_1[0], top_0[0]);
    builder.add_quad(bottom_1[last], bottom_0[last], top_0[last], top_1[last]);
}

void generate_extruded_roof(const Roof_frame& frame, Kit_mesh_builder& builder)
{
    std::vector<Roof_station> chain;
    make_roof_chain(frame, chain);
    const float t = roof_thickness;
    builder.set_slot(roof_tile_slot);
    add_extruded_section(builder, chain, -frame.outer_x, frame.outer_x,
        [&frame](const Roof_station& p) { return frame.underside(p.s); },
        [&frame, t](const Roof_station& p) { return frame.underside(p.s) + t; });

    // The body: the chain's stations inside the wall lines, its top in the slab's middle, closed at y = 0.
    const float body_x = (0.5f * frame.width) - roof_body_inset;
    const float body_z = (0.5f * frame.depth) - roof_body_inset;
    const auto  s_at   = [&frame](const float z) -> float {
        return (frame.form == Roof_form::shed) ? ((frame.outer_z - z) / (2.0f * frame.outer_z)) : (1.0f - (std::abs(z) / frame.outer_z));
    };
    std::vector<Roof_station> body;
    body.push_back(Roof_station{.z = body_z, .s = s_at(body_z)});
    for (const Roof_station& p : chain) {
        if (std::abs(p.z) < body_z - 0.02f) {
            body.push_back(p);
        }
    }
    body.push_back(Roof_station{.z = -body_z, .s = s_at(-body_z)});
    builder.set_slot(roof_trim_slot);
    add_extruded_section(builder, body, -body_x, body_x,
        [](const Roof_station&) { return 0.0f; },
        [&frame, t](const Roof_station& p) { return frame.underside(p.s) + (0.5f * t); });

    // Tile courses up each slope, and the ridge cap.
    builder.set_slot(roof_tile_slot);
    const int  n     = frame.courses;
    const auto point = [&frame, t](const float x, const Roof_station& p) { return glm::vec3{x, frame.underside(p.s) + t, p.z}; };
    for (int i = 0; i < n; ++i) {
        const Roof_station& lower = chain[static_cast<std::size_t>(i)];
        const Roof_station& upper = chain[static_cast<std::size_t>(i + 1)];
        add_roof_course(builder, point(-frame.outer_x, lower), point(frame.outer_x, lower),
                        pull_back(point(-frame.outer_x, upper), point(-frame.outer_x, lower)), pull_back(point(frame.outer_x, upper), point(frame.outer_x, lower)));
        if (frame.form == Roof_form::gable) {
            const Roof_station& back_lower = chain[chain.size() - 1 - static_cast<std::size_t>(i)];
            const Roof_station& back_upper = chain[chain.size() - 2 - static_cast<std::size_t>(i)];
            add_roof_course(builder, point(frame.outer_x, back_lower), point(-frame.outer_x, back_lower),
                            pull_back(point(frame.outer_x, back_upper), point(frame.outer_x, back_lower)), pull_back(point(-frame.outer_x, back_upper), point(-frame.outer_x, back_lower)));
        }
    }
    if (frame.form == Roof_form::gable) {
        const float ridge = frame.underside(1.0f) + t + roof_lap;
        builder.add_beam(glm::vec3{-frame.outer_x - 0.03f, ridge, 0.0f}, glm::vec3{frame.outer_x + 0.03f, ridge, 0.0f}, 0.2f, 0.12f);
    }
}

// The hip and the pyramid: rings of four corners, counter-clockwise seen from above, from the eave to the ridge; the
// last ring is the ridge (two vertices) or the apex (one).
class Roof_ring
{
public:
    float half_x;
    float half_z;
    float s;
};

// The ring at s along the run; at s = 1 the ridge, or the apex of a pyramid or of a hip roof whose ridge would be too short.
[[nodiscard]] auto roof_ring_at(const Roof_frame& frame, const float s) -> Roof_ring
{
    const float least = std::min(frame.outer_x, frame.outer_z);
    const bool  point = (frame.form == Roof_form::pyramid) || (std::abs(frame.outer_x - frame.outer_z) < 2.0f * roof_least_ridge);
    if (s >= 1.0f) {
        return point ? Roof_ring{0.0f, 0.0f, 1.0f} : Roof_ring{frame.outer_x - least, frame.outer_z - least, 1.0f};
    }
    if (frame.form == Roof_form::pyramid) {
        return Roof_ring{(1.0f - s) * frame.outer_x, (1.0f - s) * frame.outer_z, s};
    }
    return Roof_ring{frame.outer_x - (s * least), frame.outer_z - (s * least), s};
}

void make_roof_rings(const Roof_frame& frame, std::vector<Roof_ring>& out_rings)
{
    out_rings.clear();
    const int n = frame.courses;
    for (int i = 0; i <= n; ++i) {
        out_rings.push_back(roof_ring_at(frame, (i == n) ? 1.0f : (static_cast<float>(i) / static_cast<float>(n))));
    }
}

// The ring's corner vertices; a ridge or apex ring repeats its vertices.
void add_ring_corners(Kit_mesh_builder& builder, const Roof_ring& ring, const float y, std::array<uint32_t, 4>& out_corners)
{
    if ((ring.half_x == 0.0f) && (ring.half_z == 0.0f)) {
        const uint32_t apex = builder.add_vertex(glm::vec3{0.0f, y, 0.0f});
        out_corners = {apex, apex, apex, apex};
        return;
    }
    if (ring.half_z == 0.0f) {
        const uint32_t plus  = builder.add_vertex(glm::vec3{ ring.half_x, y, 0.0f});
        const uint32_t minus = builder.add_vertex(glm::vec3{-ring.half_x, y, 0.0f});
        out_corners = {plus, plus, minus, minus};
        return;
    }
    if (ring.half_x == 0.0f) {
        const uint32_t front = builder.add_vertex(glm::vec3{0.0f, y,  ring.half_z});
        const uint32_t back  = builder.add_vertex(glm::vec3{0.0f, y, -ring.half_z});
        out_corners = {front, back, back, front};
        return;
    }
    out_corners = {
        builder.add_vertex(glm::vec3{ ring.half_x, y,  ring.half_z}),
        builder.add_vertex(glm::vec3{ ring.half_x, y, -ring.half_z}),
        builder.add_vertex(glm::vec3{-ring.half_x, y, -ring.half_z}),
        builder.add_vertex(glm::vec3{-ring.half_x, y,  ring.half_z})
    };
}

[[nodiscard]] auto corner_position(const Roof_ring& ring, const std::size_t corner, const float y) -> glm::vec3
{
    const float x = ((corner == 0) || (corner == 1)) ? ring.half_x : -ring.half_x;
    const float z = ((corner == 0) || (corner == 3)) ? ring.half_z : -ring.half_z;
    return glm::vec3{x, y, z};
}

// The faces between consecutive rings of corner indices: a quad per side, a triangle where a ring's side collapses.
void add_ring_bands(Kit_mesh_builder& builder, const std::vector<std::array<uint32_t, 4>>& rings)
{
    for (std::size_t r = 0; r + 1 < rings.size(); ++r) {
        const std::array<uint32_t, 4>& a = rings[r];
        const std::array<uint32_t, 4>& b = rings[r + 1];
        for (std::size_t j = 0; j < 4; ++j) {
            const std::size_t k = (j + 1) % 4;
            std::array<uint32_t, 4> polygon{};
            std::size_t count = 0;
            for (const uint32_t v : {a[j], a[k], b[k], b[j]}) {
                if ((count == 0) || (polygon[count - 1] != v)) {
                    polygon[count++] = v;
                }
            }
            if ((count > 1) && (polygon[count - 1] == polygon[0])) {
                --count;
            }
            if (count >= 3) {
                builder.add_polygon(std::span<const uint32_t>{polygon.data(), count});
            }
        }
    }
}

void generate_ring_roof(const Roof_frame& frame, Kit_mesh_builder& builder)
{
    std::vector<Roof_ring> rings;
    make_roof_rings(frame, rings);
    const float t = roof_thickness;
    const int   n = frame.courses;

    // The slab: its underside from the ridge out to the eave, the fascia, its top back in to the ridge.
    builder.set_slot(roof_tile_slot);
    std::vector<std::array<uint32_t, 4>> slab;
    std::array<uint32_t, 4> corners{};
    for (int i = n; i >= 0; --i) {
        add_ring_corners(builder, rings[static_cast<std::size_t>(i)], frame.underside(rings[static_cast<std::size_t>(i)].s), corners);
        slab.push_back(corners);
    }
    for (int i = 0; i <= n; ++i) {
        add_ring_corners(builder, rings[static_cast<std::size_t>(i)], frame.underside(rings[static_cast<std::size_t>(i)].s) + t, corners);
        slab.push_back(corners);
    }
    add_ring_bands(builder, slab);

    // The body: a ring at the wall lines on y = 0, then the stations inside them with the top in the slab's middle.
    const float body_x = (0.5f * frame.width) - roof_body_inset;
    const float body_z = (0.5f * frame.depth) - roof_body_inset;
    Roof_ring wall{};
    if (frame.form == Roof_form::pyramid) {
        const float scale = std::min(body_x / frame.outer_x, body_z / frame.outer_z);
        wall = Roof_ring{scale * frame.outer_x, scale * frame.outer_z, 1.0f - scale};
    } else {
        wall = Roof_ring{body_x, body_z, (frame.outer_x - body_x) / std::min(frame.outer_x, frame.outer_z)};
    }
    builder.set_slot(roof_trim_slot);
    std::vector<std::array<uint32_t, 4>> body;
    add_ring_corners(builder, wall, 0.0f, corners);
    builder.add_quad(corners[3], corners[2], corners[1], corners[0]);
    body.push_back(corners);
    add_ring_corners(builder, wall, frame.underside(wall.s) + (0.5f * t), corners);
    body.push_back(corners);
    for (const Roof_ring& ring : rings) {
        if (ring.s > wall.s + 0.02f) {
            add_ring_corners(builder, ring, frame.underside(ring.s) + (0.5f * t), corners);
            body.push_back(corners);
        }
    }
    add_ring_bands(builder, body);

    // Tile courses on each face, short of the hip lines; caps along the hips and the ridge.
    builder.set_slot(roof_tile_slot);
    for (int i = 0; i < n; ++i) {
        const Roof_ring& lower = rings[static_cast<std::size_t>(i)];
        const Roof_ring& upper = rings[static_cast<std::size_t>(i + 1)];
        const float lower_y = frame.underside(lower.s) + t;
        const float upper_y = frame.underside(upper.s) + t;
        for (std::size_t j = 0; j < 4; ++j) {
            const std::size_t k = (j + 1) % 4;
            glm::vec3 l0 = corner_position(lower, j, lower_y);
            glm::vec3 l1 = corner_position(lower, k, lower_y);
            glm::vec3 u0 = corner_position(upper, j, upper_y);
            glm::vec3 u1 = corner_position(upper, k, upper_y);
            const glm::vec3 lower_step = roof_hip_gap * glm::normalize(l1 - l0);
            l0 += lower_step;
            l1 -= lower_step;
            if (glm::length(u1 - u0) > 4.0f * roof_hip_gap) {
                const glm::vec3 upper_step = roof_hip_gap * glm::normalize(u1 - u0);
                u0 = pull_back(u0 + upper_step, l0);
                u1 = pull_back(u1 - upper_step, l1);
            } else {
                u0 = pull_back(0.5f * (u0 + u1), 0.5f * (l0 + l1));
                u1 = u0;
            }
            add_roof_course(builder, l0, l1, u0, u1);
        }
    }
    // A hip cap follows a curved roof in pieces of two stations each.
    const Roof_ring& top        = rings.back();
    const float      top_y      = frame.underside(1.0f) + t + roof_lap;
    const int        cap_pieces = (frame.curve > 0.0f) ? std::max(1, n / 2) : 1;
    for (std::size_t j = 0; j < 4; ++j) {
        for (int p = 0; p < cap_pieces; ++p) {
            const float s0 = static_cast<float>(p) / static_cast<float>(cap_pieces);
            const float s1 = static_cast<float>(p + 1) / static_cast<float>(cap_pieces);
            const glm::vec3 start = corner_position(roof_ring_at(frame, s0), j, frame.underside(s0) + t + roof_lap);
            glm::vec3       end   = corner_position(roof_ring_at(frame, s1), j, frame.underside(s1) + t + roof_lap);
            if (p + 1 == cap_pieces) {
                end = start + (0.97f * (end - start)); // short of the ridge cap or the finial, which cover the joint
            }
            builder.add_beam(start, end, 0.16f, 0.1f);
        }
    }
    if (top.half_x > 0.0f) {
        builder.add_beam(glm::vec3{-top.half_x - 0.1f, top_y, 0.0f}, glm::vec3{top.half_x + 0.1f, top_y, 0.0f}, 0.2f, 0.12f);
    } else if (top.half_z > 0.0f) {
        builder.add_beam(glm::vec3{0.0f, top_y, top.half_z + 0.1f}, glm::vec3{0.0f, top_y, -top.half_z - 0.1f}, 0.2f, 0.12f);
    } else {
        builder.add_box(glm::vec3{-0.1f, top_y - 0.1f, -0.1f}, glm::vec3{0.1f, top_y + 0.2f, 0.1f}); // the finial
    }
}

void generate_roof(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Roof_frame frame{values};
    Kit_mesh_builder builder;
    if ((frame.form == Roof_form::gable) || (frame.form == Roof_form::shed)) {
        generate_extruded_roof(frame, builder);
    } else {
        generate_ring_roof(frame, builder);
    }
    builder.write(mesh);
}

} // anonymous namespace

void add_roof_shapes(std::vector<Kit_shape_definition>& shapes)
{
    Kit_shape_definition shape;
    shape.id              = "architecture.roof";
    shape.kit             = "architecture";
    shape.display_name    = "Roof";
    shape.description     =
        "Roof over a width by depth rectangle of wall lines, its base at the walls' top: a gable, hip, shed (rising toward "
        "-Z) or pyramid slab of real thickness with an eaves overhang, rows of tiles and ridge and hip caps, closed below by "
        "a body inside the wall lines whose ends are the gable ends; the curve flattens the eaves (the Japanese roof). Its "
        "front, back, left and right connectors join the tops of walls whose fronts face out.";
    shape.tags            = {"obstacle", "roof", "size:4x4x2"};
    shape.parameters      = {
        derive_enum(make_enum_parameter("form", "Gable, hip, shed or pyramid", {"gable", "hip", "shed", "pyramid"}, 0)),
        derive_float(make_length_parameter("width", "Size along X between the wall lines", 4.0f, Kit_default_scale::modules, 1.0f, 32.0f), 2.0f, 8.0f, 0.5f),
        derive_float(make_length_parameter("depth", "Size along Z between the wall lines", 4.0f, Kit_default_scale::modules, 1.0f, 32.0f), 2.0f, 8.0f, 0.5f),
        derive_float(make_angle_parameter("pitch", "Slope of the roof", 35.0f, 15.0f, 60.0f), 20.0f, 45.0f, 5.0f),
        derive_float(make_length_parameter("overhang", "How far the eaves reach out beyond the wall lines", 0.3f, Kit_default_scale::meters, 0.0f, 0.6f), 0.1f, 0.5f, 0.05f),
        derive_float(make_ratio_parameter("curve", "How much the eaves flatten and the ridge steepens", 0.0f), 0.0f, 0.3f, 0.05f),
        make_seed_parameter()
    };
    shape.parameters[5].float_max = 0.3f;
    shape.material_slots  = {"roof", "trim"};
    shape.default_shading = Kit_shading::faceted;
    shape.triangle_budget = 520;
    shape.collision       = Kit_collision::triangle_mesh;
    shape.origin_rule     = Kit_origin_rule::base_contains_origin;
    shape.generator       = generate_roof;
    shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x};
    shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
        const float half_x = 0.5f * values.get_float("width");
        const float half_z = 0.5f * values.get_float("depth");
        const glm::vec3 down{0.0f, -1.0f, 0.0f};
        out_connectors.push_back(Kit_connector{.name = "base",  .position = glm::vec3{0.0f},                 .outward = down, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
        out_connectors.push_back(Kit_connector{.name = "left",  .position = glm::vec3{-half_x, 0.0f, 0.0f}, .outward = down, .up = glm::vec3{-1.0f, 0.0f, 0.0f}});
        out_connectors.push_back(Kit_connector{.name = "right", .position = glm::vec3{ half_x, 0.0f, 0.0f}, .outward = down, .up = glm::vec3{ 1.0f, 0.0f, 0.0f}});
        out_connectors.push_back(Kit_connector{.name = "front", .position = glm::vec3{0.0f, 0.0f,  half_z}, .outward = down, .up = glm::vec3{0.0f, 0.0f,  1.0f}});
        out_connectors.push_back(Kit_connector{.name = "back",  .position = glm::vec3{0.0f, 0.0f, -half_z}, .outward = down, .up = glm::vec3{0.0f, 0.0f, -1.0f}});
    };
    shapes.push_back(std::move(shape));
}

} // namespace editor
