#include "erhe_math/curve_path.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace erhe::math {

namespace {

[[nodiscard]] auto is_zero(const glm::vec3& v) -> bool
{
    return (v.x == 0.0f) && (v.y == 0.0f) && (v.z == 0.0f);
}

// A unit vector perpendicular to v (v unit), from the axis v points least along.
[[nodiscard]] auto any_perpendicular(const glm::vec3& v) -> glm::vec3
{
    const glm::vec3 a{std::abs(v.x), std::abs(v.y), std::abs(v.z)};
    const glm::vec3 axis =
        ((a.x <= a.y) && (a.x <= a.z)) ? glm::vec3{1.0f, 0.0f, 0.0f} :
        (a.y <= a.z)                   ? glm::vec3{0.0f, 1.0f, 0.0f} :
                                         glm::vec3{0.0f, 0.0f, 1.0f};
    return glm::normalize(glm::cross(v, axis));
}

[[nodiscard]] auto span_speed(const Curve_span& span, const double t) -> double
{
    const glm::dvec3 p0{span.control[0]};
    const glm::dvec3 p1{span.control[1]};
    const glm::dvec3 p2{span.control[2]};
    const glm::dvec3 p3{span.control[3]};
    const double     s = 1.0 - t;
    const glm::dvec3 d = 3.0 * (((s * s) * (p1 - p0)) + ((2.0 * s * t) * (p2 - p1)) + ((t * t) * (p3 - p2)));
    return glm::length(d);
}

constexpr double c_locate_tolerance   = 1.0e-6;
constexpr double c_adaptive_tolerance = 1.0e-10;
constexpr int    c_max_adaptive_depth = 12;

[[nodiscard]] auto gauss_legendre_length(const Curve_span& span, const double t0, const double t1) -> double
{
    static constexpr std::array<double, 5> nodes  {0.0, -0.5384693101056831, 0.5384693101056831, -0.9061798459386640, 0.9061798459386640};
    static constexpr std::array<double, 5> weights{0.5688888888888889, 0.4786286704993665, 0.4786286704993665, 0.2369268850561891, 0.2369268850561891};
    const double half   = 0.5 * (t1 - t0);
    const double center = 0.5 * (t1 + t0);
    double sum = 0.0;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        sum += weights[i] * span_speed(span, center + (half * nodes[i]));
    }
    return sum * half;
}

// Halves the interval until both halves agree with the whole (a kink in the speed near a cusp).
[[nodiscard]] auto adaptive_length(const Curve_span& span, const double t0, const double t1, const double whole, const int depth) -> double
{
    const double middle = 0.5 * (t0 + t1);
    const double left   = gauss_legendre_length(span, t0, middle);
    const double right  = gauss_legendre_length(span, middle, t1);
    const double sum    = left + right;
    if ((depth <= 0) || (std::abs(sum - whole) <= (c_adaptive_tolerance * std::max(sum, 1.0e-30)))) {
        return sum;
    }
    return adaptive_length(span, t0, middle, left, depth - 1) + adaptive_length(span, middle, t1, right, depth - 1);
}

} // anonymous namespace

auto bezier_point(const std::span<const glm::vec3> control_points, const float t, const std::span<glm::vec3> scratch) -> glm::vec3
{
    const std::size_t count = control_points.size();
    ERHE_VERIFY((count >= 1) && (scratch.size() >= count));
    std::copy(control_points.begin(), control_points.end(), scratch.begin());
    for (std::size_t k = count - 1; k >= 1; --k) {
        for (std::size_t i = 0; i < k; ++i) {
            scratch[i] = glm::mix(scratch[i], scratch[i + 1], t);
        }
    }
    return scratch[0];
}

