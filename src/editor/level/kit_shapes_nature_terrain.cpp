#include "level/kit_mesh_builder.hpp"
#include "level/kit_shapes.hpp"

#include "erhe_geometry/geometry.hpp"

#include <fmt/format.h>
#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string_view>
#include <vector>

namespace editor {

namespace {

constexpr uint32_t cliff_rock_slot   = 0;
constexpr uint32_t cliff_grass_slot  = 1;     // the walkable top
constexpr float    cliff_cell_x      = 0.35f;
constexpr float    cliff_cell_y      = 0.4f;
constexpr int      cliff_max_columns = 28;    // with cliff_max_rows the cliff stays within the kit's ceiling at every value
constexpr int      cliff_max_rows    = 28;    // step rows included
constexpr int      cliff_max_bands   = 8;
constexpr float    cliff_step_rise   = 0.06f; // height of a band's or a ledge's shelf edge
constexpr float    cliff_ledge_depth = 0.4f;  // each ledge sets the face above it back by this
constexpr float    cliff_min_top     = 1.5f;  // the setbacks leave at least this much of the top
constexpr float    cliff_noise       = 0.1f;  // per-vertex face jitter at roughness 1, meters
constexpr float    cliff_rim_jitter  = 0.4f;  // the most the top's front rim wanders from its nominal line

constexpr float shore_cell    = 0.5f;
constexpr int   shore_max_cells = 30; // keeps the shore within the kit's ceiling at every value
constexpr float shore_lip     = 0.1f; // the tile's height at its front edge
constexpr float shore_wave    = 3.0f; // shortest wavelength of the undulation, meters

[[nodiscard]] auto cell_count(const float length, const float cell, const int max_count) -> int
{
    return std::clamp(static_cast<int>(std::lround(length / cell)), 2, max_count);
}

// One row of the cliff face: its height and what the rows at and below it have passed. Rows depend on the values alone.
class Cliff_row
{
public:
    float y       {0.0f};
    int   band    {0};     // the strata band the row lies in
    int   setbacks{0};     // ledges below the row
    bool  fixed   {false}; // the base, the top and the rows of a shelf take no height jitter
};

// The face's rows and its nominal line: batter (overhang), the talus foot and the ledge setbacks; the setbacks are
// scaled down together so the top keeps cliff_min_top of depth (R2.10). Seed independent, so equal values give equal
// end profiles and segments join in a run.
class Cliff_profile
{
public:
    explicit Cliff_profile(const Kit_parameter_values& values)
        : m_height{values.get_float("height")}
    {
        const float            depth  = values.get_float("depth");
        const std::string_view strata = values.get_enum("strata");
        // Shelves: each ledge, and for layered or blocky strata the band boundaries, about every 1 or 1.6 m.
        const float band_height = (strata == "layered") ? 1.0f : ((strata == "blocky") ? 1.6f : 0.0f);
        const int   bands       = (band_height > 0.0f) ? std::clamp(static_cast<int>(m_height / band_height) - 1, 0, cliff_max_bands) : 0;
        const int   base_rows   = std::clamp(static_cast<int>(std::lround(m_height / cliff_cell_y)), 2, cliff_max_rows / 2);
        int         ledges      = std::min(values.get_int("ledges"), (base_rows - 2) / 2);
        class Shelf
        {
        public:
            float y;
            bool  ledge;
        };
        std::vector<Shelf> shelves;
        for (int l = 0; l < ledges; ++l) {
            shelves.push_back(Shelf{.y = m_height * static_cast<float>(l + 1) / static_cast<float>(ledges + 1), .ledge = true});
        }
        for (int k = 0; k < bands; ++k) {
            shelves.push_back(Shelf{.y = m_height * static_cast<float>(k + 1) / static_cast<float>(bands + 1), .ledge = false});
        }
        std::sort(shelves.begin(), shelves.end(), [](const Shelf& lhs, const Shelf& rhs) { return lhs.y < rhs.y; });

        // A shelf takes the nearest free interior base row and adds a row just above it.
        const float cell = m_height / static_cast<float>(base_rows);
        std::vector<int> shelf_row(static_cast<std::size_t>(base_rows + 1), -1);
        int added = 0;
        for (std::size_t k = 0; k < shelves.size(); ++k) {
            const int row = std::clamp(static_cast<int>(std::lround(shelves[k].y / cell)), 1, base_rows - 1);
            if ((shelf_row[static_cast<std::size_t>(row)] >= 0) || ((base_rows + 1 + added) >= (cliff_max_rows + 1))) {
                continue;
            }
            shelf_row[static_cast<std::size_t>(row)] = static_cast<int>(k);
            ++added;
        }
        int band     = 0;
        int setbacks = 0;
        for (int j = 0; j <= base_rows; ++j) {
            const int shelf = shelf_row[static_cast<std::size_t>(j)];
            const float y   = (j == base_rows) ? m_height : (static_cast<float>(j) * cell);
            m_rows.push_back(Cliff_row{.y = y, .band = band, .setbacks = setbacks, .fixed = (j == 0) || (j == base_rows) || (shelf >= 0)});
            if (shelf >= 0) {
                ++band;
                if (shelves[static_cast<std::size_t>(shelf)].ledge) {
                    ++setbacks;
                }
                m_rows.push_back(Cliff_row{.y = y + std::min(cliff_step_rise, 0.25f * cell), .band = band, .setbacks = setbacks, .fixed = true});
            }
        }
        m_band_count = band + 1;

        m_overhang = values.get_float("overhang") * m_height;
        m_ledge    = cliff_ledge_depth;
        const float setback = std::max(0.0f, -m_overhang) + (static_cast<float>(setbacks) * m_ledge);
        const float allowed = std::max(0.0f, depth - cliff_min_top);
        if (setback > allowed) {
            const float scale = allowed / setback;
            m_overhang = (std::min(m_overhang, 0.0f) * scale) + std::max(m_overhang, 0.0f);
            m_ledge   *= scale;
        }
        m_talus = std::clamp(0.08f * m_height, 0.2f, 1.0f);
    }

