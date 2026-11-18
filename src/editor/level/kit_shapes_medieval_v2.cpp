#include "level/kit_shapes.hpp"
#include "level/kit_mesh_builder.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The recreated battlement and tower (doc/editor/level_kit_shapes.md "Battlement (F)", "Tower (F)").

namespace editor {

namespace {

constexpr uint32_t medieval_wall_slot_v2  = 0;
constexpr uint32_t medieval_trim_slot_v2  = 1;
constexpr uint32_t medieval_floor_slot_v2 = 2;

enum class Sweep_path_v2 : unsigned int { open = 0, closed };

// Whether the first and the last cell of an open sweep are triangles: their end stations are no scaled copies of the next.
enum class Sweep_ends_v2 : unsigned int { quads = 0, triangles };

// A profile carried along stations: each station holds the profile's points, counter-clockwise seen along the path.
// A quad per profile edge between two stations; an open path is closed by a fan from the apex point at each end.
void add_sweep_v2(
    Kit_mesh_builder&                          builder,
    const std::vector<std::vector<glm::vec3>>& stations,
    const Sweep_path_v2                        path,
    const Sweep_ends_v2                        ends,
    const std::size_t                          cap_apex
)
{
    const std::size_t count   = stations.size();
    const std::size_t profile = stations.front().size();
    std::vector<std::vector<uint32_t>> vertices(count);
    for (std::size_t j = 0; j < count; ++j) {
        for (const glm::vec3& point : stations[j]) {
            vertices[j].push_back(builder.add_vertex(point));
        }
    }
    const std::size_t cells = (path == Sweep_path_v2::closed) ? count : (count - 1);
    for (std::size_t j = 0; j < cells; ++j) {
        const std::vector<uint32_t>& a = vertices[j];
        const std::vector<uint32_t>& b = vertices[(j + 1) % count];
        const bool split = (ends == Sweep_ends_v2::triangles) && ((j == 0) || ((j + 1) == cells));
        for (std::size_t k = 0; k < profile; ++k) {
            const std::size_t next = (k + 1) % profile;
            if (split) {
                builder.add_triangle(a[k], b[k], b[next]);
                builder.add_triangle(a[k], b[next], a[next]);
            } else {
                builder.add_quad(a[k], b[k], b[next], a[next]);
            }
        }
    }
    if (path == Sweep_path_v2::closed) {
        return;
    }
    const std::vector<uint32_t>& first = vertices.front();
    const std::vector<uint32_t>& last  = vertices.back();
    for (std::size_t k = 1; (k + 1) < profile; ++k) {
        const std::size_t p = (cap_apex + k) % profile;
        const std::size_t q = (cap_apex + k + 1) % profile;
        builder.add_triangle(first[cap_apex], first[p], first[q]);
        builder.add_triangle(last[cap_apex],  last[q],  last[p]);
    }
}

// As add_sweep_v2() for a convex profile on an open path of scaled or parallel stations: each end is one polygon.
void add_prism_v2(Kit_mesh_builder& builder, const std::vector<std::vector<glm::vec3>>& stations)
{
    const std::size_t count   = stations.size();
    const std::size_t profile = stations.front().size();
    std::vector<std::vector<uint32_t>> vertices(count);
    for (std::size_t j = 0; j < count; ++j) {
        for (const glm::vec3& point : stations[j]) {
            vertices[j].push_back(builder.add_vertex(point));
        }
    }
    for (std::size_t j = 0; (j + 1) < count; ++j) {
        for (std::size_t k = 0; k < profile; ++k) {
            const std::size_t next = (k + 1) % profile;
            builder.add_quad(vertices[j][k], vertices[j + 1][k], vertices[j + 1][next], vertices[j][next]);
        }
    }
    builder.add_polygon(vertices.front());
    const std::vector<uint32_t> end(vertices.back().rbegin(), vertices.back().rend());
    builder.add_polygon(end);
}

constexpr int battlement_max_bays_v2 = 22; // keeps a run within the kit's ceiling at every length

enum class Merlon_top_v2 : unsigned int { square = 0, swallowtail, pointed };

// The sizes of a battlement run; its detail follows the merlon's size where the stated sizes leave little room.
class Battlement_form_v2
{
public:
    explicit Battlement_form_v2(const Kit_parameter_values& values);