auto bezier_unit_tangent(const std::span<const glm::vec3> control_points, const float t, const std::span<glm::vec3> scratch) -> glm::vec3
{
    const std::size_t count = control_points.size();
    ERHE_VERIFY((count >= 2) && (scratch.size() >= count));
    // Derivative Bezier: d_i = n * (P_{i+1} - P_i)
    const float n = static_cast<float>(count - 1);
    for (std::size_t i = 0; (i + 1) < count; ++i) {
        scratch[i] = n * (control_points[i + 1] - control_points[i]);
    }
    glm::vec3 tangent = scratch[0];
    if ((count - 1) > 1) {
        for (std::size_t k = count - 2; k >= 1; --k) {
            for (std::size_t i = 0; i < k; ++i) {
                scratch[i] = glm::mix(scratch[i], scratch[i + 1], t);
            }
        }
        tangent = scratch[0];
    }
    const float length = glm::length(tangent);
    if (length < 1.0e-8f) {
        // Repeated control points: fall back to a small finite difference.
        const float     h  = 1.0e-3f;
        const float     t0 = std::max(0.0f, t - h);
        const float     t1 = std::min(1.0f, t + h);
        const glm::vec3 a  = bezier_point(control_points, t1, scratch);
        const glm::vec3 b  = bezier_point(control_points, t0, scratch);
        tangent = a - b;
    }
    const float final_length = glm::length(tangent);
    return (final_length > 1.0e-12f) ? tangent / final_length : glm::vec3{0.0f, 1.0f, 0.0f};
}

auto initial_transport_up(const glm::vec3& tangent) -> glm::vec3
{
    glm::vec3 up = glm::vec3{0.0f, 1.0f, 0.0f} - tangent * tangent.y;
    if (glm::length(up) < 1.0e-4f) {
        up = glm::vec3{1.0f, 0.0f, 0.0f} - tangent * tangent.x;
    }
    return glm::normalize(up);
}

auto transport_up(const glm::vec3& previous_tangent, const glm::vec3& tangent, const glm::vec3& previous_up) -> glm::vec3
{
    const glm::vec3 axis = glm::cross(previous_tangent, tangent);
    const float     s    = glm::length(axis);
    const float     c    = std::clamp(glm::dot(previous_tangent, tangent), -1.0f, 1.0f);
    glm::vec3 up = previous_up;
    if (!(s < 1.0e-8f)) {
        const glm::vec3 a     = axis / s;
        const float     angle = std::atan2(s, c);
        const glm::vec3 u     = previous_up;
        // Rodrigues rotation
        up = u * std::cos(angle) + glm::cross(a, u) * std::sin(angle) + a * glm::dot(a, u) * (1.0f - std::cos(angle));
    }
    // Re-orthonormalize against drift.
    return glm::normalize(up - tangent * glm::dot(tangent, up));
}

auto get_span_count(const Curve_path& path) -> std::size_t
{
    const std::size_t n = path.points.size();
    if (n < 2) {
        return 0;
    }
    return (path.closure == Curve_closure::closed) ? n : (n - 1);
}

auto get_effective_handle(const Curve_point& point, const Curve_handle handle) -> glm::vec3
{
    if (point.kind == Curve_point_kind::corner) {
        return glm::vec3{0.0f};
    }
    return (handle == Curve_handle::in) ? point.in_handle : point.out_handle;
}

void set_curve_handle(Curve_point& point, const Curve_handle handle, const glm::vec3& offset)
{
    glm::vec3& target = (handle == Curve_handle::in) ? point.in_handle  : point.out_handle;
    glm::vec3& other  = (handle == Curve_handle::in) ? point.out_handle : point.in_handle;
    target = offset;
    if (point.kind != Curve_point_kind::smooth) {
        return;
    }
    const float offset_length = glm::length(offset);
    if (offset_length > 0.0f) {
        other = -(offset / offset_length) * glm::length(other);
    }
}

void get_catmull_rom_handles(
    const std::span<const Curve_point> points,
    const std::size_t                  index,
    const Curve_closure                closure,
    glm::vec3&                         out_in_handle,
    glm::vec3&                         out_out_handle
)
{
    out_in_handle  = glm::vec3{0.0f};
    out_out_handle = glm::vec3{0.0f};
    const std::size_t n = points.size();
    if ((n < 2) || (index >= n)) {
        return;
    }
    glm::vec3 slope{0.0f};
    if (closure == Curve_closure::closed) {
        slope = 0.5f * (points[(index + 1) % n].position - points[(index + n - 1) % n].position);
    } else if (index == 0) {
        slope = points[1].position - points[0].position;
    } else if (index == (n - 1)) {
        slope = points[n - 1].position - points[n - 2].position;
    } else {
        slope = 0.5f * (points[index + 1].position - points[index - 1].position);
    }
    out_out_handle = slope / 3.0f;
    out_in_handle  = -out_out_handle;
}

