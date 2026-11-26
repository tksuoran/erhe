#include "level/kit_shapes.hpp"
#include "level/kit_mesh_builder.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Every mesh's topology follows from its values and integer draws alone; random floats only move vertices.

namespace editor {

namespace {

constexpr float two_pi = 2.0f * std::numbers::pi_v<float>;

// Unit vector at the angle around +Y, from +X toward -Z.
[[nodiscard]] auto around_y(const float angle) -> glm::vec3
{
    return glm::vec3{std::cos(angle), 0.0f, -std::sin(angle)};
}

[[nodiscard]] auto next_signed(Kit_random& random) -> float
{
    return random.next_range(-1.0f, 1.0f);
}

[[nodiscard]] auto add_vertices(Kit_mesh_builder& builder, const std::span<const glm::vec3> points) -> uint32_t
{
    uint32_t first = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const uint32_t v = builder.add_vertex(points[i]);
        if (i == 0) {
            first = v;
        }
    }
    return first;
}

// Two triangles per quad between a lower and an upper ring of n vertices, both counter-clockwise seen from above.
void add_ring_band(Kit_mesh_builder& builder, const uint32_t lower, const uint32_t upper, const uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t j = (i + 1) % n;
        builder.add_triangle(lower + i, lower + j, upper + j);
        builder.add_triangle(lower + i, upper + j, upper + i);
    }
}

void add_fan_above(Kit_mesh_builder& builder, const uint32_t ring, const uint32_t n, const uint32_t apex)
{
    for (uint32_t i = 0; i < n; ++i) {
        builder.add_triangle(ring + i, ring + ((i + 1) % n), apex);
    }
}

void add_fan_below(Kit_mesh_builder& builder, const uint32_t ring, const uint32_t n, const uint32_t apex)
{
    for (uint32_t i = 0; i < n; ++i) {
        builder.add_triangle(apex, ring + ((i + 1) % n), ring + i);
    }
}

// Radius factors of n columns in [1 - depth, 1]: three lobes of random phase and weight, so an outline is no circle.
void make_lobes(Kit_random& random, const std::size_t n, const float depth, std::vector<float>& out_factors)
{
    constexpr std::array<float, 3> lobe_counts{2.0f, 3.0f, 5.0f};
    std::array<float, 3> phases{};
    std::array<float, 3> weights{};
    float weight_sum = 0.0f;
    for (std::size_t k = 0; k < lobe_counts.size(); ++k) {
        phases[k]  = random.next_range(0.0f, two_pi);
        weights[k] = random.next_range(0.3f, 1.0f) / lobe_counts[k];
        weight_sum += weights[k];
    }
    out_factors.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const float angle = two_pi * static_cast<float>(i) / static_cast<float>(n);
        float wave = 0.0f;
        for (std::size_t k = 0; k < lobe_counts.size(); ++k) {
            wave += weights[k] * std::cos((lobe_counts[k] * angle) + phases[k]);
        }
        out_factors[i] = 1.0f - (depth * (0.5f + (0.5f * wave / weight_sum)));
    }
}

//
// Rock: pieces of stacked convex rings whose edges keep their directions from ring to ring, so every side face is a
// planar trapezoid between two strata lines; a side whose offsets run straight up the rings is one fracture plane.
//

class Rock_station
{
public:
    float t;     // share of the piece's height
    float scale; // of the footprint
};

constexpr std::array<Rock_station, 3> rock_boulder_stations{{{0.34f, 1.00f}, {0.72f, 0.82f}, {1.00f, 0.46f}}};
constexpr std::array<Rock_station, 3> rock_angular_stations{{{0.30f, 1.00f}, {0.70f, 0.80f}, {1.00f, 0.32f}}};
constexpr std::array<Rock_station, 4> rock_slab_stations   {{{0.40f, 1.00f}, {0.50f, 0.90f}, {0.88f, 0.86f}, {1.00f, 0.70f}}};
constexpr std::array<Rock_station, 7> rock_layered_stations{{
    {0.22f, 1.00f}, {0.27f, 0.90f}, {0.50f, 0.93f}, {0.55f, 0.78f}, {0.80f, 0.80f}, {0.85f, 0.64f}, {1.00f, 0.60f}
}};

class Rock_form
{
public:
    std::span<const Rock_station> stations;  // the rings above the base ring, bottom to top; the last is the top face
    uint32_t side_min;
    uint32_t side_range;      // sides are side_min + [0, side_range)
    float    height;          // per meter of size
    float    depth;           // footprint depth over width
    float    flare;           // the base ring's growth over the first station's
    float    dip;             // strata dip at roughness 1, rise per meter
    float    lean;            // drift of the top at roughness 1, per meter of height
    float    top_tilt;        // slope of the top face at roughness 1
    float    crown;           // rise of the top's apex over its ring, a share of the height; 0: a flat top face
    uint32_t fracture_one_in; // one side in this many is a fracture plane
};

[[nodiscard]] auto get_rock_form(const std::string_view form) -> Rock_form
{
    if (form == "angular") {
        return Rock_form{rock_angular_stations, 5, 2, 0.90f, 0.70f, 0.04f, 0.45f, 0.40f, 0.9f, 0.16f, 2};
    }
    if (form == "slab") {
        return Rock_form{rock_slab_stations, 5, 3, 0.30f, 0.75f, 0.05f, 0.12f, 0.15f, 0.25f, 0.0f, 5};
    }
    if (form == "layered") {
        return Rock_form{rock_layered_stations, 5, 3, 0.70f, 0.80f, 0.05f, 0.25f, 0.20f, 0.3f, 0.0f, 3};
    }
    return Rock_form{rock_boulder_stations, 6, 3, 0.62f, 0.85f, 0.07f, 0.18f, 0.10f, 0.6f, 0.09f, 4};
}

// Edge normals of a convex polygon at rising angles around +Y; corner j lies between edges j and j + 1.
class Rock_outline
{
public:
    explicit Rock_outline(const std::span<const float> angles)
        : m_cos    (angles.size())
        , m_sin    (angles.size())
        , m_cos_gap(angles.size())
        , m_sin_gap(angles.size())
    {
        const std::size_t n = angles.size();
        for (std::size_t j = 0; j < n; ++j) {
            const std::size_t k   = (j + 1) % n;
            const float       gap = (angles[k] - angles[j]) + ((k == 0) ? two_pi : 0.0f);
            m_cos[j]     = std::cos(angles[j]);
            m_sin[j]     = std::sin(angles[j]);
            m_cos_gap[j] = std::cos(gap);
            m_sin_gap[j] = std::sin(gap);
        }
    }

    [[nodiscard]] auto edge_length(const std::span<const float> offsets, const std::size_t j) const -> float
    {
        const std::size_t n = m_cos.size();
        const std::size_t k = (j + 1) % n;
        const std::size_t i = (j + n - 1) % n;
        return ((offsets[k] - (offsets[j] * m_cos_gap[j])) / m_sin_gap[j]) + ((offsets[i] - (offsets[j] * m_cos_gap[i])) / m_sin_gap[i]);
    }

    // (x, -z) of corner j.
    [[nodiscard]] auto corner(const std::span<const float> offsets, const std::size_t j) const -> glm::vec2
    {
        const std::size_t k = (j + 1) % m_cos.size();
        return glm::vec2{
            ((offsets[j] * m_sin[k]) - (offsets[k] * m_sin[j])) / m_sin_gap[j],
            ((offsets[k] * m_cos[j]) - (offsets[j] * m_cos[k])) / m_sin_gap[j]
        };
    }

    // Moves wanted toward base by the least share that keeps every edge at least `keep` of the base's: a min of
    // continuous terms, so the polygon stays valid and no decision depends on a float comparison.
    void limit(const std::span<const float> base, const std::span<float> wanted, const float keep) const
    {
        float share = 1.0f;
        for (std::size_t j = 0; j < base.size(); ++j) {
            const float base_length   = edge_length(base, j);
            const float wanted_length = edge_length(wanted, j);
            if (wanted_length < base_length) {
                share = std::min(share, ((1.0f - keep) * base_length) / (base_length - wanted_length));
            }
        }
        for (std::size_t j = 0; j < base.size(); ++j) {
            wanted[j] = base[j] + (share * (wanted[j] - base[j]));
        }
    }

private:
    std::vector<float> m_cos;
    std::vector<float> m_sin;
    std::vector<float> m_cos_gap; // of the angle from normal j to normal j + 1
    std::vector<float> m_sin_gap;
};

