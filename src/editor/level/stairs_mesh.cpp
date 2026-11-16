#include "level/stairs_mesh.hpp"

#include "level/kit_mesh_builder.hpp"

#include "erhe_math/curve_path.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace editor {

namespace {

constexpr std::size_t c_samples_per_cubic_span = 32;
// Cross-sections nearer than this are one (meters along the plan).
constexpr double      c_section_spacing        = 1.0e-3;

// A place on the curve's plan: its distance along the plan and the span parameter there.
class Center_sample
{
public:
    double      station  {0.0};
    std::size_t span     {0};
    double      t        {0.0};
    double      curvature{0.0}; // of the plan, unsigned
};

class Corner
{
public:
    std::size_t point  {0};
    double      station{0.0};
    double      angle  {0.0};       // the turn of the plan tangent, unsigned
    glm::dvec2  side   {0.0, 0.0};  // the mitre line's direction (x, z), unit
    double      scale  {1.0};       // the mitre line's half length over the half width
};

class Center_line
{
public:
    const erhe::math::Curve_path_table* table{nullptr};
    std::vector<Center_sample>          samples;
    std::vector<double>                 point_stations; // one per span start, then the length
    std::vector<Corner>                 corners;
    std::vector<Stairs_zone>            zones;          // one per corner
    double                              length{0.0};
};

[[nodiscard]] auto get_plan_derivative(const erhe::math::Curve_span& span, const double t) -> glm::dvec2
{
    const glm::vec3 derivative = erhe::math::get_span_derivative(span, static_cast<float>(t));
    return glm::dvec2{static_cast<double>(derivative.x), static_cast<double>(derivative.z)};
}

[[nodiscard]] auto get_plan_speed(const erhe::math::Curve_span& span, const double t) -> double
{
    return glm::length(get_plan_derivative(span, t));
}

// The plan length of the span between two parameters (4-point Gauss-Legendre).
[[nodiscard]] auto get_plan_length(const erhe::math::Curve_span& span, const double t0, const double t1) -> double
{
    constexpr std::array<double, 4> nodes  {-0.8611363115940526, -0.3399810435848563, 0.3399810435848563, 0.8611363115940526};
    constexpr std::array<double, 4> weights{ 0.3478548451374538,  0.6521451548625461, 0.6521451548625461, 0.3478548451374538};
    const double half   = 0.5 * (t1 - t0);
    const double middle = 0.5 * (t1 + t0);
    double sum = 0.0;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        sum += weights[i] * get_plan_speed(span, middle + (half * nodes[i]));
    }
    return sum * half;
}

[[nodiscard]] auto get_plan_curvature(const erhe::math::Curve_span& span, const double t) -> double
{
    if (span.shape == erhe::math::Curve_span_shape::line) {
        return 0.0;
    }
    const glm::dvec3 p0{span.control[0]};
    const glm::dvec3 p1{span.control[1]};
    const glm::dvec3 p2{span.control[2]};
    const glm::dvec3 p3{span.control[3]};
    const glm::dvec3 second = 6.0 * (((1.0 - t) * (p2 - (2.0 * p1) + p0)) + (t * (p3 - (2.0 * p2) + p1)));
    const glm::dvec2 first  = get_plan_derivative(span, t);
    const double     speed  = glm::length(first);
    if (!(speed > 0.0)) {
        return 0.0;
    }
    return std::abs((first.x * second.z) - (first.y * second.x)) / (speed * speed * speed);
}

// side = cross(up, tangent) in plan (x, z): to the left of the heading seen from above.
[[nodiscard]] auto get_side(const glm::dvec2& tangent) -> glm::dvec2
{
    return glm::dvec2{tangent.y, -tangent.x};
}

[[nodiscard]] auto get_nearest_point(const Center_line& line, const double station) -> std::size_t
{
    std::size_t nearest = 0;
    for (std::size_t i = 1, end = line.point_stations.size(); i < end; ++i) {
        if (std::abs(line.point_stations[i] - station) < std::abs(line.point_stations[nearest] - station)) {
            nearest = i;
        }
    }
    return nearest;
}

