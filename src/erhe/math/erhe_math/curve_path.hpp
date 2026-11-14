#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <vector>

// Curve path: control points with handles, cubic Bezier spans, arc-length
// table, frames at a distance. Rules and accuracy: doc/erhe/math.md "Curve path".

namespace erhe::math {

// General-degree Bezier (degree = control_points.size() - 1) by De Casteljau.
// scratch.size() must be at least control_points.size().
[[nodiscard]] auto bezier_point(std::span<const glm::vec3> control_points, float t, std::span<glm::vec3> scratch) -> glm::vec3;

// Unit tangent of a general-degree Bezier; a finite difference where the derivative vanishes.
[[nodiscard]] auto bezier_unit_tangent(std::span<const glm::vec3> control_points, float t, std::span<glm::vec3> scratch) -> glm::vec3;

// Parallel transport: the first up of a frame chain (+Y off the tangent, +X when vertical).
[[nodiscard]] auto initial_transport_up(const glm::vec3& tangent) -> glm::vec3;

// Parallel transport: previous_up rotated by the minimal rotation from previous_tangent to tangent.
[[nodiscard]] auto transport_up(const glm::vec3& previous_tangent, const glm::vec3& tangent, const glm::vec3& previous_up) -> glm::vec3;

enum class Curve_point_kind : unsigned int
{
    corner = 0,
    smooth,
    free
};

enum class Curve_closure : unsigned int
{
    open = 0,
    closed
};

enum class Curve_handle : unsigned int
{
    in = 0,
    out
};

// Handles are offsets from position; a corner ignores them.
class Curve_point
{
public:
    glm::vec3        position  {0.0f};
    glm::vec3        in_handle {0.0f};
    glm::vec3        out_handle{0.0f};
    Curve_point_kind kind      {Curve_point_kind::corner};
};

class Curve_path
{
public:
    std::vector<Curve_point> points;
    Curve_closure            closure{Curve_closure::open};
};

[[nodiscard]] auto get_span_count(const Curve_path& path) -> std::size_t;

// The handle offset a point contributes to its spans: zero for a corner.
[[nodiscard]] auto get_effective_handle(const Curve_point& point, Curve_handle handle) -> glm::vec3;

// Sets one handle; a smooth point turns the other handle opposite, keeping its length.
void set_curve_handle(Curve_point& point, Curve_handle handle, const glm::vec3& offset);

// Catmull-Rom handles of point index from its neighbors (one-sided at open ends).
void get_catmull_rom_handles(
    std::span<const Curve_point> points,
    std::size_t                  index,
    Curve_closure                closure,
    glm::vec3&                   out_in_handle,
    glm::vec3&                   out_out_handle
);

// Kind change: to corner zeroes the handles, from corner gives Catmull-Rom handles,
// free to smooth aligns the in handle opposite the out handle.
void set_curve_point_kind(std::span<Curve_point> points, std::size_t index, Curve_closure closure, Curve_point_kind kind);

enum class Curve_span_shape : unsigned int
{
    line = 0,
    cubic
};

// control = p0, p0 + out, p1 + in, p1; a line keeps its inner points at the thirds.
class Curve_span
{
public:
    std::array<glm::vec3, 4> control{};
    Curve_span_shape         shape{Curve_span_shape::line};
};

[[nodiscard]] auto make_curve_span(const Curve_point& from, const Curve_point& to) -> Curve_span;
[[nodiscard]] auto get_span_position  (const Curve_span& span, float t) -> glm::vec3;
[[nodiscard]] auto get_span_derivative(const Curve_span& span, float t) -> glm::vec3;
[[nodiscard]] auto get_span_tangent   (const Curve_span& span, float t) -> glm::vec3;

class Curve_location
{
public:
    std::size_t span{0};
    float       t   {0.0f};
};

enum class Curve_end : unsigned int
{
    start = 0,
    end
};

// Splits the span at location (de Casteljau), keeping the shape; returns the new point's index.
// A line span gives a corner, a cubic span a smooth point; an end that gave the span no handle
// gets the split's handle and becomes free (a corner's other handle is zeroed).
auto insert_curve_point(Curve_path& path, const Curve_location& location) -> std::size_t;

// The point an open path is extended by at an end: along the end tangent by the end span's chord,
// a corner beyond a corner end, else smooth with handles a third of that chord.
[[nodiscard]] auto make_curve_end_point(const Curve_path& path, Curve_end end) -> Curve_point;

// Adds point before the first or after the last point; returns its index.
auto add_curve_point(Curve_path& path, Curve_end end, const Curve_point& point) -> std::size_t;

class Curve_sample
{
public:
    glm::vec3 position{0.0f};
    glm::vec3 tangent {1.0f, 0.0f, 0.0f};
};

// side x up = tangent (side = cross(up, tangent)), the sweep's convention.
class Curve_frame
{
public:
    glm::vec3 position{0.0f};
    glm::vec3 tangent {1.0f, 0.0f, 0.0f};
    glm::vec3 side    {0.0f, 0.0f, -1.0f};
    glm::vec3 up      {0.0f, 1.0f, 0.0f};
};

// Up-keeping frame from a sample; fallback_side is used where the tangent is parallel to reference_up.
[[nodiscard]] auto make_up_frame(const Curve_sample& sample, const glm::vec3& reference_up, const glm::vec3& fallback_side) -> Curve_frame;

// Baked arc-length table of a Curve_path; rebuild when the points change. Queries do not allocate.
class Curve_path_table
{
public:
    static constexpr std::size_t subdivisions_per_span = 16;