class Rock_piece
{
public:
    glm::vec3 origin{0.0f}; // of the base ring, which lies in the plane y = origin.y
    float     size     {1.0f};
    float     turn     {0.0f};
    float     roughness{0.5f};
};

void add_rock_piece(Kit_mesh_builder& builder, Kit_random& random, const Rock_form& form, const Rock_piece& piece)
{
    const std::size_t side_count    = static_cast<std::size_t>(form.side_min + (random.next_u32() % form.side_range));
    const std::size_t station_count = form.stations.size();
    std::vector<float> angles(side_count);
    for (std::size_t j = 0; j < side_count; ++j) {
        const float offset = random.next_range(-0.3f, 0.3f);
        angles[j] = piece.turn + (two_pi * (static_cast<float>(j) + offset) / static_cast<float>(side_count));
    }
    const Rock_outline outline{angles};

    // The footprint: tangents of an ellipse, each moved in or out.
    const float depth_draw = next_signed(random);
    const float half_x     = 0.5f * piece.size;
    const float half_z     = half_x * form.depth * (1.0f + (0.15f * depth_draw));
    std::vector<float> tangents (side_count);
    std::vector<float> footprint(side_count);
    for (std::size_t j = 0; j < side_count; ++j) {
        const float local = angles[j] - piece.turn;
        const float a     = half_x * std::cos(local);
        const float b     = half_z * std::sin(local);
        const float draw  = next_signed(random);
        tangents[j]  = std::sqrt((a * a) + (b * b));
        footprint[j] = tangents[j] * (1.0f + (0.3f * piece.roughness * draw));
    }
    outline.limit(tangents, footprint, 0.5f);

    const float height_draw = next_signed(random);
    const float height      = form.height * piece.size * (1.0f + (0.15f * height_draw));
    std::vector<float> station_y(station_count);
    for (std::size_t s = 0; s < station_count; ++s) {
        const float draw = next_signed(random);
        const float t    = form.stations[s].t + (((s + 1) < station_count) ? (0.015f * piece.roughness * draw) : 0.0f);
        station_y[s] = t * height;
    }

    std::vector<uint32_t> fracture     (side_count);
    std::vector<float>    fracture_foot(side_count);
    std::vector<float>    fracture_top (side_count);
    std::vector<float>    trend        (side_count); // the other sides stand steeper or slope in earlier
    for (std::size_t j = 0; j < side_count; ++j) {
        fracture[j]      = ((random.next_u32() % form.fracture_one_in) == 0u) ? 1u : 0u;
        fracture_foot[j] = random.next_range(0.92f, 1.02f);
        fracture_top[j]  = random.next_range(0.75f, 1.30f);
        trend[j]         = next_signed(random);
    }

    const Rock_station& first_station = form.stations.front();
    const Rock_station& last_station  = form.stations.back();
    std::vector<float>     ring_base(side_count);
    std::vector<float>     ring     (side_count);
    std::vector<glm::vec2> corners  (station_count * side_count);
    for (std::size_t s = 0; s < station_count; ++s) {
        const Rock_station& station = form.stations[s];
        const float         along   = (station.t - first_station.t) / (1.0f - first_station.t);
        for (std::size_t j = 0; j < side_count; ++j) {
            const float noise    = next_signed(random);
            const float straight = (first_station.scale * fracture_foot[j]) + (along * ((last_station.scale * fracture_top[j]) - (first_station.scale * fracture_foot[j])));
            ring_base[j] = footprint[j] * station.scale;
            const float worn = station.scale + (piece.roughness * ((0.35f * trend[j] * along) + (0.08f * noise)));
            ring[j]      = footprint[j] * ((fracture[j] != 0u) ? straight : worn);
        }
        outline.limit(ring_base, ring, 0.45f);
        for (std::size_t j = 0; j < side_count; ++j) {
            corners[(s * side_count) + j] = outline.corner(ring, j);
        }
    }

    // The strata dip and the piece leans: one affine map of the rings, so the faces stay planar.
    const float     reach      = 1.8f * half_x;
    const float     dip_angle  = random.next_range(0.0f, two_pi);
    const float     dip_amount = std::min(form.dip * piece.roughness, 0.6f * station_y.front() / reach);
    const glm::vec2 dip        = dip_amount * glm::vec2{std::cos(dip_angle), std::sin(dip_angle)};
    const float     lean_angle = random.next_range(0.0f, two_pi);
    const float     lean_draw  = random.next_unit();
    const glm::vec2 lean       = (form.lean * piece.roughness * lean_draw) * glm::vec2{std::cos(lean_angle), std::sin(lean_angle)};
    const auto place = [&piece, &dip, &lean](const glm::vec2 corner, const float y) -> glm::vec3 {
        return piece.origin + glm::vec3{corner.x + (lean.x * y), y + glm::dot(dip, corner), -(corner.y + (lean.y * y))};
    };

    // The base ring: the lowest level's side planes, flaring downward, cut by the ground.
    const float flare_rate = form.flare * first_station.scale / station_y.front();
    std::vector<glm::vec3> points;
    points.reserve((station_count + 1) * side_count);
    for (std::size_t j = 0; j < side_count; ++j) {
        const glm::vec2 above = corners[j];
        const glm::vec2 grow  = outline.corner(footprint, j);
        const float     drop  = (station_y.front() + glm::dot(dip, above)) / (1.0f - (flare_rate * glm::dot(dip, grow)));
        glm::vec3 point = place(above + ((drop * flare_rate) * grow), station_y.front() - drop);
        point.y = piece.origin.y;
        points.push_back(point);
    }
    // The top face tilts by itself, within the room the last band gives it.
    const std::size_t top        = station_count - 1;
    const float       top_room   = station_y[top] - station_y[top - 1];
    const float       tilt_angle = random.next_range(0.0f, two_pi);
    const float       tilt_draw  = random.next_range(0.5f, 1.0f);
    const float       tilt_slope = std::min(form.top_tilt * piece.roughness * tilt_draw, 0.6f * top_room / (reach * last_station.scale));
    const glm::vec2   tilt       = tilt_slope * glm::vec2{std::cos(tilt_angle), std::sin(tilt_angle)};
    for (std::size_t s = 0; s < station_count; ++s) {
        for (std::size_t j = 0; j < side_count; ++j) {
            const glm::vec2 corner = corners[(s * side_count) + j];
            points.push_back(place(corner, station_y[s] + ((s == top) ? glm::dot(tilt, corner) : 0.0f)));
        }
    }

    const uint32_t n     = static_cast<uint32_t>(side_count);
    const uint32_t first = add_vertices(builder, points);
    std::vector<uint32_t> polygon(side_count);
    for (uint32_t j = 0; j < n; ++j) {
        polygon[j] = first + (n - 1 - j);
    }
    builder.add_polygon(polygon);
    // Smooth shaded, the flat base and a flat top keep their outlines sharp.
    for (uint32_t j = 0; j < n; ++j) {
        builder.add_hard_edge(first + j, first + ((j + 1) % n));
    }
    for (uint32_t s = 0; s < static_cast<uint32_t>(station_count); ++s) {
        const uint32_t lower = first + (s * n);
        const uint32_t upper = lower + n;
        if ((s + 1) == static_cast<uint32_t>(station_count)) {
            add_ring_band(builder, lower, upper, n); // under the tilted top the sides are no planes
            continue;
        }
        for (uint32_t j = 0; j < n; ++j) {
            const uint32_t k = (j + 1) % n;
            builder.add_quad(lower + j, lower + k, upper + k, upper + j);
        }
    }
    const uint32_t top_ring = first + (static_cast<uint32_t>(station_count) * n);
    if (form.crown > 0.0f) {
        // An apex inside the top ring: its corners' mean drawn toward two of them.
        const float toward_a = random.next_range(0.0f, 0.35f);
        const float toward_b = random.next_range(0.0f, 0.35f);
        glm::vec3 mean{0.0f};
        for (std::size_t j = 0; j < side_count; ++j) {
            mean += points[(station_count * side_count) + j];
        }
        mean /= static_cast<float>(side_count);
        const glm::vec3 a    = points[station_count * side_count];
        const glm::vec3 b    = points[(station_count * side_count) + (side_count / 2)];
        const glm::vec3 apex = mean + (toward_a * (a - mean)) + (toward_b * (b - mean)) + glm::vec3{0.0f, form.crown * height, 0.0f};
        add_fan_above(builder, top_ring, n, builder.add_vertex(apex));
        return;
    }
    for (uint32_t j = 0; j < n; ++j) {
        polygon[j] = top_ring + j;
        builder.add_hard_edge(top_ring + j, top_ring + ((j + 1) % n));
    }
    builder.add_polygon(polygon);
}

