#include "level/kit_mesh_builder.hpp"
#include "level/kit_shapes.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <string_view>
#include <vector>

namespace editor {

namespace {

constexpr int battlement_max_bays = 32; // keeps a run within the kit's ceiling at every length

// A vertex of the battlement's front elevation, at both faces.
class Elevation_point
{
public:
    float    x;
    float    y;
    uint32_t front;
    uint32_t back;
};

// The front elevation's vertices, shared by the faces and the outline.
class Elevation
{
public:
    Elevation(Kit_mesh_builder& builder, const float half_thickness)
        : m_builder{builder}
        , m_half_thickness{half_thickness}
    {
    }

    auto point(const float x, const float y) -> Elevation_point
    {
        for (const Elevation_point& p : m_points) {
            if ((p.x == x) && (p.y == y)) {
                return p;
            }
        }
        m_points.push_back(Elevation_point{
            .x     = x,
            .y     = y,
            .front = m_builder.add_vertex(glm::vec3{x, y,  m_half_thickness}),
            .back  = m_builder.add_vertex(glm::vec3{x, y, -m_half_thickness})
        });
        return m_points.back();
    }

    // A front facet of the corners (counter-clockwise seen from +Z) and its mirror on the back.
    void add_face(const std::vector<Elevation_point>& corners)
    {
        std::vector<uint32_t> front;
        std::vector<uint32_t> back;
        for (const Elevation_point& p : corners) {
            front.push_back(p.front);
        }
        for (std::vector<Elevation_point>::const_reverse_iterator it = corners.rbegin(); it != corners.rend(); ++it) {
            back.push_back(it->back);
        }
        m_builder.add_polygon(front);
        m_builder.add_polygon(back);
    }

private:
    Kit_mesh_builder&            m_builder;
    float                        m_half_thickness;
    std::vector<Elevation_point> m_points;
};

// One merlon of a battlement run: its span and its top's corners from x0 to x1.
class Merlon
{
public:
    float                  x0;
    float                  x1;
    std::vector<glm::vec2> top; // (x, y), left to right
};

void generate_battlement(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float            half_length = 0.5f * values.get_float("length");
    const float            sill        = values.get_float("height");
    const float            top         = sill + values.get_float("merlon_height");
    const float            merlon_w    = values.get_float("merlon_width");
    const float            crenel_w    = values.get_float("crenel_width");
    const std::string_view style       = values.get_enum("merlon");
    const int   bays   = std::clamp(static_cast<int>(std::lround((2.0f * half_length) / (merlon_w + crenel_w))), 1, battlement_max_bays);
    const float pitch  = (2.0f * half_length) / static_cast<float>(bays);
    const float crenel = pitch * crenel_w / (merlon_w + crenel_w);
    // A swallowtail's notch and a pointed merlon's shoulders lie this far below its top.
    const float drop   = 0.3f * (top - sill);

    // Bays have a crenel at their center and half merlons at their ends, so runs join into whole merlons.
    std::vector<Merlon> merlons;
    for (int i = 0; i <= bays; ++i) {
        const float x0 = (i == 0)    ? -half_length : (((static_cast<float>(i) - 0.5f - (0.5f * static_cast<float>(bays))) * pitch) + (0.5f * crenel));
        const float x1 = (i == bays) ?  half_length : (((static_cast<float>(i) + 0.5f - (0.5f * static_cast<float>(bays))) * pitch) - (0.5f * crenel));
        const float xc = (i == 0) ? -half_length : ((i == bays) ? half_length : ((static_cast<float>(i) - (0.5f * static_cast<float>(bays))) * pitch));
        Merlon merlon{.x0 = x0, .x1 = x1, .top = {}};
        if (style == "square") {
            merlon.top = {{x0, top}, {x1, top}};
        } else {
            const float edge   = (style == "swallowtail") ? top : (top - drop);
            const float center = (style == "swallowtail") ? (top - drop) : top;
            if (i > 0) {
                merlon.top.emplace_back(x0, edge);
            }
            merlon.top.emplace_back(xc, center);
            if (i < bays) {
                merlon.top.emplace_back(x1, edge);
            }
        }
        merlons.push_back(std::move(merlon));
    }

    Kit_mesh_builder builder;
    Elevation        elevation{builder, 0.5f * values.get_float("thickness")};

    // The face in columns at every corner's x, so it is its own mirror image: the breastwork under every column and,
    // under each merlon's top, the merlon's face split at the top's inner corners.
    std::vector<float> columns;
    for (const Merlon& merlon : merlons) {
        for (std::size_t k = 0; k < merlon.top.size(); ++k) {
            columns.push_back(merlon.top[k].x);
            if ((k + 1) < merlon.top.size()) {
                const glm::vec2 a = merlon.top[k];
                const glm::vec2 b = merlon.top[k + 1];
                elevation.add_face({elevation.point(a.x, sill), elevation.point(b.x, sill), elevation.point(b.x, b.y), elevation.point(a.x, a.y)});
            }
        }
    }
    for (std::size_t i = 0; (i + 1) < columns.size(); ++i) {
        const float a = columns[i];
        const float b = columns[i + 1];
        elevation.add_face({elevation.point(a, 0.0f), elevation.point(b, 0.0f), elevation.point(b, sill), elevation.point(a, sill)});
    }

    // The outline counter-clockwise seen from +Z: the base left to right, the right end, merlon tops and crenels right to
    // left, the left end.
    std::vector<glm::vec2> outline;
    for (const float x : columns) {
        outline.emplace_back(x, 0.0f);
    }
    outline.emplace_back(half_length, sill);
    for (std::size_t m = merlons.size(); m-- > 0;) {
        const Merlon& merlon = merlons[m];
        if ((m + 1) < merlons.size()) {
            outline.emplace_back(merlon.x1, sill);
        }
        for (std::vector<glm::vec2>::const_reverse_iterator it = merlon.top.rbegin(); it != merlon.top.rend(); ++it) {
            outline.push_back(*it);
        }
        outline.emplace_back(merlon.x0, sill);
    }
    for (std::size_t i = 0; i < outline.size(); ++i) {
        const Elevation_point p = elevation.point(outline[i].x, outline[i].y);
        const Elevation_point q = elevation.point(outline[(i + 1) % outline.size()].x, outline[(i + 1) % outline.size()].y);
        builder.add_quad(p.front, p.back, q.back, q.front);
    }
    builder.write(mesh);
}

// One point of a tower ring in plan: x, z and its key, the angle from the front (+Z) going toward +X and the back.
class Plan_point
{
public:
    float x;
    float z;
    float key;
};

[[nodiscard]] auto plan_key(const float x, const float z) -> float
{
    float key = std::atan2(-z, x) + (0.5f * std::numbers::pi_v<float>);
    if (key < 0.0f) {
        key += 2.0f * std::numbers::pi_v<float>;
    }
    return key;
}

constexpr float tower_sill        = 1.0f;  // the parapet's breastwork above the walkway
constexpr float tower_merlon      = 0.9f;  // merlons above the breastwork
constexpr float tower_floor       = 0.3f;  // thickness of the top floor
constexpr float tower_corbel      = 0.35f; // the parapet stands out on a sloped corbel course this high
constexpr float tower_overhang    = 0.15f; // and this far beyond the walls
constexpr float tower_jamb_margin = 0.04f; // ring corners nearer an opening's jambs than this are left out
constexpr float tower_head_above  = 0.1f;  // the doorway's cut rises this far above its clear height, behind the lintel
constexpr float tower_lintel      = 0.35f; // the lintel's height over the doorway
constexpr float tower_slit_half   = 0.06f;
constexpr float tower_slit_height = 1.0f;
constexpr float tower_slit_pitch  = 1.6f;  // from one row of slits to the next
constexpr float tower_slit_gap    = 0.5f;  // the lowest row above the doorway's cut
constexpr float tower_slit_clear  = 0.2f;  // a row's top stays this far under the ceiling and the corbel course
constexpr int   tower_max_merlons = 10;    // keeps a wide round tower within the kit's ceiling

// A cut through the wall in plan: jambs on the planes dot(p, lateral) = offset +- half_width, toward direction.
class Tower_cut
{
public:
    glm::vec2 direction;
    glm::vec2 lateral;
    float     offset;
    float     half_width;
};

// An opening: a cut from y0 to y1; the rows of slits share their cuts.
class Tower_opening
{
public:
    std::size_t cut;
    float       y0;
    float       y1;
};

// An entry of the body's ring: a plan corner at half size 1, or one jamb of a cut.
class Ring_entry
{
public:
    glm::vec2                  unit{0.0f};
    std::optional<std::size_t> cut;
    float                      side{0.0f}; // -1 or +1 along the cut's lateral axis
};

// The tower's plan (doc/editor/level_kit_shapes.md "Tower"): the body's ring with its openings, and the parapet's pattern.
class Tower_plan
{
public:
    explicit Tower_plan(const Kit_parameter_values& values)
        : round       {values.get_enum("profile") == "round"}
        , radius      {0.5f * values.get_float("width")}
        , wall        {values.get_float("wall")}
        , parapet     {std::min(values.get_float("wall"), 0.45f)}
        , door_half   {0.5f * values.get_float("opening_width")}
        , door_height {values.get_float("opening_height")}
        , cut_height  {values.get_float("opening_height") + tower_head_above}
        , height      {values.get_float("height")}
        , foot        {radius + (cut_height * std::tan(glm::radians(values.get_float("batter"))))}
        , battlement  {values.get_enum("top") == "battlement"}
        , floor_y     {height - tower_floor}
        , top_y       {battlement ? (height - tower_corbel) : height}
    {
        const float inner = radius - wall;
        std::vector<glm::vec2> corners;
        if (round) {
            const int merlons = std::clamp(static_cast<int>(std::lround(std::numbers::pi_v<float> * 2.0f * radius / 1.7f)), 4, tower_max_merlons);
            sides = 3 * merlons;
            const float step = 2.0f * std::numbers::pi_v<float> / static_cast<float>(sides);
            for (int i = 0; i < sides; ++i) {
                const float a = (-0.5f * std::numbers::pi_v<float>) + ((static_cast<float>(i) + 0.5f) * step);
                corners.emplace_back(std::cos(a), -std::sin(a));
                parapet_ring.push_back(Plan_point{.x = std::cos(a), .z = -std::sin(a), .key = (static_cast<float>(i) + 0.5f) * step});
                crenel.push_back((i % 3) == 2);
            }
        } else {
            sides = 4;
            // Per side: corner merlons, crenels with merlons between them; merlons are 1.6 times as wide as crenels.
            const int   per_side   = std::clamp(static_cast<int>(std::lround((2.0f * radius) / 2.6f)), 1, 6);
            const float crenel_len = 2.0f / ((1.6f * static_cast<float>(per_side + 1)) + static_cast<float>(per_side));
            const float merlon_len = 1.6f * crenel_len;
            corners = {glm::vec2{1.0f, 1.0f}, glm::vec2{1.0f, -1.0f}, glm::vec2{-1.0f, -1.0f}, glm::vec2{-1.0f, 1.0f}};
            for (std::size_t c = 0; c < 4; ++c) {
                const glm::vec2 from = corners[c];
                const glm::vec2 dir  = 0.5f * (corners[(c + 1) % 4] - from);
                parapet_ring.push_back(Plan_point{.x = from.x, .z = from.y, .key = plan_key(from.x, from.y)});
                crenel.push_back(false);
                float along = merlon_len;
                for (int k = 0; k < per_side; ++k) {
                    for (const float offset : {along, along + crenel_len}) {
                        const glm::vec2 p = from + (offset * dir);
                        parapet_ring.push_back(Plan_point{.x = p.x, .z = p.y, .key = plan_key(p.x, p.y)});
                        crenel.push_back(offset == along);
                    }
                    along += crenel_len + merlon_len;
                }
            }
            // The ring starts at its smallest key, after the front.
            const std::vector<Plan_point>::iterator first = std::min_element(
                parapet_ring.begin(), parapet_ring.end(), [](const Plan_point& a, const Plan_point& b) { return a.key < b.key; }
            );
            const std::ptrdiff_t shift = first - parapet_ring.begin();
            std::rotate(parapet_ring.begin(), first, parapet_ring.end());
            std::rotate(crenel.begin(), crenel.begin() + shift, crenel.end());
        }

        // The doorway at the front, then rows of arrow slits between the side connectors, as many rows as fit.
        cuts.push_back(Tower_cut{.direction = {0.0f, 1.0f}, .lateral = {1.0f, 0.0f}, .offset = 0.0f, .half_width = door_half});
        openings.push_back(Tower_opening{.cut = 0, .y0 = 0.0f, .y1 = cut_height});
        if (round) {
            for (int k = 0; k < 4; ++k) {
                const float     angle = (0.25f + (0.5f * static_cast<float>(k))) * std::numbers::pi_v<float>;
                const glm::vec2 direction{std::sin(angle), std::cos(angle)};
                cuts.push_back(Tower_cut{.direction = direction, .lateral = {direction.y, -direction.x}, .offset = 0.0f, .half_width = tower_slit_half});
            }
        } else {
            for (const glm::vec2 direction : {glm::vec2{-1.0f, 0.0f}, glm::vec2{1.0f, 0.0f}, glm::vec2{0.0f, -1.0f}}) {
                for (const float offset : {-0.5f * inner, 0.5f * inner}) {
                    cuts.push_back(Tower_cut{.direction = direction, .lateral = {direction.y, -direction.x}, .offset = offset, .half_width = tower_slit_half});
                }
            }
        }
        const int rows = values.get_int("slit_rows");
        int       made = 0;
        for (int row = 0; row < rows; ++row) {
            const float y0 = cut_height + tower_slit_gap + (static_cast<float>(row) * tower_slit_pitch);
            const float y1 = y0 + tower_slit_height;
            if (y1 > (std::min(floor_y, top_y) - tower_slit_clear)) {
                break;
            }
            for (std::size_t c = 1; c < cuts.size(); ++c) {
                openings.push_back(Tower_opening{.cut = c, .y0 = y0, .y1 = y1});
            }
            ++made;
        }
        if (made == 0) {
            cuts.resize(1); // no row fits: the slits' cuts make no jambs
        }

        // A corner is kept when it lies on the same side of every jamb plane at every radius, away from the plane.
        for (const glm::vec2& unit : corners) {
            bool keep = true;
            for (const Tower_cut& opening : cuts) {
                if (glm::dot(unit, opening.direction) <= 0.0f) {
                    continue;
                }
                int inside = 0;
                for (const float r : {inner, radius, foot}) {
                    const float l = std::abs(glm::dot(unit * r, opening.lateral) - opening.offset);
                    if (std::abs(l - opening.half_width) < tower_jamb_margin) {
                        keep = false;
                    }
                    inside += (l < opening.half_width) ? 1 : 0;
                }
                if ((inside != 0) && (inside != 3)) {
                    keep = false;
                }
            }
            if (keep) {
                ring.push_back(Ring_entry{.unit = unit, .cut = std::nullopt, .side = 0.0f});
            }
        }
        for (std::size_t j = 0; j < cuts.size(); ++j) {
            for (const float side : {-1.0f, 1.0f}) {
                ring.push_back(Ring_entry{.unit = glm::vec2{0.0f}, .cut = j, .side = side});
            }
        }
        std::sort(ring.begin(), ring.end(), [this](const Ring_entry& a, const Ring_entry& b) {
            const glm::vec2 pa = position(a, radius);
            const glm::vec2 pb = position(b, radius);
            return plan_key(pa.x, pa.y) < plan_key(pb.x, pb.y);
        });
        // The cut each segment (entry k to k + 1) crosses, if any.
        for (std::size_t k = 0; k < ring.size(); ++k) {
            const glm::vec2 middle = 0.5f * (position(ring[k], radius) + position(ring[(k + 1) % ring.size()], radius));
            std::optional<std::size_t> crossed;
            for (std::size_t j = 0; j < cuts.size(); ++j) {
                const Tower_cut& opening = cuts[j];
                if ((glm::dot(middle, opening.direction) > 0.0f) && (std::abs(glm::dot(middle, opening.lateral) - opening.offset) < opening.half_width)) {
                    crossed = j;
                }
            }
            segment_cut.push_back(crossed);
        }
    }