    void build(const Curve_path& path);

    [[nodiscard]] auto get_closure        () const -> Curve_closure;
    [[nodiscard]] auto get_span_count     () const -> std::size_t;
    [[nodiscard]] auto get_span           (std::size_t span) const -> const Curve_span&;
    [[nodiscard]] auto get_length         () const -> float;
    [[nodiscard]] auto get_point_distances() const -> std::span<const float>;

    // Clamped to [0, length] on an open path, wrapped into [0, length) on a closed one.
    [[nodiscard]] auto normalize_distance(float distance) const -> float;
    [[nodiscard]] auto locate            (float distance) const -> Curve_location;
    [[nodiscard]] auto get_distance      (const Curve_location& location) const -> float;
    // The location nearest to point: 64 samples per span, then golden section search.
    [[nodiscard]] auto get_nearest_location(const glm::vec3& point) const -> Curve_location;

    [[nodiscard]] auto get_position  (const Curve_location& location) const -> glm::vec3;
    [[nodiscard]] auto get_derivative(const Curve_location& location) const -> glm::vec3;
    [[nodiscard]] auto get_sample    (const Curve_location& location) const -> Curve_sample;

    [[nodiscard]] auto sample_at_distance(float distance) const -> Curve_sample;
    [[nodiscard]] auto frame_at_distance (float distance, const glm::vec3& reference_up, const glm::vec3& fallback_side) const -> Curve_frame;

    // out_frames.size() == distances.size(); a vertical sample keeps the previous sample's side.
    void get_up_frames_at_distances(std::span<const float> distances, const glm::vec3& reference_up, std::span<Curve_frame> out_frames) const;

    // Parallel-transported frames (the sweep's transport); distances ascending.
    void get_transported_frames_at_distances(std::span<const float> distances, std::span<Curve_frame> out_frames) const;

private:
    [[nodiscard]] auto length_within_span(std::size_t span, double t0, double t1) const -> double;

    Curve_closure           m_closure       {Curve_closure::open};
    std::vector<Curve_span> m_spans;
    std::vector<float>      m_point_distances;
    std::vector<float>      m_cumulative;
    glm::vec3               m_single_position{0.0f};
    float                   m_length        {0.0f};
};

} // namespace erhe::math