constexpr int                  rock_piece_max = 4;
constexpr std::array<float, 3> rock_companion_sizes{0.62f, 0.45f, 0.30f};

void generate_rock(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const Rock_form form      = get_rock_form(values.get_enum("form"));
    const float     size      = values.get_float("size");
    const float     roughness = values.get_float("roughness");
    const int       pieces    = std::clamp(values.get_int("pieces"), 1, rock_piece_max);
    Kit_random random{values.get_seed()};
    Kit_mesh_builder builder;
    const float turn = random.next_range(0.0f, two_pi);
    add_rock_piece(builder, random, form, Rock_piece{.origin = glm::vec3{0.0f}, .size = size, .turn = turn, .roughness = roughness});

    // Smaller pieces against the largest one's flank, each about 137 degrees on from the last.
    const float first_azimuth = random.next_range(0.0f, two_pi);
    for (int k = 1; k < pieces; ++k) {
        const float     azimuth_draw = next_signed(random);
        const float     reach_draw   = random.next_range(0.95f, 1.2f);
        const float     size_draw    = next_signed(random);
        const float     piece_turn   = random.next_range(0.0f, two_pi);
        const float     azimuth      = first_azimuth + (2.4f * static_cast<float>(k)) + (0.4f * azimuth_draw);
        const glm::vec2 local        = (0.5f * size * reach_draw) * glm::vec2{std::cos(azimuth), form.depth * std::sin(azimuth)};
        const glm::vec3 origin{
            (local.x * std::cos(turn)) - (local.y * std::sin(turn)),
            0.0f,
            -((local.x * std::sin(turn)) + (local.y * std::cos(turn)))
        };
        const float piece_size = size * rock_companion_sizes[static_cast<std::size_t>(k - 1)] * (1.0f + (0.15f * size_draw));
        add_rock_piece(builder, random, form, Rock_piece{.origin = origin, .size = piece_size, .turn = piece_turn, .roughness = roughness});
    }
    builder.write(mesh);
}


//
// Land mass: a lobed plateau. Rings of one column count from the ground to the turf: a talus foot, a cliff with a
// bench over part of the perimeter, gullies down the columns, a soil band and an overhanging turf rim round a flat top.
//

constexpr uint32_t land_grass_slot = 0;
constexpr uint32_t land_earth_slot = 1;
constexpr uint32_t land_rock_slot  = 2;

// The flanks' horizontal run: less on a low or a narrow land mass, so the top stays wide.
[[nodiscard]] auto land_mass_run(const float base_radius, const float height) -> float
{
    return std::min(0.9f * height, 0.34f * base_radius);
}

constexpr float land_lobe_depth = 0.32f; // the outline's radius is at least 1 - this of the footprint's
constexpr float land_rim_inset  = 0.90f; // the turf rim's inset, a share of the run

class Land_ring
{
public:
    float inset; // toward the axis, a share of the run
    float y;     // a share of the height
    float gully; // how far the columns' gullies cut in, a share of the run at erosion 1
};

// Ground, talus top, cliff brow, bench back, cliff top, soil top, turf rim.
constexpr std::array<Land_ring, 7> land_rings{{
    {0.00f, 0.00f, 0.00f}, {0.42f, 0.20f, 0.10f}, {0.52f, 0.52f, 0.22f}, {0.52f, 0.56f, 0.22f},
    {0.95f, 0.84f, 0.22f}, {0.84f, 0.94f, 0.00f}, {land_rim_inset, 1.00f, 0.00f}
}};
constexpr float land_bench_depth = 0.30f; // the bench's added inset, a share of the run

void generate_land_mass(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float       base_radius = 0.5f * values.get_float("footprint");
    const float       height      = values.get_float("height");
    const float       erosion     = values.get_float("erosion");
    const uint32_t    n           = static_cast<uint32_t>(values.get_int("segments"));
    const float       run         = land_mass_run(base_radius, height);
    Kit_random random{values.get_seed()};

    std::vector<float> lobes;
    make_lobes(random, n, land_lobe_depth, lobes);
    std::vector<glm::vec3> directions(n);
    std::vector<float>     gullies   (n);
    for (uint32_t i = 0; i < n; ++i) {
        const float offset = random.next_range(-0.22f, 0.22f);
        gullies[i]    = random.next_unit();
        directions[i] = around_y(two_pi * (static_cast<float>(i) + offset) / static_cast<float>(n));
    }

    // Two benches: columns within a window of the perimeter; 2 is a full bench, 1 its tapering end.
    std::vector<uint32_t> bench(n, 0u);
    const uint32_t first_center  = random.next_u32() % n;
    const uint32_t second_center = (first_center + (n / 2) + (random.next_u32() % 3u)) % n;
    const auto add_bench = [&bench, n](const uint32_t center, const uint32_t half_width) {
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t ahead    = ((i + n) - center) % n;
            const uint32_t distance = std::min(ahead, n - ahead);
            const uint32_t level    = (distance <= half_width) ? 2u : ((distance == (half_width + 1u)) ? 1u : 0u);
            bench[i] = std::max(bench[i], level);
        }
    };
    add_bench(first_center, std::max(1u, n / 6u));
    add_bench(second_center, std::max(1u, n / 10u));

    std::vector<glm::vec3> points;
    points.reserve(land_rings.size() * n);
    for (std::size_t r = 0; r < land_rings.size(); ++r) {
        const Land_ring& ring  = land_rings[r];
        const bool       worn  = (r >= 1) && (r <= 4); // the rock rings move with the draws; ground, soil and turf keep their lines
        for (uint32_t i = 0; i < n; ++i) {
            const float radial_draw = next_signed(random);
            const float height_draw = next_signed(random);
            const float bench_inset = (r == 3) ? (land_bench_depth * 0.5f * static_cast<float>(bench[i])) : 0.0f;
            float radius = lobes[i] * (base_radius - (run * (ring.inset + bench_inset)));
            radius -= run * ring.gully * erosion * gullies[i];
            float y = ring.y * height;
            if (worn) {
                radius += 0.05f * run * erosion * radial_draw;
                y      += 0.02f * height * height_draw;
            }
            if (r == 3) {
                y = points[(2 * n) + i].y + (0.04f * height); // the bench lies just above the brow
            }
            points.push_back(glm::vec3{radius * directions[i].x, y, radius * directions[i].z});
        }
    }

    Kit_mesh_builder builder;
    const uint32_t first = add_vertices(builder, points);
    constexpr std::array<uint32_t, 6> band_slots{land_earth_slot, land_rock_slot, land_rock_slot, land_rock_slot, land_earth_slot, land_grass_slot};
    for (uint32_t r = 0; (r + 1) < static_cast<uint32_t>(land_rings.size()); ++r) {
        const uint32_t lower = first + (r * n);
        const uint32_t upper = lower + n;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t j = (i + 1) % n;
            const bool turf = (r == 2) && (bench[i] == 2u) && (bench[j] == 2u);
            builder.set_slot(turf ? land_grass_slot : band_slots[r]);
            builder.add_triangle(lower + i, lower + j, upper + j);
            builder.add_triangle(lower + i, upper + j, upper + i);
        }
    }
    builder.set_slot(land_earth_slot);
    add_fan_below(builder, first, n, builder.add_vertex(glm::vec3{0.0f, 0.0f, 0.0f}));
    builder.set_slot(land_grass_slot);
    add_fan_above(builder, first + ((static_cast<uint32_t>(land_rings.size()) - 1) * n), n, builder.add_vertex(glm::vec3{0.0f, height, 0.0f}));
    builder.write(mesh);
}