void set_curve_point_kind(const std::span<Curve_point> points, const std::size_t index, const Curve_closure closure, const Curve_point_kind kind)
{
    if (index >= points.size()) {
        return;
    }
    Curve_point&           point    = points[index];
    const Curve_point_kind previous = point.kind;
    if (kind == previous) {
        return;
    }
    if (kind == Curve_point_kind::corner) {
        point.in_handle  = glm::vec3{0.0f};
        point.out_handle = glm::vec3{0.0f};
    } else if (previous == Curve_point_kind::corner) {
        get_catmull_rom_handles(points, index, closure, point.in_handle, point.out_handle);
    } else if (kind == Curve_point_kind::smooth) {
        const float out_length = glm::length(point.out_handle);
        const float in_length  = glm::length(point.in_handle);
        if (out_length > 0.0f) {
            point.in_handle = -(point.out_handle / out_length) * in_length;
        } else if (in_length > 0.0f) {
            point.out_handle = -(point.in_handle / in_length) * out_length;
        }
    }
    point.kind = kind;
}

auto make_curve_span(const Curve_point& from, const Curve_point& to) -> Curve_span
{
    const glm::vec3 out_handle = get_effective_handle(from, Curve_handle::out);
    const glm::vec3 in_handle  = get_effective_handle(to,   Curve_handle::in);
    const glm::vec3 p0         = from.position;
    const glm::vec3 p3         = to.position;
    const bool      has_out    = !is_zero(out_handle);
    const bool      has_in     = !is_zero(in_handle);
    Curve_span span;
    span.control[0] = p0;
    span.control[3] = p3;
    if (!has_out && !has_in) {
        span.shape      = Curve_span_shape::line;
        span.control[1] = p0 + ((p3 - p0) / 3.0f);
        span.control[2] = p3 + ((p0 - p3) / 3.0f);
        return span;
    }
    // An end without a handle points one third of the way to the other end's handle point.
    span.shape      = Curve_span_shape::cubic;
    span.control[1] = has_out ? (p0 + out_handle) : (p0 + (((p3 + in_handle) - p0) / 3.0f));
    span.control[2] = has_in  ? (p3 + in_handle)  : (p3 + (((p0 + out_handle) - p3) / 3.0f));
    return span;
}

namespace {

// Gives an end that contributed no handle to a span the split's handle, keeping its other span.
void give_split_handle(Curve_point& point, const Curve_handle handle, const glm::vec3& offset)
{
    if (!is_zero(get_effective_handle(point, handle))) {
        if (handle == Curve_handle::in) {
            point.in_handle = offset;
        } else {
            point.out_handle = offset;
        }
        return;
    }
    if (point.kind == Curve_point_kind::corner) {
        point.in_handle  = glm::vec3{0.0f};
        point.out_handle = glm::vec3{0.0f};
    }
    if (handle == Curve_handle::in) {
        point.in_handle = offset;
    } else {
        point.out_handle = offset;
    }
    point.kind = Curve_point_kind::free;
}

} // anonymous namespace