[[nodiscard]] auto name_place(const Center_line& line, const double station) -> std::string
{
    return fmt::format("{:.3f} m along its plan, nearest control point {}", station, get_nearest_point(line, station));
}

// Samples the plan; returns the refusal.
[[nodiscard]] auto make_center_line(const erhe::math::Curve_path_table& table, const Stairs_values& values, Center_line& out_line) -> std::string
{
    out_line.table = &table;
    out_line.samples.clear();
    out_line.point_stations.clear();
    out_line.corners.clear();
    out_line.zones.clear();
    out_line.length = 0.0;
    if (table.get_closure() == erhe::math::Curve_closure::closed) {
        return "a stairs layout needs an open curve";
    }
    const std::size_t span_count = table.get_span_count();
    if (span_count == 0) {
        return "a stairs layout needs a curve of at least two points";
    }
    const double half_width = 0.5 * static_cast<double>(values.width);
    double station = 0.0;
    for (std::size_t s = 0; s < span_count; ++s) {
        const erhe::math::Curve_span& span      = table.get_span(s);
        const std::size_t             intervals = (span.shape == erhe::math::Curve_span_shape::line) ? 1 : c_samples_per_cubic_span;
        out_line.point_stations.push_back(station);
        for (std::size_t i = 0; i <= intervals; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(intervals);
            if (i > 0) {
                station += get_plan_length(span, static_cast<double>(i - 1) / static_cast<double>(intervals), t);
            }
            const double plan_speed = get_plan_speed(span, t);
            const double speed      = static_cast<double>(glm::length(erhe::math::get_span_derivative(span, static_cast<float>(t))));
            Center_sample sample{.station = station, .span = s, .t = t, .curvature = get_plan_curvature(span, t)};
            out_line.length = station;
            if (!(plan_speed > std::max(1.0e-3 * speed, 1.0e-9))) {
                out_line.point_stations.push_back(station);
                return fmt::format("the curve is vertical or stops {}: stairs need a curve that advances in plan", name_place(out_line, station));
            }
            out_line.samples.push_back(sample);
        }
    }
    out_line.point_stations.push_back(station);
    out_line.length = station;
    if (!(station > static_cast<double>(c_stairs_station_epsilon))) {
        return "the curve has no length in plan";
    }
    for (const Center_sample& sample : out_line.samples) {
        if ((sample.curvature * static_cast<double>(values.width) * static_cast<double>(c_stairs_min_radius_per_width)) > 1.0) {
            return fmt::format(
                "the curve turns too tightly {} (plan radius {:.3f} m): stairs {:.3f} m wide need a plan radius of at least {:.3f} m",
                name_place(out_line, sample.station), 1.0 / sample.curvature, values.width, values.width * c_stairs_min_radius_per_width
            );
        }
    }
    // A corner's mitre line and the landing it needs.
    for (std::size_t s = 1; s < span_count; ++s) {
        const glm::dvec2 in    = glm::normalize(get_plan_derivative(table.get_span(s - 1), 1.0));
        const glm::dvec2 out   = glm::normalize(get_plan_derivative(table.get_span(s), 0.0));
        const double     angle = std::abs(std::atan2((in.x * out.y) - (in.y * out.x), glm::dot(in, out)));
        if (angle <= static_cast<double>(c_stairs_corner_angle)) {
            continue;
        }
        const double corner_station = out_line.point_stations[s];
        if (angle > static_cast<double>(c_stairs_max_corner_angle)) {
            return fmt::format("the curve turns back on itself at control point {} ({:.3f} m along its plan): stairs cannot turn that corner", s, corner_station);
        }
        const double reach = (half_width * std::tan(0.5 * angle)) + static_cast<double>(c_stairs_corner_margin);
        const double begin = corner_station - reach;
        const double end   = corner_station + reach;
        const double previous_end = out_line.zones.empty() ? 0.0 : static_cast<double>(out_line.zones.back().end);
        if ((begin < (previous_end + c_section_spacing)) || (end > (out_line.length - c_section_spacing))) {
            return fmt::format(
                "the corner at control point {} ({:.3f} m along the plan) needs {:.3f} m of the curve on each side for its landing at a width of {:.3f} m",
                s, corner_station, reach, values.width
            );
        }
        out_line.corners.push_back(
            Corner{.point = s, .station = corner_station, .angle = angle, .side = glm::normalize(get_side(in) + get_side(out)), .scale = 1.0 / std::cos(0.5 * angle)}
        );
        out_line.zones.push_back(Stairs_zone{.begin = static_cast<float>(begin), .end = static_cast<float>(end)});
    }
    return {};
}