//
// Hill: a polar grid about a crest off the footprint's center, so one side is steep and the other long; rocks crop
// out of the steep side.
//

constexpr uint32_t hill_grass_slot = 0;
constexpr uint32_t hill_rock_slot  = 1;

// Height share at the distance share t from the crest: a rounded crest, a straight flank, a foot that eases out.
[[nodiscard]] auto hill_profile(const float t) -> float
{
    constexpr float crest = 0.35f;
    constexpr float foot  = 0.30f;
    constexpr float ease  = 0.30f; // the slope at the rim over the flank's
    constexpr float slope = 1.0f / (1.0f - (0.5f * crest) - (0.5f * (1.0f - ease) * foot));
    if (t < crest) {
        return 1.0f - (0.5f * slope * t * t / crest);
    }
    const float flank_end = 1.0f - (0.5f * slope * crest) - (slope * ((1.0f - foot) - crest));
    if (t < (1.0f - foot)) {
        return 1.0f - (0.5f * slope * crest) - (slope * (t - crest));
    }
    const float u = (t - (1.0f - foot)) / foot;
    return flank_end - (slope * foot * (u - (0.5f * (1.0f - ease) * u * u)));
}

class Hill_surface
{
public:
    uint32_t               n    {0}; // columns
    uint32_t               rings{0}; // from the rim on the ground up to the last one below the crest
    std::vector<glm::vec3> points;   // ring by ring, then the crest
    uint32_t               steep_column{0};
};

[[nodiscard]] auto make_hill_surface(const Kit_parameter_values& values, Kit_random& random) -> Hill_surface
{
    const float radius = values.get_float("radius");
    const float height = values.get_float("height");
    Hill_surface surface;
    surface.n     = static_cast<uint32_t>(values.get_int("segments"));
    surface.rings = std::max(3u, (surface.n / 4u) + 1u);
    const uint32_t n = surface.n;

    std::vector<float> lobes;
    make_lobes(random, n, 0.16f, lobes);
    std::vector<glm::vec3> rim(n);
    for (uint32_t i = 0; i < n; ++i) {
        const float offset = random.next_range(-0.15f, 0.15f);
        rim[i] = (radius * lobes[i]) * around_y(two_pi * (static_cast<float>(i) + offset) / static_cast<float>(n));
    }
    // The crest stands toward one column's rim point; that side is the steep one.
    surface.steep_column = random.next_u32() % n;
    const float     crest_draw = random.next_range(0.08f, 0.18f);
    const glm::vec3 crest      = crest_draw * rim[surface.steep_column];
    // A spur: the flank stands higher along one direction and lower across it.
    const float spur_phase = random.next_range(0.0f, two_pi);
    for (uint32_t r = 0; r < surface.rings; ++r) {
        const float t      = 1.0f - (static_cast<float>(r) / static_cast<float>(surface.rings));
        const float middle = 4.0f * t * t * (1.0f - t); // nothing at the rim, little near the crest, which stays the top
        for (uint32_t i = 0; i < n; ++i) {
            const float draw = next_signed(random);
            const float spur = std::cos((2.0f * two_pi * static_cast<float>(i) / static_cast<float>(n)) + spur_phase);
            const float y    = (r == 0) ? 0.0f : (height * hill_profile(t) * (1.0f + (middle * ((0.12f * spur) + (0.05f * draw)))));
            const glm::vec3 p = crest + (t * (rim[i] - crest));
            surface.points.push_back(glm::vec3{p.x, y, p.z});
        }
    }
    surface.points.push_back(glm::vec3{crest.x, height, crest.z});
    return surface;
}

// The steepest of the hill's facets, in degrees from level.
[[nodiscard]] auto get_hill_steepest_degrees(const Hill_surface& surface) -> float
{
    float lowest_up = 1.0f;
    const auto visit = [&surface, &lowest_up](const std::size_t a, const std::size_t b, const std::size_t c) {
        const glm::vec3 normal = glm::normalize(glm::cross(surface.points[b] - surface.points[a], surface.points[c] - surface.points[a]));
        lowest_up = std::min(lowest_up, normal.y);
    };
    const std::size_t n     = surface.n;
    const std::size_t crest = surface.points.size() - 1;
    for (std::size_t r = 0; r < surface.rings; ++r) {
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t j     = (i + 1) % n;
            const std::size_t lower = r * n;
            if ((r + 1) == surface.rings) {
                visit(lower + i, lower + j, crest);
                continue;
            }
            visit(lower + i, lower + j, lower + n + j);
            visit(lower + i, lower + n + j, lower + n + i);
        }
    }
    return glm::degrees(std::acos(std::clamp(lowest_up, -1.0f, 1.0f)));
}

constexpr int hill_rock_max = 4;

void generate_hill(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float radius = values.get_float("radius");
    const int   rocks  = std::clamp(values.get_int("rocks"), 1, hill_rock_max);
    Kit_random random{values.get_seed()};
    const Hill_surface surface = make_hill_surface(values, random);
    const uint32_t     n       = surface.n;

    Kit_mesh_builder builder;
    builder.set_slot(hill_grass_slot);
    const uint32_t first = add_vertices(builder, surface.points);
    const uint32_t crest = first + (surface.rings * n);
    for (uint32_t r = 0; (r + 1) < surface.rings; ++r) {
        add_ring_band(builder, first + (r * n), first + ((r + 1) * n), n);
    }
    add_fan_above(builder, first + ((surface.rings - 1) * n), n, crest);
    add_fan_below(builder, first, n, builder.add_vertex(glm::vec3{0.0f, 0.0f, 0.0f}));

    // Outcrops: rocks sunk into the steep side, each at a grid point.
    builder.set_slot(hill_rock_slot);
    Kit_random rock_random{values.get_seed(), "hill rocks"};
    const Rock_form form = get_rock_form("boulder");
    for (int k = 0; k < rocks; ++k) {
        const uint32_t column    = (surface.steep_column + (n - 1) + static_cast<uint32_t>(k) + (rock_random.next_u32() % 2u)) % n;
        const uint32_t ring      = 1u + (rock_random.next_u32() % (surface.rings - 1u));
        const float    size_draw = rock_random.next_range(0.30f, 0.46f);
        const float    turn      = rock_random.next_range(0.0f, two_pi);
        const float    size      = radius * size_draw / (1.0f + (0.25f * static_cast<float>(k)));
        const glm::vec3 at       = surface.points[(ring * n) + column];
        const glm::vec3 origin{at.x, std::max(0.0f, at.y - (0.42f * size)), at.z};
        add_rock_piece(builder, rock_random, form, Rock_piece{.origin = origin, .size = size, .turn = turn, .roughness = 0.8f});
    }
    builder.write(mesh);
}

//
// Tree and bush: a trunk of rings with a root flare, limbs as tapered tubes, foliage as lumpy closed clumps.
//

// A value of a ring of own.size() draws at a position in units of the ring's points, interpolated around the ring: a
// ring of more or fewer points at another detail keeps the draws of its own count, and the ring's own points.
[[nodiscard]] auto sample_ring(const std::span<const float> own, const float position) -> float
{
    const int   count = static_cast<int>(own.size());
    const float lower = std::floor(position);
    const float t     = position - lower;
    const int   i     = ((static_cast<int>(lower) % count) + count) % count;
    const int   j     = (i + 1) % count;
    return (t == 0.0f) ? own[static_cast<std::size_t>(i)] : (own[static_cast<std::size_t>(i)] + (t * (own[static_cast<std::size_t>(j)] - own[static_cast<std::size_t>(i)])));
}