    [[nodiscard]] auto rows      () const -> const std::vector<Cliff_row>& { return m_rows; }
    [[nodiscard]] auto band_count() const -> int { return m_band_count; }

    // Offset of the face from the plane z = depth / 2 at the row: the batter, the talus foot and the setbacks.
    [[nodiscard]] auto offset(const Cliff_row& row) const -> float
    {
        const float foot = std::max(0.0f, 1.0f - (row.y / (0.25f * m_height)));
        return (m_overhang * row.y / m_height) + (m_talus * foot * foot) - (static_cast<float>(row.setbacks) * m_ledge);
    }

    [[nodiscard]] auto top_offset() const -> float
    {
        return offset(m_rows.back());
    }

private:
    float                  m_height    {0.0f};
    float                  m_overhang  {0.0f};
    float                  m_ledge     {0.0f};
    float                  m_talus     {0.0f};
    int                    m_band_count{1};
    std::vector<Cliff_row> m_rows;
};

// The z of the walkable top's center: halfway between the back and the rim's nominal line.
[[nodiscard]] auto cliff_top_center_z(const Kit_parameter_values& values) -> float
{
    const Cliff_profile profile{values};
    const float depth = values.get_float("depth");
    return 0.5f * ((-0.5f * depth) + ((0.5f * depth) + profile.top_offset()));
}

void generate_cliff(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float length    = values.get_float("length");
    const float height    = values.get_float("height");
    const float depth     = values.get_float("depth");
    const float roughness = values.get_float("roughness");
    const std::string_view strata = values.get_enum("strata");
    const Cliff_profile profile{values};
    const std::vector<Cliff_row>& rows = profile.rows();
    const int   row_count = static_cast<int>(rows.size()) - 1;
    const int   columns   = cell_count(length, cliff_cell_x, cliff_max_columns);
    const float cell_x    = length / static_cast<float>(columns);
    const float back_z    = -0.5f * depth;
    // Relief: tens of centimeters to about a meter at human scale, more for a deeper cliff.
    const float relief    = roughness * std::clamp(0.45f * depth, 0.4f, 1.2f);

    Kit_random random{values.get_seed(), "cliff face"};
    // Bands step in and out (layered), blocks of three columns per band do (blocky), bulges swell (massive).
    std::vector<float> band_offset(static_cast<std::size_t>(profile.band_count()));
    for (float& offset : band_offset) {
        offset = random.next_range(-1.0f, 1.0f);
    }
    const int block_columns = (columns / 3) + 1;
    std::vector<float> block_offset(static_cast<std::size_t>(profile.band_count() * block_columns));
    for (float& offset : block_offset) {
        offset = random.next_range(-1.0f, 1.0f);
    }
    class Bulge
    {
    public:
        float x;
        float y;
        float radius;
        float amount;
    };
    std::vector<Bulge> bulges(static_cast<std::size_t>(4 + (columns / 2) + static_cast<int>(height / 3.0f)));
    for (Bulge& bulge : bulges) {
        bulge = Bulge{
            .x      = random.next_range(-0.5f * length, 0.5f * length),
            .y      = random.next_range(0.0f, height),
            .radius = random.next_range(0.7f, 2.2f),
            .amount = random.next_range(-1.0f, 1.0f)
        };
    }
    // Vertical features per column: a crack is one column cut in, a buttress two columns standing out.
    std::vector<float> column_feature(static_cast<std::size_t>(columns + 1), 0.0f);
    for (int i = 2; i + 2 <= columns; ++i) {
        const float pick = random.next_unit();
        if (pick < 0.12f) {
            column_feature[static_cast<std::size_t>(i)] = -1.0f;
        } else if ((pick < 0.22f) && (i + 3 <= columns)) {
            column_feature[static_cast<std::size_t>(i)]     = 0.7f;
            column_feature[static_cast<std::size_t>(i + 1)] = 0.7f;
            ++i;
        }
    }
    const float bulge_weight = (strata == "massive") ? 1.5f : 0.5f;

    Kit_mesh_builder builder;
    builder.set_slot(cliff_rock_slot);
    std::vector<uint32_t> front(static_cast<std::size_t>((columns + 1) * (row_count + 1)));
    std::vector<float>    front_x(front.size());
    const auto index = [columns](const int i, const int j) -> std::size_t {
        return static_cast<std::size_t>((j * (columns + 1)) + i);
    };
    for (int j = 0; j <= row_count; ++j) {
        const Cliff_row& row = rows[static_cast<std::size_t>(j)];
        for (int i = 0; i <= columns; ++i) {
            const bool  end_column = (i == 0) || (i == columns);
            const float fade       = end_column ? 0.0f : (((i == 1) || (i == columns - 1)) ? 0.5f : 1.0f);
            const float x = (-0.5f * length) + (cell_x * static_cast<float>(i)) + (fade * 0.25f * cell_x * roughness * random.next_range(-1.0f, 1.0f));
            const float y = row.fixed ? row.y : (row.y + (fade * 0.2f * cliff_cell_y * roughness * random.next_range(-1.0f, 1.0f)));
            float bulge_sum = 0.0f;
            for (const Bulge& bulge : bulges) {
                const float dx = x - bulge.x;
                const float dy = y - bulge.y;
                bulge_sum += bulge.amount * std::exp(-((dx * dx) + (dy * dy)) / (bulge.radius * bulge.radius));
            }
            float shared = 0.0f;
            if (strata == "layered") {
                shared = band_offset[static_cast<std::size_t>(row.band)];
            } else if (strata == "blocky") {
                shared = block_offset[static_cast<std::size_t>((row.band * block_columns) + (i / 3))];
            }
            const float noise  = roughness * cliff_noise * random.next_range(-1.0f, 1.0f);
            const float offset = fade * ((relief * (std::clamp(bulge_weight * bulge_sum, -1.2f, 1.2f) + (0.6f * shared) + (0.8f * column_feature[static_cast<std::size_t>(i)]))) + noise);
            // The rim of the walkable top wanders within cliff_rim_jitter of its nominal line (R2.10).
            const float limited = (j == row_count) ? std::clamp(offset, -cliff_rim_jitter, cliff_rim_jitter) : offset;
            const float z       = std::max(back_z + (0.5f * cliff_min_top), (0.5f * depth) + profile.offset(row) + limited);
            front[index(i, j)]   = builder.add_vertex(glm::vec3{x, y, z});
            front_x[index(i, j)] = x;
        }
    }
    std::vector<uint32_t> back_bottom(static_cast<std::size_t>(columns + 1));
    std::vector<uint32_t> back_top   (static_cast<std::size_t>(columns + 1));
    for (int i = 0; i <= columns; ++i) {
        back_bottom[static_cast<std::size_t>(i)] = builder.add_vertex(glm::vec3{front_x[index(i, 0)],         0.0f,   back_z});
        back_top   [static_cast<std::size_t>(i)] = builder.add_vertex(glm::vec3{front_x[index(i, row_count)], height, back_z});
    }

    // The face: two triangles per cell, the diagonal alternating.
    for (int j = 0; j < row_count; ++j) {
        for (int i = 0; i < columns; ++i) {
            const uint32_t a = front[index(i,     j)];
            const uint32_t b = front[index(i + 1, j)];
            const uint32_t c = front[index(i + 1, j + 1)];
            const uint32_t d = front[index(i,     j + 1)];
            if (((i + j) % 2) == 0) {
                builder.add_triangle(a, b, c);
                builder.add_triangle(a, c, d);
            } else {
                builder.add_triangle(a, b, d);
                builder.add_triangle(b, c, d);
            }
        }
    }
    for (int i = 0; i < columns; ++i) {
        const std::size_t s = static_cast<std::size_t>(i);
        builder.add_quad(back_bottom[s], back_top[s], back_top[s + 1], back_bottom[s + 1]);                          // back
        builder.add_quad(back_bottom[s], back_bottom[s + 1], front[index(i + 1, 0)], front[index(i, 0)]);           // bottom
    }
    // The ends: fans from the back bottom corner, which sees the whole end outline.
    const uint32_t left_corner = back_bottom.front();
    for (int j = 0; j < row_count; ++j) {
        builder.add_triangle(left_corner, front[index(0, j)], front[index(0, j + 1)]);
    }
    builder.add_triangle(left_corner, front[index(0, row_count)], back_top.front());
    const uint32_t right_corner = back_bottom.back();
    builder.add_triangle(right_corner, back_top.back(), front[index(columns, row_count)]);
    for (int j = row_count; j > 0; --j) {
        builder.add_triangle(right_corner, front[index(columns, j)], front[index(columns, j - 1)]);
    }
    builder.set_slot(cliff_grass_slot);
    for (int i = 0; i < columns; ++i) {
        const std::size_t s = static_cast<std::size_t>(i);
        builder.add_quad(back_top[s], front[index(i, row_count)], front[index(i + 1, row_count)], back_top[s + 1]);
    }
    builder.write(mesh);
}

// Nominal height of the shore's sand at s, 0 at the back edge and 1 at the front edge: the berm, then an S curve that
// is steepest halfway down and flattens toward the front lip.
[[nodiscard]] auto shore_profile(const float s, const float berm, const float rise) -> float
{
    if (s <= berm) {
        return shore_lip + rise;
    }
    if (s >= 1.0f) {
        return shore_lip;
    }
    const float q = (s - berm) / (1.0f - berm);
    return shore_lip + (rise * (1.0f - (q * q * (3.0f - (2.0f * q)))));
}

// The steepest slope of the sand, in degrees: the S curve's middle and the undulation's steepest wave.
[[nodiscard]] auto shore_steepest_degrees(const Kit_parameter_values& values) -> float
{
    const float slope_run = (1.0f - values.get_float("berm")) * values.get_float("run");
    const float gradient  =
        (1.5f * values.get_float("rise") / slope_run) +
        (2.0f * std::numbers::pi_v<float> * values.get_float("undulation") / shore_wave);
    return glm::degrees(std::atan(gradient));
}

void generate_shore(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float length     = values.get_float("length");
    const float run        = values.get_float("run");
    const float rise       = values.get_float("rise");
    const float undulation = values.get_float("undulation");
    const float berm       = values.get_float("berm");
    const int   columns    = cell_count(length, shore_cell, shore_max_cells);
    const int   rows       = cell_count(run, shore_cell, shore_max_cells);

    // Two crossing waves of seeded heading and phase, faded to zero at the four edges, so tiles meet with equal ends.
    Kit_random random{values.get_seed(), "shore undulation"};
    const float pi = std::numbers::pi_v<float>;
    std::array<glm::vec3, 2> waves{}; // direction x, direction z, phase
    std::array<float, 2>     wave_numbers{};
    for (std::size_t w = 0; w < waves.size(); ++w) {
        const float heading = random.next_range(0.0f, pi);
        waves[w]        = glm::vec3{std::cos(heading), std::sin(heading), random.next_range(0.0f, 2.0f * pi)};
        wave_numbers[w] = 2.0f * pi / (shore_wave * (1.0f + random.next_range(0.0f, 1.0f)));
    }

    Kit_mesh_builder builder;
    std::vector<uint32_t> top(static_cast<std::size_t>((columns + 1) * (rows + 1)));
    const auto index = [columns](const int i, const int k) -> std::size_t {
        return static_cast<std::size_t>((k * (columns + 1)) + i);
    };
    for (int k = 0; k <= rows; ++k) {
        const float s = static_cast<float>(k) / static_cast<float>(rows);
        const float z = (-0.5f * run) + (run * s);
        for (int i = 0; i <= columns; ++i) {
            const float u      = static_cast<float>(i) / static_cast<float>(columns);
            const float x      = (-0.5f * length) + (length * u);
            const bool  edge   = (i == 0) || (i == columns) || (k == 0) || (k == rows);
            float       height = shore_profile(s, berm, rise);
            if (!edge) {
                const float window = std::sin(pi * u) * std::sin(pi * s);
                float wave = 0.0f;
                for (std::size_t w = 0; w < waves.size(); ++w) {
                    wave += 0.5f * std::sin((wave_numbers[w] * ((waves[w].x * x) + (waves[w].y * z))) + waves[w].z);
                }
                height = std::max(0.5f * shore_lip, height + (undulation * window * wave));
            }
            top[index(i, k)] = builder.add_vertex(glm::vec3{x, height, z});
        }
    }
    const uint32_t back_left   = builder.add_vertex(glm::vec3{-0.5f * length, 0.0f, -0.5f * run});
    const uint32_t back_right  = builder.add_vertex(glm::vec3{ 0.5f * length, 0.0f, -0.5f * run});
    const uint32_t front_left  = builder.add_vertex(glm::vec3{-0.5f * length, 0.0f,  0.5f * run});
    const uint32_t front_right = builder.add_vertex(glm::vec3{ 0.5f * length, 0.0f,  0.5f * run});

    for (int k = 0; k < rows; ++k) {
        for (int i = 0; i < columns; ++i) {
            const uint32_t a = top[index(i,     k)];
            const uint32_t b = top[index(i,     k + 1)];
            const uint32_t c = top[index(i + 1, k + 1)];
            const uint32_t d = top[index(i + 1, k)];
            if (((i + k) % 2) == 0) {
                builder.add_triangle(a, b, c);
                builder.add_triangle(a, c, d);
            } else {
                builder.add_triangle(a, b, d);
                builder.add_triangle(b, c, d);
            }
        }
    }
    builder.add_quad(back_left, back_right, front_right, front_left); // bottom
    // Back, front and ends: fans from a bottom corner. The sand falls toward the front, so a back corner sees each end's outline.
    for (int i = 0; i < columns; ++i) {
        builder.add_triangle(back_left, top[index(i, 0)], top[index(i + 1, 0)]);
    }
    builder.add_triangle(back_left, top[index(columns, 0)], back_right);
    builder.add_triangle(front_left, front_right, top[index(columns, rows)]);
    for (int i = columns; i > 0; --i) {
        builder.add_triangle(front_left, top[index(i, rows)], top[index(i - 1, rows)]);
    }
    builder.add_triangle(back_left, front_left, top[index(0, rows)]);
    for (int k = rows; k > 0; --k) {
        builder.add_triangle(back_left, top[index(0, k)], top[index(0, k - 1)]);
    }
    for (int k = 0; k < rows; ++k) {
        builder.add_triangle(back_right, top[index(columns, k)], top[index(columns, k + 1)]);
    }
    builder.add_triangle(back_right, top[index(columns, rows)], front_right);
    builder.write(mesh);
}

void generate_water(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float width        = values.get_float("width");
    const float depth        = values.get_float("depth");
    const int   subdivisions = values.get_int("subdivisions");
    Kit_mesh_builder builder;
    std::vector<uint32_t> grid(static_cast<std::size_t>((subdivisions + 1) * (subdivisions + 1)));
    const auto index = [subdivisions](const int i, const int k) -> std::size_t {
        return static_cast<std::size_t>((k * (subdivisions + 1)) + i);
    };
    for (int k = 0; k <= subdivisions; ++k) {
        const float z = depth * ((static_cast<float>(k) / static_cast<float>(subdivisions)) - 0.5f);
        for (int i = 0; i <= subdivisions; ++i) {
            const float x = width * ((static_cast<float>(i) / static_cast<float>(subdivisions)) - 0.5f);
            grid[index(i, k)] = builder.add_vertex(glm::vec3{x, 0.0f, z});
        }
    }
    for (int k = 0; k < subdivisions; ++k) {
        for (int i = 0; i < subdivisions; ++i) {
            builder.add_quad(grid[index(i, k)], grid[index(i, k + 1)], grid[index(i + 1, k + 1)], grid[index(i + 1, k)]);
        }
    }
    builder.write(mesh);
}

// The dune's profile at t along its rings, 0 at the rim and 1 at the crest: eased in at the foot and over the crest,
// straight between; its steepest rate is dune_profile_rate.
constexpr float dune_profile_rate = 1.25f;
constexpr float dune_crest_t      = 0.92f; // the last ring's t: a narrow flat crest
constexpr int   dune_rings        = 9;
constexpr float dune_outline      = 0.06f; // the most the seeded outline scales a ring's radius at an angle
constexpr float dune_horn         = 0.1f;  // the most the crest shifts along X, a share of the half width
constexpr float dune_least_length = 1.04f; // times the width: the stadium keeps straight sides
constexpr float dune_facets       = 0.95f; // the rings' chords and triangles against the smooth surface

[[nodiscard]] auto dune_profile(const float t) -> float
{
    float q = 0.0f;
    if (t < 0.2f) {
        q = (t * t) / 0.4f;
    } else if (t < 0.8f) {
        q = t - 0.1f;
    } else {
        q = 0.7f + (t - 0.8f) - (((t - 0.8f) * (t - 0.8f)) / 0.4f);
    }
    return q / 0.8f;
}

// The steepest slope of the dune in degrees. Each ring is a stadium whose radius shrinks by the half width over t, the
// next one inside it at that distance less the outline and the crest's drift toward the lee and along X.
[[nodiscard]] auto dune_steepest_degrees(const Kit_parameter_values& values) -> float
{
    const float asymmetry = values.get_float("asymmetry");
    const float distance  = 0.5f * values.get_float("width") * ((1.0f - dune_outline) - std::sqrt((asymmetry * asymmetry) + (dune_horn * dune_horn)));
    const float rate      = dune_profile_rate * values.get_float("height") / (dune_facets * distance);
    return glm::degrees(std::atan(rate));
}

void generate_dune(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float length    = values.get_float("length");
    const float width     = values.get_float("width");
    const float height    = values.get_float("height");
    const float asymmetry = values.get_float("asymmetry");
    const int   half      = values.get_int("segments") / 2; // points on each end's half circle
    const float pi        = std::numbers::pi_v<float>;
    const float straight  = 0.5f * (length - width);        // half the stadium's straight sides, at least 0.02 widths (check_combination)

    // The outline's lobes and the crest's drift along X are seeded.
    Kit_random random{values.get_seed(), "dune"};
    const float a2   = random.next_range(-1.0f, 1.0f);
    const float a3   = random.next_range(-1.0f, 1.0f);
    const float p2   = random.next_range(0.0f, 2.0f * pi);
    const float p3   = random.next_range(0.0f, 2.0f * pi);
    const float horn = dune_horn * 0.5f * width * random.next_range(-1.0f, 1.0f);
    const float norm = std::max(1.0e-3f, std::abs(a2) + std::abs(a3));
    std::vector<glm::vec3> points;
    points.reserve(static_cast<std::size_t>(dune_rings * 2 * half));
    for (int k = 0; k < dune_rings; ++k) {
        const float t      = dune_crest_t * static_cast<float>(k) / static_cast<float>(dune_rings - 1);
        const float y      = height * dune_profile(t);
        const float radius = 0.5f * width * (1.0f - t);
        const glm::vec2 center{horn * t, 0.5f * width * asymmetry * t};
        // The +X end's half circle from the front (+Z) to the back, then the -X end's from the back to the front.
        for (int side = 0; side < 2; ++side) {
            for (int j = 0; j < half; ++j) {
                const float angle  = (pi * (static_cast<float>(side) - 0.5f)) + (pi * static_cast<float>(j) / static_cast<float>(half - 1));
                // The outline fades out at the rim and the crest, so the cap strips pair points of equal z.
                const float fade   = std::sin(std::numbers::pi_v<float> * static_cast<float>(k) / static_cast<float>(dune_rings - 1));
                const float factor = 1.0f + (fade * dune_outline * ((a2 * std::sin((2.0f * angle) + p2)) + (a3 * std::sin((3.0f * angle) + p3))) / norm);
                const float end_x  = (side == 0) ? straight : -straight;
                points.emplace_back(center.x + end_x + (factor * radius * std::cos(angle)), y, center.y - (factor * radius * std::sin(angle)));
            }
        }
    }
    Kit_mesh_builder builder;
    builder.add_ring_solid_paired_caps(points, 2 * half);
    builder.write(mesh);
}

constexpr float ground_patch_bevel = 0.97f; // the top ring's share of the outline: a bevelled edge
constexpr float ground_patch_inner = 0.55f; // the dome ring's share

// The patch's outline at an angle: 1 plus harmonics 2, 3 and 5 of seeded amplitudes, scaled to the irregularity.
class Ground_patch_outline
{
public:
    Ground_patch_outline(Kit_random& random, const float irregularity)
        : m_irregularity{irregularity}
    {
        float sum = 0.0f;
        for (std::size_t k = 0; k < m_amplitudes.size(); ++k) {
            m_amplitudes[k] = random.next_range(0.3f, 1.0f) / static_cast<float>(k + 1);
            m_phases[k]     = random.next_range(0.0f, 2.0f * std::numbers::pi_v<float>);
            sum += m_amplitudes[k];
        }
        for (float& amplitude : m_amplitudes) {
            amplitude /= sum;
        }
    }