    // An entry's plan position at a half size: a corner scaled, or a jamb on its plane at the ring's distance.
    [[nodiscard]] auto position(const Ring_entry& entry, const float half_size) const -> glm::vec2
    {
        if (!entry.cut.has_value()) {
            return half_size * entry.unit;
        }
        const Tower_cut& opening = cuts[entry.cut.value()];
        const float l     = opening.offset + (entry.side * opening.half_width);
        const float depth = round ? std::sqrt((half_size * half_size) - (l * l)) : half_size;
        return (l * opening.lateral) + (depth * opening.direction);
    }

    bool                                    round;
    float                                   radius;
    float                                   wall;
    float                                   parapet;
    float                                   door_half;
    float                                   door_height; // clear, under the lintel
    float                                   cut_height;
    float                                   height;
    float                                   foot;        // outer half size at the base, battered out from the cut's head down
    bool                                    battlement;
    float                                   floor_y;     // the ceiling of the hollow
    float                                   top_y;       // the outer wall's top: the corbel course's foot or the flat top
    int                                     sides{0};
    std::vector<Tower_cut>                  cuts;
    std::vector<Tower_opening>              openings;
    std::vector<Ring_entry>                 ring;
    std::vector<std::optional<std::size_t>> segment_cut;
    std::vector<Plan_point>                 parapet_ring; // the parapet's corners at half size 1, by key
    std::vector<bool>                       crenel;       // per parapet segment (corner i to i + 1): a crenel, else a merlon
};

constexpr uint32_t tower_wall_slot  = 0;
constexpr uint32_t tower_floor_slot = 1;

// The vertices of plan points at a height, and their keys; a closed ring repeats its first vertex a turn later.
class Tower_level
{
public:
    std::vector<uint32_t> vertices;
    std::vector<float>    keys;
};

enum class Ring_closure : unsigned int { open = 0, closed };

void add_level(Kit_mesh_builder& builder, const std::vector<Plan_point>& points, const float y, const Ring_closure closure, Tower_level& out_level)
{
    out_level.vertices.clear();
    out_level.keys.clear();
    for (const Plan_point& p : points) {
        out_level.vertices.push_back(builder.add_vertex(glm::vec3{p.x, y, p.z}));
        out_level.keys.push_back(p.key);
    }
    if (closure == Ring_closure::closed) {
        out_level.vertices.push_back(out_level.vertices.front());
        out_level.keys.push_back(out_level.keys.front() + (2.0f * std::numbers::pi_v<float>));
    }
}

// The body's ring entries at a half size and height. At the base an entry between the doorway's jambs gets no vertex
// (its index is never used): nothing meets it there.
void add_ring(Kit_mesh_builder& builder, const Tower_plan& plan, const float half_size, const float y, std::vector<uint32_t>& out_vertices)
{
    out_vertices.clear();
    const std::size_t n = plan.ring.size();
    for (std::size_t k = 0; k < n; ++k) {
        const bool in_doorway = (y == 0.0f) && (plan.segment_cut[k] == 0u) && (plan.segment_cut[(k + n - 1) % n] == 0u);
        if (in_doorway) {
            out_vertices.push_back(0);
            continue;
        }
        const glm::vec2 p = plan.position(plan.ring[k], half_size);
        out_vertices.push_back(builder.add_vertex(glm::vec3{p.x, y, p.y}));
    }
}

void generate_tower(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Tower_plan  plan{values};
    const float       inner = plan.radius - plan.wall;
    const std::size_t n     = plan.ring.size();
    Kit_mesh_builder  builder;
    builder.set_slot(tower_wall_slot);

    // The levels: the base, the doorway's head, each slit row's sill and head, and the tops of the outer and inner walls.
    std::vector<float> levels{0.0f, plan.cut_height};
    for (std::size_t j = 1; j < plan.openings.size(); ++j) {
        for (const float y : {plan.openings[j].y0, plan.openings[j].y1}) {
            if (y > levels.back()) {
                levels.push_back(y);
            }
        }
    }
    const std::size_t body_levels = levels.size();
    std::vector<std::vector<uint32_t>> outer(body_levels + 1);
    std::vector<std::vector<uint32_t>> inside(body_levels + 1);
    for (std::size_t l = 0; l < body_levels; ++l) {
        add_ring(builder, plan, (l == 0) ? plan.foot : plan.radius, levels[l], outer[l]);
        add_ring(builder, plan, inner, levels[l], inside[l]);
    }
    add_ring(builder, plan, plan.radius, plan.top_y, outer[body_levels]);
    add_ring(builder, plan, inner, plan.floor_y, inside[body_levels]);

    // The opening a segment crosses in the band from level l up, if the band is open there.
    const auto opening_in_band = [&plan, &levels](const std::size_t k, const std::size_t l) -> std::optional<std::size_t> {
        const std::optional<std::size_t> cut = plan.segment_cut[k];
        if (!cut.has_value() || ((l + 1) >= levels.size())) {
            return std::nullopt;
        }
        for (std::size_t j = 0; j < plan.openings.size(); ++j) {
            const Tower_opening& opening = plan.openings[j];
            if ((opening.cut == cut.value()) && (opening.y0 <= levels[l]) && (opening.y1 >= levels[l + 1])) {
                return j;
            }
        }
        return std::nullopt;
    };
    const std::vector<glm::vec3>& positions = builder.get_positions();
    // A horizontal quad between an inner and an outer segment, split at its reflex corner where the corners turn.
    const auto add_annulus_quad = [&builder, &positions](const uint32_t a, const uint32_t b, const uint32_t c, const uint32_t d) {
        const glm::vec3 n0 = glm::cross(positions[b] - positions[a], positions[c] - positions[b]);
        const glm::vec3 n1 = glm::cross(positions[d] - positions[c], positions[a] - positions[d]);
        if ((n0.y * n1.y) > 0.0f) {
            builder.add_triangle(a, b, c);
            builder.add_triangle(a, c, d);
        } else {
            builder.add_triangle(a, b, d);
            builder.add_triangle(b, c, d);
        }
    };
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t m = (k + 1) % n;
        if (!opening_in_band(k, 0).has_value()) {
            add_annulus_quad(inside[0][k], inside[0][m], outer[0][m], outer[0][k]); // base
        }
        for (std::size_t l = 0; l < body_levels; ++l) {
            const std::optional<std::size_t> j = opening_in_band(k, l);
            if (j.has_value()) {
                // The opening's sill unless at the base, and its head.
                if (l > 0) {
                    add_annulus_quad(outer[l][k], outer[l][m], inside[l][m], inside[l][k]);
                }
                add_annulus_quad(inside[l + 1][k], inside[l + 1][m], outer[l + 1][m], outer[l + 1][k]);
                continue;
            }
            const std::vector<uint32_t>& a = outer[l];
            const std::vector<uint32_t>& b = outer[l + 1];
            if (l == 0) { // battered: its jamb corners are not scaled copies, so triangles
                builder.add_triangle(a[k], a[m], b[m]);
                builder.add_triangle(a[k], b[m], b[k]);
            } else {
                builder.add_quad(a[k], a[m], b[m], b[k]);
            }
            builder.add_quad(inside[l][k], inside[l + 1][k], inside[l + 1][m], inside[l][m]);
        }
    }
    // Each opening's two jambs, facing into it.
    for (std::size_t k = 0; k < n; ++k) {
        const Ring_entry& entry = plan.ring[k];
        if (!entry.cut.has_value()) {
            continue;
        }
        const Tower_cut& cut = plan.cuts[entry.cut.value()];
        // The jamb's segment into the cut: the one of its two that crosses it.
        const std::size_t into_segment = (plan.segment_cut[k] == entry.cut) ? k : ((k + n - 1) % n);
        for (std::size_t l = 0; l < body_levels; ++l) {
            if (!opening_in_band(into_segment, l).has_value()) {
                continue;
            }
            const glm::vec3 o0     = positions[outer[l][k]];
            const glm::vec3 o1     = positions[outer[l + 1][k]];
            const glm::vec3 i1     = positions[inside[l + 1][k]];
            const glm::vec3 normal = glm::cross(o1 - o0, i1 - o1);
            const glm::vec2 into   = -entry.side * cut.lateral;
            if (((normal.x * into.x) + (normal.z * into.y)) > 0.0f) {
                builder.add_quad(outer[l][k], outer[l + 1][k], inside[l + 1][k], inside[l][k]);
            } else {
                builder.add_quad(outer[l][k], inside[l][k], inside[l + 1][k], outer[l + 1][k]);
            }
        }
    }
    // The hollow's ceiling.
    const uint32_t ceiling = builder.add_vertex(glm::vec3{0.0f, plan.floor_y, 0.0f});
    for (std::size_t k = 0; k < n; ++k) {
        builder.add_triangle(ceiling, inside[body_levels][(k + 1) % n], inside[body_levels][k]);
    }
    // A stone lintel standing out over the doorway, its ends in the wall.
    {
        const float x  = plan.door_half + 0.2f;
        const float z0 = (plan.round ? std::sqrt((plan.radius * plan.radius) - (x * x)) : plan.radius) - 0.1f;
        builder.add_box(glm::vec3{-x, plan.door_height, z0}, glm::vec3{x, plan.door_height + tower_lintel, plan.foot + 0.06f});
    }