// A lumpy closed clump of foliage about its center; a flat clump stands on its base ring at the center's height.
enum class Clump_base : unsigned int { round = 0, flat };

constexpr int clump_sides = 6; // at the default detail

void add_clump(Kit_mesh_builder& builder, Kit_random& random, const glm::vec3& center, const glm::vec3& radii, const Clump_base base, const float detail)
{
    class Station { public: float y; float radius; };
    constexpr std::array<Station, 4> round_stations{{{-0.50f, 0.66f}, {0.02f, 1.00f}, {0.52f, 0.76f}, {0.88f, 0.36f}}};
    constexpr std::array<Station, 4> flat_stations {{{ 0.00f, 0.80f}, {0.32f, 1.00f}, {0.66f, 0.78f}, {0.90f, 0.38f}}};
    const bool                        flat     = (base == Clump_base::flat);
    const std::array<Station, 4>&     stations = flat ? flat_stations : round_stations;
    const float                       phase    = random.next_range(0.0f, two_pi);
    // The draws of the default detail's points, ring by ring, whatever the detail.
    std::array<std::array<float, clump_sides>, 4> radial_draws{};
    std::array<std::array<float, clump_sides>, 4> height_draws{};
    for (std::size_t r = 0; r < stations.size(); ++r) {
        for (int i = 0; i < clump_sides; ++i) {
            radial_draws[r][static_cast<std::size_t>(i)] = next_signed(random);
            height_draws[r][static_cast<std::size_t>(i)] = next_signed(random);
        }
    }
    const int sides = kit_detail_count(detail, clump_sides, 4, 2 * clump_sides);
    std::vector<glm::vec3> points;
    points.reserve(stations.size() * static_cast<std::size_t>(sides));
    for (std::size_t r = 0; r < stations.size(); ++r) {
        for (int i = 0; i < sides; ++i) {
            const float     position    = static_cast<float>(i * clump_sides) / static_cast<float>(sides);
            const float     radial_draw = sample_ring(radial_draws[r], position);
            const float     height_draw = sample_ring(height_draws[r], position);
            const bool      on_ground   = flat && (r == 0);
            const glm::vec3 direction   = around_y(phase + (two_pi * (position + (0.5f * static_cast<float>(r))) / static_cast<float>(clump_sides)));
            const float     reach       = stations[r].radius * (1.0f + (0.13f * radial_draw));
            const float     y           = on_ground ? 0.0f : (radii.y * (stations[r].y + (0.08f * height_draw)));
            points.push_back(center + glm::vec3{radii.x * reach * direction.x, y, radii.z * reach * direction.z});
        }
    }
    const float     top_x  = next_signed(random);
    const float     top_z  = next_signed(random);
    const glm::vec3 top    = center + glm::vec3{0.2f * radii.x * top_x, radii.y, 0.2f * radii.z * top_z};
    const glm::vec3 bottom = center + glm::vec3{0.0f, flat ? 0.0f : (-0.8f * radii.y), 0.0f};
    const uint32_t  first  = static_cast<uint32_t>(builder.get_positions().size());
    builder.add_ring_solid(points, sides, bottom, top);
    builder.set_smooth_from(first); // a lump of leaves: round all over
    if (flat) {
        builder.add_hard_loop(first, static_cast<uint32_t>(sides)); // the flat base on the ground
    }
}

// A tapered closed tube through the centers in the vertical plane whose horizontal normal is side; pointed at its end.
void add_limb(Kit_mesh_builder& builder, const std::span<const glm::vec3> centers, const std::span<const float> radii, const glm::vec3& side, const int sides)
{
    std::vector<glm::vec3> points;
    points.reserve(centers.size() * static_cast<std::size_t>(sides));
    const std::size_t last = centers.size() - 1;
    for (std::size_t k = 0; k <= last; ++k) {
        const glm::vec3 along = glm::normalize(centers[std::min(k + 1, last)] - centers[(k > 0) ? (k - 1) : 0]);
        const glm::vec3 up    = glm::normalize(glm::cross(along, side));
        for (int i = 0; i < sides; ++i) {
            const glm::vec2 circle = get_kit_circle_point(i, sides);
            // (side, along, -up) is right-handed like (x, y, z): the ring runs counter-clockwise seen from the limb's end.
            points.push_back(centers[k] + (radii[k] * ((circle.x * side) - (circle.y * up))));
        }
    }
    const glm::vec3 start_along = glm::normalize(centers[1] - centers[0]);
    const glm::vec3 end_along   = glm::normalize(centers[last] - centers[last - 1]);
    const uint32_t  first       = static_cast<uint32_t>(builder.get_positions().size());
    builder.add_ring_solid(points, sides, centers[0] - (radii[0] * start_along), centers[last] + ((2.0f * radii[last]) * end_along));
    builder.set_smooth_from(first); // round however few its sides
}

constexpr uint32_t tree_bark_slot    = 0;
constexpr uint32_t tree_foliage_slot = 1;
constexpr int      tree_trunk_sides  = 8;
constexpr int      tree_limb_sides   = 5;

class Tree_trunk
{
public:
    float     height{1.0f};
    float     radius{0.1f}; // above the root flare
    glm::vec3 bend  {0.0f}; // the top's horizontal drift

    // The trunk's axis at the share t of its height: the bend grows with the square of the height.
    [[nodiscard]] auto center(const float t) const -> glm::vec3
    {
        return glm::vec3{bend.x * t * t, t * height, bend.z * t * t};
    }
};

// Rings from a lobed root flare on the ground up to a point; the base's center vertex is the origin.
void add_trunk(Kit_mesh_builder& builder, Kit_random& random, const Tree_trunk& trunk, const float detail)
{
    class Station { public: float t; float radius; float lobes; };
    constexpr std::array<Station, 5> stations{{{0.00f, 1.75f, 0.30f}, {0.07f, 1.22f, 0.12f}, {0.24f, 1.00f, 0.0f}, {0.62f, 0.80f, 0.0f}, {1.00f, 0.50f, 0.0f}}};
    std::array<float, tree_trunk_sides> roots{};
    for (float& root : roots) {
        root = next_signed(random);
    }
    const int sides = kit_detail_count(detail, tree_trunk_sides, 4, 2 * tree_trunk_sides);
    std::vector<glm::vec3> points;
    points.reserve(stations.size() * static_cast<std::size_t>(sides));
    for (const Station& station : stations) {
        const glm::vec3 center = trunk.center(station.t);
        for (int i = 0; i < sides; ++i) {
            const glm::vec2 circle = get_kit_circle_point(i, sides);
            const float     root   = sample_ring(roots, static_cast<float>(i * tree_trunk_sides) / static_cast<float>(sides));
            const float     reach  = trunk.radius * station.radius * (1.0f + (station.lobes * root));
            points.push_back(center + glm::vec3{reach * circle.x, 0.0f, reach * circle.y});
        }
    }
    const glm::vec3 top   = trunk.center(1.0f) + glm::vec3{0.0f, trunk.radius, 0.0f};
    const uint32_t  first = static_cast<uint32_t>(builder.get_positions().size());
    builder.add_ring_solid(points, sides, glm::vec3{0.0f}, top);
    builder.set_smooth_from(first);
    builder.add_hard_loop(first, static_cast<uint32_t>(sides)); // the flat base on the ground
}

// A limb from the trunk at the share t of its height out to the end, with an elbow lifted above the straight line.
void add_branch(Kit_mesh_builder& builder, const Tree_trunk& trunk, const float t, const glm::vec3& end, const float radius, const float lift, const float detail)
{
    // No lower than the limb's own width above the ground.
    const glm::vec3 start  = trunk.center(std::max(t, 2.2f * radius / trunk.height));
    const glm::vec3 out    = end - start;
    const glm::vec3 side   = glm::normalize(glm::vec3{out.z, 0.0f, -out.x});
    const glm::vec3 elbow  = start + (0.5f * out) + glm::vec3{0.0f, lift, 0.0f};
    const std::array<glm::vec3, 3> centers{start, elbow, end};
    const std::array<float, 3>     radii{radius, 0.75f * radius, 0.45f * radius};
    add_limb(builder, centers, radii, side, kit_detail_count(detail, tree_limb_sides, 3, 2 * tree_limb_sides));
}