auto insert_curve_point(Curve_path& path, const Curve_location& location) -> std::size_t
{
    const std::size_t n          = path.points.size();
    const std::size_t span_count = get_span_count(path);
    ERHE_VERIFY(location.span < span_count);
    const std::size_t from_index = location.span;
    const std::size_t to_index   = (location.span + 1) % n;
    const Curve_span  span       = make_curve_span(path.points[from_index], path.points[to_index]);
    const float       t          = location.t;

    Curve_point middle{};
    if (span.shape == Curve_span_shape::line) {
        middle.position = glm::mix(span.control[0], span.control[3], t);
        middle.kind     = Curve_point_kind::corner;
    } else {
        const glm::vec3 a  = glm::mix(span.control[0], span.control[1], t);
        const glm::vec3 b  = glm::mix(span.control[1], span.control[2], t);
        const glm::vec3 c  = glm::mix(span.control[2], span.control[3], t);
        const glm::vec3 ab = glm::mix(a, b, t);
        const glm::vec3 bc = glm::mix(b, c, t);
        const glm::vec3 m  = glm::mix(ab, bc, t);
        middle.position   = m;
        middle.in_handle  = ab - m;
        middle.out_handle = bc - m;
        middle.kind       = Curve_point_kind::smooth;
        give_split_handle(path.points[from_index], Curve_handle::out, a - span.control[0]);
        give_split_handle(path.points[to_index],   Curve_handle::in,  c - span.control[3]);
    }
    const std::size_t index = from_index + 1;
    path.points.insert(path.points.begin() + static_cast<std::ptrdiff_t>(index), middle);
    return index;
}

auto make_curve_end_point(const Curve_path& path, const Curve_end end) -> Curve_point
{
    const std::size_t n = path.points.size();
    ERHE_VERIFY(n >= 2);
    const bool         at_end   = (end == Curve_end::end);
    const Curve_point& last     = at_end ? path.points[n - 1] : path.points[0];
    const Curve_point& previous = at_end ? path.points[n - 2] : path.points[1];
    const Curve_span   span     = at_end ? make_curve_span(previous, last) : make_curve_span(last, previous);
    const glm::vec3    tangent  = at_end ? get_span_tangent(span, 1.0f) : -get_span_tangent(span, 0.0f);
    const float        chord    = glm::distance(last.position, previous.position);
    const float        length   = (chord > 0.0f) ? chord : 1.0f;
    Curve_point point{};
    point.position = last.position + (tangent * length);
    if (last.kind == Curve_point_kind::corner) {
        point.kind = Curve_point_kind::corner;
        return point;
    }
    point.kind = Curve_point_kind::smooth;
    const glm::vec3 forward = tangent * (length / 3.0f);
    point.in_handle  = at_end ? -forward :  forward;
    point.out_handle = at_end ?  forward : -forward;
    return point;
}

auto add_curve_point(Curve_path& path, const Curve_end end, const Curve_point& point) -> std::size_t
{
    if (end == Curve_end::start) {
        path.points.insert(path.points.begin(), point);
        return 0;
    }
    path.points.push_back(point);
    return path.points.size() - 1;
}

auto get_span_position(const Curve_span& span, const float t) -> glm::vec3
{
    if (span.shape == Curve_span_shape::line) {
        return glm::mix(span.control[0], span.control[3], t);
    }
    const glm::vec3 a  = glm::mix(span.control[0], span.control[1], t);
    const glm::vec3 b  = glm::mix(span.control[1], span.control[2], t);
    const glm::vec3 c  = glm::mix(span.control[2], span.control[3], t);
    const glm::vec3 ab = glm::mix(a, b, t);
    const glm::vec3 bc = glm::mix(b, c, t);
    return glm::mix(ab, bc, t);
}

auto get_span_derivative(const Curve_span& span, const float t) -> glm::vec3
{
    if (span.shape == Curve_span_shape::line) {
        return span.control[3] - span.control[0];
    }
    const glm::vec3 d0 = span.control[1] - span.control[0];
    const glm::vec3 d1 = span.control[2] - span.control[1];
    const glm::vec3 d2 = span.control[3] - span.control[2];
    const glm::vec3 a  = glm::mix(d0, d1, t);
    const glm::vec3 b  = glm::mix(d1, d2, t);
    return 3.0f * glm::mix(a, b, t);
}