// The span parameter at a plan distance.
[[nodiscard]] auto locate(const Center_line& line, const double station) -> Center_sample
{
    const std::vector<Center_sample>& samples = line.samples;
    const std::vector<Center_sample>::const_iterator upper = std::upper_bound(
        samples.begin(), samples.end(), station, [](const double value, const Center_sample& sample) -> bool { return value < sample.station; }
    );
    if (upper == samples.begin()) {
        return samples.front();
    }
    if (upper == samples.end()) {
        return samples.back();
    }
    const Center_sample& a = *(upper - 1);
    const Center_sample& b = *upper;
    if (a.span != b.span) {
        return (std::abs(station - a.station) <= std::abs(b.station - station)) ? a : b;
    }
    const erhe::math::Curve_span& span   = line.table->get_span(a.span);
    const double                  target = station - a.station;
    double t = a.t + ((b.t - a.t) * (target / (b.station - a.station)));
    if (span.shape != erhe::math::Curve_span_shape::line) {
        for (int i = 0; i < 3; ++i) {
            const double speed = get_plan_speed(span, t);
            if (!(speed > 0.0)) {
                break;
            }
            t = std::clamp(t - ((get_plan_length(span, a.t, t) - target) / speed), a.t, b.t);
        }
    }
    return Center_sample{.station = station, .span = a.span, .t = t, .curvature = a.curvature + ((b.curvature - a.curvature) * ((t - a.t) / (b.t - a.t)))};
}

[[nodiscard]] auto get_max_curvature(const Center_line& line, const double begin, const double end) -> double
{
    double curvature = std::max(locate(line, begin).curvature, locate(line, end).curvature);
    for (const Center_sample& sample : line.samples) {
        if ((sample.station > begin) && (sample.station < end)) {
            curvature = std::max(curvature, sample.curvature);
        }
    }
    return curvature;
}

[[nodiscard]] auto find_zone(const Center_line& line, const double station) -> const Stairs_zone*
{
    for (const Stairs_zone& zone : line.zones) {
        if ((station > static_cast<double>(zone.begin)) && (station < static_cast<double>(zone.end))) {
            return &zone;
        }
    }
    return nullptr;
}

// The underside's height: the thickness below a landing and below the foot of each riser, a flight's slope between them.
[[nodiscard]] auto get_soffit(const Stairs_fit& fit, const Stairs_values& values, const std::size_t tread, const double station) -> double
{
    const double level     = static_cast<double>(fit.get_tread_level(tread));
    const double thickness = static_cast<double>(values.thickness);
    const double going     = static_cast<double>(values.min_tread);
    double soffit = level - thickness;
    if (tread > 0) {
        const double below = static_cast<double>(fit.get_tread_level(tread - 1));
        if (below < level) {
            soffit = std::min(soffit, (below - thickness) + (((level - below) / going) * (station - static_cast<double>(fit.get_tread_begin(tread)))));
        }
    }
    if ((tread + 1) < fit.get_tread_count()) {
        const double below = static_cast<double>(fit.get_tread_level(tread + 1));
        if (below < level) {
            soffit = std::min(soffit, (below - thickness) + (((level - below) / going) * (static_cast<double>(fit.get_tread_end(tread)) - station)));
        }
    }
    return soffit;
}