    const std::vector<uint32_t>& top = outer[body_levels];
    if (!plan.battlement) {
        builder.set_slot(tower_floor_slot);
        const uint32_t center = builder.add_vertex(glm::vec3{0.0f, plan.height, 0.0f});
        for (std::size_t k = 0; k < n; ++k) {
            builder.add_triangle(top[k], top[(k + 1) % n], center);
        }
        builder.write(mesh);
        return;
    }

    // The parapet, standing out on a corbel course: outer and inner faces up to the sill, merlons above, the walkway
    // inside it.
    Tower_level corbel;
    for (std::size_t k = 0; k <= n; ++k) {
        const glm::vec2 p = plan.position(plan.ring[k % n], plan.radius);
        corbel.vertices.push_back(top[k % n]);
        corbel.keys.push_back(plan_key(p.x, p.y) + ((k == n) ? (2.0f * std::numbers::pi_v<float>) : 0.0f));
    }
    const float sill_y   = plan.height + tower_sill;
    const float merlon_y = sill_y + tower_merlon;
    const float walk     = plan.radius - plan.parapet;
    const float outside  = plan.radius + tower_overhang;
    std::vector<Plan_point> scaled;
    const auto scale = [&scaled, &plan](const float half_size) -> const std::vector<Plan_point>& {
        scaled.clear();
        for (const Plan_point& p : plan.parapet_ring) {
            scaled.push_back(Plan_point{.x = half_size * p.x, .z = half_size * p.z, .key = p.key});
        }
        return scaled;
    };
    Tower_level outer_top;
    Tower_level outer_sill;
    Tower_level inner_top;
    Tower_level inner_sill;
    add_level(builder, scale(outside), plan.height, Ring_closure::closed, outer_top);
    add_level(builder, scale(outside), sill_y,      Ring_closure::closed, outer_sill);
    add_level(builder, scale(walk),    plan.height, Ring_closure::closed, inner_top);
    add_level(builder, scale(walk),    sill_y,      Ring_closure::closed, inner_sill);
    builder.add_band(corbel.vertices, corbel.keys, outer_top.vertices, outer_top.keys);