auto get_span_tangent(const Curve_span& span, const float t) -> glm::vec3
{
    const float scale =
        glm::length(span.control[1] - span.control[0]) +
        glm::length(span.control[2] - span.control[1]) +
        glm::length(span.control[3] - span.control[2]);
    if (!(scale > 0.0f)) {
        return glm::vec3{1.0f, 0.0f, 0.0f};
    }
    const glm::vec3 derivative = get_span_derivative(span, t);
    const float     length     = glm::length(derivative);
    if (length > (1.0e-6f * scale)) {
        return derivative / length;
    }
    // Cusp: the chord across a small parameter step, then the whole chord.
    const float     h     = 1.0e-3f;
    const glm::vec3 chord = get_span_position(span, std::min(1.0f, t + h)) - get_span_position(span, std::max(0.0f, t - h));
    const float     chord_length = glm::length(chord);
    if (chord_length > 0.0f) {
        return chord / chord_length;
    }
    const glm::vec3 whole        = span.control[3] - span.control[0];
    const float     whole_length = glm::length(whole);
    return (whole_length > 0.0f) ? (whole / whole_length) : glm::vec3{1.0f, 0.0f, 0.0f};
}

auto make_up_frame(const Curve_sample& sample, const glm::vec3& reference_up, const glm::vec3& fallback_side) -> Curve_frame
{
    Curve_frame frame;
    frame.position = sample.position;
    frame.tangent  = sample.tangent;
    const glm::vec3 up        = reference_up - (sample.tangent * glm::dot(sample.tangent, reference_up));
    const float     up_length = glm::length(up);
    if (up_length > 1.0e-6f) {
        frame.up   = up / up_length;
        frame.side = glm::normalize(glm::cross(frame.up, frame.tangent));
        return frame;
    }
    // Tangent along the up axis: keep the given side (the previous heading).
    glm::vec3   side        = fallback_side - (sample.tangent * glm::dot(sample.tangent, fallback_side));
    const float side_length = glm::length(side);
    side       = (side_length > 1.0e-6f) ? (side / side_length) : any_perpendicular(sample.tangent);
    frame.side = side;
    frame.up   = glm::cross(frame.tangent, side);
    return frame;
}

void Curve_path_table::build(const Curve_path& path)
{
    m_closure = path.closure;
    m_spans.clear();
    m_point_distances.clear();
    m_cumulative.clear();
    m_length          = 0.0f;
    m_single_position = path.points.empty() ? glm::vec3{0.0f} : path.points.front().position;

    const std::size_t n          = path.points.size();
    const std::size_t span_count = erhe::math::get_span_count(path);
    for (std::size_t s = 0; s < span_count; ++s) {
        m_spans.push_back(make_curve_span(path.points[s], path.points[(s + 1) % n]));
    }
    double total = 0.0;
    m_point_distances.push_back(0.0f);
    m_cumulative.push_back(0.0f);
    const double step = 1.0 / static_cast<double>(subdivisions_per_span);
    for (std::size_t s = 0; s < span_count; ++s) {
        for (std::size_t j = 0; j < subdivisions_per_span; ++j) {
            const double t0 = static_cast<double>(j) * step;
            const double t1 = (j == (subdivisions_per_span - 1)) ? 1.0 : (static_cast<double>(j + 1) * step);
            total += length_within_span(s, t0, t1);
            m_cumulative.push_back(static_cast<float>(total));
        }
        m_point_distances.push_back(static_cast<float>(total));
    }
    m_length = static_cast<float>(total);
}

auto Curve_path_table::get_closure() const -> Curve_closure
{
    return m_closure;
}

auto Curve_path_table::get_span_count() const -> std::size_t
{
    return m_spans.size();
}

auto Curve_path_table::get_span(const std::size_t span) const -> const Curve_span&
{
    return m_spans.at(span);
}

auto Curve_path_table::get_length() const -> float
{
    return m_length;
}

auto Curve_path_table::get_point_distances() const -> std::span<const float>
{
    return m_point_distances;
}

// Adaptive 5-point Gauss-Legendre over [t0, t1], an interval inside one subdivision of the span.
auto Curve_path_table::length_within_span(const std::size_t span, const double t0, const double t1) const -> double
{
    const Curve_span& curve_span = m_spans[span];
    if (curve_span.shape == Curve_span_shape::line) {
        return glm::length(glm::dvec3{curve_span.control[3]} - glm::dvec3{curve_span.control[0]}) * (t1 - t0);
    }
    return adaptive_length(curve_span, t0, t1, gauss_legendre_length(curve_span, t0, t1), c_max_adaptive_depth);
}

