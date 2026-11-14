#include "erhe_math/curve_path.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <vector>

namespace {

using erhe::math::Curve_closure;
using erhe::math::Curve_end;
using erhe::math::Curve_frame;
using erhe::math::Curve_handle;
using erhe::math::Curve_location;
using erhe::math::Curve_path;
using erhe::math::Curve_path_table;
using erhe::math::Curve_point;
using erhe::math::Curve_point_kind;
using erhe::math::Curve_sample;
using erhe::math::Curve_span;
using erhe::math::Curve_span_shape;

[[nodiscard]] auto corner(const glm::vec3& position) -> Curve_point
{
    return Curve_point{.position = position};
}

[[nodiscard]] auto free_point(const glm::vec3& position, const glm::vec3& in_handle, const glm::vec3& out_handle) -> Curve_point
{
    return Curve_point{.position = position, .in_handle = in_handle, .out_handle = out_handle, .kind = Curve_point_kind::free};
}

// Reference span evaluation in double precision, independent of the code under test.
[[nodiscard]] auto reference_speed(const Curve_span& span, const double t) -> double
{
    const glm::dvec3 p0{span.control[0]};
    const glm::dvec3 p1{span.control[1]};
    const glm::dvec3 p2{span.control[2]};
    const glm::dvec3 p3{span.control[3]};
    const double     s = 1.0 - t;
    return glm::length(3.0 * ((s * s * (p1 - p0)) + (2.0 * s * t * (p2 - p1)) + (t * t * (p3 - p2))));
}

[[nodiscard]] auto reference_point(const Curve_span& span, const double t) -> glm::dvec3
{
    const double s = 1.0 - t;
    return
        (s * s * s)         * glm::dvec3{span.control[0]} +
        (3.0 * s * s * t)   * glm::dvec3{span.control[1]} +
        (3.0 * s * t * t)   * glm::dvec3{span.control[2]} +
        (t * t * t)         * glm::dvec3{span.control[3]};
}

// Composite Simpson of the double precision speed over [0, t].
[[nodiscard]] auto reference_length(const Curve_span& span, const double t, const int intervals = 20000) -> double
{
    const double h   = t / static_cast<double>(intervals);
    double       sum = reference_speed(span, 0.0) + reference_speed(span, t);
    for (int i = 1; i < intervals; ++i) {
        sum += ((i % 2) == 1 ? 4.0 : 2.0) * reference_speed(span, h * static_cast<double>(i));
    }
    return sum * h / 3.0;
}

// Chord sum in double precision, for spans whose speed has a kink (cusps).
[[nodiscard]] auto reference_chord_length(const Curve_span& span, const int segments = 1000000) -> double
{
    double     sum      = 0.0;
    glm::dvec3 previous = reference_point(span, 0.0);
    for (int i = 1; i <= segments; ++i) {
        const glm::dvec3 p = reference_point(span, static_cast<double>(i) / static_cast<double>(segments));
        sum += glm::length(p - previous);
        previous = p;
    }
    return sum;
}

void expect_orthonormal(const Curve_frame& frame)
{
    EXPECT_NEAR(glm::length(frame.tangent), 1.0f, 1.0e-5f);
    EXPECT_NEAR(glm::length(frame.side),    1.0f, 1.0e-5f);
    EXPECT_NEAR(glm::length(frame.up),      1.0f, 1.0e-5f);
    EXPECT_NEAR(glm::dot(frame.tangent, frame.side), 0.0f, 1.0e-5f);
    EXPECT_NEAR(glm::dot(frame.tangent, frame.up),   0.0f, 1.0e-5f);
    EXPECT_NEAR(glm::dot(frame.side,    frame.up),   0.0f, 1.0e-5f);
    EXPECT_NEAR(glm::length(glm::cross(frame.side, frame.up) - frame.tangent), 0.0f, 1.0e-5f);
}

constexpr float c_kappa = 0.5522847498f; // quarter circle cubic handle length (unit radius)

TEST(CurvePath, GeneralDegreeBezier)
{
    const std::array<glm::vec3, 4> control{glm::vec3{0.0f}, glm::vec3{1.0f, 2.0f, 0.0f}, glm::vec3{3.0f, 2.0f, 1.0f}, glm::vec3{4.0f, 0.0f, 0.0f}};
    std::array<glm::vec3, 4> scratch{};
    const Curve_span span{.control = control, .shape = Curve_span_shape::cubic};
    for (int i = 0; i <= 8; ++i) {
        const float t = static_cast<float>(i) / 8.0f;
        EXPECT_NEAR(glm::length(erhe::math::bezier_point(control, t, scratch) - erhe::math::get_span_position(span, t)), 0.0f, 1.0e-5f);
        EXPECT_NEAR(glm::length(erhe::math::bezier_unit_tangent(control, t, scratch) - erhe::math::get_span_tangent(span, t)), 0.0f, 1.0e-5f);
    }
    // Repeated end control points: the derivative vanishes at the ends, the tangent does not.
    const std::array<glm::vec3, 4> repeated{glm::vec3{0.0f}, glm::vec3{0.0f}, glm::vec3{2.0f, 0.0f, 0.0f}, glm::vec3{2.0f, 0.0f, 0.0f}};
    EXPECT_NEAR(glm::length(erhe::math::bezier_unit_tangent(repeated, 0.0f, scratch) - glm::vec3{1.0f, 0.0f, 0.0f}), 0.0f, 1.0e-5f);
    const std::array<glm::vec3, 2> line{glm::vec3{1.0f, 1.0f, 1.0f}, glm::vec3{1.0f, 1.0f, 3.0f}};
    EXPECT_EQ(erhe::math::bezier_point(line, 0.5f, scratch), (glm::vec3{1.0f, 1.0f, 2.0f}));
}

TEST(CurvePath, PolylineIsExactAndCornersTakeTheLeavingSpan)
{
    Curve_path path;
    path.points = {corner({0.0f, 0.0f, 0.0f}), corner({3.0f, 0.0f, 0.0f}), corner({3.0f, 4.0f, 0.0f})};
    Curve_path_table table;
    table.build(path);
    ASSERT_EQ(table.get_span_count(), 2u);
    EXPECT_EQ(table.get_span(0).shape, Curve_span_shape::line);
    EXPECT_FLOAT_EQ(table.get_length(), 7.0f);
    const std::span<const float> corners = table.get_point_distances();
    ASSERT_EQ(corners.size(), 3u);
    EXPECT_FLOAT_EQ(corners[1], 3.0f);
    EXPECT_FLOAT_EQ(corners[2], 7.0f);

    EXPECT_EQ(table.sample_at_distance(1.5f).position, (glm::vec3{1.5f, 0.0f, 0.0f}));
    EXPECT_EQ(table.sample_at_distance(5.0f).position, (glm::vec3{3.0f, 2.0f, 0.0f}));
    const Curve_sample at_corner = table.sample_at_distance(3.0f);
    EXPECT_EQ(at_corner.position, (glm::vec3{3.0f, 0.0f, 0.0f}));
    EXPECT_EQ(at_corner.tangent,  (glm::vec3{0.0f, 1.0f, 0.0f}));
    EXPECT_EQ(table.locate(3.0f).span, 1u);
    EXPECT_EQ(table.locate(3.0f).t, 0.0f);
    EXPECT_EQ(table.sample_at_distance(2.999f).tangent, (glm::vec3{1.0f, 0.0f, 0.0f}));

    // Open: distances clamp; the end takes the arriving span.
    EXPECT_EQ(table.sample_at_distance(-1.0f).position, (glm::vec3{0.0f, 0.0f, 0.0f}));
    EXPECT_EQ(table.sample_at_distance(10.0f).position, (glm::vec3{3.0f, 4.0f, 0.0f}));
    EXPECT_EQ(table.sample_at_distance(10.0f).tangent,  (glm::vec3{0.0f, 1.0f, 0.0f}));
    EXPECT_EQ(table.locate(7.0f).span, 1u);
    EXPECT_EQ(table.locate(7.0f).t, 1.0f);
}

TEST(CurvePath, ClosedPathWraps)
{
    Curve_path path;
    path.closure = Curve_closure::closed;
    path.points  = {corner({0.0f, 0.0f, 0.0f}), corner({1.0f, 0.0f, 0.0f}), corner({1.0f, 1.0f, 0.0f}), corner({0.0f, 1.0f, 0.0f})};
    Curve_path_table table;
    table.build(path);
    ASSERT_EQ(table.get_span_count(), 4u);
    EXPECT_FLOAT_EQ(table.get_length(), 4.0f);
    EXPECT_EQ(table.get_point_distances().size(), 5u);
    EXPECT_NEAR(glm::length(table.sample_at_distance(5.5f).position  - glm::vec3{1.0f, 0.5f, 0.0f}), 0.0f, 1.0e-6f);
    EXPECT_NEAR(glm::length(table.sample_at_distance(-0.5f).position - glm::vec3{0.0f, 0.5f, 0.0f}), 0.0f, 1.0e-6f);
    // The wrap point is the first corner and takes the leaving span.
    EXPECT_EQ(table.locate(4.0f).span, 0u);
    EXPECT_EQ(table.sample_at_distance(4.0f).tangent, (glm::vec3{1.0f, 0.0f, 0.0f}));
    EXPECT_EQ(table.sample_at_distance(3.5f).tangent, (glm::vec3{0.0f, -1.0f, 0.0f}));
}

TEST(CurvePath, ParabolaLengthAndDistanceMapping)
{
    // y = x^2 for x in [0, 1]: a quadratic, raised to a cubic.
    Curve_path path;
    path.points = {
        free_point({0.0f, 0.0f, 0.0f}, glm::vec3{0.0f}, {1.0f / 3.0f, 0.0f, 0.0f}),
        free_point({1.0f, 1.0f, 0.0f}, {-1.0f / 3.0f, -2.0f / 3.0f, 0.0f}, glm::vec3{0.0f})
    };
    Curve_path_table table;
    table.build(path);
    const double analytic = ((2.0 * std::sqrt(5.0)) + std::asinh(2.0)) / 4.0;
    EXPECT_NEAR(static_cast<double>(table.get_length()), analytic, 1.0e-6 * analytic);

    const Curve_span& span = table.get_span(0);
    for (int i = 0; i <= 50; ++i) {
        const float          d        = table.get_length() * static_cast<float>(i) / 50.0f;
        const Curve_location location = table.locate(d);
        EXPECT_NEAR(reference_length(span, static_cast<double>(location.t)), static_cast<double>(d), 2.0e-6);
        EXPECT_NEAR(table.get_distance(location), d, 2.0e-6f);
        // The point lies on the parabola.
        const glm::vec3 p = table.get_position(location);
        EXPECT_NEAR(p.y, p.x * p.x, 2.0e-6f);
    }
}

TEST(CurvePath, QuarterCircleApproximation)
{
    Curve_path path;
    path.points = {
        free_point({1.0f, 0.0f, 0.0f}, glm::vec3{0.0f}, {0.0f, 0.0f, c_kappa}),
        free_point({0.0f, 0.0f, 1.0f}, {c_kappa, 0.0f, 0.0f}, glm::vec3{0.0f})
    };
    Curve_path_table table;
    table.build(path);
    const double reference = reference_length(table.get_span(0), 1.0);
    EXPECT_NEAR(static_cast<double>(table.get_length()), reference, 1.0e-6 * reference);
    EXPECT_NEAR(static_cast<double>(table.get_length()), 0.5 * 3.14159265358979, 1.0e-3);
    // Equal distances give equal angles on the (near) circle.
    for (int i = 0; i <= 8; ++i) {
        const glm::vec3 p     = table.sample_at_distance(table.get_length() * static_cast<float>(i) / 8.0f).position;
        const float     angle = std::atan2(p.z, p.x);
        EXPECT_NEAR(angle, 1.5707963f * static_cast<float>(i) / 8.0f, 1.0e-3f);
    }
    // A horizontal curve transports +Y unchanged.
    std::array<float, 9>       distances{};
    std::array<Curve_frame, 9> frames{};
    for (std::size_t i = 0; i < distances.size(); ++i) {
        distances[i] = table.get_length() * static_cast<float>(i) / 8.0f;
    }
    table.get_transported_frames_at_distances(distances, frames);
    for (const Curve_frame& frame : frames) {
        expect_orthonormal(frame);
        EXPECT_NEAR(glm::length(frame.up - glm::vec3{0.0f, 1.0f, 0.0f}), 0.0f, 1.0e-5f);
    }
}

TEST(CurvePath, CuspLength)
{
    // Control points 0, 2, -1, 1 on x: the curve runs out, back and out again.
    Curve_path path;
    path.points = {
        free_point({0.0f, 0.0f, 0.0f}, glm::vec3{0.0f}, {2.0f, 0.0f, 0.0f}),
        free_point({1.0f, 0.0f, 0.0f}, {-2.0f, 0.0f, 0.0f}, glm::vec3{0.0f})
    };
    Curve_path_table table;
    table.build(path);
    const double reference = reference_chord_length(table.get_span(0));
    EXPECT_NEAR(static_cast<double>(table.get_length()), reference, 1.0e-6 * reference);
    for (int i = 0; i <= 20; ++i) {
        const float d = table.get_length() * static_cast<float>(i) / 20.0f;
        EXPECT_NEAR(table.get_distance(table.locate(d)), d, 1.0e-6f * table.get_length());
        EXPECT_NEAR(std::abs(table.sample_at_distance(d).tangent.x), 1.0f, 1.0e-5f);
    }
}

TEST(CurvePath, CornerEndPointsAtTheOtherHandle)
{
    const Curve_point from = corner({0.0f, 0.0f, 0.0f});
    const Curve_point to   = free_point({3.0f, 0.0f, 0.0f}, {0.0f, 3.0f, 0.0f}, glm::vec3{0.0f});
    const Curve_span  span = erhe::math::make_curve_span(from, to);
    EXPECT_EQ(span.shape, Curve_span_shape::cubic);
    EXPECT_NEAR(glm::length(span.control[1] - glm::vec3{1.0f, 1.0f, 0.0f}), 0.0f, 1.0e-6f);
    EXPECT_NEAR(glm::length(erhe::math::get_span_tangent(span, 0.0f) - glm::normalize(glm::vec3{1.0f, 1.0f, 0.0f})), 0.0f, 1.0e-6f);
    // A corner ignores its stored handles.
    Curve_point stale = corner({3.0f, 0.0f, 0.0f});
    stale.in_handle = glm::vec3{0.0f, 5.0f, 0.0f};
    EXPECT_EQ(erhe::math::make_curve_span(from, stale).shape, Curve_span_shape::line);
}

TEST(CurvePath, HandleAndKindHelpers)
{
    Curve_point smooth{.position = glm::vec3{0.0f}, .in_handle = {-2.0f, 0.0f, 0.0f}, .out_handle = {1.0f, 0.0f, 0.0f}, .kind = Curve_point_kind::smooth};
    erhe::math::set_curve_handle(smooth, Curve_handle::out, glm::vec3{0.0f, 3.0f, 0.0f});
    EXPECT_EQ(smooth.out_handle, (glm::vec3{0.0f, 3.0f, 0.0f}));
    EXPECT_NEAR(glm::length(smooth.in_handle - glm::vec3{0.0f, -2.0f, 0.0f}), 0.0f, 1.0e-6f);

    Curve_point free = smooth;
    free.kind = Curve_point_kind::free;
    erhe::math::set_curve_handle(free, Curve_handle::in, glm::vec3{1.0f, 1.0f, 0.0f});
    EXPECT_EQ(free.out_handle, (glm::vec3{0.0f, 3.0f, 0.0f}));

    std::vector<Curve_point> points{corner({0.0f, 0.0f, 0.0f}), corner({1.0f, 0.0f, 0.0f}), corner({2.0f, 1.0f, 0.0f})};
    erhe::math::set_curve_point_kind(points, 1, Curve_closure::open, Curve_point_kind::smooth);
    EXPECT_EQ(points[1].kind, Curve_point_kind::smooth);
    EXPECT_NEAR(glm::length(points[1].out_handle - glm::vec3{2.0f / 6.0f, 1.0f / 6.0f, 0.0f}), 0.0f, 1.0e-6f);
    EXPECT_NEAR(glm::length(points[1].in_handle  + points[1].out_handle), 0.0f, 1.0e-6f);
    erhe::math::set_curve_point_kind(points, 0, Curve_closure::open, Curve_point_kind::free);
    EXPECT_NEAR(glm::length(points[0].out_handle - glm::vec3{1.0f / 3.0f, 0.0f, 0.0f}), 0.0f, 1.0e-6f);
    erhe::math::set_curve_point_kind(points, 2, Curve_closure::closed, Curve_point_kind::free);
    EXPECT_NEAR(glm::length(points[2].out_handle - glm::vec3{-1.0f / 6.0f, 0.0f, 0.0f}), 0.0f, 1.0e-6f);

    // Free to smooth: the in handle turns opposite the out handle, keeping its length.
    points[0].in_handle = glm::vec3{0.0f, 2.0f, 0.0f};
    erhe::math::set_curve_point_kind(points, 0, Curve_closure::open, Curve_point_kind::smooth);
    EXPECT_NEAR(glm::length(points[0].in_handle - glm::vec3{-2.0f, 0.0f, 0.0f}), 0.0f, 1.0e-6f);

    erhe::math::set_curve_point_kind(points, 1, Curve_closure::open, Curve_point_kind::corner);
    EXPECT_EQ(points[1].in_handle,  glm::vec3{0.0f});
    EXPECT_EQ(points[1].out_handle, glm::vec3{0.0f});
}

TEST(CurvePath, UpFramesKeepTheHeadingOnVerticalSpans)
{
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    Curve_path path;
    path.points = {corner({0.0f, 0.0f, 0.0f}), corner({1.0f, 0.0f, 0.0f}), corner({1.0f, 2.0f, 0.0f}), corner({1.0f, 2.0f, 1.0f})};
    Curve_path_table table;
    table.build(path);
    const std::array<float, 3> distances{0.5f, 2.0f, 3.5f};
    std::array<Curve_frame, 3> frames{};
    table.get_up_frames_at_distances(distances, up, frames);
    for (const Curve_frame& frame : frames) {
        expect_orthonormal(frame);
    }
    EXPECT_NEAR(glm::length(frames[0].up - up), 0.0f, 1.0e-6f);
    EXPECT_NEAR(glm::length(frames[0].side - glm::vec3{0.0f, 0.0f, -1.0f}), 0.0f, 1.0e-6f);
    EXPECT_NEAR(glm::length(frames[1].side - frames[0].side), 0.0f, 1.0e-6f);
    EXPECT_NEAR(glm::length(frames[2].up - up), 0.0f, 1.0e-6f);
    EXPECT_NEAR(glm::length(frames[2].side - glm::vec3{1.0f, 0.0f, 0.0f}), 0.0f, 1.0e-6f);

    // A path that starts vertical takes the side of its first non-vertical sample.
    Curve_path rising;
    rising.points = {corner({0.0f, 0.0f, 0.0f}), corner({0.0f, 1.0f, 0.0f}), corner({1.0f, 1.0f, 0.0f})};
    Curve_path_table rising_table;
    rising_table.build(rising);
    const std::array<float, 2> rising_distances{0.5f, 1.5f};
    std::array<Curve_frame, 2> rising_frames{};
    rising_table.get_up_frames_at_distances(rising_distances, up, rising_frames);
    expect_orthonormal(rising_frames[0]);
    EXPECT_NEAR(glm::length(rising_frames[0].side - rising_frames[1].side), 0.0f, 1.0e-6f);
}

TEST(CurvePath, TransportedFramesAreOrthonormal)
{
    Curve_path path;
    path.points = {
        free_point({0.0f, 0.0f, 0.0f}, glm::vec3{0.0f}, {1.0f, 1.0f, 0.0f}),
        free_point({2.0f, 1.0f, 2.0f}, {-1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 1.0f}),
        free_point({0.0f, 3.0f, 3.0f}, {1.0f, -1.0f, 0.0f}, glm::vec3{0.0f})
    };
    Curve_path_table table;
    table.build(path);
    std::array<float, 33>       distances{};
    std::array<Curve_frame, 33> frames{};
    for (std::size_t i = 0; i < distances.size(); ++i) {
        distances[i] = table.get_length() * static_cast<float>(i) / 32.0f;
    }
    table.get_transported_frames_at_distances(distances, frames);
    for (const Curve_frame& frame : frames) {
        expect_orthonormal(frame);
    }
}

// The same curve: equal lengths and positions at equal distances.
void expect_same_shape(const Curve_path& a, const Curve_path& b)
{
    Curve_path_table ta;
    Curve_path_table tb;
    ta.build(a);
    tb.build(b);
    ASSERT_NEAR(ta.get_length(), tb.get_length(), 1.0e-5f);
    for (int i = 0; i <= 200; ++i) {
        const float     d  = ta.get_length() * static_cast<float>(i) / 200.0f;
        const glm::vec3 pa = ta.sample_at_distance(d).position;
        const glm::vec3 pb = tb.sample_at_distance(d).position;
        EXPECT_LT(glm::distance(pa, pb), 1.0e-5f) << "at distance " << d;
    }
}

TEST(CurvePath, InsertOnLineSpanGivesCorner)
{
    Curve_path path;
    path.points = {corner({0.0f, 0.0f, 0.0f}), corner({4.0f, 0.0f, 0.0f}), corner({4.0f, 0.0f, 2.0f})};
    Curve_path split = path;
    const std::size_t index = erhe::math::insert_curve_point(split, Curve_location{.span = 0, .t = 0.25f});
    ASSERT_EQ(index, 1u);
    ASSERT_EQ(split.points.size(), 4u);
    EXPECT_EQ(split.points[1].kind, Curve_point_kind::corner);
    EXPECT_EQ(split.points[1].position, (glm::vec3{1.0f, 0.0f, 0.0f}));
    EXPECT_EQ(split.points[0].kind, Curve_point_kind::corner);
    EXPECT_EQ(split.points[2].kind, Curve_point_kind::corner);
    expect_same_shape(path, split);
}

TEST(CurvePath, InsertOnCubicSpanKeepsTheShape)
{
    Curve_path path;
    path.points = {
        free_point({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}),
        Curve_point{.position = {3.0f, 0.0f, 1.0f}, .in_handle = {-1.0f, 0.5f, 0.0f}, .out_handle = {2.0f, -1.0f, 0.0f}, .kind = Curve_point_kind::smooth},
        free_point({5.0f, 2.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 0.0f})
    };
    for (const float t : {0.1f, 0.5f, 0.83f}) {
        Curve_path split = path;
        const std::size_t index = erhe::math::insert_curve_point(split, Curve_location{.span = 1, .t = t});
        ASSERT_EQ(index, 2u);
        EXPECT_EQ(split.points[2].kind, Curve_point_kind::smooth);
        EXPECT_EQ(split.points[1].kind, Curve_point_kind::smooth);
        EXPECT_EQ(split.points[3].kind, Curve_point_kind::free);
        // The new point's handles are on one line.
        const glm::vec3 in  = glm::normalize(split.points[2].in_handle);
        const glm::vec3 out = glm::normalize(split.points[2].out_handle);
        EXPECT_NEAR(glm::dot(in, out), -1.0f, 1.0e-5f);
        expect_same_shape(path, split);
    }
}

TEST(CurvePath, InsertNextToCornerEndMakesItFree)
{
    // Span 1 runs from a corner to a point with a handle: the corner's span is synthesized.
    Curve_path path;
    path.points = {
        corner({-2.0f, 0.0f, 0.0f}),
        Curve_point{.position = {0.0f, 0.0f, 0.0f}, .in_handle = {5.0f, 5.0f, 5.0f}, .out_handle = {7.0f, 7.0f, 7.0f}, .kind = Curve_point_kind::corner},
        free_point({3.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}, {0.0f, 0.0f, 0.0f})
    };
    Curve_path split = path;
    static_cast<void>(erhe::math::insert_curve_point(split, Curve_location{.span = 1, .t = 0.4f}));
    EXPECT_EQ(split.points[1].kind, Curve_point_kind::free);
    EXPECT_EQ(split.points[1].in_handle, glm::vec3{0.0f}); // the span before it stays straight
    expect_same_shape(path, split);
}

TEST(CurvePath, InsertOnClosingSpan)
{
    Curve_path path;
    path.closure = Curve_closure::closed;
    path.points  = {
        free_point({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}),
        corner({2.0f, 0.0f, 0.0f}),
        free_point({2.0f, 0.0f, 2.0f}, {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 1.0f})
    };
    Curve_path split = path;
    const std::size_t index = erhe::math::insert_curve_point(split, Curve_location{.span = 2, .t = 0.5f});
    EXPECT_EQ(index, 3u);
    EXPECT_EQ(split.points.size(), 4u);
    expect_same_shape(path, split);
}

TEST(CurvePath, EndPointsExtendAlongTheEndTangent)
{
    Curve_path polyline;
    polyline.points = {corner({0.0f, 0.0f, 0.0f}), corner({2.0f, 0.0f, 0.0f})};
    const Curve_point after = erhe::math::make_curve_end_point(polyline, Curve_end::end);
    EXPECT_EQ(after.kind, Curve_point_kind::corner);
    EXPECT_LT(glm::distance(after.position, glm::vec3{4.0f, 0.0f, 0.0f}), 1.0e-6f);
    const Curve_point before = erhe::math::make_curve_end_point(polyline, Curve_end::start);
    EXPECT_LT(glm::distance(before.position, glm::vec3{-2.0f, 0.0f, 0.0f}), 1.0e-6f);
    EXPECT_EQ(erhe::math::add_curve_point(polyline, Curve_end::start, before), 0u);
    EXPECT_EQ(erhe::math::add_curve_point(polyline, Curve_end::end, after), 3u);
    EXPECT_EQ(polyline.points.front().position, before.position);

    // A smooth end: the tangent there is the arriving direction, -in of the end point.
    Curve_path smooth;
    smooth.points = {
        Curve_point{.position = {0.0f, 0.0f, 0.0f}, .in_handle = {-1.0f, 0.0f, 0.0f}, .out_handle = {1.0f, 0.0f, 0.0f}, .kind = Curve_point_kind::smooth},
        Curve_point{.position = {3.0f, 0.0f, 0.0f}, .in_handle = {0.0f, 0.0f, -1.0f}, .out_handle = {0.0f, 0.0f, 1.0f}, .kind = Curve_point_kind::smooth}
    };
    const Curve_point end = erhe::math::make_curve_end_point(smooth, Curve_end::end);
    EXPECT_EQ(end.kind, Curve_point_kind::smooth);
    EXPECT_LT(glm::distance(end.position, glm::vec3{3.0f, 0.0f, 3.0f}), 1.0e-5f);
    EXPECT_LT(glm::distance(end.out_handle, glm::vec3{0.0f, 0.0f, 1.0f}), 1.0e-5f);
    EXPECT_LT(glm::distance(end.in_handle, glm::vec3{0.0f, 0.0f, -1.0f}), 1.0e-5f);
}

TEST(CurvePath, NearestLocation)
{
    Curve_path path;
    path.points = {
        free_point({1.0f, 0.0f, 0.0f}, glm::vec3{0.0f}, {0.0f, 0.0f, 0.5522847f}),
        free_point({0.0f, 0.0f, 1.0f}, {0.5522847f, 0.0f, 0.0f}, glm::vec3{0.0f}),
        corner({-1.0f, 0.0f, 1.0f})
    };
    Curve_path_table table;
    table.build(path);
    const Curve_location on_arc = table.get_nearest_location(glm::vec3{2.0f, 0.0f, 2.0f});
    EXPECT_EQ(on_arc.span, 0u);
    const glm::vec3 p = table.get_position(on_arc);
    EXPECT_NEAR(p.x, p.z, 1.0e-4f);
    const Curve_location on_line = table.get_nearest_location(glm::vec3{-0.25f, 3.0f, 1.0f});
    EXPECT_EQ(on_line.span, 1u);
    EXPECT_NEAR(on_line.t, 0.25f, 1.0e-4f);
}

TEST(CurvePath, EmptyAndSinglePoint)
{
    Curve_path_table table;
    table.build(Curve_path{});
    EXPECT_EQ(table.get_span_count(), 0u);
    EXPECT_EQ(table.get_length(), 0.0f);
    Curve_path single;
    single.points = {corner({1.0f, 2.0f, 3.0f})};
    table.build(single);
    EXPECT_EQ(table.sample_at_distance(5.0f).position, (glm::vec3{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(table.get_point_distances().size(), 1u);
}

} // namespace