    float         half_length   {0.0f};
    float         half_thickness{0.0f};
    float         sill          {0.0f}; // the crenels' sill, the top of the sill band
    float         top           {0.0f}; // the copings' highest line
    int           bays          {1};
    float         pitch         {0.0f};
    float         merlon        {0.0f}; // width
    Merlon_top_v2 style         {Merlon_top_v2::square};
    float         band          {0.0f}; // height of the sill band
    float         overhang      {0.0f}; // of the band and the copings beyond the faces
    float         cap           {0.0f}; // height of a coping
    float         drop          {0.0f}; // a swallowtail's notch and a pointed merlon's shoulders below its top
    float         chamfer       {0.0f}; // of a merlon's upright edges
    float         loop_half     {0.0f}; // half the arrow loop's width
    float         loop_bottom   {0.0f};
    float         loop_top      {0.0f};
    float         splay         {0.0f}; // the loop's reveal on a face
    float         splay_depth   {0.0f};
};

Battlement_form_v2::Battlement_form_v2(const Kit_parameter_values& values)
    : half_length   {0.5f * values.get_float("length")}
    , half_thickness{0.5f * values.get_float("thickness")}
    , sill          {values.get_float("height")}
    , top           {values.get_float("height") + values.get_float("merlon_height")}
{
    const float            merlon_height = values.get_float("merlon_height");
    const float            merlon_width  = values.get_float("merlon_width");
    const float            crenel_width  = values.get_float("crenel_width");
    const std::string_view merlon_top    = values.get_enum("merlon");
    bays   = std::clamp(static_cast<int>(std::lround((2.0f * half_length) / (merlon_width + crenel_width))), 1, battlement_max_bays_v2);
    pitch  = (2.0f * half_length) / static_cast<float>(bays);
    merlon = pitch * merlon_width / (merlon_width + crenel_width);
    style  = (merlon_top == "swallowtail") ? Merlon_top_v2::swallowtail : ((merlon_top == "pointed") ? Merlon_top_v2::pointed : Merlon_top_v2::square);
    band     = std::min(0.07f, 0.25f * sill);
    overhang = std::min(0.035f, 0.4f * half_thickness);
    cap      = std::min(0.12f, 0.25f * merlon_height);
    drop     = (style == Merlon_top_v2::square) ? 0.0f : (0.3f * merlon_height);
    const float body = merlon_height - cap - drop; // the merlon's height under its lowest top line
    chamfer     = std::min(std::min(0.04f, 0.08f * merlon), 0.35f * half_thickness);
    loop_half   = std::min(0.05f, 0.1f * merlon);
    splay       = std::min(std::min(0.06f, 0.12f * merlon), 0.12f * body);
    splay_depth = std::min(splay, 0.5f * half_thickness);
    loop_bottom = sill + (0.2f * body);
    loop_top    = loop_bottom + (0.55f * body);
}

// A vertex of a merlon's face and its mirror image on the other face.
class Face_pair_v2
{
public:
    uint32_t front{0};
    uint32_t back {0};
};

// A facet of the front face (counter-clockwise seen from +Z) and its mirror image on the back.
void add_face_pair_v2(Kit_mesh_builder& builder, const std::vector<Face_pair_v2>& corners)
{
    std::vector<uint32_t> front;
    std::vector<uint32_t> back;
    for (const Face_pair_v2& corner : corners) {
        front.push_back(corner.front);
    }
    for (std::vector<Face_pair_v2>::const_reverse_iterator it = corners.rbegin(); it != corners.rend(); ++it) {
        back.push_back(it->back);
    }
    builder.add_polygon(front);
    builder.add_polygon(back);
}

// One merlon on the sill band, centered at center_x: chamfered upright edges and an arrow loop splayed on both faces.
void add_merlon_v2(Kit_mesh_builder& builder, const Battlement_form_v2& form, const float center_x)
{
    const float half    = 0.5f * form.merlon;
    const float x0      = center_x - half;
    const float x1      = center_x + half;
    const float z_face  = form.half_thickness;
    const float z_side  = form.half_thickness - form.chamfer;
    // The body's top line under the coping: its height at the merlon's sides and at its center.
    const float y_edge  = (form.style == Merlon_top_v2::pointed) ? (form.top - form.cap - form.drop) : (form.top - form.cap);
    const float y_mid   = (form.style == Merlon_top_v2::swallowtail) ? (form.top - form.cap - form.drop) : (form.top - form.cap);
    const float y_inset = y_edge + ((y_mid - y_edge) * (form.chamfer / half)); // at the chamfer's inner line

    // The plan's corners counter-clockwise seen from above, from the front face's left end.
    const std::array<glm::vec2, 8> plan{
        glm::vec2{x0 + form.chamfer,  z_face}, glm::vec2{x1 - form.chamfer,  z_face}, glm::vec2{x1,  z_side}, glm::vec2{x1, -z_side},
        glm::vec2{x1 - form.chamfer, -z_face}, glm::vec2{x0 + form.chamfer, -z_face}, glm::vec2{x0, -z_side}, glm::vec2{x0,  z_side}
    };
    std::array<uint32_t, 8> bottom{};
    std::array<uint32_t, 8> upper{};
    for (std::size_t i = 0; i < plan.size(); ++i) {
        const bool inset = (i == 0) || (i == 1) || (i == 4) || (i == 5);
        bottom[i] = builder.add_vertex(glm::vec3{plan[i].x, form.sill, plan[i].y});
        upper[i]  = builder.add_vertex(glm::vec3{plan[i].x, inset ? y_inset : y_edge, plan[i].y});
    }
    const auto pair_at = [&builder](const float x, const float y, const float z) -> Face_pair_v2 {
        return Face_pair_v2{.front = builder.add_vertex(glm::vec3{x, y, z}), .back = builder.add_vertex(glm::vec3{x, y, -z})};
    };
    const float hole_x0 = center_x - form.loop_half;
    const float hole_x1 = center_x + form.loop_half;
    const float z_loop  = z_face - form.splay_depth;
    const std::array<Face_pair_v2, 4> reveal{
        pair_at(hole_x0 - form.splay, form.loop_bottom - form.splay, z_face), pair_at(hole_x1 + form.splay, form.loop_bottom - form.splay, z_face),
        pair_at(hole_x1 + form.splay, form.loop_top + form.splay,    z_face), pair_at(hole_x0 - form.splay, form.loop_top + form.splay,    z_face)
    };
    const std::array<Face_pair_v2, 4> loop{
        pair_at(hole_x0, form.loop_bottom, z_loop), pair_at(hole_x1, form.loop_bottom, z_loop),
        pair_at(hole_x1, form.loop_top,    z_loop), pair_at(hole_x0, form.loop_top,    z_loop)
    };
    const Face_pair_v2 low_left  {.front = bottom[0], .back = bottom[5]};
    const Face_pair_v2 low_right {.front = bottom[1], .back = bottom[4]};
    const Face_pair_v2 high_left {.front = upper[0],  .back = upper[5]};
    const Face_pair_v2 high_right{.front = upper[1],  .back = upper[4]};

    // A face: the frame between its outline and the reveal's rectangle, then the reveal down to the loop.
    add_face_pair_v2(builder, {low_left,  low_right,  reveal[1], reveal[0]});
    add_face_pair_v2(builder, {low_right, high_right, reveal[2], reveal[1]});
    add_face_pair_v2(builder, {high_left, low_left,   reveal[0], reveal[3]});
    if (form.style == Merlon_top_v2::square) {
        add_face_pair_v2(builder, {high_right, high_left, reveal[3], reveal[2]});
        builder.add_polygon(upper);
    } else {
        const Face_pair_v2 middle = pair_at(center_x, y_mid, z_face);
        if (form.style == Merlon_top_v2::pointed) {
            add_face_pair_v2(builder, {high_right, middle, high_left, reveal[3], reveal[2]});
        } else {
            add_face_pair_v2(builder, {high_right, middle, reveal[2]});
            add_face_pair_v2(builder, {middle, reveal[3], reveal[2]});
            add_face_pair_v2(builder, {middle, high_left, reveal[3]});
        }
        builder.add_polygon(std::array<uint32_t, 6>{middle.front, upper[1], upper[2], upper[3], upper[4], middle.back});
        builder.add_polygon(std::array<uint32_t, 6>{middle.back, upper[5], upper[6], upper[7], upper[0], middle.front});
    }
    for (std::size_t i = 0; i < 4; ++i) {
        const std::size_t next = (i + 1) % 4;
        add_face_pair_v2(builder, {reveal[i], reveal[next], loop[next], loop[i]});
        builder.add_quad(loop[i].front, loop[next].front, loop[next].back, loop[i].back);
    }
    for (const std::size_t i : std::array<std::size_t, 6>{1, 2, 3, 5, 6, 7}) {
        const std::size_t next = (i + 1) % plan.size();
        builder.add_quad(bottom[i], bottom[next], upper[next], upper[i]);
    }
    const std::array<uint32_t, 8> base{bottom[7], bottom[6], bottom[5], bottom[4], bottom[3], bottom[2], bottom[1], bottom[0]};
    builder.add_polygon(base);
}

// A merlon's coping: a saddleback over a square merlon, a slab along the top lines of the two other forms.
void add_merlon_coping_v2(Kit_mesh_builder& builder, const Battlement_form_v2& form, const float center_x)
{
    const float half   = 0.5f * form.merlon;
    const float width  = form.half_thickness + form.overhang;
    const float bottom = form.top - form.cap;
    std::vector<std::vector<glm::vec3>> stations;
    if (form.style == Merlon_top_v2::square) {
        const float reach = std::min(form.overhang, 0.2f * (form.pitch - form.merlon)); // into the crenels
        const float eaves = bottom + (0.45f * form.cap);
        for (const float x : {center_x - half - reach, center_x + half + reach}) {
            stations.push_back({{x, bottom, -width}, {x, bottom, width}, {x, eaves, width}, {x, form.top, 0.0f}, {x, eaves, -width}});
        }
        add_prism_v2(builder, stations);
        return;
    }
    const float edge   = (form.style == Merlon_top_v2::pointed) ? (bottom - form.drop) : bottom;
    const float middle = (form.style == Merlon_top_v2::pointed) ? bottom : (bottom - form.drop);
    for (const glm::vec2 point : {glm::vec2{center_x - half, edge}, glm::vec2{center_x, middle}, glm::vec2{center_x + half, edge}}) {
        stations.push_back({
            {point.x, point.y, -width}, {point.x, point.y, width}, {point.x, point.y + form.cap, width}, {point.x, point.y + form.cap, -width}
        });
    }
    add_prism_v2(builder, stations);
}

void generate_battlement_v2(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Battlement_form_v2 form{values};
    Kit_mesh_builder builder;

    // The breastwork under the sill band, flush with the wall it stands on.
    const float band_bottom = form.sill - form.band;
    builder.set_slot(medieval_wall_slot_v2);
    builder.add_box(glm::vec3{-form.half_length, 0.0f, -form.half_thickness}, glm::vec3{form.half_length, band_bottom, form.half_thickness});

    // The sill band: the crenels' sill stones as one course, weathered on both edges.
    const float band_width = form.half_thickness + form.overhang;
    const float band_top   = form.half_thickness + (0.3f * form.overhang);
    const float band_edge  = form.sill - (0.4f * form.band);
    builder.set_slot(medieval_trim_slot_v2);
    std::vector<std::vector<glm::vec3>> course;
    for (const float x : {-form.half_length, form.half_length}) {
        course.push_back({
            {x, band_bottom, -band_width}, {x, band_bottom, band_width}, {x, band_edge, band_width},
            {x, form.sill, band_top}, {x, form.sill, -band_top}, {x, band_edge, -band_width}
        });
    }
    add_prism_v2(builder, course);

    // A bay is a merlon between two half crenels, so runs join into whole crenels and their ends are the breastwork's.
    for (int i = 0; i < form.bays; ++i) {
        const float center_x = (static_cast<float>(i) + 0.5f - (0.5f * static_cast<float>(form.bays))) * form.pitch;
        builder.set_slot(medieval_wall_slot_v2);
        add_merlon_v2(builder, form, center_x);
        builder.set_slot(medieval_trim_slot_v2);
        add_merlon_coping_v2(builder, form, center_x);
    }
    builder.write(mesh);
}

constexpr float tower_floor_v2        = 0.3f;   // thickness of the top floor: the ceiling lies this far under the height
constexpr float tower_head_above_v2   = 0.1f;   // the wall's cut rises this far above the doorway's clear height
constexpr float tower_sill_v2         = 1.0f;   // the parapet's breastwork above the walkway
constexpr float tower_merlon_v2       = 0.9f;   // merlons above the breastwork, their copings included
constexpr float tower_coping_v2       = 0.14f;  // height of a merlon's coping
constexpr float tower_overhang_v2     = 0.25f;  // the parapet stands this far out on its corbels
constexpr float tower_parapet_v2      = 0.35f;  // thickness of the parapet
constexpr float tower_rebate_v2       = 0.04f;  // the wall's cut is this much wider than the doorway's frame on each side
constexpr float tower_frame_v2        = 0.28f;  // width of the doorway's jamb stones
constexpr float tower_ear_v2          = 0.07f;  // the lintel reaches this far past the jamb stones
constexpr float tower_plinth_v2       = 0.07f;  // the plinth course stands this far out of the batter's foot
constexpr float tower_plinth_high_v2  = 0.35f;
constexpr float tower_slit_gap_v2     = 0.4f;   // the lowest row of slits above the wall's cut
constexpr float tower_slit_height_v2  = 0.9f;
constexpr float tower_slit_pitch_v2   = 1.5f;   // from one row of slits to the next
constexpr float tower_slit_outer_v2   = 0.14f;  // half widths of a slit: on the outer face,
constexpr float tower_slit_throat_v2  = 0.045f; // at its narrowest, 0.4 of the wall in,
constexpr float tower_slit_inner_v2   = 0.22f;  // and on the inner face, where the wall has room
constexpr float tower_point_margin_v2 = 0.07f;  // plan corners nearer a jamb than this along the wall are left out
constexpr int   tower_min_merlons_v2  = 5;
constexpr int   tower_max_merlons_v2  = 6;      // keeps a wide round tower within the kit's ceiling

// The tower's sizes and plan, shared by the generator, the connectors and the checks. A plan point is a unit corner
// times a scale: a round tower's corner radius, a square tower's half width.
class Tower_form_v2
{
public:
    explicit Tower_form_v2(const Kit_parameter_values& values);