auto Curve_path_table::normalize_distance(const float distance) const -> float
{
    if (!(m_length > 0.0f)) {
        return 0.0f;
    }
    if (m_closure == Curve_closure::open) {
        return std::clamp(distance, 0.0f, m_length);
    }
    float d = std::fmod(distance, m_length);
    if (d < 0.0f) {
        d += m_length;
    }
    return (d >= m_length) ? 0.0f : d;
}

auto Curve_path_table::locate(const float distance) const -> Curve_location
{
    if (m_spans.empty()) {
        return Curve_location{};
    }
    const float       d          = normalize_distance(distance);
    const std::size_t last_index = (m_spans.size() * subdivisions_per_span) - 1;
    // upper_bound: a distance on a boundary takes the interval that leaves it.
    const std::vector<float>::const_iterator it = std::upper_bound(m_cumulative.begin(), m_cumulative.end(), d);
    const std::size_t k    = std::min(last_index, static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, (it - m_cumulative.begin()) - 1)));
    const std::size_t span = k / subdivisions_per_span;
    const std::size_t j    = k % subdivisions_per_span;

    const Curve_span& curve_span = m_spans[span];
    if (curve_span.shape == Curve_span_shape::line) {
        const double span_length = static_cast<double>(m_point_distances[span + 1]) - static_cast<double>(m_point_distances[span]);
        const double t = (span_length > 0.0)
            ? std::clamp((static_cast<double>(d) - static_cast<double>(m_point_distances[span])) / span_length, 0.0, 1.0)
            : 0.0;
        return Curve_location{.span = span, .t = static_cast<float>(t)};
    }

    const double step       = 1.0 / static_cast<double>(subdivisions_per_span);
    const double t0         = static_cast<double>(j) * step;
    const double t1         = (j == (subdivisions_per_span - 1)) ? 1.0 : (static_cast<double>(j + 1) * step);
    const double sub_length = static_cast<double>(m_cumulative[k + 1]) - static_cast<double>(m_cumulative[k]);
    const double target     = static_cast<double>(d) - static_cast<double>(m_cumulative[k]);
    if (!(sub_length > 0.0)) {
        return Curve_location{.span = span, .t = static_cast<float>(t0)};
    }
    double t = std::clamp(t0 + ((t1 - t0) * (target / sub_length)), t0, t1);
    for (int iteration = 0; iteration < 4; ++iteration) {
        const double error = length_within_span(span, t0, t) - target;
        if (std::abs(error) <= (c_locate_tolerance * sub_length)) {
            break;
        }
        const double speed = span_speed(curve_span, t);
        if (!(speed > 0.0)) {
            break;
        }
        t = std::clamp(t - (error / speed), t0, t1);
    }
    return Curve_location{.span = span, .t = static_cast<float>(t)};
}

auto Curve_path_table::get_distance(const Curve_location& location) const -> float
{
    if (location.span >= m_spans.size()) {
        return m_length;
    }
    const double      t = std::clamp(static_cast<double>(location.t), 0.0, 1.0);
    const std::size_t j = std::min(subdivisions_per_span - 1, static_cast<std::size_t>(t * static_cast<double>(subdivisions_per_span)));
    const double      t0 = static_cast<double>(j) / static_cast<double>(subdivisions_per_span);
    const std::size_t k  = (location.span * subdivisions_per_span) + j;
    return static_cast<float>(static_cast<double>(m_cumulative[k]) + length_within_span(location.span, t0, t));
}

