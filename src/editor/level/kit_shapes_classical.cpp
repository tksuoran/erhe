#include "level/kit_leaf.hpp"
#include "level/kit_mesh_builder.hpp"
#include "level/kit_shapes.hpp"

#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <string_view>
#include <vector>

namespace editor {

namespace {

enum class Column_capital : unsigned int {
    echinus = 0, // doric: a flaring cushion under a square abacus
    volutes,     // ionic: two scrolls on the sides
    leaves       // corinthian: a bell with two rows of leaves and corner scrolls
};

// What an order sets (doc/editor/level_kit_shapes.md "Column"); lengths in shaft diameters.
class Column_order
{
public:
    std::vector<glm::vec2> base;           // lathe rings (y above the plinth, radius)
    float                  plinth_height;  // 0: no plinth
    float                  plinth_side;
    std::vector<glm::vec2> capital;        // lathe rings (y above the shaft's top, radius beyond the shaft's top radius)
    float                  capital_height; // to the abacus' base
    float                  abacus_side;
    float                  abacus_height;
    float                  flute_depth;    // share of the shaft radius
    float                  fillet;         // share of a flute's angle left flat between grooves; 0 for sharp arrises
    Column_capital         capital_kind;
};

[[nodiscard]] auto get_column_order(const std::string_view order) -> Column_order
{
    // The Attic base of the ionic and corinthian orders: a plinth, a torus, a scotia and a smaller torus.
    const std::vector<glm::vec2> attic{
        {0.0f, 0.66f}, {0.04f, 0.71f}, {0.10f, 0.69f}, {0.13f, 0.61f}, {0.17f, 0.56f}, {0.21f, 0.58f}, {0.24f, 0.61f}, {0.28f, 0.60f}, {0.32f, 0.53f}
    };
    if (order == "ionic") {
        return Column_order{
            .base = attic, .plinth_height = 0.14f, .plinth_side = 1.5f,
            .capital = {{0.0f, 0.005f}, {0.05f, 0.04f}, {0.10f, 0.07f}}, .capital_height = 0.27f,
            .abacus_side = 1.15f, .abacus_height = 0.08f, .flute_depth = 0.08f, .fillet = 0.2f, .capital_kind = Column_capital::volutes
        };
    }
    if (order == "corinthian") {
        return Column_order{
            .base = attic, .plinth_height = 0.14f, .plinth_side = 1.5f,
            .capital = {{0.0f, 0.005f}, {0.35f, 0.02f}, {0.75f, 0.06f}, {1.0f, 0.14f}}, .capital_height = 1.0f,
            .abacus_side = 1.45f, .abacus_height = 0.13f, .flute_depth = 0.08f, .fillet = 0.2f, .capital_kind = Column_capital::leaves
        };
    }
    return Column_order{ // doric: a low torus base, annulets under the echinus
        .base = {{0.0f, 0.60f}, {0.03f, 0.63f}, {0.09f, 0.63f}, {0.13f, 0.58f}, {0.16f, 0.53f}}, .plinth_height = 0.0f, .plinth_side = 0.0f,
        .capital = {{0.0f, 0.005f}, {0.03f, 0.02f}, {0.05f, 0.005f}, {0.07f, 0.02f}, {0.12f, 0.07f}, {0.18f, 0.11f}}, .capital_height = 0.18f,
        .abacus_side = 1.26f, .abacus_height = 0.16f, .flute_depth = 0.07f, .fillet = 0.0f, .capital_kind = Column_capital::echinus
    };
}

// Unit-radius section of the shaft as (angle, radius factor): plain sides, or flutes whose rounded grooves run between
// sharp arrises or flat fillets; symmetric about the angle 0, so the shaft is about the plane z = 0.
void make_shaft_section(const int flutes, const int sides, const Column_order& order, std::vector<glm::vec2>& out_section)
{
    out_section.clear();
    if (flutes == 0) {
        for (int i = 0; i < 2 * sides; ++i) {
            out_section.emplace_back(std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(sides), 1.0f);
        }
        return;
    }
    const float span    = 2.0f * std::numbers::pi_v<float> / static_cast<float>(flutes);
    const float fillet  = order.fillet * span;
    const int   edges   = (order.fillet > 0.0f) ? 2 : 1;
    const int   grooves = std::max(2, static_cast<int>(std::lround((2.0f * static_cast<float>(sides)) / static_cast<float>(flutes))) - edges);
    for (int k = 0; k < flutes; ++k) {
        const float center = span * static_cast<float>(k);
        if (edges == 2) {
            out_section.emplace_back(center - (0.5f * fillet), 1.0f);
            out_section.emplace_back(center + (0.5f * fillet), 1.0f);
        } else {
            out_section.emplace_back(center, 1.0f);
        }
        for (int j = 1; j <= grooves; ++j) {
            const float s = static_cast<float>(j) / static_cast<float>(grooves + 1);
            out_section.emplace_back(center + (0.5f * fillet) + ((span - fillet) * s), 1.0f - (order.flute_depth * std::sin(std::numbers::pi_v<float> * s)));
        }
    }
}

// A lathe of the section through rings (y, radius), closed by fans from the axis.
void add_section_lathe(Kit_mesh_builder& builder, const std::vector<glm::vec2>& section, const std::vector<glm::vec2>& rings)
{
    const uint32_t n = static_cast<uint32_t>(section.size());
    std::vector<uint32_t> ring_base;
    for (const glm::vec2& ring : rings) {
        ring_base.push_back(static_cast<uint32_t>(builder.get_positions().size()));
        for (const glm::vec2& point : section) {
            const float r = ring.y * point.y;
            builder.add_vertex(glm::vec3{r * std::cos(point.x), ring.x, -r * std::sin(point.x)});
        }
    }
    for (std::size_t r = 0; (r + 1) < ring_base.size(); ++r) {
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t j = (i + 1) % n;
            builder.add_quad(ring_base[r] + i, ring_base[r] + j, ring_base[r + 1] + j, ring_base[r + 1] + i);
        }
    }
    const uint32_t bottom_center = builder.add_vertex(glm::vec3{0.0f, rings.front().x, 0.0f});
    const uint32_t top_center    = builder.add_vertex(glm::vec3{0.0f, rings.back().x, 0.0f});
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t j = (i + 1) % n;
        builder.add_triangle(bottom_center, ring_base.front() + j, ring_base.front() + i);
        builder.add_triangle(ring_base.back() + i, ring_base.back() + j, top_center);
    }
}