void add_broadleaf(Kit_mesh_builder& builder, Kit_random& random, const float trunk_height, const float canopy_size, const float detail)
{
    const float canopy_height = 0.85f * canopy_size;
    Tree_trunk trunk;
    trunk.height = trunk_height + (0.5f * canopy_height);
    trunk.radius = std::max(0.07f * canopy_size, 0.02f * (trunk_height + canopy_size));
    const float bend_angle = random.next_range(0.0f, two_pi);
    const float bend_draw  = random.next_range(0.0f, 0.1f);
    trunk.bend = (bend_draw * canopy_size) * around_y(bend_angle);
    builder.set_slot(tree_bark_slot);
    add_trunk(builder, random, trunk, detail);

    const int       branch_count  = 3 + static_cast<int>(random.next_u32() % 2u);
    const float     first_azimuth = random.next_range(0.0f, two_pi);
    const glm::vec3 crown         = trunk.center(1.0f);
    const float     trunk_share   = trunk_height / trunk.height;
    for (int b = 0; b < branch_count; ++b) {
        const float azimuth_draw = next_signed(random);
        const float start_draw   = random.next_range(0.72f, 1.0f);
        const float reach_draw   = random.next_range(0.25f, 0.31f);
        const float rise_draw    = random.next_range(0.30f, 0.55f);
        const float size_draw    = random.next_range(0.85f, 1.15f);
        const glm::vec3 out = around_y(first_azimuth + (two_pi * (static_cast<float>(b) + (0.3f * azimuth_draw)) / static_cast<float>(branch_count)));
        const glm::vec3 end{crown.x + (reach_draw * canopy_size * out.x), trunk_height + (rise_draw * canopy_height), crown.z + (reach_draw * canopy_size * out.z)};
        builder.set_slot(tree_bark_slot);
        add_branch(builder, trunk, trunk_share * start_draw, end, 0.5f * trunk.radius, 0.06f * canopy_size, detail);
        builder.set_slot(tree_foliage_slot);
        const glm::vec3 radii = (canopy_size * size_draw) * glm::vec3{0.29f, 0.22f, 0.29f};
        add_clump(builder, random, end + glm::vec3{0.0f, 0.04f * canopy_size, 0.0f}, radii, Clump_base::round, detail);
    }
    // The crown clump on the trunk's top and two small ones that break the outline.
    builder.set_slot(tree_foliage_slot);
    add_clump(builder, random, glm::vec3{crown.x, trunk_height + (0.70f * canopy_height), crown.z}, canopy_size * glm::vec3{0.34f, 0.25f, 0.34f}, Clump_base::round, detail);
    for (int k = 0; k < 2; ++k) {
        const float azimuth    = random.next_range(0.0f, two_pi);
        const float reach_draw = random.next_range(0.30f, 0.38f);
        const float rise_draw  = random.next_range(0.35f, 0.80f);
        const glm::vec3 out    = around_y(azimuth);
        const glm::vec3 center{crown.x + (reach_draw * canopy_size * out.x), trunk_height + (rise_draw * canopy_height), crown.z + (reach_draw * canopy_size * out.z)};
        add_clump(builder, random, center, canopy_size * glm::vec3{0.17f, 0.13f, 0.17f}, Clump_base::round, detail);
    }
}

// A tier of a conifer: a skirt with a ragged drooping edge, hollow underneath, closed about the trunk.
void add_conifer_tier(Kit_mesh_builder& builder, Kit_random& random, const glm::vec3& base, const float radius, const float height, const float detail)
{
    constexpr int own_sides = 9;
    const float phase = random.next_range(0.0f, two_pi);
    const float droop = std::min(0.12f * height, 0.7f * base.y);
    // The draws of the default detail's points, ring by ring, whatever the detail.
    std::array<std::array<float, own_sides>, 3> radial_draws{};
    std::array<std::array<float, own_sides>, 3> droop_draws{};
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t i = 0; i < own_sides; ++i) {
            radial_draws[r][i] = next_signed(random);
            droop_draws[r][i]  = random.next_unit();
        }
    }
    const int sides = kit_detail_count(detail, own_sides, 5, 2 * own_sides);
    std::vector<glm::vec3> points;
    points.reserve(3 * static_cast<std::size_t>(sides));
    for (int r = 0; r < 3; ++r) {
        for (int i = 0; i < sides; ++i) {
            const float     position    = static_cast<float>(i * own_sides) / static_cast<float>(sides);
            const float     radial_draw = sample_ring(radial_draws[static_cast<std::size_t>(r)], position);
            const float     droop_draw  = sample_ring(droop_draws[static_cast<std::size_t>(r)], position);
            const glm::vec3 direction   = around_y(phase + (two_pi * position / static_cast<float>(own_sides)));
            float reach = 0.28f * radius;
            float y     = 0.10f * height;
            if (r == 1) {
                reach = radius * (1.0f + (0.2f * radial_draw));
                y     = -droop * droop_draw;
            } else if (r == 2) {
                reach = 0.5f * radius * (1.0f + (0.12f * radial_draw));
                y     = 0.42f * height;
            }
            points.push_back(base + glm::vec3{reach * direction.x, y, reach * direction.z});
        }
    }
    const uint32_t first = static_cast<uint32_t>(builder.get_positions().size());
    builder.add_ring_solid(points, sides, base + glm::vec3{0.0f, 0.16f * height, 0.0f}, base + glm::vec3{0.0f, height, 0.0f});
    builder.set_smooth_from(first);
    builder.add_hard_loop(first + static_cast<uint32_t>(sides), static_cast<uint32_t>(sides)); // the drooping rim
}

void add_conifer(Kit_mesh_builder& builder, Kit_random& random, const float trunk_height, const float canopy_size, const float detail)
{
    const float canopy_height = 1.4f * canopy_size;
    Tree_trunk trunk;
    trunk.height = trunk_height + (0.9f * canopy_height);
    trunk.radius = std::max(0.055f * canopy_size, 0.015f * (trunk_height + canopy_height));
    const float bend_angle = random.next_range(0.0f, two_pi);
    const float bend_draw  = random.next_range(0.0f, 0.04f);
    trunk.bend = (bend_draw * canopy_size) * around_y(bend_angle);
    builder.set_slot(tree_bark_slot);
    add_trunk(builder, random, trunk, detail);

    builder.set_slot(tree_foliage_slot);
    const int   tier_count = 4 + static_cast<int>(random.next_u32() % 2u);
    const float step       = 0.8f * canopy_height / static_cast<float>(tier_count);
    for (int k = 0; k < tier_count; ++k) {
        const float share       = static_cast<float>(k) / static_cast<float>(tier_count);
        const float radius_draw = next_signed(random);
        const float y           = trunk_height + (static_cast<float>(k) * step);
        const float radius      = 0.5f * canopy_size * (1.0f - (0.78f * share)) * (1.0f + (0.08f * radius_draw));
        const float height      = ((k + 1) == tier_count) ? (trunk_height + canopy_height - y) : (1.75f * step);
        const glm::vec3 axis    = trunk.center(y / trunk.height);
        add_conifer_tier(builder, random, glm::vec3{axis.x, y, axis.z}, radius, height, detail);
    }
}