auto Curve_path_table::get_nearest_location(const glm::vec3& point) const -> Curve_location
{
    if (m_spans.empty()) {
        return Curve_location{};
    }
    constexpr int c_samples = 64;
    Curve_location best{};
    float          best_distance = std::numeric_limits<float>::max();
    for (std::size_t s = 0; s < m_spans.size(); ++s) {
        for (int i = 0; i <= c_samples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(c_samples);
            const float d = glm::distance(get_span_position(m_spans[s], t), point);
            if (d < best_distance) {
                best_distance = d;
                best          = Curve_location{.span = s, .t = t};
            }
        }
    }
    // Golden section over the neighboring sample intervals.
    const Curve_span& span = m_spans[best.span];
    const float step  = 1.0f / static_cast<float>(c_samples);
    float       lo    = std::max(0.0f, best.t - step);
    float       hi    = std::min(1.0f, best.t + step);
    const float ratio = 0.6180339887f;
    for (int iteration = 0; iteration < 40; ++iteration) {
        const float t1 = hi - (ratio * (hi - lo));
        const float t2 = lo + (ratio * (hi - lo));
        if (glm::distance(get_span_position(span, t1), point) < glm::distance(get_span_position(span, t2), point)) {
            hi = t2;
        } else {
            lo = t1;
        }
    }
    const float t = 0.5f * (lo + hi);
    if (glm::distance(get_span_position(span, t), point) < best_distance) {
        best.t = t;
    }
    return best;
}

auto Curve_path_table::get_position(const Curve_location& location) const -> glm::vec3
{
    if (m_spans.empty()) {
        return m_single_position;
    }
    return get_span_position(m_spans[std::min(location.span, m_spans.size() - 1)], location.t);
}

auto Curve_path_table::get_derivative(const Curve_location& location) const -> glm::vec3
{
    if (m_spans.empty()) {
        return glm::vec3{0.0f};
    }
    return get_span_derivative(m_spans[std::min(location.span, m_spans.size() - 1)], location.t);
}

auto Curve_path_table::get_sample(const Curve_location& location) const -> Curve_sample
{
    if (m_spans.empty()) {
        return Curve_sample{.position = m_single_position, .tangent = glm::vec3{1.0f, 0.0f, 0.0f}};
    }
    const Curve_span& span = m_spans[std::min(location.span, m_spans.size() - 1)];
    return Curve_sample{
        .position = get_span_position(span, location.t),
        .tangent  = get_span_tangent (span, location.t)
    };
}

auto Curve_path_table::sample_at_distance(const float distance) const -> Curve_sample
{
    return get_sample(locate(distance));
}

auto Curve_path_table::frame_at_distance(const float distance, const glm::vec3& reference_up, const glm::vec3& fallback_side) const -> Curve_frame
{
    return make_up_frame(sample_at_distance(distance), reference_up, fallback_side);
}

void Curve_path_table::get_up_frames_at_distances(
    const std::span<const float> distances,
    const glm::vec3&             reference_up,
    const std::span<Curve_frame> out_frames
) const
{
    ERHE_VERIFY(out_frames.size() == distances.size());
    // Leading vertical samples take the side of the first sample that is not vertical.
    glm::vec3 side = any_perpendicular(reference_up);
    for (const float distance : distances) {
        const Curve_sample sample = sample_at_distance(distance);
        const glm::vec3    up     = reference_up - (sample.tangent * glm::dot(sample.tangent, reference_up));
        if (glm::length(up) > 1.0e-6f) {
            side = glm::normalize(glm::cross(glm::normalize(up), sample.tangent));
            break;
        }
    }
    for (std::size_t i = 0; i < distances.size(); ++i) {
        out_frames[i] = frame_at_distance(distances[i], reference_up, side);
        side          = out_frames[i].side;
    }
}

void Curve_path_table::get_transported_frames_at_distances(const std::span<const float> distances, const std::span<Curve_frame> out_frames) const
{
    ERHE_VERIFY(out_frames.size() == distances.size());
    for (std::size_t i = 0; i < distances.size(); ++i) {
        const Curve_sample sample = sample_at_distance(distances[i]);
        Curve_frame&       frame  = out_frames[i];
        frame.position = sample.position;
        frame.up       = (i == 0)
            ? initial_transport_up(sample.tangent)
            : transport_up(out_frames[i - 1].tangent, sample.tangent, out_frames[i - 1].up);
        frame.tangent  = sample.tangent;
        frame.side     = glm::normalize(glm::cross(frame.up, frame.tangent));
    }
}

} // namespace erhe::math