// A round lathe of rings (y, radius) in quads, so it is its own mirror image across z = 0.
void add_round_lathe(Kit_mesh_builder& builder, const int sides, const std::vector<glm::vec2>& rings)
{
    std::vector<glm::vec2> section;
    for (int i = 0; i < sides; ++i) {
        section.emplace_back(2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(sides), 1.0f);
    }
    add_section_lathe(builder, section, rings);
}

// A cylinder from a to b; u and v span its section, (u, v, b - a) right-handed.
void add_cylinder(Kit_mesh_builder& builder, const glm::vec3& a, const glm::vec3& b, const glm::vec3& u, const glm::vec3& v, const float radius, const int sides)
{
    std::vector<uint32_t> start;
    std::vector<uint32_t> end;
    for (int i = 0; i < sides; ++i) {
        const float     angle  = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(sides);
        const glm::vec3 offset = radius * ((std::cos(angle) * u) + (std::sin(angle) * v));
        start.push_back(builder.add_vertex(a + offset));
        end  .push_back(builder.add_vertex(b + offset));
    }
    const std::vector<uint32_t> start_cap{start.rbegin(), start.rend()};
    builder.add_polygon(end);
    builder.add_polygon(start_cap);
    for (std::size_t i = 0; i < end.size(); ++i) {
        const std::size_t j = (i + 1) % end.size();
        builder.add_quad(end[i], start[i], start[j], end[j]);
    }
}