// The places inside a tread where the underside changes slope.
void add_soffit_knots(const Stairs_fit& fit, const Stairs_values& values, std::vector<double>& out_stations)
{
    const double going = static_cast<double>(values.min_tread);
    for (std::size_t tread = 0, end = fit.get_tread_count(); tread < end; ++tread) {
        const double level  = static_cast<double>(fit.get_tread_level(tread));
        const double begin  = static_cast<double>(fit.get_tread_begin(tread));
        const double finish = static_cast<double>(fit.get_tread_end(tread));
        const double before = (tread > 0) ? static_cast<double>(fit.get_tread_level(tread - 1)) : level;
        const double after  = ((tread + 1) < end) ? static_cast<double>(fit.get_tread_level(tread + 1)) : level;
        const bool   upper_begin = before < level;
        const bool   upper_end   = after < level;
        if (upper_begin && upper_end && ((begin + going) > (finish - going - c_section_spacing))) {
            // A short top landing: the two slopes meet below it.
            const double rise_begin = level - before;
            const double rise_end   = level - after;
            out_stations.push_back(((going * (after - before)) + (rise_begin * begin) + (rise_end * finish)) / (rise_begin + rise_end));
            continue;
        }
        if (upper_begin) {
            out_stations.push_back(begin + going);
        }
        if (upper_end) {
            out_stations.push_back(finish - going);
        }
    }
}

class Cross_section
{
public:
    double      station     {0.0};
    glm::dvec2  left        {0.0, 0.0}; // plan (x, z)
    glm::dvec2  right       {0.0, 0.0};
    double      bottom      {0.0};
    std::size_t tread_before{0};
    std::size_t tread_after {0};
    // Per side (left, right): the bottom vertex and the top vertices at the level before and after.
    std::array<uint32_t, 2> bottom_vertex{};
    std::array<uint32_t, 2> before_vertex{};
    std::array<uint32_t, 2> after_vertex {};
};

enum class Stairs_side : unsigned int { left = 0, right };

// Adds a facet wound counter-clockwise seen from the right side's outside; the left side takes the reverse.
void add_side_facet(Kit_mesh_builder& builder, const Stairs_side side, const std::initializer_list<uint32_t> corners)
{
    std::vector<uint32_t> ordered{corners};
    if (side == Stairs_side::left) {
        std::reverse(ordered.begin(), ordered.end());
    }
    builder.add_polygon(ordered);
}

// e1 x e2 seen from above: positive where the corner turns as an upward facet's does.
[[nodiscard]] auto cross_from_above(const glm::dvec2& e1, const glm::dvec2& e2) -> double
{
    return (e1.y * e2.x) - (e1.x * e2.y);
}

} // anonymous namespace

auto check_stairs_values(const Stairs_values& values) -> std::string
{
    const auto check = [](const char* const name, const float value, const float min_value, const float max_value) -> std::string {
        if (!std::isfinite(value) || (value < min_value) || (value > max_value)) {
            return fmt::format("the stairs' {} {:.3f} m is outside {:.3f} - {:.3f} m", name, value, min_value, max_value);
        }
        return {};
    };
    std::string error = check("width", values.width, c_min_stairs_width, c_max_stairs_width);
    if (error.empty()) { error = check("step rise limit", values.max_rise, c_min_stairs_rise, c_max_stairs_rise); }
    if (error.empty()) { error = check("tread depth", values.min_tread, c_min_stairs_tread, c_max_stairs_tread); }
    if (error.empty()) { error = check("thickness", values.thickness, c_min_stairs_thickness, c_max_stairs_thickness); }
    if (error.empty()) { error = check("edge tolerance", values.tolerance, c_min_stairs_tolerance, c_max_stairs_tolerance); }
    return error;
}

void Stairs_report::clear()
{
    fit.clear();
    section_count = 0;
}