    [[nodiscard]] auto at(const float angle) const -> float
    {
        constexpr std::array<float, 3> harmonics{2.0f, 3.0f, 4.0f};
        float wave = 0.0f;
        for (std::size_t k = 0; k < harmonics.size(); ++k) {
            wave += m_amplitudes[k] * std::sin((harmonics[k] * angle) + m_phases[k]);
        }
        return 1.0f + (m_irregularity * wave);
    }

private:
    float                m_irregularity{0.0f};
    std::array<float, 3> m_amplitudes{};
    std::array<float, 3> m_phases{};
};

void generate_ground_patch(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float radius    = 0.5f * values.get_float("size");
    const float thickness = values.get_float("thickness");
    const float dome      = values.get_float("dome") * values.get_float("size");
    const int   segments  = values.get_int("segments");
    Kit_random random{values.get_seed(), "ground patch"};
    const Ground_patch_outline outline{random, values.get_float("irregularity")};
    const std::array<glm::vec2, 3> rings{glm::vec2{0.0f, 1.0f}, glm::vec2{thickness, ground_patch_bevel}, glm::vec2{thickness + (0.7f * dome), ground_patch_inner}};
    std::vector<glm::vec3> points;
    points.reserve(rings.size() * static_cast<std::size_t>(segments));
    for (const glm::vec2& ring : rings) {
        for (int i = 0; i < segments; ++i) {
            const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(segments);
            const float r     = radius * ring.y * outline.at(angle);
            points.emplace_back(r * std::cos(angle), ring.x, -r * std::sin(angle));
        }
    }
    Kit_mesh_builder builder;
    builder.add_ring_solid(points, segments, glm::vec3{0.0f}, glm::vec3{0.0f, thickness + dome, 0.0f});
    builder.write(mesh);
}

// The steepest top triangle of the patch in degrees; the bevelled edge, no higher than the thickness, is a step.
[[nodiscard]] auto ground_patch_steepest_degrees(const Kit_parameter_values& values) -> float
{
    GEO::Mesh mesh{3, true};
    generate_ground_patch(values, mesh);
    const float edge     = values.get_float("thickness");
    float       steepest = 0.0f;
    for (GEO::index_t f = 0, end = mesh.facets.nb(); f < end; ++f) {
        std::array<glm::vec3, 3> p{};
        for (GEO::index_t c = 0; c < 3; ++c) {
            const GEO::vec3f q = erhe::geometry::get_pointf(mesh.vertices, mesh.facets.vertex(f, c));
            p[c] = glm::vec3{q.x, q.y, q.z};
        }
        const glm::vec3 normal = glm::cross(p[1] - p[0], p[2] - p[0]);
        if ((normal.y <= 0.0f) || (std::min({p[0].y, p[1].y, p[2].y}) < edge)) {
            continue;
        }
        steepest = std::max(steepest, glm::degrees(std::acos(std::min(1.0f, normal.y / glm::length(normal)))));
    }
    return steepest;
}

} // anonymous namespace

void add_nature_terrain_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_parameter overhang = derive_float(make_ratio_parameter("overhang", "How far the face leans out at the top, a share of the height; negative leans back (a batter)", -0.1f), -0.2f, 0.05f, 0.05f);
        overhang.float_min = -0.25f;
        overhang.float_max = 0.25f;