void generate_column(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Column_order order  = get_column_order(values.get_enum("order"));
    const float        height = values.get_float("height");
    const float        d      = values.get_float("width");
    const float        taper  = values.get_float("taper");
    const int          flutes = values.get_int("flutes");
    const int          sides  = values.get_int("segments");

    // Base and capital keep at most half the height, so a short column keeps its shaft.
    const float parts  = (order.plinth_height + order.base.back().x + order.capital_height + order.abacus_height) * d;
    const float squash = std::min(1.0f, (0.5f * height) / parts);
    const float vd     = squash * d; // vertical diameter unit

    Kit_mesh_builder builder;
    if (order.plinth_height > 0.0f) {
        const float half = 0.5f * order.plinth_side * d;
        builder.add_box(glm::vec3{-half, 0.0f, -half}, glm::vec3{half, order.plinth_height * vd, half});
    }
    const float base_y = order.plinth_height * vd;
    std::vector<glm::vec2> rings;
    for (const glm::vec2& ring : order.base) {
        rings.emplace_back((ring.x == 0.0f) ? ((base_y > 0.0f) ? (base_y - (0.02f * vd)) : 0.0f) : (base_y + (ring.x * vd)), ring.y * d);
    }
    add_round_lathe(builder, sides, rings);

    const float abacus_y  = height - (order.abacus_height * vd);
    const float capital_y = abacus_y - (order.capital_height * vd);
    const float shaft_y0  = base_y + (order.base.back().x * vd) - (0.03f * vd);
    const float shaft_y1  = capital_y + (0.02f * vd);
    const float radius    = 0.5f * d;
    const float top_scale = 1.0f - taper;
    std::vector<glm::vec2> section;
    make_shaft_section(flutes, sides, order, section);
    // Entasis: the shaft keeps its width to a third of its height, then tapers.
    const float third = shaft_y0 + ((shaft_y1 - shaft_y0) / 3.0f);
    add_section_lathe(builder, section, {{shaft_y0, radius}, {third, radius}, {shaft_y1, radius * top_scale}});

    const float top_radius = radius * top_scale;
    rings.clear();
    for (const glm::vec2& ring : order.capital) {
        rings.emplace_back(capital_y + (ring.x * vd), top_radius + (ring.y * d));
    }
    rings.back().x = abacus_y + (0.01f * vd);
    add_round_lathe(builder, sides, rings);
    const float abacus_half = 0.5f * order.abacus_side * d;
    builder.add_box(glm::vec3{-abacus_half, abacus_y, -abacus_half}, glm::vec3{abacus_half, height, abacus_half});

    if (order.capital_kind == Column_capital::volutes) {
        // Two scrolls along Z on the sides, each with an eye standing out of its faces, and the channel between them.
        const float scroll  = 0.17f * vd; // squashed with the capital, so a short column's scrolls stay above its base
        const float x       = 0.5f * d;
        const float y       = abacus_y - scroll;
        const float z       = 0.42f * d;
        const float eye     = 0.06f * vd;
        builder.add_box(glm::vec3{-x, y, -z}, glm::vec3{x, abacus_y + (0.01f * vd), z});
        for (const float side : {-1.0f, 1.0f}) {
            const glm::vec3 center{side * x, y, 0.0f};
            add_cylinder(builder, center - glm::vec3{0.0f, 0.0f, z}, center + glm::vec3{0.0f, 0.0f, z}, glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}, scroll, sides);
            add_cylinder(builder, center - glm::vec3{0.0f, 0.0f, z + (0.03f * d)}, center + glm::vec3{0.0f, 0.0f, z + (0.03f * d)}, glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}, eye, 8);
        }
    } else if (order.capital_kind == Column_capital::leaves) {
        // Two rows of eight leaves rising from the bell and curling out, and four scrolls reaching for the abacus' corners.
        GEO::Mesh scratch{3, true};
        const float bell = abacus_y - capital_y;
        class Leaf_row
        {
        public:
            int              count;
            float            phase;  // turns of an eighth
            float            start;  // share of the bell's height
            float            rise;
            float            out;    // diameters the tip curls out
            float            width;  // diameters
            Kit_leaf_section section;
        };
        const std::array<Leaf_row, 3> leaf_rows{
            Leaf_row{8, 0.0f, 0.02f, 0.45f, 0.16f, 0.34f, Kit_leaf_section::flat},
            Leaf_row{8, 0.5f, 0.05f, 0.75f, 0.20f, 0.30f, Kit_leaf_section::flat},
            Leaf_row{4, 1.0f, 0.50f, 0.48f, 0.42f, 0.09f, Kit_leaf_section::thick} // the scrolls under the abacus' corners
        };
        for (const Leaf_row& row : leaf_rows) {
            for (int k = 0; k < row.count; ++k) {
                const float     angle   = (2.0f * std::numbers::pi_v<float> * ((static_cast<float>(k) * (8.0f / static_cast<float>(row.count))) + row.phase)) / 8.0f;
                const glm::vec3 outward{std::cos(angle), 0.0f, -std::sin(angle)};
                const float     rise    = row.rise * bell;
                const glm::vec3 base    = ((top_radius + (0.01f * d)) * outward) + glm::vec3{0.0f, capital_y + (row.start * bell), 0.0f};
                Kit_leaf leaf;
                leaf.spine = {
                    base,
                    base + glm::vec3{0.0f, 0.45f * rise, 0.0f} + ((0.05f * d) * outward),
                    base + glm::vec3{0.0f, 0.95f * rise, 0.0f} + ((0.4f * row.out * d) * outward),
                    base + glm::vec3{0.0f, 0.82f * rise, 0.0f} + ((row.out * d) * outward)
                };
                leaf.section    = row.section;
                leaf.width      = row.width * d;
                leaf.thickness  = 0.025f * d;
                leaf.widest_at  = 0.45f;
                leaf.base_width = 0.5f;
                leaf.steps      = 3;
                add_leaf(builder, leaf, scratch);
            }
        }
    }
    builder.write(mesh);
}

