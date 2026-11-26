#include "level/kit_shapes.hpp"
#include "level/kit_mesh_builder.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace editor {

namespace {

void generate_wall(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float l = values.get_float("length");
    const float h = values.get_float("height");
    const float t = values.get_float("thickness");
    Kit_mesh_builder builder;
    builder.add_box(glm::vec3{-0.5f * l, 0.0f, -0.5f * t}, glm::vec3{0.5f * l, h, 0.5f * t});
    builder.write(mesh);
}

// The half ellipse of the span and rise through `segments` + 1 points, the feet exact: inside the arch's opening.
auto make_arch_intrados(const Kit_parameter_values& values) -> std::vector<glm::vec2>
{
    const float a = 0.5f * values.get_float("span");
    const float b = values.get_float("rise");
    const int   n = values.get_int("segments");
    std::vector<glm::vec2> points(static_cast<std::size_t>(n) + 1);
    for (int i = 0; (2 * i) <= n; ++i) {
        const float angle = std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(n);
        const bool  foot  = (i == 0);
        const bool  crown = ((2 * i) == n);
        const glm::vec2 point{a * (crown ? 0.0f : std::cos(angle)), b * (foot ? 0.0f : std::sin(angle))};
        points[static_cast<std::size_t>(i)]     = point;
        points[static_cast<std::size_t>(n - i)] = glm::vec2{-point.x, point.y};
    }
    return points;
}

// A slab in the XY plane, mirrored about z = 0: grid nodes at a few depths below the faces, open cells cut through.
class Relief_slab
{
public:
    Relief_slab(std::vector<float> xs, std::vector<float> ys, std::vector<float> depths, float half_thickness);

    void set_node_depth(std::size_t ix, std::size_t iy, uint8_t depth_index);
    void set_cell_open (std::size_t ix, std::size_t iy);
    void set_cell_slot (std::size_t ix, std::size_t iy, uint32_t slot);
    void build         (Kit_mesh_builder& builder);

private:
    [[nodiscard]] auto is_solid (std::ptrdiff_t ix, std::ptrdiff_t iy) const -> bool;
    [[nodiscard]] auto get_depth(std::size_t ix, std::size_t iy) const -> uint8_t;
    void               add_cell (Kit_mesh_builder& builder, std::size_t ix, std::size_t iy) const;
    [[nodiscard]] auto get_slot (std::ptrdiff_t ix, std::ptrdiff_t iy) const -> uint32_t;
    void               add_wall (Kit_mesh_builder& builder, std::size_t node_a, std::size_t node_b) const;