    const std::size_t count = plan.parapet_ring.size();
    std::vector<uint32_t> outer_merlon(count, 0);
    std::vector<uint32_t> inner_merlon(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t previous = (i + count - 1) % count;
        if (!plan.crenel[i] || !plan.crenel[previous]) {
            const Plan_point& p = plan.parapet_ring[i];
            outer_merlon[i] = builder.add_vertex(glm::vec3{outside * p.x, merlon_y, outside * p.z});
            inner_merlon[i] = builder.add_vertex(glm::vec3{walk * p.x, merlon_y, walk * p.z});
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t j = i + 1; // in the closed levels
        const std::size_t next = (i + 1) % count;
        builder.add_quad(outer_top.vertices[i], outer_top.vertices[j], outer_sill.vertices[j], outer_sill.vertices[i]);
        builder.add_quad(inner_top.vertices[i], inner_sill.vertices[i], inner_sill.vertices[j], inner_top.vertices[j]);
        if (plan.crenel[i]) {
            builder.add_quad(outer_sill.vertices[i], outer_sill.vertices[j], inner_sill.vertices[j], inner_sill.vertices[i]);
            continue;
        }
        builder.add_quad(outer_sill.vertices[i], outer_sill.vertices[j], outer_merlon[next], outer_merlon[i]);
        builder.add_quad(inner_sill.vertices[i], inner_merlon[i], inner_merlon[next], inner_sill.vertices[j]);
        builder.add_quad(outer_merlon[i], outer_merlon[next], inner_merlon[next], inner_merlon[i]);
    }
    // A merlon's end faces its neighbouring crenel.
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t previous = (i + count - 1) % count;
        if (plan.crenel[i] == plan.crenel[previous]) {
            continue;
        }
        if (plan.crenel[i]) {
            builder.add_quad(outer_sill.vertices[i], inner_sill.vertices[i], inner_merlon[i], outer_merlon[i]);
        } else {
            builder.add_quad(outer_sill.vertices[i], outer_merlon[i], inner_merlon[i], inner_sill.vertices[i]);
        }
    }
    builder.set_slot(tower_floor_slot);
    const uint32_t center = builder.add_vertex(glm::vec3{0.0f, plan.height, 0.0f});
    for (std::size_t i = 0; i < count; ++i) {
        builder.add_triangle(inner_top.vertices[i], inner_top.vertices[i + 1], center);
    }
    builder.write(mesh);
}

} // anonymous namespace

