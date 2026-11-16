#include "level/stairs_mesh.hpp"

#include "level/kit_shape_checks.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_math/curve_path.hpp"

#include <fmt/format.h>
#include <geogram/mesh/mesh.h>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

namespace {

using editor::Kit_mesh_report;
using editor::Stairs_report;
using editor::Stairs_values;
using erhe::math::Curve_path;
using erhe::math::Curve_path_table;
using erhe::math::Curve_point;
using erhe::math::Curve_point_kind;

auto describe(const Kit_mesh_report& report) -> std::string
{
    return fmt::format(
        "{} vertices, {} facets, {} triangles, {} components; problems: {}",
        report.vertex_count, report.facet_count, report.triangle_count, report.component_count,
        report.problems.empty() ? std::string{"none"} : report.problems.front()
    );
}

auto corner(const glm::vec3 position) -> Curve_point
{
    return Curve_point{.position = position};
}

auto smooth(const glm::vec3 position, const glm::vec3 out_handle) -> Curve_point
{
    return Curve_point{.position = position, .in_handle = -out_handle, .out_handle = out_handle, .kind = Curve_point_kind::smooth};
}

// A quarter circle from the origin heading +x and turning towards -z, climbing evenly.
auto make_quarter_turn(const float radius, const float rise) -> Curve_path
{
    const float handle = 0.5522847f * radius;
    Curve_path path;
    path.points.push_back(smooth(glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{handle, rise / 3.0f, 0.0f}));
    path.points.push_back(smooth(glm::vec3{radius, rise, -radius}, glm::vec3{0.0f, rise / 3.0f, -handle}));
    return path;
}

// Heads +x, swings to -z and back, climbing, level in the middle, climbing again.
auto make_s_curve() -> Curve_path
{
    Curve_path path;
    path.points.push_back(smooth(glm::vec3{0.0f, 0.0f,  0.0f}, glm::vec3{2.0f, 0.5f,  0.0f}));
    path.points.push_back(smooth(glm::vec3{5.0f, 1.2f, -3.0f}, glm::vec3{2.0f, 0.0f, -1.0f}));
    path.points.push_back(smooth(glm::vec3{10.0f, 1.2f, -5.0f}, glm::vec3{2.0f, 0.0f, 1.0f}));
    path.points.push_back(smooth(glm::vec3{15.0f, 2.4f, -3.0f}, glm::vec3{2.0f, 0.5f, 0.0f}));
    return path;
}

// Builds the stairs and holds them to the kit mesh checks; returns the refusal.
auto build(const Curve_path& path, const Stairs_values& values, Stairs_report& report, Kit_mesh_report& mesh_report, GEO::Mesh& mesh) -> std::string
{
    Curve_path_table table;
    table.build(path);
    const std::string refusal = editor::make_stairs(table, values, report, &mesh);
    if (refusal.empty()) {
        mesh_report = editor::check_kit_mesh(mesh);
    }
    return refusal;
}

void expect_valid(const Curve_path& path, const Stairs_values& values, const char* const name)
{
    Stairs_report   report;
    Kit_mesh_report mesh_report;
    GEO::Mesh       mesh{3, true};
    const std::string refusal = build(path, values, report, mesh_report, mesh);
    ASSERT_EQ(refusal, "") << name;
    EXPECT_TRUE(mesh_report.is_valid()) << name << ": " << describe(mesh_report);
    EXPECT_EQ(mesh_report.component_count, 1u) << name;
    EXPECT_LE(report.fit.max_deviation, values.max_rise + 1.0e-3f) << name;
    ::testing::Test::RecordProperty(fmt::format("{}_triangles", name), static_cast<int>(mesh_report.triangle_count));
    ::testing::Test::RecordProperty(fmt::format("{}_steps", name), report.fit.step_count);
}

TEST(Stairs_mesh, a_straight_flight_is_a_closed_solid)
{
    Curve_path path;
    path.points.push_back(corner(glm::vec3{0.0f, 0.0f, 0.0f}));
    path.points.push_back(corner(glm::vec3{3.0f, 2.0f, 0.0f}));
    const Stairs_values values{};
    Stairs_report   report;
    Kit_mesh_report mesh_report;
    GEO::Mesh       mesh{3, true};
    ASSERT_EQ(build(path, values, report, mesh_report, mesh), "");
    EXPECT_TRUE(mesh_report.is_valid()) << describe(mesh_report);
    EXPECT_EQ(mesh_report.component_count, 1u);
    EXPECT_EQ(report.fit.step_count, 10);
    ASSERT_EQ(report.fit.flights.size(), 1u);
    EXPECT_TRUE(report.fit.landings.empty());
    // No vertex beyond the risers on a straight plan: 11 cross-sections.
    EXPECT_EQ(report.section_count, 11u);
    EXPECT_NEAR(mesh_report.bounds_min.x, 0.0f, 1.0e-4f);
    EXPECT_NEAR(mesh_report.bounds_max.x, 3.0f, 1.0e-4f);
    EXPECT_NEAR(mesh_report.bounds_min.y, -0.2f, 1.0e-4f);
    EXPECT_NEAR(mesh_report.bounds_max.y, 2.0f, 1.0e-4f);
    EXPECT_NEAR(mesh_report.bounds_min.z, -0.5f, 1.0e-4f);
    EXPECT_NEAR(mesh_report.bounds_max.z, 0.5f, 1.0e-4f);
    ::testing::Test::RecordProperty("triangles", static_cast<int>(mesh_report.triangle_count));
    EXPECT_LE(mesh_report.triangle_count, 140u);

    // The fit alone gives the same report and writes no mesh.
    Curve_path_table table;
    table.build(path);
    Stairs_report fit_only;
    ASSERT_EQ(editor::make_stairs(table, values, fit_only, nullptr), "");
    EXPECT_EQ(fit_only.fit.step_count, 10);
    EXPECT_EQ(fit_only.section_count, 11u);
}

TEST(Stairs_mesh, a_quarter_turn_has_smooth_side_edges)
{
    const Stairs_values values{.width = 1.0f, .tolerance = 0.005f};
    Stairs_report   report;
    Kit_mesh_report mesh_report;
    GEO::Mesh       mesh{3, true};
    ASSERT_EQ(build(make_quarter_turn(4.0f, 1.2f), values, report, mesh_report, mesh), "");
    EXPECT_TRUE(mesh_report.is_valid()) << describe(mesh_report);
    EXPECT_EQ(mesh_report.component_count, 1u);
    EXPECT_EQ(report.fit.step_count, 6);
    EXPECT_NEAR(report.fit.total_rise, 1.2f, 1.0e-6f);
    // Every vertex is on the inner or the outer arc about the turn's center (the cubic leaves the circle by 0.03 %).
    const glm::vec2 center{0.0f, -4.0f};
    std::size_t inner = 0;
    std::size_t outer = 0;
    for (const GEO::index_t vertex : mesh.vertices) {
        const GEO::vec3f position = erhe::geometry::get_pointf(mesh.vertices, vertex);
        const float radius = glm::length(glm::vec2{position.x, position.z} - center);
        if (radius < 4.0f) {
            EXPECT_NEAR(radius, 3.5f, 5.0e-3f);
            ++inner;
        } else {
            EXPECT_NEAR(radius, 4.5f, 5.0e-3f);
            ++outer;
        }
    }
    EXPECT_EQ(inner, outer);
    // The outer arc of 7.07 m at a 5 mm chord tolerance takes more cross-sections than the 6 risers.
    EXPECT_GT(report.section_count, 12u);
    // A coarser tolerance takes fewer.
    Stairs_report   coarse_report;
    Kit_mesh_report coarse_mesh_report;
    GEO::Mesh       coarse{3, true};
    ASSERT_EQ(build(make_quarter_turn(4.0f, 1.2f), Stairs_values{.width = 1.0f, .tolerance = 0.1f}, coarse_report, coarse_mesh_report, coarse), "");
    EXPECT_TRUE(coarse_mesh_report.is_valid()) << describe(coarse_mesh_report);
    EXPECT_LT(coarse_report.section_count, report.section_count);
}

TEST(Stairs_mesh, a_corner_point_gets_a_mitred_landing)
{
    Curve_path path;
    path.points.push_back(corner(glm::vec3{0.0f, 0.0f,  0.0f}));
    path.points.push_back(corner(glm::vec3{3.0f, 0.6f,  0.0f}));
    path.points.push_back(corner(glm::vec3{3.0f, 1.2f, -3.0f}));
    const Stairs_values values{};
    Stairs_report   report;
    Kit_mesh_report mesh_report;
    GEO::Mesh       mesh{3, true};
    ASSERT_EQ(build(path, values, report, mesh_report, mesh), "");
    EXPECT_TRUE(mesh_report.is_valid()) << describe(mesh_report);
    EXPECT_EQ(report.fit.step_count, 6);
    // The mitre line runs from the inner corner (2.5, -0.5) to the outer corner (3.5, 0.5).
    bool inner_found = false;
    bool outer_found = false;
    for (const GEO::index_t vertex : mesh.vertices) {
        const GEO::vec3f position = erhe::geometry::get_pointf(mesh.vertices, vertex);
        inner_found = inner_found || ((std::abs(position.x - 2.5f) < 1.0e-4f) && (std::abs(position.z + 0.5f) < 1.0e-4f));
        outer_found = outer_found || ((std::abs(position.x - 3.5f) < 1.0e-4f) && (std::abs(position.z - 0.5f) < 1.0e-4f));
    }
    EXPECT_TRUE(inner_found);
    EXPECT_TRUE(outer_found);
    // No riser within the corner's landing: half the width each side, and the margin.
    for (const editor::Stairs_riser& riser : report.fit.risers) {
        EXPECT_GE(std::abs(riser.distance - 3.0f), 0.5f + editor::c_stairs_corner_margin - 1.0e-4f) << riser.distance;
    }
    EXPECT_NEAR(mesh_report.bounds_max.x, 3.5f, 1.0e-4f);
    EXPECT_NEAR(mesh_report.bounds_max.z, 0.5f, 1.0e-4f);
}

// The largest distance of a side edge's chords from the curve's true plan offset, sampled along each chord.
auto get_edge_error(const Curve_path& path, const float half_width, const GEO::Mesh& mesh) -> float
{
    Curve_path_table table;
    table.build(path);
    // Dense samples of the centre line in plan.
    std::vector<glm::vec2> center;
    for (std::size_t s = 0; s < table.get_span_count(); ++s) {
        for (int i = 0; i <= 2000; ++i) {
            const glm::vec3 p = erhe::math::get_span_position(table.get_span(s), static_cast<float>(i) / 2000.0f);
            center.push_back(glm::vec2{p.x, p.z});
        }
    }
    const auto offset_error = [&center, half_width](const glm::vec2 point) -> float {
        float nearest = 1.0e9f;
        for (const glm::vec2& c : center) {
            nearest = std::min(nearest, glm::length(point - c));
        }
        return std::abs(nearest - half_width);
    };
    float worst = 0.0f;
    for (const GEO::index_t facet : mesh.facets) {
        const GEO::index_t count = mesh.facets.nb_vertices(facet);
        for (GEO::index_t c = 0; c < count; ++c) {
            const GEO::vec3f a = erhe::geometry::get_pointf(mesh.vertices, mesh.facets.vertex(facet, c));
            const GEO::vec3f b = erhe::geometry::get_pointf(mesh.vertices, mesh.facets.vertex(facet, (c + 1) % count));
            // A side edge: level, both ends on the offset curve.
            const glm::vec2 pa{a.x, a.z};
            const glm::vec2 pb{b.x, b.z};
            if ((a.y != b.y) || (offset_error(pa) > 1.0e-3f) || (offset_error(pb) > 1.0e-3f) || (glm::length(pb - pa) > (1.9f * half_width))) {
                continue;
            }
            for (int i = 1; i < 8; ++i) {
                worst = std::max(worst, offset_error(glm::mix(pa, pb, static_cast<float>(i) / 8.0f)));
            }
        }
    }
    return worst;
}

TEST(Stairs_mesh, side_edges_keep_the_tolerance_on_curved_spans)
{
    for (const float tolerance : {0.002f, 0.01f, 0.05f}) {
        const Stairs_values values{.width = 1.0f, .tolerance = tolerance};
        Stairs_report   report;
        Kit_mesh_report mesh_report;
        GEO::Mesh       mesh{3, true};
        ASSERT_EQ(build(make_quarter_turn(3.0f, 1.0f), values, report, mesh_report, mesh), "");
        const float error = get_edge_error(make_quarter_turn(3.0f, 1.0f), 0.5f, mesh);
        EXPECT_LE(error, tolerance * 1.05f + 2.0e-4f) << tolerance;
        EXPECT_GT(error, tolerance * 0.1f) << tolerance;
    }
    // A tolerance that would take more cross-sections than the limit is refused naming the place, not followed loosely.
    Curve_path wide;
    wide.points.push_back(smooth(glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{33000.0f, 0.0f, 0.0f}));
    wide.points.push_back(smooth(glm::vec3{60000.0f, 0.0f, -60000.0f}, glm::vec3{0.0f, 0.0f, -33000.0f}));
    Stairs_report   report;
    Kit_mesh_report mesh_report;
    GEO::Mesh       mesh{3, true};
    const std::string refusal = build(wide, Stairs_values{.width = 32.0f, .tolerance = 0.001f}, report, mesh_report, mesh);
    EXPECT_NE(refusal.find("cross-sections"), std::string::npos) << refusal;
    EXPECT_NE(refusal.find("control point"), std::string::npos) << refusal;
}

TEST(Stairs_mesh, a_corner_landing_ends_at_cross_sections)
{
    // A corner between a straight and a curved span: the landing's two ends are cross-sections, so the curved edge
    // beyond them keeps the tolerance and the landing's own edges are the straight lines to the mitre.
    Curve_path path;
    path.points.push_back(corner(glm::vec3{0.0f, 0.0f, 0.0f}));
    path.points.push_back(Curve_point{.position = glm::vec3{4.0f, 0.0f, 0.0f}, .out_handle = glm::vec3{0.0f, 0.0f, -2.2f}, .kind = Curve_point_kind::free});
    path.points.push_back(Curve_point{.position = glm::vec3{8.0f, 0.5f, -4.0f}, .in_handle = glm::vec3{-2.2f, 0.0f, 0.0f}, .kind = Curve_point_kind::free});
    const Stairs_values values{.width = 1.0f};
    Stairs_report   report;
    Kit_mesh_report mesh_report;
    GEO::Mesh       mesh{3, true};
    ASSERT_EQ(build(path, values, report, mesh_report, mesh), "");
    EXPECT_TRUE(mesh_report.is_valid()) << describe(mesh_report);
    // The cross-section at the landing's begin, 0.52 m before the corner on the straight span.
    bool found = false;
    for (const GEO::index_t vertex : mesh.vertices) {
        const GEO::vec3f position = erhe::geometry::get_pointf(mesh.vertices, vertex);
        found = found || ((std::abs(position.x - 3.48f) < 1.0e-4f) && (std::abs(std::abs(position.z) - 0.5f) < 1.0e-4f));
    }
    EXPECT_TRUE(found);
}

TEST(Stairs_mesh, an_s_curve_with_height_is_a_closed_solid)
{
    expect_valid(make_s_curve(), Stairs_values{.width = 1.2f}, "s_curve");
    Curve_path_table table;
    table.build(make_s_curve());
    Stairs_report report;
    ASSERT_EQ(editor::make_stairs(table, Stairs_values{.width = 1.2f}, report, nullptr), "");
    EXPECT_EQ(report.fit.step_count, 12);
    EXPECT_GE(report.fit.flights.size(), 2u);
    EXPECT_GE(report.fit.landings.size(), 1u);
    EXPECT_NEAR(report.fit.total_rise, 2.4f, 1.0e-6f);
}

TEST(Stairs_mesh, up_and_down_is_a_closed_solid)
{
    Curve_path path;
    path.points.push_back(corner(glm::vec3{0.0f, 0.0f, 0.0f}));
    path.points.push_back(corner(glm::vec3{3.0f, 1.0f, 0.0f}));
    path.points.push_back(corner(glm::vec3{6.0f, 0.0f, 0.0f}));
    expect_valid(path, Stairs_values{}, "up_down");
    // A short top landing between two flights at the flight slope.
    Curve_path peak;
    peak.points.push_back(corner(glm::vec3{0.0f, 0.0f, 0.0f}));
    peak.points.push_back(corner(glm::vec3{1.5f, 1.0f, 0.0f}));
    peak.points.push_back(corner(glm::vec3{1.9f, 1.0f, 0.0f}));
    peak.points.push_back(corner(glm::vec3{3.4f, 0.0f, 0.0f}));
    expect_valid(peak, Stairs_values{}, "peak");
    Curve_path valley;
    valley.points.push_back(corner(glm::vec3{0.0f, 1.0f, 0.0f}));
    valley.points.push_back(corner(glm::vec3{1.5f, 0.0f, 0.0f}));
    valley.points.push_back(corner(glm::vec3{1.9f, 0.0f, 0.0f}));
    valley.points.push_back(corner(glm::vec3{3.4f, 1.0f, 0.0f}));
    expect_valid(valley, Stairs_values{}, "valley");
}

TEST(Stairs_mesh, refusals_name_the_place)
{
    Stairs_report   report;
    Kit_mesh_report mesh_report;
    GEO::Mesh       mesh{3, true};
    // A plan radius of 0.5 m under stairs 1 m wide.
    const std::string tight = build(make_quarter_turn(0.5f, 0.2f), Stairs_values{}, report, mesh_report, mesh);
    EXPECT_NE(tight.find("too tightly"), std::string::npos) << tight;
    EXPECT_NE(tight.find("control point"), std::string::npos) << tight;
    EXPECT_EQ(mesh.vertices.nb(), 0u);
    // The same turn carries stairs 0.5 m wide.
    EXPECT_EQ(build(make_quarter_turn(0.5f, 0.2f), Stairs_values{.width = 0.5f}, report, mesh_report, mesh), "");

    Curve_path steep;
    steep.points.push_back(corner(glm::vec3{0.0f, 0.0f, 0.0f}));
    steep.points.push_back(corner(glm::vec3{4.0f, 0.0f, 0.0f}));
    steep.points.push_back(corner(glm::vec3{5.0f, 3.0f, 0.0f}));
    GEO::Mesh steep_mesh{3, true};
    const std::string too_steep = build(steep, Stairs_values{}, report, mesh_report, steep_mesh);
    EXPECT_NE(too_steep.find("too steep"), std::string::npos) << too_steep;
    EXPECT_NE(too_steep.find("nearest control point"), std::string::npos) << too_steep;
    EXPECT_EQ(steep_mesh.vertices.nb(), 0u);

    Curve_path vertical;
    vertical.points.push_back(corner(glm::vec3{0.0f, 0.0f, 0.0f}));
    vertical.points.push_back(corner(glm::vec3{2.0f, 0.0f, 0.0f}));
    vertical.points.push_back(corner(glm::vec3{2.0f, 1.0f, 0.0f}));
    EXPECT_NE(build(vertical, Stairs_values{}, report, mesh_report, steep_mesh).find("vertical"), std::string::npos);

    Curve_path closed;
    closed.points.push_back(corner(glm::vec3{0.0f, 0.0f, 0.0f}));
    closed.points.push_back(corner(glm::vec3{4.0f, 0.0f, 0.0f}));
    closed.points.push_back(corner(glm::vec3{4.0f, 0.0f, 4.0f}));
    closed.closure = erhe::math::Curve_closure::closed;
    EXPECT_NE(build(closed, Stairs_values{}, report, mesh_report, steep_mesh).find("open curve"), std::string::npos);

    // A corner 0.2 m from the end has no room for its landing.
    Curve_path short_leg;
    short_leg.points.push_back(corner(glm::vec3{0.0f, 0.0f, 0.0f}));
    short_leg.points.push_back(corner(glm::vec3{3.0f, 0.0f, 0.0f}));
    short_leg.points.push_back(corner(glm::vec3{3.0f, 0.0f, -0.2f}));
    const std::string no_room = build(short_leg, Stairs_values{}, report, mesh_report, steep_mesh);
    EXPECT_NE(no_room.find("corner at control point 1"), std::string::npos) << no_room;

    Curve_path straight;
    straight.points.push_back(corner(glm::vec3{0.0f, 0.0f, 0.0f}));
    straight.points.push_back(corner(glm::vec3{3.0f, 1.0f, 0.0f}));
    EXPECT_NE(build(straight, Stairs_values{.width = 0.0f}, report, mesh_report, steep_mesh).find("width"), std::string::npos);
    EXPECT_NE(build(straight, Stairs_values{.thickness = 0.0f}, report, mesh_report, steep_mesh).find("thickness"), std::string::npos);
    EXPECT_NE(build(straight, Stairs_values{.tolerance = 0.0f}, report, mesh_report, steep_mesh).find("tolerance"), std::string::npos);
}

TEST(Stairs_mesh, the_mesh_checks_hold_over_a_sweep_of_values)
{
    unsigned int largest = 0;
    for (const float width : {0.5f, 1.0f, 2.5f}) {
        for (const float rise : {0.0f, 0.4f, 1.7f, 3.3f, -2.1f}) {
            for (const float radius : {2.0f, 5.5f}) {
                for (const float tolerance : {0.002f, 0.01f, 0.2f}) {
                    for (const float thickness : {0.05f, 0.3f}) {
                        const std::string name = fmt::format("w{} h{} r{} tol{} t{}", width, rise, radius, tolerance, thickness);
                        const Stairs_values values{.width = width, .thickness = thickness, .tolerance = tolerance};
                        Stairs_report   report;
                        Kit_mesh_report mesh_report;
                        GEO::Mesh       mesh{3, true};
                        const std::string refusal = build(make_quarter_turn(radius, rise), values, report, mesh_report, mesh);
                        // The arcs are 3.14 m and 8.64 m long: the steeper climbs do not fit the shorter one.
                        if (!refusal.empty()) {
                            EXPECT_TRUE((refusal.find("too steep") != std::string::npos) || (refusal.find("too short") != std::string::npos)) << name << ": " << refusal;
                            EXPECT_GT(std::abs(rise), (0.2f / 0.3f) * 1.5707963f * radius * 0.9f) << name << ": " << refusal;
                            continue;
                        }
                        EXPECT_TRUE(mesh_report.is_valid()) << name << ": " << describe(mesh_report);
                        EXPECT_EQ(mesh_report.component_count, 1u) << name;
                        EXPECT_LE(report.fit.max_deviation, values.max_rise + 1.0e-3f) << name;
                        EXPECT_NEAR(report.fit.total_rise, rise, 1.0e-5f) << name;
                        largest = std::max(largest, mesh_report.triangle_count);
                    }
                }
            }
        }
    }
    ::testing::Test::RecordProperty("largest_triangle_count", static_cast<int>(largest));
    // Step rules other than the defaults, on the S-curve, whose steepest part climbs about 0.3 m per meter.
    std::size_t fitted = 0;
    for (const float max_rise : {0.1f, 0.2f, 0.35f}) {
        for (const float min_tread : {0.25f, 0.4f, 0.8f}) {
            const std::string name = fmt::format("rise{} tread{}", max_rise, min_tread);
            const Stairs_values values{.width = 1.5f, .max_rise = max_rise, .min_tread = min_tread};
            Stairs_report   report;
            Kit_mesh_report mesh_report;
            GEO::Mesh       mesh{3, true};
            const std::string refusal = build(make_s_curve(), values, report, mesh_report, mesh);
            if (!refusal.empty()) {
                EXPECT_NE(refusal.find("too steep"), std::string::npos) << name << ": " << refusal;
                EXPECT_LT(max_rise / min_tread, 0.32f) << name << ": " << refusal;
                continue;
            }
            ++fitted;
            EXPECT_TRUE(mesh_report.is_valid()) << name << ": " << describe(mesh_report);
            EXPECT_LE(report.fit.max_deviation, max_rise + 1.0e-3f) << name;
            EXPECT_NEAR(report.fit.total_rise, 2.4f, 1.0e-5f) << name;
        }
    }
    EXPECT_GE(fitted, 5u);
}

// About a hundred steps: the size a live drag regenerates.
TEST(Stairs_mesh, a_hundred_steps_stay_within_the_budget)
{
    Curve_path path;
    path.points.push_back(smooth(glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{8.0f, 2.5f, 0.0f}));
    path.points.push_back(smooth(glm::vec3{20.0f, 10.0f, -12.0f}, glm::vec3{8.0f, 2.5f, 0.0f}));
    path.points.push_back(smooth(glm::vec3{40.0f, 20.0f, 0.0f}, glm::vec3{8.0f, 2.5f, 0.0f}));
    Stairs_report   report;
    Kit_mesh_report mesh_report;
    GEO::Mesh       mesh{3, true};
    ASSERT_EQ(build(path, Stairs_values{.width = 1.5f}, report, mesh_report, mesh), "");
    EXPECT_TRUE(mesh_report.is_valid()) << describe(mesh_report);
    EXPECT_EQ(report.fit.step_count, 100);
    ::testing::Test::RecordProperty("triangles", static_cast<int>(mesh_report.triangle_count));
    EXPECT_LE(mesh_report.triangle_count, 4000u);
}

} // anonymous namespace