    // The point of the wall line of the scale toward the direction, the lateral distance along the wall from it.
    [[nodiscard]] auto plan_point(const glm::vec2& direction, float lateral, float scale) const -> glm::vec2;

    bool                   round        {true};
    bool                   battlement   {true};
    float                  radius       {0.0f}; // of the outer face
    float                  wall         {0.0f};
    float                  inner        {0.0f}; // of the inner face
    float                  door_half    {0.0f};
    float                  door_height  {0.0f};
    float                  cut_height   {0.0f}; // the head of the wall's cut, and of the battered base
    float                  height       {0.0f}; // the top floor
    float                  soffit       {0.0f}; // the ceiling inside and the parapet's underside
    float                  wall_top     {0.0f}; // inside the top floor
    float                  batter       {0.0f}; // the batter's reach at the ground
    float                  foot         {0.0f}; // the plinth's scale
    float                  frame_front  {0.0f}; // z of the doorway's frame
    float                  corbel_height{0.0f};
    float                  lintel       {0.0f}; // height of the lintel at its ends
    float                  inscribe     {1.0f}; // a side's distance from the axis over its corners'
    int                    slit_rows    {0};    // the rows that fit
    std::vector<glm::vec2> unit;                // plan corners (x, z), counter-clockwise seen from above, by rising key
    std::vector<float>     keys;                // angle from the front (+Z) toward +X, in (-pi, pi]
    std::size_t            front        {0};    // the corner right of the front side
};

Tower_form_v2::Tower_form_v2(const Kit_parameter_values& values)
    : round      {values.get_enum("profile") == "round"}
    , battlement {values.get_enum("top") == "battlement"}
    , radius     {0.5f * values.get_float("width")}
    , wall       {values.get_float("wall")}
    , inner      {(0.5f * values.get_float("width")) - values.get_float("wall")}
    , door_half  {0.5f * values.get_float("opening_width")}
    , door_height{values.get_float("opening_height")}
    , cut_height {values.get_float("opening_height") + tower_head_above_v2}
    , height     {values.get_float("height")}
    , soffit     {values.get_float("height") - tower_floor_v2}
    , wall_top   {values.get_float("height") - (0.6f * tower_floor_v2)}
{
    constexpr float pi = std::numbers::pi_v<float>;
    batter        = cut_height * std::tan(glm::radians(values.get_float("batter")));
    foot          = radius + batter + tower_plinth_v2;
    corbel_height = std::min(0.4f, soffit - cut_height - 0.2f);
    lintel        = std::min(0.3f, 0.5f * (soffit - cut_height));
    if (round) {
        // Three sides per merlon; a side lies across the front, so the doorway is under a crenel.
        const int merlons = std::clamp(static_cast<int>(std::lround(2.0f * pi * radius / 1.6f)), tower_min_merlons_v2, tower_max_merlons_v2);
        const int sides   = 3 * merlons;
        for (int h = ((sides % 2) == 0) ? (1 - sides) : (2 - sides); h <= sides; h += 2) {
            if (h == sides) {
                unit.emplace_back(0.0f, -1.0f);
                keys.push_back(pi);
                continue;
            }
            const float angle = static_cast<float>(std::abs(h)) * pi / static_cast<float>(sides);
            unit.emplace_back((h < 0) ? -std::sin(angle) : std::sin(angle), std::cos(angle));
            keys.push_back((h < 0) ? -angle : angle);
        }
        inscribe = std::cos(pi / static_cast<float>(sides));
    } else {
        unit = {glm::vec2{-1.0f, -1.0f}, glm::vec2{-1.0f, 1.0f}, glm::vec2{1.0f, 1.0f}, glm::vec2{1.0f, -1.0f}};
        keys = {-0.75f * pi, -0.25f * pi, 0.25f * pi, 0.75f * pi};
    }
    // The frame stands out of the plinth where the base ends in it.
    frame_front = plan_point(glm::vec2{0.0f, 1.0f}, door_half + (0.5f * tower_frame_v2), foot).y + 0.06f;
    while (keys[front] < 0.0f) {
        ++front;
    }
    // As many rows of slits as stay clear of the corbels, or of the top floor.
    const float limit = battlement ? (soffit - corbel_height - 0.15f) : (soffit - 0.2f);
    const int   rows  = values.get_int("slit_rows");
    while ((slit_rows < rows) && ((cut_height + tower_slit_gap_v2 + (static_cast<float>(slit_rows) * tower_slit_pitch_v2) + tower_slit_height_v2) <= limit)) {
        ++slit_rows;
    }
}

auto Tower_form_v2::plan_point(const glm::vec2& direction, const float lateral, const float scale) const -> glm::vec2
{
    const glm::vec2 tangent{direction.y, -direction.x};
    const float     depth = round ? std::sqrt((scale * scale) - (lateral * lateral)) : scale;
    return (lateral * tangent) + (depth * direction);
}

[[nodiscard]] auto plan_key_v2(const glm::vec2& point) -> float
{
    return std::atan2(point.x, point.y);
}

// A point of the wall's outer or inner ring in plan; the tag names a corner (its index) or a jamb (negative).
class Wall_point_v2
{
public:
    glm::vec2 position{0.0f};
    float     key     {0.0f};
    int       tag     {0};
};

constexpr int wall_door_left_tag_v2  = -1;
constexpr int wall_door_right_tag_v2 = -2;

[[nodiscard]] constexpr auto wall_slit_tag_v2(const std::size_t slit, const std::size_t side) -> int
{
    return -10 - static_cast<int>((2 * slit) + side);
}

enum class Wall_link_kind_v2 : unsigned int { corner = 0, door_inside, door_left, door_right, slit_low, slit_high };

// A line through the wall from an outer point to an inner one; the cells between links are solid or an opening's.
class Wall_link_v2
{
public:
    Wall_link_kind_v2 kind  {Wall_link_kind_v2::corner};
    std::size_t       outer {0};
    std::size_t       inner {0};
    glm::vec2         throat{0.0f}; // a slit jamb's point at its narrowest
};

enum class Wall_cell_v2 : unsigned int { solid = 0, door, slit };

// An arrow slit in plan: both jambs, by rising key, on the outer face, at the throat and on the inner face.
class Wall_slit_v2
{
public:
    std::array<glm::vec2, 2> outer {};
    std::array<glm::vec2, 2> throat{};
    std::array<glm::vec2, 2> inner {};
};

enum class Wall_ring_v2 : unsigned int { outer = 0, inner };

// The tower's wall: an upright shell between an outer and an inner ring, cut through by the doorway and the slits.
class Tower_wall_v2
{
public:
    Tower_wall_v2(Kit_mesh_builder& builder, const Tower_form_v2& form);