// One horizontal band of the entablature's profile: its top and its half depth.
class Entablature_band
{
public:
    float top;
    float half;
};

// Bands bottom to top: two fasciae and the taenia of the architrave, the frieze, the bed moulding, corona and cyma.
void make_entablature_bands(const Kit_parameter_values& values, std::vector<Entablature_band>& out_bands)
{
    const float h = values.get_float("height");
    const float a = 0.5f * values.get_float("depth");
    const float c = values.get_float("cornice");
    out_bands = {
        {0.15f * h, a - 0.025f},
        {0.30f * h, a},
        {0.35f * h, a + 0.02f},
        {0.73f * h, a - 0.04f},
        {0.79f * h, a + 0.02f + (0.3f * c)},
        {0.92f * h, a + 0.04f + c},
        {h,         a + 0.025f + (0.8f * c)}
    };
}

constexpr std::size_t entablature_taenia    = 2;
constexpr std::size_t entablature_frieze    = 3;
constexpr int         entablature_max_bays  = 16; // keeps the triglyphs within the kit's ceiling at every length
constexpr float       entablature_relief    = 0.055f; // from the recessed frieze to just short of the taenia, so no bar corner meets its outline

void generate_entablature(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float half_length = 0.5f * values.get_float("length");
    std::vector<Entablature_band> bands;
    make_entablature_bands(values, bands);

    // The outline of the +Z side walking up, mirrored for -Z; each point at both end planes.
    class Outline_point
    {
    public:
        float    y;
        float    z;
        uint32_t start;
        uint32_t end;
    };
    Kit_mesh_builder builder;
    std::vector<Outline_point> front;
    std::vector<Outline_point> back;
    const auto add_point = [&](std::vector<Outline_point>& side, const float y, const float z) {
        side.push_back(Outline_point{
            .y     = y,
            .z     = z,
            .start = builder.add_vertex(glm::vec3{-half_length, y, z}),
            .end   = builder.add_vertex(glm::vec3{ half_length, y, z})
        });
    };
    float bottom = 0.0f;
    for (std::size_t k = 0; k < bands.size(); ++k) {
        for (const float sign : {1.0f, -1.0f}) {
            std::vector<Outline_point>& side = (sign > 0.0f) ? front : back;
            add_point(side, bottom, sign * bands[k].half);
            add_point(side, bands[k].top, sign * bands[k].half);
        }
        bottom = bands[k].top;
    }
    for (std::size_t i = 0; (i + 1) < front.size(); ++i) {
        builder.add_quad(front[i].start, front[i].end, front[i + 1].end, front[i + 1].start);
        builder.add_quad(back[i].end, back[i].start, back[i + 1].start, back[i + 1].end);
    }
    builder.add_quad(back.front().start, back.front().end, front.front().end, front.front().start);  // soffit
    builder.add_quad(front.back().start, front.back().end, back.back().end, back.back().start);      // top

    // End caps: each band's end rectangle, split where a narrower neighbouring band's corners lie on its bottom or top
    // line into a middle quad and two side pieces, so the caps are their own mirror images.
    std::vector<uint32_t> start_cap;
    std::vector<uint32_t> end_cap;
    // The corners face -X in the given order: the start cap; the end cap takes them in reverse.
    const auto add_cap = [&](const std::span<const Outline_point> corners) {
        start_cap.clear();
        end_cap.clear();
        for (const Outline_point& p : corners) {
            start_cap.push_back(p.start);
        }
        for (std::span<const Outline_point>::reverse_iterator it = corners.rbegin(); it != corners.rend(); ++it) {
            end_cap.push_back(it->end);
        }
        builder.add_polygon(start_cap);
        builder.add_polygon(end_cap);
    };
    std::vector<Outline_point> piece;
    for (std::size_t k = 0; k < bands.size(); ++k) {
        const std::size_t bottom_corner = 2 * k;
        const std::size_t top_corner    = (2 * k) + 1;
        const bool        inner_below   = (k > 0) && (front[bottom_corner - 1].z < front[bottom_corner].z);
        const bool        inner_above   = ((k + 1) < bands.size()) && (front[top_corner + 1].z < front[top_corner].z);
        // The middle piece's corners: the inner points where the neighbour is narrower, else the band's own corners.
        const std::size_t middle_bottom = inner_below ? (bottom_corner - 1) : bottom_corner;
        const std::size_t middle_top    = inner_above ? (top_corner + 1) : top_corner;
        piece = {back[middle_bottom], front[middle_bottom], front[middle_top], back[middle_top]};
        add_cap(piece);
        if (inner_below || inner_above) {
            // Counter-clockwise seen from -X: bottom from -Z to +Z, then top from +Z to -Z.
            piece.clear();
            piece.push_back(front[middle_bottom]);
            if (inner_below) {
                piece.push_back(front[bottom_corner]);
            }
            piece.push_back(front[top_corner]);
            if (inner_above) {
                piece.push_back(front[middle_top]);
            }
            add_cap(piece);
            piece.clear();
            if (inner_below) {
                piece.push_back(back[bottom_corner]);
            }
            piece.push_back(back[middle_bottom]);
            if (inner_above) {
                piece.push_back(back[middle_top]);
            }
            piece.push_back(back[top_corner]);
            add_cap(piece);
        }
    }

    if (values.get_enum("frieze") == "triglyph") {
        // Triglyphs over every bay's ends and centers, half ones at the piece's ends so joined runs make whole ones; each
        // is three bars with the glyphs between, and a regula under the taenia.
        const float frieze_bottom = bands[entablature_frieze - 1].top;
        const float frieze_top    = bands[entablature_frieze].top;
        const float frieze_height = frieze_top - frieze_bottom;
        const float frieze_half   = bands[entablature_frieze].half;
        const float regula_half   = bands[entablature_taenia - 1].half;
        const float taenia_bottom = bands[entablature_taenia - 1].top;
        const int   bays          = std::clamp(static_cast<int>(std::lround((2.0f * half_length) / (1.6f * frieze_height))), 1, entablature_max_bays);
        const float pitch         = (2.0f * half_length) / static_cast<float>(bays);
        const float unit          = std::min(0.6f * frieze_height, 0.4f * pitch) / 5.0f; // a bar or glyph's width
        for (int i = 0; i <= bays; ++i) {
            const float center = (static_cast<float>(i) - (0.5f * static_cast<float>(bays))) * pitch;
            for (const float side : {1.0f, -1.0f}) {
                const float z0 = side * (frieze_half - 0.01f);
                const float z1 = side * (frieze_half + entablature_relief);
                for (const float bar : {-2.0f, 0.0f, 2.0f}) {
                    const float x0 = std::max(-half_length, center + ((bar - 0.5f) * unit));
                    const float x1 = std::min( half_length, center + ((bar + 0.5f) * unit));
                    if (x1 > x0) {
                        builder.add_box(glm::vec3{x0, frieze_bottom, std::min(z0, z1)}, glm::vec3{x1, frieze_top, std::max(z0, z1)});
                    }
                }
                const float x0 = std::max(-half_length, center - (2.5f * unit));
                const float x1 = std::min( half_length, center + (2.5f * unit));
                const float r0 = side * (regula_half - 0.01f);
                const float r1 = side * (regula_half + 0.018f);
                builder.add_box(glm::vec3{x0, taenia_bottom - (0.1f * frieze_height), std::min(r0, r1)}, glm::vec3{x1, taenia_bottom, std::max(r0, r1)});
            }
        }
    }
    builder.write(mesh);
}

} // anonymous namespace