        Kit_shape_definition shape;
        shape.id              = "nature.cliff";
        shape.kit             = "nature";
        shape.display_name    = "Cliff";
        shape.description     =
            "Cliff segment along X: a rock face toward +Z, battered and with a talus foot, of massive, layered or blocky "
            "strata stepping in and out, with cracks, buttresses and up to three ledges; flat ends, back and base, and a "
            "flat walkable grass top whose rim wanders; segments of equal values join end to end in a run.";
        shape.tags            = {"walkable", "obstacle", "ground", "tiles", "size:4x2x3"};
        shape.parameters      = {
            derive_float(make_length_parameter("length", "Size along X", 4.0f, Kit_default_scale::modules, 1.0f, 64.0f), 2.0f, 16.0f, 0.5f),
            from_seed_by_default(derive_float(make_length_parameter("height", "Height of the flat top", 1.0f, Kit_default_scale::wall_heights, 0.5f, 48.0f), 1.0f, 4.0f, 0.1f)),
            derive_float(make_length_parameter("depth", "Size along Z from the back to the face's plane", 2.0f, Kit_default_scale::modules, 1.5f, 32.0f), 1.5f, 3.0f, 0.25f),
            derive_enum(make_enum_parameter("strata", "Face structure", {"massive", "layered", "blocky"}, 0)),
            from_seed_by_default(overhang),
            derive_float(make_ratio_parameter("roughness", "Amount of random jitter of the face", 0.5f), 0.3f, 0.8f, 0.05f),
            derive_int(make_count_parameter("ledges", "Shelves where the face above steps back", 1, 0, 3), 0, 3),
            make_seed_parameter()
        };
        shape.material_slots  = {"cliff", "grass"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 500;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_cliff;
        shape.get_seam_sides  = [](const Kit_parameter_values&) { return std::vector<Kit_side>{Kit_side::z_neg}; };
        shape.check_walkability = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            const Cliff_profile profile{values};
            const float top  = values.get_float("depth") + profile.top_offset() - cliff_rim_jitter;
            const float need = (2.0f * agent.radius) + kit_opening_margin;
            if (top < need) {
                return fmt::format("{}: top {} m deep is narrower than the agent's {} m", values.get_shape()->id, top, need);
            }
            return {};
        };
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float half_length = 0.5f * values.get_float("length");
            add_side_connector(out_connectors, "start", glm::vec3{-half_length, 0.0f, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "end",   glm::vec3{ half_length, 0.0f, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
            // "top" at the center of the walkable top, which a backward lean moves toward the back.
            out_connectors.push_back(Kit_connector{.name = "base", .position = glm::vec3{0.0f}, .outward = glm::vec3{0.0f, -1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
            out_connectors.push_back(Kit_connector{.name = "top", .position = glm::vec3{0.0f, values.get_float("height"), cliff_top_center_z(values)}, .outward = glm::vec3{0.0f, 1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_parameter undulation = derive_float(make_length_parameter("undulation", "Height of the seeded waves of the sand", 0.08f, Kit_default_scale::meters, 0.0f, 1.0f), 0.0f, 0.15f, 0.01f);
        Kit_parameter berm       = derive_float(make_ratio_parameter("berm", "Flat part at the back, a share of the run", 0.25f), 0.0f, 0.4f, 0.05f);
        berm.float_max = 0.8f;

        Kit_shape_definition shape;
        shape.id              = "nature.shore";
        shape.kit             = "nature";
        shape.display_name    = "Shore";
        shape.description     =
            "Beach tile along X: sand from a flat berm at its back edge sloping down toward +Z to a low front lip, with a "
            "seeded undulation that fades out at the edges, so tiles join end to end; a water piece placed below the berm "
            "meets the sand along the shoreline.";
        shape.tags            = {"walkable", "ground", "beach", "tiles", "size:4x6x1"};
        shape.parameters      = {
            derive_float(make_length_parameter("length", "Size along X", 4.0f, Kit_default_scale::modules, 1.0f, 64.0f), 2.0f, 8.0f, 1.0f),
            derive_float(make_length_parameter("run",    "Size along Z from the berm's back edge to the front lip", 6.0f, Kit_default_scale::modules, 2.0f, 64.0f), 5.0f, 10.0f, 1.0f),
            make_length_parameter("rise", "Height of the berm above the front lip", 1.0f, Kit_default_scale::meters, 0.1f, 4.0f),
            from_seed_by_default(undulation),
            berm,
            make_seed_parameter()
        };
        shape.material_slots  = {"sand"};
        shape.normal_style    = Kit_normal_style::smooth;
        shape.triangle_budget = 260;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_shore;
        shape.get_seam_sides  = [](const Kit_parameter_values&) { return std::vector<Kit_side>{Kit_side::y_neg}; };
        shape.check_walkability = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            const float steepest = shore_steepest_degrees(values);
            if (steepest > agent.max_slope_degrees) {
                return fmt::format("{}: slope {} degrees is steeper than the agent's {} degrees", values.get_shape()->id, steepest, agent.max_slope_degrees);
            }
            return {};
        };
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float half_length = 0.5f * values.get_float("length");
            const float half_run    = 0.5f * values.get_float("run");
            add_side_connector(out_connectors, "start", glm::vec3{-half_length, 0.0f, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "end",   glm::vec3{ half_length, 0.0f, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "top",   glm::vec3{0.0f, shore_lip + values.get_float("rise"), -half_run}, glm::vec3{0.0f, 0.0f, -1.0f});
            out_connectors.push_back(Kit_connector{.name = "base", .position = glm::vec3{0.0f}, .outward = glm::vec3{0.0f, -1.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.water";
        shape.kit             = "nature";
        shape.display_name    = "Water";
        shape.description     =
            "Flat rectangular water surface at y = 0, facing up: a stated surface (open mesh) with no physics body, left "
            "out of the navigation input; other pieces pass through it, and patches tile by their edge connectors.";
        shape.tags            = {"water", "surface", "beach", "tiles", "size:8x8"};
        shape.parameters      = {
            derive_float(make_length_parameter("width", "Size along X", 8.0f, Kit_default_scale::modules, 1.0f, 64.0f), 4.0f, 16.0f, 1.0f),
            derive_float(make_length_parameter("depth", "Size along Z", 8.0f, Kit_default_scale::modules, 1.0f, 64.0f), 4.0f, 16.0f, 1.0f),
            make_count_parameter("subdivisions", "Quads along each side", 1, 1, 31),
            make_seed_parameter()
        };
        shape.material_slots  = {"water"};
        shape.normal_style    = Kit_normal_style::faceted;
        shape.triangle_budget = 2;
        shape.collision       = Kit_collision::none;
        shape.mesh_kind       = Kit_mesh_kind::surface;
        shape.generator       = generate_water;
        shape.mirror_planes   = std::vector<Kit_mirror_plane>{Kit_mirror_plane::x, Kit_mirror_plane::z};
        shape.get_connectors  = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            const float half_width = 0.5f * values.get_float("width");
            const float half_depth = 0.5f * values.get_float("depth");
            add_side_connector(out_connectors, "left",  glm::vec3{-half_width, 0.0f, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "right", glm::vec3{ half_width, 0.0f, 0.0f}, glm::vec3{ 1.0f, 0.0f, 0.0f});
            add_side_connector(out_connectors, "front", glm::vec3{0.0f, 0.0f,  half_depth}, glm::vec3{0.0f, 0.0f,  1.0f});
            add_side_connector(out_connectors, "back",  glm::vec3{0.0f, 0.0f, -half_depth}, glm::vec3{0.0f, 0.0f, -1.0f});
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.dune";
        shape.kit             = "nature";
        shape.display_name    = "Dune";
        shape.description     =
            "Elongated sand mound along X, at least 1.04 times as long as wide: a long gentle windward slope toward -Z, a "
            "narrow crest shifted toward +Z by the asymmetry and a steeper lee below it, eased into the ground at its foot; "
            "its outline and the crest's drift along X are seeded. Its sides are walkable.";
        shape.tags            = {"walkable", "ground", "beach", "size:8x5x1"};
        // In meters: the lee's slope bounds the height by the width, so derived dunes stay walkable at any level lengths.
        shape.parameters      = {
            from_seed_by_default(derive_float(make_length_parameter("length", "Size along X", 10.0f, Kit_default_scale::meters, 2.0f, 64.0f), 9.0f, 15.0f, 0.5f)),
            from_seed_by_default(derive_float(make_length_parameter("width",  "Size along Z", 5.5f, Kit_default_scale::meters, 2.0f, 32.0f), 4.5f, 7.0f, 0.25f)),
            from_seed_by_default(derive_float(make_length_parameter("height", "Height of the crest", 0.9f, Kit_default_scale::meters, 0.05f, 8.0f), 0.5f, 0.85f, 0.05f)),
            derive_float(make_ratio_parameter("asymmetry", "How far the crest lies toward the lee (+Z) side, a share of the half width", 0.3f), 0.0f, 0.4f, 0.05f),
            make_seed_parameter(),
            make_count_parameter("segments", "Points around the mound; an odd count uses one less", 16, 8, 48)
        };
        shape.parameters[3].float_max = 0.8f;
        shape.material_slots  = {"sand"};
        shape.normal_style    = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = 30.0f; // the brink of the lee stays sharp
        shape.triangle_budget = 290;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_dune;
        shape.check_combination = [](const Kit_parameter_values& values) -> std::string {
            if (values.get_float("length") < dune_least_length * values.get_float("width")) {
                return fmt::format("{}: length {} m is less than {} times the width {} m", values.get_shape()->id, values.get_float("length"), dune_least_length, values.get_float("width"));
            }
            return {};
        };
        shape.check_walkability = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            const float steepest = dune_steepest_degrees(values);
            if (steepest > agent.max_slope_degrees) {
                return fmt::format("{}: slope {} degrees is steeper than the agent's {} degrees", values.get_shape()->id, steepest, agent.max_slope_degrees);
            }
            return {};
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_parameter dome = derive_float(make_ratio_parameter("dome", "How far the middle rises, a share of the size", 0.03f), 0.0f, 0.1f, 0.01f);
        dome.float_max = 0.1f;
        Kit_parameter irregularity = from_seed_by_default(derive_float(make_ratio_parameter("irregularity", "How far the outline wanders from a circle", 0.3f), 0.1f, 0.5f, 0.01f));
        irregularity.float_max = 0.6f;

        Kit_shape_definition shape;
        shape.id              = "nature.ground_patch";
        shape.kit             = "nature";
        shape.display_name    = "Ground patch";
        shape.description     =
            "Thin irregular patch of earth, sand or grass lying on other ground: a seeded lobed outline, a bevelled edge "
            "no higher than the agent's climb and a low dome; walkable, a convex hull body.";
        shape.tags            = {"walkable", "ground", "decoration", "size:3x3"};
        shape.parameters      = {
            from_seed_by_default(derive_float(make_length_parameter("size", "Diameter", 3.0f, Kit_default_scale::modules, 0.2f, 32.0f), 1.0f, 6.0f, 0.25f)),
            make_length_parameter("thickness", "Height of the edge", 0.05f, Kit_default_scale::meters, 0.01f, 0.4f),
            irregularity,
            dome,
            make_seed_parameter(),
            make_count_parameter("segments", "Points of the outline", 24, 8, 48)
        };
        shape.material_slots  = {"earth"};
        shape.normal_style    = Kit_normal_style::smooth;
        shape.triangle_budget = 150;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_ground_patch;
        shape.check_walkability = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            // The outline varies between neighbouring points, so the slope is measured on the patch's own triangles.
            const float slope = ground_patch_steepest_degrees(values);
            if (values.get_float("thickness") > agent.max_climb) {
                return fmt::format("{}: edge {} m is higher than the agent's climb {} m", values.get_shape()->id, values.get_float("thickness"), agent.max_climb);
            }
            if (slope > agent.max_slope_degrees) {
                return fmt::format("{}: slope {} degrees is steeper than the agent's {} degrees", values.get_shape()->id, slope, agent.max_slope_degrees);
            }
            return {};
        };
        shapes.push_back(std::move(shape));
    }
}

} // namespace editor