void add_medieval_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.battlement";
        shape.kit             = "architecture";
        shape.display_name    = "Battlement";
        shape.description     =
            "Medieval parapet run along X on a wall top: a breastwork to the crenels' sill and merlons above it (square, "
            "swallowtail or pointed), crenels at the bays' centers and half merlons at the ends, so runs join into whole merlons.";
        shape.tags            = {"obstacle", "wall", "parapet", "medieval", "tiles", "size:2x0.6"};
        shape.parameters      = {
            derive_float(make_length_parameter("length",        "Size along X", 2.0f, Kit_default_scale::modules, 0.5f, 24.0f), 1.0f, 6.0f, 0.5f),
            derive_float(make_length_parameter("height",        "Height of the crenels' sill", 0.9f, Kit_default_scale::meters, 0.2f, 3.0f), 0.8f, 1.2f, 0.05f),
            derive_float(make_length_parameter("thickness",     "Size along Z", 0.25f, Kit_default_scale::meters, 0.05f, 4.0f), 0.2f, 0.6f, 0.05f),
            derive_float(make_length_parameter("merlon_height", "Height of the merlons above the sill", 0.9f, Kit_default_scale::meters, 0.2f, 3.0f), 0.7f, 1.2f, 0.05f),
            derive_float(make_length_parameter("merlon_width",  "Nominal width of a merlon", 1.2f, Kit_default_scale::meters, 0.4f, 4.0f), 0.8f, 1.6f, 0.1f),
            derive_float(make_length_parameter("crenel_width",  "Nominal width of a crenel", 0.8f, Kit_default_scale::meters, 0.3f, 2.0f), 0.5f, 1.0f, 0.05f),
            derive_enum (make_enum_parameter  ("merlon",        "Merlon top", {"square", "swallowtail", "pointed"}, 0)),
            make_seed_parameter()
        };
        shape.material_slots  = {"wall"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 48;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.generator       = generate_battlement;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.get_seam_sides  = [](const Kit_parameter_values& values) {
            return (values.get_enum("merlon") == "square")
                ? std::vector<Kit_side>{Kit_side::x_neg, Kit_side::x_pos, Kit_side::y_neg}
                : std::vector<Kit_side>{Kit_side::y_neg};
        };
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float half_length = 0.5f * values.get_float("length");
            out_connectors.push_back(Kit_connector{.name = "base", .position = glm::vec3{0.0f}, .outward = glm::vec3{0.0f, -1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
            add_side_connector(out_connectors, "start", glm::vec3{-half_length, 0.0f, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "end",   glm::vec3{ half_length, 0.0f, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.tower";
        shape.kit             = "architecture";
        shape.display_name    = "Tower";
        shape.description     =
            "Medieval tower segment, round or square, standing at its origin: walls battered out below the door's head, a "
            "doorway under a stone lintel at the front (+Z) into the hollow inside, rows of arrow slits away from the side "
            "connectors, and a walkable top floor at the height behind a corbelled crenellated parapet, or a flat top a "
            "further segment stands on.";
        shape.tags            = {"walkable", "obstacle", "opening", "tower", "medieval", "size:3x3x1.5"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("profile",        "Plan", {"round", "square"}, 0)),
            // In meters: the doorway and the agent fix the least width, whatever the module.
            derive_float(make_length_parameter("width",          "Outer width above the batter", 3.0f, Kit_default_scale::meters, 2.5f, 16.0f), 3.0f, 6.0f, 0.5f),
            derive_float(make_length_parameter("height",         "Height of the top floor", 1.5f, Kit_default_scale::wall_heights, 3.5f, 30.0f), 1.2f, 3.0f, 0.1f),
            derive_float(make_angle_parameter ("batter",         "Outward slope of the walls below the door's head", 4.0f, 0.0f, 10.0f), 0.0f, 8.0f, 0.5f),
            derive_float(make_length_parameter("wall",           "Wall thickness", 0.4f, Kit_default_scale::meters, 0.2f, 0.7f), 0.3f, 0.6f, 0.05f),
            make_length_parameter("opening_width",  "Clear width of the doorway", 1.2f, Kit_default_scale::meters, 0.8f, 1.8f),
            make_length_parameter("opening_height", "Clear height of the doorway, under the lintel", 2.2f, Kit_default_scale::meters, 2.0f, 3.4f),
            derive_enum (make_enum_parameter  ("top",            "Top: a crenellated parapet, or flat for stacking", {"battlement", "flat"}, 0)),
            derive_int  (make_count_parameter ("slit_rows",      "Rows of arrow slits above the doorway, as many as the height has room for", 1, 0, 2), 0, 2),
            make_seed_parameter()
        };
        shape.material_slots  = {"wall", "floor"};
        shape.normal_style    = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = 30.0f;
        shape.triangle_budget = 700;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.generator       = generate_tower;
        shape.origin_rule     = Kit_origin_rule::base_contains_origin; // the doorway leaves the base's front open
        shape.check_combination = [](const Kit_parameter_values& values) -> std::string {
            const float width  = values.get_float("width");
            const float wall   = values.get_float("wall");
            const float door_w = values.get_float("opening_width");
            const float door_h = values.get_float("opening_height");
            if ((width - (2.0f * wall)) < (door_w + 0.4f)) {
                return fmt::format("{}: the inside, {} m wide, is narrower than the doorway's {} m and 0.4 m", values.get_shape()->id, width - (2.0f * wall), door_w);
            }
            if (values.get_float("height") < (door_h + tower_head_above + 0.8f)) {
                return fmt::format("{}: height {} m leaves no top floor above the doorway's {} m and 0.9 m", values.get_shape()->id, values.get_float("height"), door_h);
            }
            return {};
        };
        shape.check_walkability = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            const Tower_plan  plan{values};
            const float       need    = (2.0f * agent.radius) + kit_opening_margin;
            const float       inscribe = plan.round ? std::cos(std::numbers::pi_v<float> / static_cast<float>(plan.sides)) : 1.0f;
            const std::string opening = check_kit_opening(*values.get_shape(), 2.0f * plan.door_half, plan.door_height, agent);
            if (!opening.empty()) {
                return opening;
            }
            if ((2.0f * (plan.radius - plan.wall) * inscribe) < need) {
                return fmt::format("{}: the inside is narrower than the agent's {} m", values.get_shape()->id, need);
            }
            if (plan.battlement && ((2.0f * (plan.radius - plan.parapet) * inscribe) < need)) {
                return fmt::format("{}: the walkway is narrower than the agent's {} m", values.get_shape()->id, need);
            }
            return {};
        };
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const Tower_plan plan{values};
            const float      middle = plan.radius - (0.5f * plan.wall); // walls joined to the sides end inside the tower's wall
            add_base_and_top_connectors(out_connectors, plan.height);
            const glm::vec2  jamb   = plan.position(Ring_entry{.unit = glm::vec2{0.0f}, .cut = 0, .side = 1.0f}, plan.foot);
            add_side_connector(out_connectors, "opening_front", glm::vec3{0.0f, 0.0f, jamb.y}, glm::vec3{0.0f, 0.0f, 1.0f});
            add_side_connector(out_connectors, "left",  glm::vec3{-middle, 0.0f, 0.0f},    glm::vec3{-1.0f, 0.0f,  0.0f});
            add_side_connector(out_connectors, "right", glm::vec3{ middle, 0.0f, 0.0f},    glm::vec3{ 1.0f, 0.0f,  0.0f});
            add_side_connector(out_connectors, "back",  glm::vec3{0.0f, 0.0f, -middle},    glm::vec3{ 0.0f, 0.0f, -1.0f});
        };
        shapes.push_back(std::move(shape));
    }
}

} // namespace editor