    void build();

private:
    void               add_slits     ();
    void               add_points    (Wall_ring_v2 ring);
    void               add_links     ();
    [[nodiscard]] auto find_tag      (const std::vector<Wall_point_v2>& points, int tag) const -> std::optional<std::size_t>;
    [[nodiscard]] auto is_open       (std::size_t cell, std::size_t band) const -> bool;
    [[nodiscard]] auto get_vertex    (Wall_ring_v2 ring, std::size_t index, std::size_t level) -> uint32_t;
    [[nodiscard]] auto get_throat    (std::size_t link, std::size_t level) -> uint32_t;
    void               get_cell_run  (Wall_ring_v2 ring, std::size_t cell, std::vector<std::size_t>& out_indices) const;
    void               add_walls     (std::size_t cell, std::size_t band);
    void               add_jamb      (std::size_t link, std::size_t band);
    void               add_level_face(std::size_t cell, std::size_t level);

    Kit_mesh_builder&          m_builder;
    const Tower_form_v2&       m_form;
    float                      m_door_key_outer{0.0f}; // the right jamb's keys; the left jamb's are their negatives
    float                      m_door_key_inner{0.0f};
    std::vector<Wall_slit_v2>  m_slits;
    std::vector<Wall_point_v2> m_outer;
    std::vector<Wall_point_v2> m_inner;
    std::vector<Wall_link_v2>  m_links;
    std::vector<Wall_cell_v2>  m_cells; // cell i lies between link i and the next
    std::vector<float>         m_levels;
    std::vector<uint32_t>      m_outer_vertices;
    std::vector<uint32_t>      m_inner_vertices;
    std::vector<uint32_t>      m_throat_vertices;
};

constexpr uint32_t wall_no_vertex_v2 = std::numeric_limits<uint32_t>::max();

Tower_wall_v2::Tower_wall_v2(Kit_mesh_builder& builder, const Tower_form_v2& form)
    : m_builder{builder}
    , m_form   {form}
{
    const float     cut_half = form.door_half + tower_rebate_v2;
    const glm::vec2 toward{0.0f, 1.0f};
    m_door_key_outer = plan_key_v2(form.plan_point(toward, cut_half, form.radius));
    m_door_key_inner = plan_key_v2(form.plan_point(toward, cut_half, form.inner));
    m_levels = {0.0f, form.cut_height};
    for (int row = 0; row < form.slit_rows; ++row) {
        const float y = form.cut_height + tower_slit_gap_v2 + (static_cast<float>(row) * tower_slit_pitch_v2);
        m_levels.push_back(y);
        m_levels.push_back(y + tower_slit_height_v2);
    }
    m_levels.push_back(form.wall_top);
}

// The slits: four on a round tower, a third of a half turn before and after each side connector, the front two where the
// doorway leaves them room; two on each of a square tower's side and back faces, either side of the wall a connector joins.
void Tower_wall_v2::add_slits()
{
    if (m_form.slit_rows == 0) {
        return;
    }
    constexpr float pi = std::numbers::pi_v<float>;
    const float inner_half   = std::min(tower_slit_inner_v2, 0.2f * m_form.inner);
    const float throat_scale = m_form.radius - (0.4f * m_form.wall);
    const auto make_slit = [this, inner_half, throat_scale](const glm::vec2& direction, const float offset) -> Wall_slit_v2 {
        Wall_slit_v2 slit;
        for (std::size_t side = 0; side < 2; ++side) {
            const float sign = (side == 0) ? -1.0f : 1.0f;
            slit.outer [side] = m_form.plan_point(direction, offset + (sign * tower_slit_outer_v2),  m_form.radius);
            slit.throat[side] = m_form.plan_point(direction, offset + (sign * tower_slit_throat_v2), throat_scale);
            slit.inner [side] = m_form.plan_point(direction, offset + (sign * inner_half),           m_form.inner);
        }
        return slit;
    };
    if (!m_form.round) {
        for (const glm::vec2 direction : {glm::vec2{-1.0f, 0.0f}, glm::vec2{1.0f, 0.0f}, glm::vec2{0.0f, -1.0f}}) {
            for (const float offset : {-0.5f * m_form.inner, 0.5f * m_form.inner}) {
                m_slits.push_back(make_slit(direction, offset));
            }
        }
        return;
    }
    const float outer_clear = m_door_key_outer + (2.0f * tower_point_margin_v2 / m_form.radius);
    const float inner_clear = m_door_key_inner + (2.0f * tower_point_margin_v2 / m_form.inner);
    for (const float angle : {-pi * (2.0f / 3.0f), -pi / 3.0f, pi / 3.0f, pi * (2.0f / 3.0f)}) {
        const Wall_slit_v2 slit = make_slit(glm::vec2{std::sin(angle), std::cos(angle)}, 0.0f);
        const bool clear =
            (std::min(std::abs(plan_key_v2(slit.outer[0])), std::abs(plan_key_v2(slit.outer[1]))) > outer_clear) &&
            (std::min(std::abs(plan_key_v2(slit.inner[0])), std::abs(plan_key_v2(slit.inner[1]))) > inner_clear);
        if (clear) {
            m_slits.push_back(slit);
        }
    }
}

// A ring's points by rising key: the plan's corners but those too near a jamb or inside a slit, and the jambs.
void Tower_wall_v2::add_points(const Wall_ring_v2 ring)
{
    const bool                  outside  = (ring == Wall_ring_v2::outer);
    const float                 scale    = outside ? m_form.radius : m_form.inner;
    const float                 margin   = tower_point_margin_v2 / scale;
    const float                 door_key = outside ? m_door_key_outer : m_door_key_inner;
    std::vector<Wall_point_v2>& points   = outside ? m_outer : m_inner;
    for (std::size_t k = 0; k < m_form.unit.size(); ++k) {
        const float key  = m_form.keys[k];
        bool        keep = std::abs(std::abs(key) - door_key) >= margin;
        for (const Wall_slit_v2& slit : m_slits) {
            const std::array<glm::vec2, 2>& jambs = outside ? slit.outer : slit.inner;
            if ((key > (plan_key_v2(jambs[0]) - margin)) && (key < (plan_key_v2(jambs[1]) + margin))) {
                keep = false;
            }
        }
        if (keep) {
            points.push_back(Wall_point_v2{.position = scale * m_form.unit[k], .key = key, .tag = static_cast<int>(k)});
        }
    }
    const glm::vec2 jamb = m_form.plan_point(glm::vec2{0.0f, 1.0f}, m_form.door_half + tower_rebate_v2, scale);
    points.push_back(Wall_point_v2{.position = glm::vec2{-jamb.x, jamb.y}, .key = -door_key, .tag = wall_door_left_tag_v2});
    points.push_back(Wall_point_v2{.position = jamb,                       .key =  door_key, .tag = wall_door_right_tag_v2});
    for (std::size_t s = 0; s < m_slits.size(); ++s) {
        const std::array<glm::vec2, 2>& jambs = outside ? m_slits[s].outer : m_slits[s].inner;
        for (std::size_t side = 0; side < 2; ++side) {
            points.push_back(Wall_point_v2{.position = jambs[side], .key = plan_key_v2(jambs[side]), .tag = wall_slit_tag_v2(s, side)});
        }
    }
    // No two keys are near each other: corners near a jamb are left out, and a slit near the doorway is not made.
    std::sort(points.begin(), points.end(), [](const Wall_point_v2& a, const Wall_point_v2& b) { return a.key < b.key; });
}

auto Tower_wall_v2::find_tag(const std::vector<Wall_point_v2>& points, const int tag) const -> std::optional<std::size_t>
{
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (points[i].tag == tag) {
            return i;
        }
    }
    return std::nullopt;
}

// Links: the jambs, and each corner both rings have on the same side of the doorway's jambs.
void Tower_wall_v2::add_links()
{
    for (std::size_t k = 0; k < m_form.unit.size(); ++k) {
        const std::optional<std::size_t> outside = find_tag(m_outer, static_cast<int>(k));
        const std::optional<std::size_t> inside  = find_tag(m_inner, static_cast<int>(k));
        if (!outside.has_value() || !inside.has_value()) {
            continue;
        }
        const float key         = std::abs(m_form.keys[k]);
        const bool  door_outer  = key < m_door_key_outer;
        const bool  door_inner  = key < m_door_key_inner;
        if (door_outer == door_inner) {
            m_links.push_back(Wall_link_v2{
                .kind = door_outer ? Wall_link_kind_v2::door_inside : Wall_link_kind_v2::corner, .outer = outside.value(), .inner = inside.value(), .throat = {}
            });
        }
    }
    m_links.push_back(Wall_link_v2{
        .kind = Wall_link_kind_v2::door_left, .outer = find_tag(m_outer, wall_door_left_tag_v2).value(), .inner = find_tag(m_inner, wall_door_left_tag_v2).value(), .throat = {}
    });
    m_links.push_back(Wall_link_v2{
        .kind = Wall_link_kind_v2::door_right, .outer = find_tag(m_outer, wall_door_right_tag_v2).value(), .inner = find_tag(m_inner, wall_door_right_tag_v2).value(), .throat = {}
    });
    for (std::size_t s = 0; s < m_slits.size(); ++s) {
        for (std::size_t side = 0; side < 2; ++side) {
            const int tag = wall_slit_tag_v2(s, side);
            m_links.push_back(Wall_link_v2{
                .kind   = (side == 0) ? Wall_link_kind_v2::slit_low : Wall_link_kind_v2::slit_high,
                .outer  = find_tag(m_outer, tag).value(),
                .inner  = find_tag(m_inner, tag).value(),
                .throat = m_slits[s].throat[side]
            });
        }
    }
    std::sort(m_links.begin(), m_links.end(), [](const Wall_link_v2& a, const Wall_link_v2& b) { return a.outer < b.outer; });
    for (std::size_t c = 0; c < m_links.size(); ++c) {
        const Wall_link_kind_v2 a = m_links[c].kind;
        const Wall_link_kind_v2 b = m_links[(c + 1) % m_links.size()].kind;
        const bool door =
            ((a == Wall_link_kind_v2::door_left) || (a == Wall_link_kind_v2::door_inside)) &&
            ((b == Wall_link_kind_v2::door_right) || (b == Wall_link_kind_v2::door_inside));
        const bool slit = (a == Wall_link_kind_v2::slit_low) && (b == Wall_link_kind_v2::slit_high);
        m_cells.push_back(door ? Wall_cell_v2::door : (slit ? Wall_cell_v2::slit : Wall_cell_v2::solid));
    }
}

// Bands: under the cut's head, then under, across and over each row of slits.
auto Tower_wall_v2::is_open(const std::size_t cell, const std::size_t band) const -> bool
{
    if (m_cells[cell] == Wall_cell_v2::door) {
        return band == 0;
    }
    return (m_cells[cell] == Wall_cell_v2::slit) && (band >= 2) && ((band % 2) == 0);
}

// Vertices are made when a facet first uses them: a corner over the doorway has none under its head.
auto Tower_wall_v2::get_vertex(const Wall_ring_v2 ring, const std::size_t index, const std::size_t level) -> uint32_t
{
    const bool                        outside  = (ring == Wall_ring_v2::outer);
    const std::vector<Wall_point_v2>& points   = outside ? m_outer : m_inner;
    std::vector<uint32_t>&            vertices = outside ? m_outer_vertices : m_inner_vertices;
    uint32_t&                         entry    = vertices[(level * points.size()) + index];
    if (entry == wall_no_vertex_v2) {
        entry = m_builder.add_vertex(glm::vec3{points[index].position.x, m_levels[level], points[index].position.y});
    }
    return entry;
}

auto Tower_wall_v2::get_throat(const std::size_t link, const std::size_t level) -> uint32_t
{
    uint32_t& entry = m_throat_vertices[(level * m_links.size()) + link];
    if (entry == wall_no_vertex_v2) {
        entry = m_builder.add_vertex(glm::vec3{m_links[link].throat.x, m_levels[level], m_links[link].throat.y});
    }
    return entry;
}

// The ring's points from the cell's first link to its second.
void Tower_wall_v2::get_cell_run(const Wall_ring_v2 ring, const std::size_t cell, std::vector<std::size_t>& out_indices) const
{
    const bool          outside = (ring == Wall_ring_v2::outer);
    const std::size_t   count   = outside ? m_outer.size() : m_inner.size();
    const Wall_link_v2& a       = m_links[cell];
    const Wall_link_v2& b       = m_links[(cell + 1) % m_links.size()];
    const std::size_t   last    = outside ? b.outer : b.inner;
    out_indices.clear();
    for (std::size_t i = outside ? a.outer : a.inner; ; i = (i + 1) % count) {
        out_indices.push_back(i);
        if (i == last) {
            break;
        }
    }
}

void Tower_wall_v2::add_walls(const std::size_t cell, const std::size_t band)
{
    std::vector<std::size_t> run;
    get_cell_run(Wall_ring_v2::outer, cell, run);
    for (std::size_t i = 0; (i + 1) < run.size(); ++i) {
        const uint32_t a_low  = get_vertex(Wall_ring_v2::outer, run[i],     band);
        const uint32_t b_low  = get_vertex(Wall_ring_v2::outer, run[i + 1], band);
        const uint32_t b_high = get_vertex(Wall_ring_v2::outer, run[i + 1], band + 1);
        const uint32_t a_high = get_vertex(Wall_ring_v2::outer, run[i],     band + 1);
        m_builder.add_quad(a_low, b_low, b_high, a_high);
    }
    get_cell_run(Wall_ring_v2::inner, cell, run);
    for (std::size_t i = 0; (i + 1) < run.size(); ++i) {
        const uint32_t a_low  = get_vertex(Wall_ring_v2::inner, run[i],     band);
        const uint32_t b_low  = get_vertex(Wall_ring_v2::inner, run[i + 1], band);
        const uint32_t b_high = get_vertex(Wall_ring_v2::inner, run[i + 1], band + 1);
        const uint32_t a_high = get_vertex(Wall_ring_v2::inner, run[i],     band + 1);
        m_builder.add_quad(a_low, a_high, b_high, b_low);
    }
}

// The face through the wall at a link with an opening on one side: a doorway's jamb, a slit's two splays.
void Tower_wall_v2::add_jamb(const std::size_t link, const std::size_t band)
{
    const std::size_t before      = (link + m_links.size() - 1) % m_links.size();
    const bool        open_before = is_open(before, band);
    const bool        open_after  = is_open(link, band);
    if (open_before == open_after) {
        return;
    }
    const Wall_link_v2& at = m_links[link];
    std::vector<uint32_t> low {get_vertex(Wall_ring_v2::outer, at.outer, band)};
    std::vector<uint32_t> high{get_vertex(Wall_ring_v2::outer, at.outer, band + 1)};
    if ((at.kind == Wall_link_kind_v2::slit_low) || (at.kind == Wall_link_kind_v2::slit_high)) {
        low.push_back(get_throat(link, band));
        high.push_back(get_throat(link, band + 1));
    }
    low.push_back(get_vertex(Wall_ring_v2::inner, at.inner, band));
    high.push_back(get_vertex(Wall_ring_v2::inner, at.inner, band + 1));
    for (std::size_t i = 0; (i + 1) < low.size(); ++i) {
        if (open_after) {
            m_builder.add_quad(low[i], low[i + 1], high[i + 1], high[i]);
        } else {
            m_builder.add_quad(low[i], high[i], high[i + 1], low[i + 1]);
        }
    }
}

// The level face of a cell with the wall on one side of the level: the wall's base and top, a head or a sill.
void Tower_wall_v2::add_level_face(const std::size_t cell, const std::size_t level)
{
    const bool open_below = (level > 0) && is_open(cell, level - 1);
    const bool open_above = ((level + 1) < m_levels.size()) && is_open(cell, level);
    const bool below      = (level > 0) && !open_below;
    const bool above      = ((level + 1) < m_levels.size()) && !open_above;
    if (below == above) {
        return;
    }
    const std::size_t next = (cell + 1) % m_links.size();
    if ((m_cells[cell] == Wall_cell_v2::slit) && (open_below || open_above)) {
        // A slit's sill or head, by way of its throat as its jambs are.
        const std::array<uint32_t, 3> a{get_vertex(Wall_ring_v2::outer, m_links[cell].outer, level), get_throat(cell, level), get_vertex(Wall_ring_v2::inner, m_links[cell].inner, level)};
        const std::array<uint32_t, 3> b{get_vertex(Wall_ring_v2::outer, m_links[next].outer, level), get_throat(next, level), get_vertex(Wall_ring_v2::inner, m_links[next].inner, level)};
        for (std::size_t i = 0; i < 2; ++i) {
            if (below) {
                m_builder.add_quad(a[i], b[i], b[i + 1], a[i + 1]);
            } else {
                m_builder.add_quad(a[i], a[i + 1], b[i + 1], b[i]);
            }
        }
        return;
    }
    constexpr float turn = 2.0f * std::numbers::pi_v<float>;
    std::vector<std::size_t> run;
    std::array<std::vector<uint32_t>, 2> vertices;
    std::array<std::vector<float>, 2>    keys;
    for (std::size_t r = 0; r < 2; ++r) {
        const Wall_ring_v2                ring   = (r == 0) ? Wall_ring_v2::outer : Wall_ring_v2::inner;
        const std::vector<Wall_point_v2>& points = (r == 0) ? m_outer : m_inner;
        get_cell_run(ring, cell, run);
        for (const std::size_t index : run) {
            // Keys rise along the run: past the back they go on by a turn.
            vertices[r].push_back(get_vertex(ring, index, level));
            keys[r].push_back(points[index].key + ((index < run.front()) ? turn : 0.0f));
        }
    }
    if ((vertices[0].size() == 2) && (vertices[1].size() == 2)) {
        if (below) {
            m_builder.add_quad(vertices[0][0], vertices[0][1], vertices[1][1], vertices[1][0]);
        } else {
            m_builder.add_quad(vertices[0][0], vertices[1][0], vertices[1][1], vertices[0][1]);
        }
    } else if (below) {
        m_builder.add_band(vertices[0], keys[0], vertices[1], keys[1]);
    } else {
        m_builder.add_band(vertices[1], keys[1], vertices[0], keys[0]);
    }
}

void Tower_wall_v2::build()
{
    add_slits();
    add_points(Wall_ring_v2::outer);
    add_points(Wall_ring_v2::inner);
    add_links();
    m_outer_vertices .assign(m_levels.size() * m_outer.size(), wall_no_vertex_v2);
    m_inner_vertices .assign(m_levels.size() * m_inner.size(), wall_no_vertex_v2);
    m_throat_vertices.assign(m_levels.size() * m_links.size(), wall_no_vertex_v2);
    for (std::size_t band = 0; (band + 1) < m_levels.size(); ++band) {
        for (std::size_t cell = 0; cell < m_cells.size(); ++cell) {
            if (!is_open(cell, band)) {
                add_walls(cell, band);
            }
            add_jamb(cell, band);
        }
    }
    for (std::size_t level = 0; level < m_levels.size(); ++level) {
        for (std::size_t cell = 0; cell < m_cells.size(); ++cell) {
            add_level_face(cell, level);
        }
    }
}

// A ring of a solid around the axis: the plan times the scale, at the height.
class Tower_ring_v2
{
public:
    float scale{0.0f};
    float y    {0.0f};
};

// Rings bottom to top over the plan: a quad per plan side between two rings, in the band's slot, and a polygon per cap.
void add_plan_solid_v2(
    Kit_mesh_builder&                 builder,
    const std::vector<glm::vec2>&     plan,
    const std::vector<Tower_ring_v2>& rings,
    const std::vector<uint32_t>&      band_slots,
    const uint32_t                    bottom_slot,
    const uint32_t                    top_slot
)
{
    const std::size_t count = plan.size();
    std::vector<uint32_t> previous;
    std::vector<uint32_t> current;
    for (std::size_t r = 0; r < rings.size(); ++r) {
        current.clear();
        for (const glm::vec2& point : plan) {
            current.push_back(builder.add_vertex(glm::vec3{rings[r].scale * point.x, rings[r].y, rings[r].scale * point.y}));
        }
        if (r == 0) {
            const std::vector<uint32_t> bottom_cap(current.rbegin(), current.rend());
            builder.set_slot(bottom_slot);
            builder.add_polygon(bottom_cap);
        } else {
            builder.set_slot(band_slots[r - 1]);
            for (std::size_t i = 0; i < count; ++i) {
                const std::size_t next = (i + 1) % count;
                builder.add_quad(previous[i], previous[next], current[next], current[i]);
            }
        }
        previous = current;
    }
    builder.set_slot(top_slot);
    builder.add_polygon(previous);
}

// The station of a profile of (scale, y) points at a plan point.
[[nodiscard]] auto make_station_v2(const glm::vec2& plan_point, const std::vector<glm::vec2>& profile) -> std::vector<glm::vec3>
{
    std::vector<glm::vec3> station;
    for (const glm::vec2& point : profile) {
        station.emplace_back(point.x * plan_point.x, point.y, point.x * plan_point.y);
    }
    return station;
}

// The top floor behind the corbelled parapet: a cup whose bottom is the ceiling inside and the parapet's underside.
void add_tower_head_v2(Kit_mesh_builder& builder, const Tower_form_v2& form)
{
    const float outer = form.radius + tower_overhang_v2;
    const float walk  = outer - tower_parapet_v2;
    const float sill  = form.height + tower_sill_v2;
    add_plan_solid_v2(
        builder, form.unit,
        {{outer, form.soffit}, {outer, form.height}, {outer, sill}, {walk, sill}, {walk, form.height}},
        {medieval_trim_slot_v2, medieval_wall_slot_v2, medieval_trim_slot_v2, medieval_wall_slot_v2},
        medieval_wall_slot_v2, medieval_floor_slot_v2
    );

    // Merlons as runs of plan points along the parapet: around a round tower's corners, along and around a square one's.
    std::vector<std::vector<glm::vec2>> merlons;
    const std::size_t corners = form.unit.size();
    if (form.round) {
        for (std::size_t first = 0; first < corners; first += 3) {
            merlons.push_back({form.unit[(form.front + first) % corners], form.unit[(form.front + first + 1) % corners], form.unit[(form.front + first + 2) % corners]});
        }
    } else {
        // Per side: corner merlons and crenels with merlons between them; merlons are 1.6 times as wide as crenels.
        const int   crenels    = std::clamp(static_cast<int>(std::lround((2.0f * form.radius) / 2.6f)), 1, 6);
        const float crenel_run = 2.0f / ((1.6f * static_cast<float>(crenels + 1)) + static_cast<float>(crenels));
        const float merlon_run = 1.6f * crenel_run;
        for (std::size_t c = 0; c < corners; ++c) {
            const glm::vec2 corner = form.unit[c];
            const glm::vec2 along  = 0.5f * (form.unit[(c + 1) % corners] - corner);
            const glm::vec2 before = 0.5f * (corner - form.unit[(c + corners - 1) % corners]);
            merlons.push_back({corner - (merlon_run * before), corner, corner + (merlon_run * along)});
            for (int k = 1; k < crenels; ++k) {
                const float start = static_cast<float>(k) * (crenel_run + merlon_run);
                merlons.push_back({corner + (start * along), corner + ((start + merlon_run) * along)});
            }
        }
    }
    // A merlon stands a little inside the breastwork's faces; its coping is a saddleback that overhangs them.
    constexpr float inset    = 0.02f;
    constexpr float overhang = 0.04f;
    const float body_top = sill + tower_merlon_v2 - tower_coping_v2;
    const float eaves    = body_top + (0.4f * tower_coping_v2);
    const std::vector<glm::vec2> body{{walk + inset, sill}, {outer - inset, sill}, {outer - inset, body_top}, {walk + inset, body_top}};
    const std::vector<glm::vec2> coping{
        {walk - overhang, body_top}, {outer + overhang, body_top}, {outer + overhang, eaves},
        {0.5f * (walk + outer), sill + tower_merlon_v2}, {walk - overhang, eaves}
    };
    for (const std::vector<glm::vec2>& merlon : merlons) {
        std::vector<std::vector<glm::vec3>> body_stations;
        std::vector<std::vector<glm::vec3>> coping_stations;
        for (const glm::vec2& point : merlon) {
            body_stations.push_back(make_station_v2(point, body));
            coping_stations.push_back(make_station_v2(point, coping));
        }
        builder.set_slot(medieval_wall_slot_v2);
        add_prism_v2(builder, body_stations);
        builder.set_slot(medieval_trim_slot_v2);
        add_prism_v2(builder, coping_stations);
    }
}

// One corbel under the parapet: a bracket out of the wall at the anchor, its nose sloped back to the wall.
void add_corbel_v2(
    Kit_mesh_builder&    builder,
    const Tower_form_v2& form,
    const glm::vec2&     anchor,
    const glm::vec2&     outward,
    const float          half_width,
    const float          back,
    const float          reach
)
{
    const glm::vec2 tangent{outward.y, -outward.x};
    const float     top    = form.soffit + 0.03f;
    const float     bottom = form.soffit - form.corbel_height;
    const float     nose   = form.soffit - (0.3f * form.corbel_height);
    std::vector<std::vector<glm::vec3>> stations;
    for (const float side : {-half_width, half_width}) {
        const glm::vec2 inside  = anchor + (side * tangent) - (back * outward);
        const glm::vec2 outside = anchor + (side * tangent) + (reach * outward);
        stations.push_back({{inside.x, top, inside.y}, {inside.x, bottom, inside.y}, {outside.x, nose, outside.y}, {outside.x, top, outside.y}});
    }
    add_prism_v2(builder, stations);
}

// The corbels that carry the parapet: one at each corner, and along a square tower's sides.
void add_tower_corbels_v2(Kit_mesh_builder& builder, const Tower_form_v2& form)
{
    constexpr float   pi      = std::numbers::pi_v<float>;
    const std::size_t corners = form.unit.size();
    builder.set_slot(medieval_trim_slot_v2);
    if (form.round) {
        // A corbel's sides stay inside the wall at their back and under the parapet at their nose.
        const float bend  = std::tan(pi / static_cast<float>(corners));
        const float width = std::clamp(0.4f * form.radius * std::sin(pi / static_cast<float>(corners)), 0.08f, 0.3f);
        for (const glm::vec2& corner : form.unit) {
            add_corbel_v2(builder, form, form.radius * corner, corner, width, (width * bend) + 0.06f, tower_overhang_v2 - (width * bend) - 0.05f);
        }
        return;
    }
    constexpr float half_width = 0.09f;
    const float     diagonal   = std::sqrt(0.5f);
    const int       between    = std::clamp(static_cast<int>(std::lround((2.0f * form.radius) / 0.75f)) - 1, 1, 7);
    for (std::size_t c = 0; c < corners; ++c) {
        const glm::vec2 corner = form.unit[c];
        const glm::vec2 along  = 0.5f * (form.unit[(c + 1) % corners] - corner);
        add_corbel_v2(builder, form, form.radius * corner, diagonal * corner, half_width, half_width + 0.06f, (tower_overhang_v2 / diagonal) - half_width - 0.05f);
        for (int k = 1; k <= between; ++k) {
            const glm::vec2 point = corner + ((2.0f * static_cast<float>(k) / static_cast<float>(between + 1)) * along);
            add_corbel_v2(builder, form, form.radius * point, glm::vec2{-along.y, along.x}, half_width, 0.06f, tower_overhang_v2 - 0.05f);
        }
    }
}

// The battered base: a plinth course and the batter above it, around the tower from one side of the doorway's frame to
// the other; its ends lie inside the jamb stones.
void add_tower_base_v2(Kit_mesh_builder& builder, const Tower_form_v2& form)
{
    constexpr float sink     = 0.025f; // the base's back lies this far inside the wall's face at most
    const float     slope_y0 = tower_plinth_high_v2 + tower_plinth_v2;
    // Five points at every batter: the batter's face from its foot into the wall, at most 0.15 over the cut's head.
    // The face stays at least 5 mm into the wall at its top, so a batter of 0 leaves no edge on the wall's face.
    const float run  = form.cut_height - slope_y0;
    const float back = std::max(std::min(sink, 0.15f * form.batter / run), 0.005f);
    const float rise = std::min(0.15f, back * run / std::max(form.batter, 1.0e-6f));
    std::vector<glm::vec2> profile{{form.radius - back, 0.0f}, {form.foot, 0.0f}, {form.foot, tower_plinth_high_v2}};
    profile.emplace_back(form.radius + form.batter, slope_y0);
    profile.emplace_back(form.radius - back, form.cut_height + rise);

    // The plan's corners clear of the ends at every scale of the profile.
    const float     end_x   = form.door_half + (0.5f * tower_frame_v2);
    const glm::vec2 toward{0.0f, 1.0f};
    const float     end_key = plan_key_v2(form.plan_point(toward, end_x, form.radius - back)) + (tower_point_margin_v2 / form.radius);
    const auto make_end = [&form, &profile, &toward](const float x) -> std::vector<glm::vec3> {
        std::vector<glm::vec3> station;
        for (const glm::vec2& point : profile) {
            const glm::vec2 at = form.plan_point(toward, x, point.x);
            station.emplace_back(at.x, point.y, at.y);
        }
        return station;
    };
    std::vector<std::vector<glm::vec3>> stations{make_end(end_x)};
    for (std::size_t k = 0; k < form.unit.size(); ++k) {
        if (form.keys[k] > end_key) {
            stations.push_back(make_station_v2(form.unit[k], profile));
        }
    }
    for (std::size_t k = 0; k < form.unit.size(); ++k) {
        if (form.keys[k] < -end_key) {
            stations.push_back(make_station_v2(form.unit[k], profile));
        }
    }
    stations.push_back(make_end(-end_x));
    builder.set_slot(medieval_wall_slot_v2);
    add_sweep_v2(builder, stations, Sweep_path_v2::open, Sweep_ends_v2::triangles, 3);
}

// The string course over the battered base, around the whole tower.
void add_tower_string_course_v2(Kit_mesh_builder& builder, const Tower_form_v2& form)
{
    const float y = form.cut_height - 0.02f;
    const std::vector<glm::vec2> profile{
        {form.radius - 0.03f, y}, {form.radius + 0.06f, y + 0.04f}, {form.radius + 0.06f, y + 0.11f}, {form.radius - 0.03f, y + 0.17f}
    };
    std::vector<std::vector<glm::vec3>> stations;
    for (const glm::vec2& corner : form.unit) {
        stations.push_back(make_station_v2(corner, profile));
    }
    builder.set_slot(medieval_trim_slot_v2);
    add_sweep_v2(builder, stations, Sweep_path_v2::closed, Sweep_ends_v2::quads, 0);
}

// The doorway's surround: two jamb stones through the batter and a gabled lintel on them; the clear opening is between
// the jambs and under the lintel.
void add_tower_doorway_v2(Kit_mesh_builder& builder, const Tower_form_v2& form)
{
    const glm::vec2 toward{0.0f, 1.0f};
    const float jamb_outer = form.door_half + tower_frame_v2;
    const float lintel_end = jamb_outer + tower_ear_v2;
    // The stones' backs lie inside the wall's face at their outer edges.
    const float jamb_back   = form.plan_point(toward, jamb_outer, form.radius).y - 0.05f;
    const float lintel_back = form.plan_point(toward, lintel_end, form.radius).y - 0.05f;
    const float jamb_top    = form.door_height + (0.5f * form.lintel);
    builder.set_slot(medieval_trim_slot_v2);
    builder.add_box(glm::vec3{-jamb_outer, 0.0f, jamb_back}, glm::vec3{-form.door_half, jamb_top, form.frame_front});
    builder.add_box(glm::vec3{form.door_half, 0.0f, jamb_back}, glm::vec3{jamb_outer, jamb_top, form.frame_front});
    const float y0 = form.door_height;
    const float y1 = form.door_height + form.lintel;
    const float y2 = y1 + (0.5f * form.lintel);
    std::vector<std::vector<glm::vec3>> stations;
    for (const float z : {form.frame_front + 0.04f, lintel_back}) {
        stations.push_back({{-lintel_end, y0, z}, {lintel_end, y0, z}, {lintel_end, y1, z}, {0.0f, y2, z}, {-lintel_end, y1, z}});
    }
    add_prism_v2(builder, stations);
}

void generate_tower_v2(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Tower_form_v2 form{values};
    Kit_mesh_builder builder;
    builder.set_slot(medieval_wall_slot_v2);
    Tower_wall_v2 wall{builder, form};
    wall.build();
    add_tower_base_v2(builder, form);
    add_tower_string_course_v2(builder, form);
    add_tower_doorway_v2(builder, form);
    if (form.battlement) {
        add_tower_head_v2(builder, form);
        add_tower_corbels_v2(builder, form);
    } else {
        // A flat top: the floor as a course a little proud of the wall, which a further segment stands on.
        const float scale = form.radius + 0.04f;
        add_plan_solid_v2(
            builder, form.unit, {{scale, form.soffit}, {scale, form.height}}, {medieval_trim_slot_v2}, medieval_wall_slot_v2, medieval_floor_slot_v2
        );
    }
    builder.write(mesh);
}

} // anonymous namespace