    std::vector<float>    m_xs;     // rising
    std::vector<float>    m_ys;     // rising
    std::vector<float>    m_depths; // index 0: the face
    float                 m_half_thickness{0.0f};
    std::vector<uint8_t>  m_node_depths;
    std::vector<bool>     m_open;
    std::vector<uint32_t> m_cell_slots;
    std::vector<uint32_t> m_front;
    std::vector<uint32_t> m_back;
};

Relief_slab::Relief_slab(std::vector<float> xs, std::vector<float> ys, std::vector<float> depths, const float half_thickness)
    : m_xs            {std::move(xs)}
    , m_ys            {std::move(ys)}
    , m_depths        {std::move(depths)}
    , m_half_thickness{half_thickness}
    , m_node_depths   (m_xs.size() * m_ys.size(), uint8_t{0})
    , m_open          ((m_xs.size() - 1) * (m_ys.size() - 1), false)
    , m_cell_slots    ((m_xs.size() - 1) * (m_ys.size() - 1), uint32_t{0})
{
}

void Relief_slab::set_node_depth(const std::size_t ix, const std::size_t iy, const uint8_t depth_index)
{
    m_node_depths[(iy * m_xs.size()) + ix] = depth_index;
}

void Relief_slab::set_cell_open(const std::size_t ix, const std::size_t iy)
{
    m_open[(iy * (m_xs.size() - 1)) + ix] = true;
}

void Relief_slab::set_cell_slot(const std::size_t ix, const std::size_t iy, const uint32_t slot)
{
    m_cell_slots[(iy * (m_xs.size() - 1)) + ix] = slot;
}

// The slot of a solid cell.
auto Relief_slab::get_slot(const std::ptrdiff_t ix, const std::ptrdiff_t iy) const -> uint32_t
{
    const std::ptrdiff_t columns = static_cast<std::ptrdiff_t>(m_xs.size()) - 1;
    return m_cell_slots[static_cast<std::size_t>((iy * columns) + ix)];
}

auto Relief_slab::is_solid(const std::ptrdiff_t ix, const std::ptrdiff_t iy) const -> bool
{
    const std::ptrdiff_t columns = static_cast<std::ptrdiff_t>(m_xs.size()) - 1;
    const std::ptrdiff_t rows    = static_cast<std::ptrdiff_t>(m_ys.size()) - 1;
    if ((ix < 0) || (iy < 0) || (ix >= columns) || (iy >= rows)) {
        return false;
    }
    return !m_open[static_cast<std::size_t>((iy * columns) + ix)];
}

auto Relief_slab::get_depth(const std::size_t ix, const std::size_t iy) const -> uint8_t
{
    return m_node_depths[(iy * m_xs.size()) + ix];
}

// A flat or bevelled cell is a quad; a mitre, three corners at one depth, splits through the fourth corner.
void Relief_slab::add_cell(Kit_mesh_builder& builder, const std::size_t ix, const std::size_t iy) const
{
    const std::size_t n00 = (iy * m_xs.size()) + ix;
    const std::size_t n10 = n00 + 1;
    const std::size_t n01 = n00 + m_xs.size();
    const std::size_t n11 = n01 + 1;
    const uint8_t     d00 = get_depth(ix,     iy);
    const uint8_t     d10 = get_depth(ix + 1, iy);
    const uint8_t     d01 = get_depth(ix,     iy + 1);
    const uint8_t     d11 = get_depth(ix + 1, iy + 1);
    if (((d00 == d10) && (d01 == d11)) || ((d00 == d01) && (d10 == d11))) {
        builder.add_quad(m_front[n00], m_front[n10], m_front[n11], m_front[n01]);
        builder.add_quad(m_back[n00],  m_back[n01],  m_back[n11],  m_back[n10]);
    } else if ((d10 == d01) && ((d00 == d10) || (d11 == d10))) {
        builder.add_triangle(m_front[n00], m_front[n10], m_front[n11]);
        builder.add_triangle(m_front[n00], m_front[n11], m_front[n01]);
        builder.add_triangle(m_back[n00],  m_back[n11],  m_back[n10]);
        builder.add_triangle(m_back[n00],  m_back[n01],  m_back[n11]);
    } else {
        builder.add_triangle(m_front[n00], m_front[n10], m_front[n01]);
        builder.add_triangle(m_front[n10], m_front[n11], m_front[n01]);
        builder.add_triangle(m_back[n00],  m_back[n01],  m_back[n10]);
        builder.add_triangle(m_back[n10],  m_back[n01],  m_back[n11]);
    }
}

// The wall through the slab on the grid edge from node a to node b, the solid cell on the left seen from the front.
void Relief_slab::add_wall(Kit_mesh_builder& builder, const std::size_t node_a, const std::size_t node_b) const
{
    builder.add_quad(m_front[node_b], m_front[node_a], m_back[node_a], m_back[node_b]);
}

void Relief_slab::build(Kit_mesh_builder& builder)
{
    const std::size_t line_count = m_xs.size();
    const std::size_t columns    = line_count - 1;
    const std::size_t rows       = m_ys.size() - 1;
    for (std::size_t iy = 0; iy <= rows; ++iy) {
        for (std::size_t ix = 0; ix <= columns; ++ix) {
            const float z = m_half_thickness - m_depths[get_depth(ix, iy)];
            m_front.push_back(builder.add_vertex(glm::vec3{m_xs[ix], m_ys[iy],  z}));
            m_back .push_back(builder.add_vertex(glm::vec3{m_xs[ix], m_ys[iy], -z}));
        }
    }
    for (std::size_t iy = 0; iy < rows; ++iy) {
        for (std::size_t ix = 0; ix < columns; ++ix) {
            if (is_solid(static_cast<std::ptrdiff_t>(ix), static_cast<std::ptrdiff_t>(iy))) {
                builder.set_slot(get_slot(static_cast<std::ptrdiff_t>(ix), static_cast<std::ptrdiff_t>(iy)));
                add_cell(builder, ix, iy);
            }
        }
    }
    for (std::size_t iy = 0; iy < rows; ++iy) {
        for (std::size_t ix = 0; ix <= columns; ++ix) {
            const std::ptrdiff_t x     = static_cast<std::ptrdiff_t>(ix);
            const std::ptrdiff_t y     = static_cast<std::ptrdiff_t>(iy);
            const bool           left  = is_solid(x - 1, y);
            const bool           right = is_solid(x, y);
            const std::size_t    lower = (iy * line_count) + ix;
            const std::size_t    upper = lower + line_count;
            // A wall through the slab takes its solid cell's slot.
            if (left && !right) {
                builder.set_slot(get_slot(x - 1, y));
                add_wall(builder, lower, upper);
            } else if (right && !left) {
                builder.set_slot(get_slot(x, y));
                add_wall(builder, upper, lower);
            }
        }
    }
    for (std::size_t iy = 0; iy <= rows; ++iy) {
        for (std::size_t ix = 0; ix < columns; ++ix) {
            const std::ptrdiff_t x     = static_cast<std::ptrdiff_t>(ix);
            const std::ptrdiff_t y     = static_cast<std::ptrdiff_t>(iy);
            const bool           below = is_solid(x, y - 1);
            const bool           above = is_solid(x, y);
            const std::size_t    first = (iy * line_count) + ix;
            if (below && !above) {
                builder.set_slot(get_slot(x, y - 1));
                add_wall(builder, first + 1, first);
            } else if (above && !below) {
                builder.set_slot(get_slot(x, y));
                add_wall(builder, first, first + 1);
            }
        }
    }
}

constexpr uint32_t door_wall_frame_slot = 1;

void generate_door_wall(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float length         = values.get_float("length");
    const float height         = values.get_float("height");
    const float thickness      = values.get_float("thickness");
    const float opening_width  = values.get_float("opening_width");
    const float opening_height = values.get_float("opening_height");

    // The frame keeps its sizes where the jambs and the header have room; its widths shrink with a narrow jamb, its heights with a low header.
    const float jamb     = 0.5f * (length - opening_width);
    const float header   = height - opening_height;
    const float scale_x  = std::min(1.0f, jamb / 0.42f);
    const float scale_y  = std::min(1.0f, header / 0.45f);
    const float splay_x  = 0.07f  * scale_x; // the chamfer from the frame down to the opening's edge
    const float splay_y  = 0.07f  * scale_y;
    const float band     = 0.10f  * scale_x; // the frame's flat band
    const float bevel_x  = 0.035f * scale_x; // half the width of the groove around the frame
    const float bevel_y  = 0.035f * scale_y;
    const float lintel   = 0.20f  * scale_y; // between the groove's centers; its ears reach two bevels past the jambs
    const float cut      = std::min(std::min(bevel_x, bevel_y), 0.16f * thickness);
    const float reveal   = std::min(0.9f * std::min(splay_x, splay_y), 0.28f * thickness);

    std::array<float, 8> half_xs{};
    half_xs[0] = 0.5f * opening_width;
    half_xs[1] = half_xs[0] + splay_x;
    half_xs[2] = half_xs[1] + band;
    half_xs[3] = half_xs[2] + bevel_x; // the groove beside the jamb
    half_xs[4] = half_xs[3] + bevel_x;
    half_xs[5] = half_xs[4] + bevel_x; // the groove around the ear
    half_xs[6] = half_xs[5] + bevel_x;
    half_xs[7] = 0.5f * length;
    std::vector<float> xs;
    for (std::size_t i = half_xs.size(); i > 0; --i) {
        xs.push_back(-half_xs[i - 1]);
    }
    xs.insert(xs.end(), half_xs.begin(), half_xs.end());

    std::vector<float> ys(9);
    ys[0] = 0.0f;
    ys[1] = opening_height;
    ys[2] = ys[1] + splay_y;
    ys[3] = ys[2] + bevel_y; // the groove under the ear
    ys[4] = ys[3] + bevel_y;
    ys[6] = ys[3] + lintel; // the groove over the lintel
    ys[5] = ys[6] - bevel_y;
    ys[7] = ys[6] + bevel_y;
    ys[8] = height;

    constexpr uint8_t groove = 1;
    constexpr uint8_t edge   = 2;
    Relief_slab slab{xs, ys, {0.0f, cut, reveal}, 0.5f * thickness};
    // Both halves: line i of the half is grid line (half + i) on the right and (half - 1 - i) on the left.
    const std::size_t half = half_xs.size();
    for (std::size_t side = 0; side < 2; ++side) {
        std::array<std::size_t, 8> line{};
        for (std::size_t i = 0; i < half; ++i) {
            line[i] = (side == 0) ? (half - 1 - i) : (half + i);
        }
        slab.set_node_depth(line[0], 0, edge);
        slab.set_node_depth(line[0], 1, edge);
        for (std::size_t j = 0; j <= 3; ++j) {
            slab.set_node_depth(line[3], j, groove);
        }
        slab.set_node_depth(line[4], 3, groove);
        for (std::size_t j = 3; j <= 6; ++j) {
            slab.set_node_depth(line[5], j, groove);
        }
        for (std::size_t i = 0; i <= 4; ++i) {
            slab.set_node_depth(line[i], 6, groove);
        }
    }
    slab.set_cell_open(half - 1, 0);
    // The frame slot: the reveal and the band out to the groove's center, and the lintel with its ears.
    for (std::size_t iy = 0; iy < 6; ++iy) {
        const std::size_t reach = (iy < 3) ? 3 : 5; // half lines from the opening's edge
        for (std::size_t ix = half - 1 - reach; ix < (half + reach); ++ix) {
            slab.set_cell_slot(ix, iy, door_wall_frame_slot);
        }
    }
    Kit_mesh_builder builder;
    slab.build(builder);
    builder.write(mesh);
}

// A ring of the lathe around the Y axis.
class Lathe_ring
{
public:
    float radius{0.0f};
    float y     {0.0f};
};

// Rings bottom to top, each the plan (counter-clockwise seen from above) times its radius: a quad per plan side between
// two rings and a polygon per cap.
void add_lathe(Kit_mesh_builder& builder, const std::vector<Lathe_ring>& rings, const std::vector<glm::vec2>& circle)
{
    const std::size_t count = circle.size();
    std::vector<uint32_t> previous;
    std::vector<uint32_t> current;
    for (const Lathe_ring& ring : rings) {
        current.clear();
        for (const glm::vec2& point : circle) {
            current.push_back(builder.add_vertex(glm::vec3{ring.radius * point.x, ring.y, ring.radius * point.y}));
        }
        if (previous.empty()) {
            const std::vector<uint32_t> bottom_cap(current.rbegin(), current.rend());
            builder.add_polygon(bottom_cap);
        } else {
            for (std::size_t i = 0; i < count; ++i) {
                const std::size_t next = (i + 1) % count;
                builder.add_quad(previous[i], previous[next], current[next], current[i]);
            }
        }
        previous = current;
    }
    builder.add_polygon(previous);
}

void generate_pillar(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float width  = values.get_float("width");
    const float height = values.get_float("height");
    Kit_mesh_builder builder;
    // Base and capital follow the width; a low pillar gives each a share of its height, a thin tall one a least height.
    const float radius  = 0.5f * width;
    const float base    = std::min(std::max(0.34f * width, 0.02f * height), 0.20f * height);
    const float capital = std::min(std::max(0.40f * width, 0.02f * height), 0.22f * height);
    const float neck    = height - capital;
    if (values.get_enum("profile") != "round") {
        // A pier: the plinth and the abacus fill the top and bottom seam sides, chamfers lead to the inset shaft.
        const std::vector<Lathe_ring> pier_rings{
            {1.00f * radius, 0.0f},                     // plinth
            {1.00f * radius, 0.58f * base},
            {0.90f * radius, 0.66f * base},             // base course
            {0.90f * radius, 0.92f * base},
            {0.80f * radius, base},                     // shaft
            {0.80f * radius, neck},
            {0.87f * radius, neck + (0.05f * capital)}, // necking band
            {0.87f * radius, neck + (0.17f * capital)},
            {0.80f * radius, neck + (0.22f * capital)},
            {0.80f * radius, neck + (0.46f * capital)},
            {0.94f * radius, neck + (0.58f * capital)}, // flare
            {1.00f * radius, neck + (0.64f * capital)}, // abacus
            {1.00f * radius, height}
        };
        const std::vector<glm::vec2> square{{1.0f, -1.0f}, {-1.0f, -1.0f}, {-1.0f, 1.0f}, {1.0f, 1.0f}};
        add_lathe(builder, pier_rings, square);
        builder.write(mesh);
        return;
    }
    const std::vector<Lathe_ring> rings{
        {1.00f * radius, 0.0f},                    // plinth
        {1.00f * radius, 0.38f * base},
        {0.88f * radius, 0.44f * base},
        {0.95f * radius, 0.56f * base},            // torus
        {0.95f * radius, 0.70f * base},
        {0.87f * radius, 0.84f * base},
        {0.80f * radius, base},                    // shaft, tapering
        {0.70f * radius, neck},
        {0.78f * radius, neck + (0.10f * capital)}, // astragal
        {0.70f * radius, neck + (0.20f * capital)},
        {0.70f * radius, neck + (0.38f * capital)}, // necking
        {0.86f * radius, neck + (0.54f * capital)}, // echinus
        {0.95f * radius, neck + (0.66f * capital)},
        {1.00f * radius, neck + (0.70f * capital)}, // abacus
        {1.00f * radius, height}
    };
    const int segments = values.get_int("segments");
    std::vector<glm::vec2> circle;
    for (int i = 0; i < segments; ++i) {
        circle.push_back(get_kit_circle_point(i, segments));
    }
    add_lathe(builder, rings, circle);
    builder.write(mesh);
}

// The cross sections of the arch ring: the springers and the keystone fill the envelope, the voussoirs between them lie back.
enum class Arch_profile : unsigned int { proud = 0, proud_joint, ring, ring_joint };

class Arch_section
{
public:
    glm::vec2       inner{0.0f};
    glm::vec2       outer{0.0f};
    float           half_depth{0.0f};
    int             station{0}; // sections at one place along the curve share it: a step between two stones
    Arch_profile profile{Arch_profile::proud};
    bool            accent{false}; // of a stone of the accent slot, or the joint between two stones
};

class Arch_form
{
public:
    explicit Arch_form(const Kit_parameter_values& values);