// A bare tree: forked limbs and a few last tufts of leaves.
void add_bare_tree(Kit_mesh_builder& builder, Kit_random& random, const float trunk_height, const float canopy_size, const float detail)
{
    constexpr int branch_count = 4;
    constexpr int tuft_count   = 3;
    Tree_trunk trunk;
    trunk.height = trunk_height + (0.85f * canopy_size);
    trunk.radius = std::max(0.07f * canopy_size, 0.02f * (trunk_height + canopy_size));
    const float bend_angle = random.next_range(0.0f, two_pi);
    const float bend_draw  = random.next_range(0.04f, 0.14f);
    trunk.bend = (bend_draw * canopy_size) * around_y(bend_angle);
    builder.set_slot(tree_bark_slot);
    add_trunk(builder, random, trunk, detail);

    const float first_azimuth = random.next_range(0.0f, two_pi);
    const float trunk_share   = trunk_height / trunk.height;
    for (int b = 0; b < branch_count; ++b) {
        const float share        = static_cast<float>(b) / static_cast<float>(branch_count - 1);
        const float azimuth_draw = next_signed(random);
        const float reach_draw   = random.next_range(0.32f, 0.46f);
        const float rise_draw    = random.next_range(0.25f, 0.45f);
        const float fork_turn    = random.next_range(0.5f, 0.9f);
        const float fork_side    = ((random.next_u32() % 2u) == 0u) ? 1.0f : -1.0f;
        const float t            = trunk_share + ((0.92f - trunk_share) * 0.8f * share);
        const float azimuth      = first_azimuth + (2.4f * static_cast<float>(b)) + (0.4f * azimuth_draw);
        const float reach        = reach_draw * canopy_size * (1.0f - (0.35f * share));
        const glm::vec3 start    = trunk.center(t);
        const glm::vec3 out      = around_y(azimuth);
        const glm::vec3 end      = start + (reach * out) + glm::vec3{0.0f, rise_draw * canopy_size, 0.0f};
        builder.set_slot(tree_bark_slot);
        add_branch(builder, trunk, t, end, 0.42f * trunk.radius, 0.05f * canopy_size, detail);
        // The fork leaves the limb's elbow to one side.
        const glm::vec3 elbow      = start + (0.5f * (end - start)) + glm::vec3{0.0f, 0.05f * canopy_size, 0.0f};
        const glm::vec3 fork_out   = around_y(azimuth + (fork_side * fork_turn));
        const glm::vec3 fork_end   = elbow + ((0.6f * reach) * fork_out) + glm::vec3{0.0f, 0.75f * rise_draw * canopy_size, 0.0f};
        const glm::vec3 fork_side_normal = glm::normalize(glm::vec3{fork_out.z, 0.0f, -fork_out.x});
        const std::array<glm::vec3, 3> centers{elbow, elbow + (0.5f * (fork_end - elbow)) + glm::vec3{0.0f, 0.03f * canopy_size, 0.0f}, fork_end};
        const std::array<float, 3>     radii{0.26f * trunk.radius, 0.2f * trunk.radius, 0.12f * trunk.radius};
        add_limb(builder, centers, radii, fork_side_normal, kit_detail_count(detail, tree_limb_sides, 3, 2 * tree_limb_sides));
        if (b < tuft_count) {
            builder.set_slot(tree_foliage_slot);
            add_clump(builder, random, end, canopy_size * glm::vec3{0.09f, 0.07f, 0.09f}, Clump_base::round, detail);
        }
    }
}

void generate_tree(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float            trunk_height = values.get_float("trunk_height");
    const float            canopy_size  = values.get_float("canopy_size");
    const std::string_view species      = values.get_enum("species");
    const float            detail       = get_kit_detail_factor(values);
    Kit_random random{values.get_seed()};
    Kit_mesh_builder builder;
    if (species == "conifer") {
        add_conifer(builder, random, trunk_height, canopy_size, detail);
    } else if (species == "bare") {
        add_bare_tree(builder, random, trunk_height, canopy_size, detail);
    } else {
        add_broadleaf(builder, random, trunk_height, canopy_size, detail);
    }
    builder.write(mesh);
}

constexpr uint32_t bush_foliage_slot = 0;
constexpr uint32_t bush_bark_slot    = 1;
constexpr int      bush_twig_max     = 6;

void generate_bush(const Kit_parameter_values& values, GEO::Mesh& mesh)
{
    const float size  = values.get_float("size");
    const int   twigs = std::clamp(values.get_int("twigs"), 1, bush_twig_max);
    const float detail = get_kit_detail_factor(values);
    Kit_random random{values.get_seed()};
    Kit_mesh_builder builder;

    // The heart on the origin, a ring of lower clumps about it, small ones on top.
    builder.set_slot(bush_foliage_slot);
    add_clump(builder, random, glm::vec3{0.0f}, size * glm::vec3{0.30f, 0.56f, 0.30f}, Clump_base::flat, detail);
    const int   ring_count    = 4 + static_cast<int>(random.next_u32() % 2u);
    const float first_azimuth = random.next_range(0.0f, two_pi);
    for (int k = 0; k < ring_count; ++k) {
        const float azimuth_draw = next_signed(random);
        const float reach_draw   = random.next_range(0.22f, 0.30f);
        const float width_draw   = random.next_range(0.20f, 0.27f);
        const float height_draw  = random.next_range(0.26f, 0.46f);
        const glm::vec3 out = around_y(first_azimuth + (two_pi * (static_cast<float>(k) + (0.3f * azimuth_draw)) / static_cast<float>(ring_count)));
        add_clump(builder, random, (reach_draw * size) * out, size * glm::vec3{width_draw, height_draw, width_draw}, Clump_base::flat, detail);
    }
    for (int k = 0; k < 2; ++k) {
        const float azimuth    = random.next_range(0.0f, two_pi);
        const float reach_draw = random.next_range(0.10f, 0.22f);
        const float rise_draw  = random.next_range(0.36f, 0.46f);
        const glm::vec3 out    = around_y(azimuth);
        add_clump(builder, random, glm::vec3{reach_draw * size * out.x, rise_draw * size, reach_draw * size * out.z}, size * glm::vec3{0.14f, 0.12f, 0.14f}, Clump_base::round, detail);
    }

    // Twigs out of the heart, past the leaves.
    builder.set_slot(bush_bark_slot);
    const float twig_azimuth = random.next_range(0.0f, two_pi);
    for (int k = 0; k < twigs; ++k) {
        const float azimuth_draw = next_signed(random);
        const float rise_draw    = random.next_range(0.50f, 0.72f);
        const float reach_draw   = random.next_range(0.22f, 0.42f);
        const glm::vec3 out   = around_y(twig_azimuth + (2.4f * static_cast<float>(k)) + (0.4f * azimuth_draw));
        const glm::vec3 side  = glm::vec3{out.z, 0.0f, -out.x};
        const glm::vec3 start = glm::vec3{0.0f, 0.12f * size, 0.0f};
        const glm::vec3 end   = (reach_draw * size) * out + glm::vec3{0.0f, rise_draw * size, 0.0f};
        const glm::vec3 elbow = start + (0.55f * (end - start)) + ((0.05f * size) * out);
        const std::array<glm::vec3, 3> centers{start, elbow, end};
        const std::array<float, 3>     radii{0.022f * size, 0.016f * size, 0.009f * size};
        add_limb(builder, centers, radii, side, kit_detail_count(detail, 4, 3, 8));
    }
    builder.write(mesh);
}

} // anonymous namespace