void add_medieval_shapes_v2(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape = make_recreated_shape(shapes, "architecture.battlement", generate_battlement_v2, 120);
        shape.description    =
            "Medieval parapet run along X on a wall top: a breastwork under a sill band and merlons with copings (square, "
            "swallowtail or pointed), each with an arrow loop; a merlon at each bay's center and half crenels at the ends, "
            "so runs join into whole crenels.";
        shape.material_slots = {"wall", "trim"}; // the masonry; the sill band and the copings
        // The band and the copings overhang the faces, so no side fills its rectangle.
        shape.get_seam_sides = [](const Kit_parameter_values&) { return std::vector<Kit_side>{}; };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape = make_recreated_shape(shapes, "architecture.tower", generate_tower_v2, 1540);
        shape.description    =
            "Medieval tower segment, round or square, standing at its origin: a plinth and a battered base under a string "
            "course, a doorway in a stone surround at the front (+Z) into the hollow inside, rows of splayed arrow slits away "
            "from the side connectors, and a walkable top floor at the height behind a parapet on corbels with coped merlons, "
            "or a flat top a further segment stands on.";
        shape.material_slots = {"wall", "trim", "floor"}; // the masonry; the dressed stone; the walkable top
        shape.check_combination = [](const Kit_parameter_values& values) -> std::string {
            const float inside = values.get_float("width") - (2.0f * values.get_float("wall"));
            const float door_w = values.get_float("opening_width");
            const float door_h = values.get_float("opening_height");
            if (inside < (door_w + 0.4f)) {
                return fmt::format("{}: the inside, {} m wide, is narrower than the doorway's {} m and 0.4 m", values.get_shape()->id, inside, door_w);
            }
            if (values.get_float("height") < (door_h + tower_head_above_v2 + 0.8f)) {
                return fmt::format("{}: height {} m leaves no top floor above the doorway's {} m and 0.9 m", values.get_shape()->id, values.get_float("height"), door_h);
            }
            return {};
        };
        shape.check_walkability = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            const Tower_form_v2 form{values};
            const float         need    = (2.0f * agent.radius) + kit_opening_margin;
            const std::string   opening = check_kit_opening(*values.get_shape(), 2.0f * form.door_half, form.door_height, agent);
            if (!opening.empty()) {
                return opening;
            }
            if ((2.0f * form.inner * form.inscribe) < need) {
                return fmt::format("{}: the inside is narrower than the agent's {} m", values.get_shape()->id, need);
            }
            if (form.battlement && ((2.0f * (form.radius + tower_overhang_v2 - tower_parapet_v2) * form.inscribe) < need)) {
                return fmt::format("{}: the walkway is narrower than the agent's {} m", values.get_shape()->id, need);
            }
            return {};
        };
        shape.get_connectors = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const Tower_form_v2 form{values};
            const float         middle = form.radius - (0.5f * form.wall); // walls joined to the sides end inside the tower's wall
            add_base_and_top_connectors(out_connectors, form.height);
            add_side_connector(out_connectors, "opening_front", glm::vec3{0.0f, 0.0f, form.frame_front}, glm::vec3{0.0f, 0.0f, 1.0f});
            add_side_connector(out_connectors, "left",  glm::vec3{-middle, 0.0f, 0.0f}, glm::vec3{-1.0f, 0.0f,  0.0f});
            add_side_connector(out_connectors, "right", glm::vec3{ middle, 0.0f, 0.0f}, glm::vec3{ 1.0f, 0.0f,  0.0f});
            add_side_connector(out_connectors, "back",  glm::vec3{0.0f, 0.0f, -middle}, glm::vec3{ 0.0f, 0.0f, -1.0f});
        };
        shapes.push_back(std::move(shape));
    }
}

} // namespace editor