    [[nodiscard]] auto make_section(float angle_cos, float angle_sin, Arch_profile profile, int station) const -> Arch_section;
    [[nodiscard]] auto get_speed   (float angle_cos, float angle_sin) const -> float;

    float half_span {0.0f};
    float rise      {0.0f};
    float thickness {0.0f};
    float half_depth{0.0f};
    float recess    {0.0f}; // the voussoirs' intrados lies this far outside the opening's curve
    float drop      {0.0f}; // the voussoirs' extrados lies this far inside the envelope
    float inset     {0.0f}; // the voussoirs' faces lie this far inside the envelope
    float groove    {0.0f}; // depth of the joint between two stones of one profile
};

Arch_form::Arch_form(const Kit_parameter_values& values)
    : half_span {0.5f * values.get_float("span")}
    , rise      {values.get_float("rise")}
    , thickness {values.get_float("thickness")}
    , half_depth{0.5f * values.get_float("depth")}
    , recess    {0.05f * thickness}
    , drop      {0.16f * thickness}
    , inset     {std::min(0.16f * half_depth, 0.5f * thickness)}
    , groove    {std::min(0.09f * thickness, 0.24f * half_depth)}
{
}

// The section on the normal of the opening's half ellipse at the angle.
auto Arch_form::make_section(const float angle_cos, const float angle_sin, const Arch_profile profile, const int station) const -> Arch_section
{
    const bool      joint  = (profile == Arch_profile::proud_joint) || (profile == Arch_profile::ring_joint);
    const bool      back   = (profile == Arch_profile::ring) || (profile == Arch_profile::ring_joint);
    const float     cut    = joint ? groove : 0.0f;
    const glm::vec2 point  {half_span * angle_cos, rise * angle_sin};
    const glm::vec2 normal = glm::normalize(glm::vec2{rise * angle_cos, half_span * angle_sin});
    Arch_section section;
    section.inner      = point + (((back ? recess : 0.0f) + cut) * normal);
    section.outer      = point + ((thickness - (back ? drop : 0.0f) - cut) * normal);
    section.half_depth = half_depth - (back ? inset : 0.0f) - cut;
    section.station    = station;
    section.profile    = profile;
    return section;
}

// Meters along the opening's curve per radian of the angle.
auto Arch_form::get_speed(const float angle_cos, const float angle_sin) const -> float
{
    return std::sqrt((half_span * half_span * angle_sin * angle_sin) + (rise * rise * angle_cos * angle_cos));
}

// The right half's sections from the foot (+X) to the crown; the `segments` value counts the stones (level_tools.md).
auto make_arch_half(const Arch_form& form, const int segments) -> std::vector<Arch_section>
{
    // Units of half a segment: the keystone takes one unit of each half, a voussoir two, the springer the rest.
    const int fine_per_unit  = std::max(1, (16 + segments - 1) / segments);
    const int rest           = segments - 1;
    const int springer_units = (rest == 1) ? 1 : (((rest % 2) == 0) ? 2 : 3);
    const int voussoirs      = (rest - springer_units) / 2;
    const int fine_count     = segments * fine_per_unit;
    const float fine_angle   = 0.5f * std::numbers::pi_v<float> / static_cast<float>(fine_count);

    std::vector<Arch_section> sections;
    int station    = 0;
    int stone      = 0; // 0: the springer, then the voussoirs, last the keystone
    int stone_end  = springer_units * fine_per_unit;
    // The accent slot: the keystone, and the springers where voussoirs lie between (else the springers are the ring).
    const auto is_accent = [voussoirs](const int index) -> bool { return (index > voussoirs) || ((index == 0) && (voussoirs > 0)); };
    for (int fine = 0; fine <= fine_count; ++fine) {
        const bool            foot    = (fine == 0);
        const bool            crown   = (fine == fine_count);
        const float           angle   = fine_angle * static_cast<float>(fine);
        const float           c       = crown ? 0.0f : (foot ? 1.0f : std::cos(angle));
        const float           s       = foot ? 0.0f : (crown ? 1.0f : std::sin(angle));
        const bool            ring    = (stone >= 1) && (stone <= voussoirs);
        const Arch_profile profile = ring ? Arch_profile::ring : Arch_profile::proud;
        if ((fine != stone_end) || crown) {
            sections.push_back(form.make_section(c, s, profile, station++));
            sections.back().accent = is_accent(stone);
            continue;
        }
        const bool accent_before = is_accent(stone);
        ++stone;
        stone_end += ((stone <= voussoirs) ? 2 : 1) * fine_per_unit;
        const bool            next_ring    = (stone >= 1) && (stone <= voussoirs);
        const Arch_profile next_profile = next_ring ? Arch_profile::ring : Arch_profile::proud;
        if (next_profile != profile) {
            // A step: both stones' sections at the joint.
            sections.push_back(form.make_section(c, s, profile,      station));
            sections.back().accent = accent_before;
            sections.push_back(form.make_section(c, s, next_profile, station++));
            sections.back().accent = is_accent(stone);
            continue;
        }
        // A groove: the joint cut back between two sections of the stones' profile.
        const float           delta = std::min((0.6f * form.groove) / form.get_speed(c, s), 0.4f * fine_angle);
        const Arch_profile cut   = ring ? Arch_profile::ring_joint : Arch_profile::proud_joint;
        sections.push_back(form.make_section(std::cos(angle - delta), std::sin(angle - delta), profile, station++));
        sections.back().accent = accent_before;
        sections.push_back(form.make_section(c, s, cut, station++));
        sections.back().accent = true;
        sections.push_back(form.make_section(std::cos(angle + delta), std::sin(angle + delta), profile, station++));
        sections.back().accent = is_accent(stone);
    }
    return sections;
}

constexpr uint32_t arch_accent_slot = 1;

void generate_arch(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Arch_form form{values};
    std::vector<Arch_section> sections = make_arch_half(form, values.get_int("segments"));
    // The left half is the right half's mirror image, so both feet and every pair of stones are equal to the bit.
    const int crown_station = sections.back().station;
    for (std::size_t i = sections.size() - 1; i > 0; --i) {
        Arch_section mirrored = sections[i - 1];
        mirrored.inner.x = -mirrored.inner.x;
        mirrored.outer.x = -mirrored.outer.x;
        mirrored.station = (2 * crown_station) - mirrored.station;
        sections.push_back(mirrored);
    }

    const std::size_t count = sections.size();
    Kit_mesh_builder  builder;
    std::vector<uint32_t> inner_front(count);
    std::vector<uint32_t> inner_back (count);
    std::vector<uint32_t> outer_front(count);
    std::vector<uint32_t> outer_back (count);
    for (std::size_t i = 0; i < count; ++i) {
        const Arch_section& section = sections[i];
        inner_front[i] = builder.add_vertex({section.inner.x, section.inner.y,  section.half_depth});
        inner_back [i] = builder.add_vertex({section.inner.x, section.inner.y, -section.half_depth});
        outer_front[i] = builder.add_vertex({section.outer.x, section.outer.y,  section.half_depth});
        outer_back [i] = builder.add_vertex({section.outer.x, section.outer.y, -section.half_depth});
    }
    for (std::size_t i = 0; (i + 1) < count; ++i) {
        const std::size_t j = i + 1;
        // A stone's facets are its slot's; a step between two stones is the proud stone's side.
        const bool step = (sections[i].station == sections[j].station);
        builder.set_slot((step ? (sections[i].accent || sections[j].accent) : (sections[i].accent && sections[j].accent)) ? arch_accent_slot : 0u);
        builder.add_quad(inner_front[i], inner_front[j], inner_back[j],  inner_back[i]);  // intrados
        builder.add_quad(outer_front[i], outer_back[i],  outer_back[j],  outer_front[j]); // extrados
        if ((sections[i].station == sections[j].station) || (sections[i].profile == sections[j].profile)) {
            builder.add_quad(inner_front[i], outer_front[i], outer_front[j], inner_front[j]); // front
            builder.add_quad(inner_back[i],  inner_back[j],  outer_back[j],  outer_back[i]);  // back
            continue;
        }
        // A groove's wall on a face is not planar: two triangles, the diagonal from the joint's inner corner.
        const bool joint_first = (sections[i].profile == Arch_profile::proud_joint) || (sections[i].profile == Arch_profile::ring_joint);
        if (joint_first) {
            builder.add_triangle(inner_front[i], outer_front[i], outer_front[j]);
            builder.add_triangle(inner_front[i], outer_front[j], inner_front[j]);
            builder.add_triangle(inner_back[i],  outer_back[j],  outer_back[i]);
            builder.add_triangle(inner_back[i],  inner_back[j],  outer_back[j]);
        } else {
            builder.add_triangle(inner_front[i], outer_front[i], inner_front[j]);
            builder.add_triangle(outer_front[i], outer_front[j], inner_front[j]);
            builder.add_triangle(inner_back[i],  inner_back[j],  outer_back[i]);
            builder.add_triangle(outer_back[i],  inner_back[j],  outer_back[j]);
        }
    }
    const std::size_t last = count - 1;
    builder.set_slot(sections[0].accent ? arch_accent_slot : 0u);
    builder.add_quad(inner_front[0],    inner_back[0],    outer_back[0],    outer_front[0]);    // right foot
    builder.add_quad(outer_front[last], outer_back[last], inner_back[last], inner_front[last]); // left foot
    builder.write(mesh);
}

// "start" (-X) and "end" (+X) at the centers of the base's end edges, half_length from the origin.
void add_end_connectors(std::vector<Kit_connector>& out_connectors, const float half_length)
{
    add_side_connector(out_connectors, "start", glm::vec3{-half_length, 0.0f, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
    add_side_connector(out_connectors, "end",   glm::vec3{ half_length, 0.0f, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
}

} // anonymous namespace

auto check_kit_opening(const Kit_shape_definition& shape, const float width, const float height, const Kit_agent& agent) -> std::string
{
    const float min_width  = (2.0f * agent.radius) + kit_opening_margin;
    const float min_height = agent.height + kit_opening_margin;
    if (width < min_width) {
        return fmt::format("{}: opening width {} m is narrower than the agent's {} m", shape.id, width, min_width);
    }
    if (height < min_height) {
        return fmt::format("{}: opening height {} m is lower than the agent's {} m", shape.id, height, min_height);
    }
    return {};
}

auto get_arch_clear_width(const Kit_parameter_values& values, const float height) -> float
{
    const std::vector<glm::vec2> points = make_arch_intrados(values);
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const glm::vec2 p0 = points[i];
        const glm::vec2 p1 = points[i + 1];
        if ((p0.x <= 0.0f) || (p0.y > height) || (p1.y < height)) {
            continue;
        }
        const float dy = p1.y - p0.y;
        const float t  = (dy > 0.0f) ? ((height - p0.y) / dy) : 0.0f;
        return 2.0f * std::max(0.0f, p0.x + (t * (p1.x - p0.x)));
    }
    return 0.0f;
}

void add_architecture_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.wall";
        shape.kit             = "architecture";
        shape.display_name    = "Wall";
        shape.description     = "Straight wall along X with its faces toward +Z and -Z; origin at the center of its base.";
        shape.tags            = {"obstacle", "wall", "tiles", "size:4x1"};
        shape.parameters      = {
            derive_float(make_length_parameter("length",    "Size along X", 4.0f,  Kit_default_scale::modules,      0.1f,  64.0f), 1.0f,  6.0f,  0.5f),
            derive_float(make_length_parameter("height",    "Size along Y", 1.0f,  Kit_default_scale::wall_heights, 0.1f,  32.0f), 0.75f, 1.25f, 0.05f),
            derive_float(make_length_parameter("thickness", "Size along Z", 0.25f, Kit_default_scale::meters,       0.02f, 4.0f),  0.15f, 0.4f,  0.05f),
            make_seed_parameter()
        };
        shape.material_slots  = {"wall"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 12;
        shape.collision       = Kit_collision::box;
        shape.generator       = generate_wall;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.get_seam_sides  = [](const Kit_parameter_values&) { return all_sides(); };
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float half_t = 0.5f * values.get_float("thickness");
            add_end_connectors(out_connectors, 0.5f * values.get_float("length"));
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
        shape.id              = "architecture.door_wall";
        shape.former_ids      = {"architecture.door_wall.v2"};
        shape.kit             = "architecture";
        shape.display_name    = "Wall with door opening";
        shape.description     = "Wall along X with a door opening from the floor at its center in a frame with a lintel; ends and top meet plain walls.";
        shape.tags            = {"obstacle", "wall", "opening", "tiles", "size:4x1"};
        // The ranges alone keep both jambs and the header at least 0.1 m thick; derived openings stay walkable (R2.10).
        shape.parameters      = {
            derive_float(make_length_parameter("length",         "Size along X",            4.0f,  Kit_default_scale::modules,      2.0f,  64.0f), 2.5f,  6.0f,  0.5f),
            derive_float(make_length_parameter("height",         "Size along Y",            1.0f,  Kit_default_scale::wall_heights, 2.5f,  32.0f), 0.85f, 1.25f, 0.05f),
            derive_float(make_length_parameter("thickness",      "Size along Z",            0.25f, Kit_default_scale::meters,       0.02f, 4.0f),  0.15f, 0.4f,  0.05f),
            derive_float(make_length_parameter("opening_width",  "Clear width of the door", 1.2f,  Kit_default_scale::meters,       0.3f,  1.8f),  0.9f,  1.6f,  0.1f),
            derive_float(make_length_parameter("opening_height", "Clear height of the door",2.2f,  Kit_default_scale::meters,       0.5f,  2.4f),  2.0f,  2.4f,  0.05f),
            make_seed_parameter()
        };
        shape.material_slots  = {"wall", "trim"}; // the wall, the door frame
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 572;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.generator       = generate_door_wall;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.get_seam_sides  = [](const Kit_parameter_values&) {
            return std::vector<Kit_side>{Kit_side::x_neg, Kit_side::x_pos, Kit_side::y_pos};
        };
        shape.check_walkability = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            return check_kit_opening(*values.get_shape(), values.get_float("opening_width"), values.get_float("opening_height"), agent);
        };
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float half_t = 0.5f * values.get_float("thickness");
            add_end_connectors(out_connectors, 0.5f * values.get_float("length"));
            add_base_and_top_connectors(out_connectors, values.get_float("height"));
            add_side_connector(out_connectors, "opening_front", glm::vec3{0.0f, 0.0f,  half_t}, glm::vec3{0.0f, 0.0f,  1.0f});
            add_side_connector(out_connectors, "opening_back",  glm::vec3{0.0f, 0.0f, -half_t}, glm::vec3{0.0f, 0.0f, -1.0f});
            type_wall_run_connectors(out_connectors);
        };
        set_wall_run_flow(shape, "thickness");
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.pillar";
        shape.former_ids      = {"architecture.pillar.v2"};
        shape.kit             = "architecture";
        shape.display_name    = "Pillar";
        shape.description     = "Upright pillar with a base and a capital, a square pier or a round column, with its origin at the center of its base.";
        shape.tags            = {"obstacle", "pillar", "size:0.5x1"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("profile",  "Cross section",         {"square", "round"}, 0)),
            derive_float(make_length_parameter("width",    "Width or diameter",     0.5f, Kit_default_scale::modules,      0.05f, 16.0f), 0.3f, 1.0f, 0.05f),
            derive_float(make_length_parameter("height",   "Size along Y",          1.0f, Kit_default_scale::wall_heights, 0.1f,  64.0f), 0.5f, 1.5f, 0.05f),
            make_count_parameter ("segments", "Sides of a round pillar", 12, 3, 64),
            make_seed_parameter  ()
        };
        shape.material_slots          = {"trim"};
        shape.normal_style            = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = 45.0f;
        shape.triangle_budget         = 100;
        shape.detail_counts           = {{"segments"}};
        shape.generator               = generate_pillar;
        // The pier's plinth and abacus fill its bounds, so its hull is its box (U54).
        shape.get_collision           = [](const Kit_parameter_values& values) {
            return (values.get_enum("profile") == "square") ? Kit_collision::box : Kit_collision::convex_hull;
        };
        shape.mirror_planes           = std::vector<Kit_mirror_plane>{Kit_mirror_plane::z};
        // The pier's shaft is inset, so only its plinth and abacus fill their sides.
        shape.get_seam_sides          = [](const Kit_parameter_values& values) {
            return (values.get_enum("profile") == "square") ? std::vector<Kit_side>{Kit_side::y_neg, Kit_side::y_pos} : std::vector<Kit_side>{};
        };
        shape.get_connectors          = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            add_base_and_top_connectors(out_connectors, values.get_float("height"));
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.arch";
        shape.former_ids      = {"architecture.arch.v2"};
        shape.kit             = "architecture";
        shape.display_name    = "Arch";
        shape.description     =
            "Free-standing arch ring across X with a half-elliptic opening of the span and rise, standing on its two feet; "
            "its faces point toward +Z and -Z.";
        shape.tags            = {"obstacle", "opening", "arch", "size:2x1"};
        shape.parameters      = {
            // The narrowest derived opening (span 2 modules, rise 0.8 wall heights) is the default's, which is walkable (R2.10).
            derive_float(make_length_parameter("span",      "Clear width of the opening at the base", 2.0f, Kit_default_scale::modules,      0.3f,  32.0f), 2.0f, 4.0f, 0.25f),
            derive_float(make_length_parameter("rise",      "Clear height of the opening",            0.8f, Kit_default_scale::wall_heights, 0.2f,  32.0f), 0.8f, 1.2f, 0.05f),
            derive_float(make_length_parameter("depth",     "Size along Z",                           0.5f, Kit_default_scale::modules,      0.05f, 16.0f), 0.3f, 1.0f, 0.05f),
            derive_float(make_length_parameter("thickness", "Thickness of the ring",                  0.3f, Kit_default_scale::meters,       0.05f, 8.0f),  0.2f, 0.5f, 0.05f),
            make_count_parameter ("segments",  "Segments along the curve",               12, 2, 48),
            make_seed_parameter  ()
        };
        shape.material_slots          = {"trim", "stone"}; // the voussoirs, the keystone and the springers
        shape.normal_style            = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = 45.0f;
        shape.triangle_budget         = 516;
        shape.collision               = Kit_collision::triangle_mesh;
        shape.generator               = generate_arch;
        shape.mirror_planes           = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.check_walkability       = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            const float clear_height = agent.height + kit_opening_margin;
            return check_kit_opening(*values.get_shape(), get_arch_clear_width(values, clear_height), values.get_float("rise"), agent);
        };
        shape.get_connectors          = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float thickness = values.get_float("thickness");
            add_end_connectors(out_connectors, (0.5f * values.get_float("span")) + thickness);
            add_base_and_top_connectors(out_connectors, values.get_float("rise") + thickness);
        };
        shapes.push_back(std::move(shape));
    }
    add_classical_shapes(shapes);
    add_japanese_shapes (shapes);
    add_medieval_shapes (shapes);
    add_roof_shapes     (shapes);
}

} // namespace editor