auto make_stairs(
    const erhe::math::Curve_path_table& table,
    const Stairs_values&                values,
    Stairs_report&                      out_report,
    GEO::Mesh* const                    out_mesh
) -> std::string
{
    out_report.clear();
    std::string error = check_stairs_values(values);
    if (!error.empty()) {
        return error;
    }
    static thread_local Center_line                       line;
    static thread_local std::vector<Stairs_height_sample> heights;
    static thread_local std::vector<double>               stations;
    static thread_local std::vector<double>               optional_stations;
    static thread_local std::vector<Cross_section>        sections;
    error = make_center_line(table, values, line);
    if (!error.empty()) {
        return error;
    }
    heights.clear();
    for (const Center_sample& sample : line.samples) {
        const glm::vec3 position = erhe::math::get_span_position(table.get_span(sample.span), static_cast<float>(sample.t));
        heights.push_back(Stairs_height_sample{.distance = static_cast<float>(sample.station), .height = position.y});
    }
    heights.front().distance = 0.0f;
    Stairs_fit& fit = out_report.fit;
    const Stairs_fit_refusal refusal = fit_stairs(heights, Stairs_rules{.max_rise = values.max_rise, .min_tread = values.min_tread}, line.zones, fit);
    if (!refusal.message.empty()) {
        return fmt::format("{} (nearest control point {})", refusal.message, get_nearest_point(line, static_cast<double>(refusal.distance)));
    }

    // The cross-sections: the ends, the risers, the corners and their landings' ends, then the underside's knots and the
    // curved edges' subdivision.
    const double length = static_cast<double>(fit.length);
    stations.clear();
    stations.push_back(0.0);
    stations.push_back(length);
    for (const Corner& corner : line.corners) {
        stations.push_back(corner.station);
    }
    for (const Stairs_zone& zone : line.zones) {
        stations.push_back(static_cast<double>(zone.begin));
        stations.push_back(static_cast<double>(zone.end));
    }
    std::sort(stations.begin(), stations.end());
    const auto is_free = [](const std::vector<double>& taken, const double station) -> bool {
        const std::vector<double>::const_iterator next = std::lower_bound(taken.begin(), taken.end(), station);
        return
            ((next == taken.end())   || ((*next - station) > c_section_spacing)) &&
            ((next == taken.begin()) || ((station - *(next - 1)) > c_section_spacing));
    };
    // A riser at an end or at a landing's end stands on that cross-section.
    optional_stations.clear();
    for (const Stairs_riser& riser : fit.risers) {
        if (is_free(stations, static_cast<double>(riser.distance))) {
            optional_stations.push_back(static_cast<double>(riser.distance));
        }
    }
    stations.insert(stations.end(), optional_stations.begin(), optional_stations.end());
    std::sort(stations.begin(), stations.end());
    optional_stations.clear();
    add_soffit_knots(fit, values, optional_stations);
    std::sort(optional_stations.begin(), optional_stations.end());
    {
        std::size_t kept = 0;
        for (const double station : optional_stations) {
            const bool apart = (kept == 0) || ((station - optional_stations[kept - 1]) > c_section_spacing);
            if (apart && (station > 0.0) && (station < length) && (find_zone(line, station) == nullptr) && is_free(stations, station)) {
                optional_stations[kept++] = station;
            }
        }
        optional_stations.resize(kept);
    }
    stations.insert(stations.end(), optional_stations.begin(), optional_stations.end());
    std::sort(stations.begin(), stations.end());
    optional_stations.clear();
    const double half_width = 0.5 * static_cast<double>(values.width);
    for (std::size_t i = 0; (i + 1) < stations.size(); ++i) {
        const double begin = stations[i];
        const double end   = stations[i + 1];
        // A corner's landing has straight edges to its mitre line.
        if (find_zone(line, 0.5 * (begin + end)) != nullptr) {
            continue;
        }
        const double curvature = get_max_curvature(line, begin, end);
        if (!(curvature > 1.0e-12)) {
            continue;
        }
        // The outer edge's chord leaves its arc by at most the tolerance.
        const double step  = std::sqrt((8.0 * static_cast<double>(values.tolerance)) / (curvature * (1.0 + (curvature * half_width))));
        const double count = std::ceil(((end - begin) / step) - 1.0e-3);
        if ((static_cast<double>(stations.size() + optional_stations.size()) + count) > static_cast<double>(c_max_stairs_sections)) {
            return fmt::format(
                "the stairs need more than {} cross-sections at an edge tolerance of {:.3f} m ({}): raise the edge tolerance",
                c_max_stairs_sections, values.tolerance, name_place(line, begin)
            );
        }
        const int pieces = static_cast<int>(count);
        for (int k = 1; k < pieces; ++k) {
            optional_stations.push_back(begin + ((end - begin) * (static_cast<double>(k) / static_cast<double>(pieces))));
        }
    }
    stations.insert(stations.end(), optional_stations.begin(), optional_stations.end());
    std::sort(stations.begin(), stations.end());

    sections.clear();
    const std::size_t tread_count = fit.get_tread_count();
    const auto tread_at = [&fit](const double station) -> std::size_t {
        std::size_t tread = 0;
        for (const Stairs_riser& riser : fit.risers) {
            if (static_cast<double>(riser.distance) < station) {
                ++tread;
            }
        }
        return tread;
    };
    for (std::size_t i = 0, end = stations.size(); i < end; ++i) {
        Cross_section section;
        section.station      = stations[i];
        section.tread_before = (i == 0) ? 0 : tread_at(0.5 * (stations[i - 1] + stations[i]));
        section.tread_after  = ((i + 1) == end) ? (tread_count - 1) : tread_at(0.5 * (stations[i] + stations[i + 1]));
        const Corner* corner = nullptr;
        for (const Corner& candidate : line.corners) {
            if (candidate.station == section.station) {
                corner = &candidate;
            }
        }
        glm::dvec2 center{0.0, 0.0};
        glm::dvec2 side  {0.0, 0.0};
        if (corner != nullptr) {
            const glm::vec3 position = table.get_span(corner->point).control[0];
            center = glm::dvec2{static_cast<double>(position.x), static_cast<double>(position.z)};
            side   = corner->side * corner->scale;
        } else {
            const Center_sample           place    = locate(line, section.station);
            const erhe::math::Curve_span& span     = table.get_span(place.span);
            const glm::vec3               position = erhe::math::get_span_position(span, static_cast<float>(place.t));
            center = glm::dvec2{static_cast<double>(position.x), static_cast<double>(position.z)};
            side   = get_side(glm::normalize(get_plan_derivative(span, place.t)));
        }
        section.left   = center + (side * half_width);
        section.right  = center - (side * half_width);
        section.bottom = std::min(get_soffit(fit, values, section.tread_before, section.station), get_soffit(fit, values, section.tread_after, section.station));
        sections.push_back(section);
    }
    // Each cell between two cross-sections is a strictly convex quad in plan, its edges with a length.
    for (std::size_t i = 0; (i + 1) < sections.size(); ++i) {
        const Cross_section& a = sections[i];
        const Cross_section& b = sections[i + 1];
        const std::array<glm::dvec2, 4> corners{a.left, a.right, b.right, b.left};
        bool folded = (glm::length(b.left - a.left) < c_section_spacing) || (glm::length(b.right - a.right) < c_section_spacing);
        for (std::size_t c = 0; c < 4; ++c) {
            const glm::dvec2 e1 = corners[(c + 1) % 4] - corners[c];
            const glm::dvec2 e2 = corners[(c + 2) % 4] - corners[(c + 1) % 4];
            folded = folded || !(cross_from_above(e1, e2) > 1.0e-9);
        }
        if (folded) {
            return fmt::format(
                "the stairs fold over themselves {}: the curve turns more tightly than the width {:.3f} m allows",
                name_place(line, 0.5 * (a.station + b.station)), values.width
            );
        }
    }
    out_report.section_count = sections.size();
    if (out_mesh == nullptr) {
        return {};
    }

    Kit_mesh_builder builder;
    builder.set_slot(0);
    for (Cross_section& section : sections) {
        const float level_before = fit.get_tread_level(section.tread_before);
        const float level_after  = fit.get_tread_level(section.tread_after);
        for (std::size_t s = 0; s < 2; ++s) {
            const glm::dvec2& plan = (s == 0) ? section.left : section.right;
            const float       x    = static_cast<float>(plan.x);
            const float       z    = static_cast<float>(plan.y);
            section.bottom_vertex[s] = builder.add_vertex(glm::vec3{x, static_cast<float>(section.bottom), z});
            section.before_vertex[s] = builder.add_vertex(glm::vec3{x, level_before, z});
            section.after_vertex[s]  = (section.tread_before == section.tread_after) ? section.before_vertex[s] : builder.add_vertex(glm::vec3{x, level_after, z});
        }
    }
    constexpr std::size_t left  = 0;
    constexpr std::size_t right = 1;
    // The start cap up to the lower level there, facing back along the curve.
    {
        const Cross_section& section = sections.front();
        const bool           rises   = fit.get_tread_level(section.tread_after) > fit.get_tread_level(section.tread_before);
        const std::array<uint32_t, 2>& top = rises ? section.before_vertex : section.after_vertex;
        builder.add_quad(section.bottom_vertex[left], section.bottom_vertex[right], top[right], top[left]);
    }
    for (std::size_t i = 0, end = sections.size(); i < end; ++i) {
        const Cross_section& a = sections[i];
        // A riser faces the lower tread.
        if (a.tread_before != a.tread_after) {
            if (fit.get_tread_level(a.tread_after) > fit.get_tread_level(a.tread_before)) {
                builder.add_quad(a.before_vertex[left], a.before_vertex[right], a.after_vertex[right], a.after_vertex[left]);
            } else {
                builder.add_quad(a.after_vertex[right], a.after_vertex[left], a.before_vertex[left], a.before_vertex[right]);
            }
        }
        if ((i + 1) == end) {
            break;
        }
        const Cross_section& b = sections[i + 1];
        const float level   = fit.get_tread_level(a.tread_after);
        const bool  upper_a = (a.tread_before != a.tread_after) && (fit.get_tread_level(a.tread_before) < level);
        const bool  upper_b = (b.tread_before != b.tread_after) && (fit.get_tread_level(b.tread_after) < level);
        builder.add_quad(a.after_vertex[left], a.after_vertex[right], b.before_vertex[right], b.before_vertex[left]);
        if (a.bottom == b.bottom) {
            builder.add_quad(a.bottom_vertex[left], b.bottom_vertex[left], b.bottom_vertex[right], a.bottom_vertex[right]);
        } else {
            builder.add_triangle(a.bottom_vertex[left], b.bottom_vertex[left], b.bottom_vertex[right]);
            builder.add_triangle(a.bottom_vertex[left], b.bottom_vertex[right], a.bottom_vertex[right]);
        }
        for (std::size_t s = 0; s < 2; ++s) {
            const Stairs_side side = (s == left) ? Stairs_side::left : Stairs_side::right;
            const uint32_t bottom_a = a.bottom_vertex[s];
            const uint32_t bottom_b = b.bottom_vertex[s];
            const uint32_t top_a    = a.after_vertex[s];
            const uint32_t top_b    = b.before_vertex[s];
            const uint32_t foot_a   = a.before_vertex[s]; // the lower level at a riser this cell is above
            const uint32_t foot_b   = b.after_vertex[s];
            if (upper_a && upper_b) {
                add_side_facet(builder, side, {bottom_a, bottom_b, foot_b, foot_a});
                add_side_facet(builder, side, {foot_a, foot_b, top_b, top_a});
            } else if (upper_a) {
                add_side_facet(builder, side, {bottom_a, bottom_b, top_b, foot_a});
                add_side_facet(builder, side, {foot_a, top_b, top_a});
            } else if (upper_b) {
                add_side_facet(builder, side, {bottom_a, bottom_b, foot_b, top_a});
                add_side_facet(builder, side, {foot_b, top_b, top_a});
            } else {
                add_side_facet(builder, side, {bottom_a, bottom_b, top_b, top_a});
            }
        }
    }
    // The end cap up to the lower level there, facing along the curve.
    {
        const Cross_section& section = sections.back();
        const bool           drops   = fit.get_tread_level(section.tread_after) < fit.get_tread_level(section.tread_before);
        const std::array<uint32_t, 2>& top = drops ? section.after_vertex : section.before_vertex;
        builder.add_quad(section.bottom_vertex[right], section.bottom_vertex[left], top[left], top[right]);
    }
    builder.write(*out_mesh);
    return {};
}

} // namespace editor