void add_classical_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.column";
        shape.kit             = "architecture";
        shape.display_name    = "Column";
        shape.description     =
            "Classical column standing at its origin: a base (a torus for doric, a plinth and an Attic base for ionic and "
            "corinthian), a fluted shaft with entasis and taper, and a capital (an echinus, two volutes, or a bell of leaves) "
            "under a square abacus whose top is the column's height.";
        shape.tags            = {"obstacle", "pillar", "classical", "size:0.5x1"};
        shape.parameters      = {
            derive_enum (make_enum_parameter  ("order",  "Classical order: base and capital", {"doric", "ionic", "corinthian"}, 0)),
            derive_float(make_length_parameter("height", "Size along Y, to the top of the abacus", 1.0f, Kit_default_scale::wall_heights, 0.5f, 24.0f), 0.8f, 1.6f, 0.05f),
            derive_float(make_length_parameter("width",  "Diameter of the shaft at its foot", 0.5f, Kit_default_scale::modules, 0.1f, 4.0f), 0.3f, 0.9f, 0.05f),
            derive_int  (make_count_parameter ("flutes", "Flutes around the shaft; 0 for a plain shaft", 20, 0, 24), 12, 24),
            derive_float(make_ratio_parameter ("taper",  "How much narrower the shaft's top is than its foot", 0.15f), 0.0f, 0.2f, 0.01f),
            make_seed_parameter  (),
            make_count_parameter ("segments", "Sides of the base and the capital", 16, 8, 20)
        };
        shape.parameters[4].float_max = 0.3f;
        shape.material_slots          = {"trim"};
        shape.normal_style            = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = 50.0f;
        shape.triangle_budget         = 760;
        shape.generator               = generate_column;
        shape.get_connectors          = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            add_base_and_top_connectors(out_connectors, values.get_float("height"));
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "architecture.entablature";
        shape.kit             = "architecture";
        shape.display_name    = "Entablature";
        shape.description     =
            "Classical beam run along X resting on column tops: an architrave of two fasciae and a taenia, a frieze of "
            "triglyphs or plain, and a projecting cornice; half triglyphs at its ends make whole ones where runs join.";
        shape.tags            = {"obstacle", "beam", "classical", "tiles", "size:2x0.25"};
        shape.parameters      = {
            derive_float(make_length_parameter("length",  "Size along X", 2.0f, Kit_default_scale::modules, 0.5f, 8.0f), 1.0f, 4.0f, 0.5f),
            derive_float(make_length_parameter("height",  "Size along Y below the cornice's projection", 0.25f, Kit_default_scale::wall_heights, 0.2f, 2.5f), 0.2f, 0.35f, 0.01f),
            derive_float(make_length_parameter("depth",   "Size along Z of the architrave", 0.5f, Kit_default_scale::modules, 0.2f, 3.0f), 0.3f, 0.9f, 0.05f),
            derive_float(make_length_parameter("cornice", "How far the cornice projects beyond the architrave", 0.15f, Kit_default_scale::meters, 0.0f, 0.3f), 0.05f, 0.3f, 0.01f),
            derive_enum (make_enum_parameter  ("frieze",  "Frieze: triglyphs (doric) or plain", {"triglyph", "plain"}, 0)),
            make_seed_parameter()
        };
        shape.material_slots  = {"trim"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 560;
        // A triangle mesh: a hull would fill the space under the cornice and before the recessed frieze.
        shape.collision       = Kit_collision::triangle_mesh;
        shape.generator       = generate_entablature;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float half_length = 0.5f * values.get_float("length");
            add_base_and_top_connectors(out_connectors, values.get_float("height"));
            add_side_connector(out_connectors, "start", glm::vec3{-half_length, 0.0f, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "end",   glm::vec3{ half_length, 0.0f, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
        };
        shapes.push_back(std::move(shape));
    }
}

} // namespace editor