void add_nature_shapes(std::vector<Kit_shape_definition>& shapes)
{
    {
        Kit_shape_definition shape;
        shape.id              = "nature.rock";
        shape.former_ids      = {"nature.rock.v2"};
        shape.kit             = "nature";
        shape.display_name    = "Rock";
        shape.tags            = {"obstacle", "decoration", "size:1x1"};
        shape.material_slots  = {"rock"};
        shape.normal_style    = Kit_normal_style::smooth; // when the shading parameter is smooth; base and flat tops are hard edges
        shape.default_shading = Kit_shading::faceted;
        shape.triangle_budget = 200;
        shape.randomness      = Kit_randomness::seeded;
        shape.generator       = generate_rock;
        shape.description =
            "Rock of one to four pieces, the largest at the origin: each a stack of convex strata rings on a flat base whose "
            "side faces are planar, some sides one fracture plane from foot to top; the strata dip and the piece leans with "
            "the roughness. Forms: boulder, angular (tall shards), slab (low and wide), layered (ledges and overhangs).";
        shape.parameters  = {
            derive_enum(make_enum_parameter("form", "Boulder, angular shard, low slab or layered ledges", {"boulder", "angular", "slab", "layered"}, 0)),
            from_seed_by_default(derive_float(make_length_parameter("size", "Width of the largest piece before roughness", 1.0f, Kit_default_scale::modules, 0.05f, 32.0f), 0.6f, 1.5f, 0.05f)),
            derive_float(make_ratio_parameter("roughness", "Irregular sides, strata dip and lean", 0.5f), 0.25f, 0.8f, 0.01f),
            derive_int(make_count_parameter("pieces", "The largest piece and the smaller ones against its flank", 3, 1, rock_piece_max), 1, rock_piece_max),
            make_seed_parameter()
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.land_mass";
        shape.former_ids      = {"nature.land_mass.v2"};
        shape.kit             = "nature";
        shape.display_name    = "Land mass";
        shape.tags            = {"walkable", "ground", "size:4x4x1"};
        shape.normal_style    = Kit_normal_style::smooth; // when the shading parameter is smooth
        shape.default_shading = Kit_shading::faceted;
        shape.triangle_budget = 280;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.randomness      = Kit_randomness::seeded;
        shape.detail_counts   = {{"segments"}};
        shape.generator       = generate_land_mass;
        shape.description =
            "Plateau with a lobed outline and a flat walkable top of turf: an overhanging turf rim over a soil band, a cliff "
            "with gullies and a bench or two of turf, and a talus foot.";
        shape.parameters  = {
            derive_float(make_length_parameter("footprint", "Diameter of the foot before the outline's lobes", 4.0f, Kit_default_scale::modules, 0.5f, 128.0f), 3.0f, 8.0f, 0.5f),
            derive_float(make_length_parameter("height",    "Height of the flat top",                          1.0f, Kit_default_scale::modules, 0.05f, 32.0f), 0.5f, 2.0f, 0.25f),
            from_seed_by_default(derive_float(make_ratio_parameter("erosion", "Depth of the gullies and roughness of the cliff", 0.5f), 0.2f, 0.9f, 0.01f)),
            make_seed_parameter (),
            make_count_parameter("segments", "Columns around the axis", 20, 8, 48)
        };
        shape.material_slots = {"grass", "earth", "rock"};
        shape.get_connectors = [](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
            add_base_and_top_connectors(out_connectors, values.get_float("height"));
        };
        shape.check_walkability = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            const float base_radius = 0.5f * values.get_float("footprint");
            const float run         = land_mass_run(base_radius, values.get_float("height"));
            const float across      = 2.0f * (1.0f - land_lobe_depth) * (base_radius - (land_rim_inset * run));
            const float need        = (2.0f * agent.radius) + kit_opening_margin;
            if (across < need) {
                return fmt::format("{}: flat top at least {} m across is narrower than the agent's {} m", values.get_shape()->id, across, need);
            }
            return {};
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.hill";
        shape.former_ids      = {"nature.hill.v2"};
        shape.kit             = "nature";
        shape.display_name    = "Hill / mound";
        shape.tags            = {"walkable", "ground", "size:6x6x1"};
        shape.triangle_budget = 320;
        shape.randomness      = Kit_randomness::seeded;
        shape.detail_counts   = {{"segments", 2}};
        shape.generator       = generate_hill;
        shape.description =
            "Hill with a lobed foot and its crest off center: one side steep, the other long, a rounded crest and an easing "
            "foot; rocks crop out of the steep side (slot rock). Its sides are walkable.";
        shape.parameters  = {
            // Derived height over radius stays at most 0.36, which keeps the steep side under the agent's slope (R2.10).
            derive_float(make_length_parameter("radius", "Radius of the foot before its lobes", 3.0f, Kit_default_scale::modules, 0.5f, 128.0f), 2.5f, 6.0f, 0.5f),
            from_seed_by_default(derive_float(make_length_parameter("height", "Height of the crest", 1.0f, Kit_default_scale::modules, 0.05f, 32.0f), 0.5f, 0.9f, 0.05f)),
            derive_int(make_count_parameter("rocks", "Rocks cropping out of the steep side", 2, 1, hill_rock_max), 1, 3),
            make_seed_parameter (),
            make_count_parameter("segments", "Columns around the crest; rings are a quarter of it and one", 18, 8, 40)
        };
        shape.material_slots          = {"grass", "rock"};
        shape.normal_style            = Kit_normal_style::smooth_with_hard_edges;
        shape.hard_edge_angle_degrees = 38.0f;
        shape.default_shading   = Kit_shading::smooth;
        shape.check_walkability = [](const Kit_parameter_values& values, const Kit_agent& agent) -> std::string {
            Kit_random random{values.get_seed()};
            const float steepest = get_hill_steepest_degrees(make_hill_surface(values, random));
            if (steepest > agent.max_slope_degrees) {
                return fmt::format("{}: slope {} degrees is steeper than the agent's {} degrees", values.get_shape()->id, steepest, agent.max_slope_degrees);
            }
            return {};
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.tree";
        shape.foliage_radius  = 1.5f;
        shape.former_ids      = {"nature.tree.v2"};
        shape.kit             = "nature";
        shape.display_name    = "Tree";
        shape.tags            = {"obstacle", "decoration", "size:2x2x3"};
        shape.material_slots  = {"bark", "foliage"};
        shape.normal_style    = Kit_normal_style::smooth; // round limbs and clumps; hard edges at flat bases and tier rims
        shape.default_shading = Kit_shading::smooth;
        shape.triangle_budget = 480;
        shape.collision       = Kit_collision::triangle_mesh;
        shape.randomness      = Kit_randomness::seeded;
        shape.detail_response = Kit_detail_response::scaled;
        shape.generator       = generate_tree;
        shape.description =
            "Tree standing at the origin: a tapering trunk with a root flare (slot bark) and, by species, limbs carrying "
            "clumps of leaves (broadleaf), tiers of drooping skirts (conifer) or forked bare limbs with a few last tufts "
            "(bare), all slot foliage. Every part is closed and they overlap.";
        shape.parameters  = {
            from_seed_by_default(derive_float(make_length_parameter("trunk_height", "Height of the canopy's base", 0.6f, Kit_default_scale::wall_heights, 0.1f, 32.0f), 0.4f, 0.8f, 0.05f)),
            from_seed_by_default(derive_float(make_length_parameter("canopy_size",  "Diameter of the canopy",      2.0f, Kit_default_scale::modules,      0.2f, 32.0f), 1.5f, 3.0f, 0.1f)),
            derive_enum(make_enum_parameter("species", "Broadleaf, conifer or bare", {"broadleaf", "conifer", "bare"}, 0)),
            make_seed_parameter()
        };
        shapes.push_back(std::move(shape));
    }
    {
        Kit_shape_definition shape;
        shape.id              = "nature.bush";
        shape.foliage_radius  = 0.6f;
        shape.former_ids      = {"nature.bush.v2"};
        shape.kit             = "nature";
        shape.display_name    = "Bush";
        shape.tags            = {"obstacle", "decoration", "size:1x1"};
        shape.normal_style    = Kit_normal_style::smooth; // round limbs and clumps; hard edges at flat bases and tier rims
        shape.default_shading = Kit_shading::smooth;
        shape.triangle_budget = 420;
        shape.randomness      = Kit_randomness::seeded;
        shape.detail_response = Kit_detail_response::scaled;
        shape.generator       = generate_bush;
        shape.description =
            "Bush on the origin: a heart clump, a ring of lower clumps about it and small ones on top (slot foliage), with "
            "bare twigs reaching past the leaves (slot bark). Every part is closed and they overlap.";
        shape.parameters  = {
            from_seed_by_default(derive_float(make_length_parameter("size", "Diameter", 1.0f, Kit_default_scale::modules, 0.1f, 16.0f), 0.6f, 1.4f, 0.05f)),
            derive_int(make_count_parameter("twigs", "Bare twigs reaching past the leaves", 3, 1, bush_twig_max), 2, 4),
            make_seed_parameter()
        };
        shape.material_slots = {"foliage", "bark"};
        shapes.push_back(std::move(shape));
    }
    add_nature_terrain_shapes(shapes);
    add_nature_rock_shapes   (shapes);
    add_nature_plant_shapes  (shapes);
}

} // namespace editor
