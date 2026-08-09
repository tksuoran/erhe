#include "erhe_geometry/shapes/sweep.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_profile/profile.hpp"

#include <geogram/mesh/mesh.h>

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace erhe::geometry::shapes {

namespace {

// De Casteljau evaluation of a single bezier segment whose degree is
// control_points.size() - 1.
auto bezier_point(std::vector<glm::vec3> points, const float t) -> glm::vec3
{
    for (std::size_t k = points.size() - 1; k >= 1; --k) {
        for (std::size_t i = 0; i < k; ++i) {
            points[i] = glm::mix(points[i], points[i + 1], t);
        }
    }
    return points[0];
}

auto bezier_tangent(const std::vector<glm::vec3>& points, const float t) -> glm::vec3
{
    // Derivative bezier: d_i = n * (P_{i+1} - P_i)
    std::vector<glm::vec3> derivative;
    derivative.reserve(points.size() - 1);
    const float n = static_cast<float>(points.size() - 1);
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        derivative.push_back(n * (points[i + 1] - points[i]));
    }
    glm::vec3 tangent = (derivative.size() == 1) ? derivative[0] : bezier_point(derivative, t);
    const float length = glm::length(tangent);
    if (length < 1.0e-8f) {
        // Repeated control points: fall back to a small finite difference.
        const float h  = 1.0e-3f;
        const float t0 = std::max(0.0f, t - h);
        const float t1 = std::min(1.0f, t + h);
        tangent = bezier_point(std::vector<glm::vec3>{points}, t1) - bezier_point(std::vector<glm::vec3>{points}, t0);
    }
    const float final_length = glm::length(tangent);
    return (final_length > 1.0e-12f) ? tangent / final_length : glm::vec3{0.0f, 1.0f, 0.0f};
}

auto evaluate_taper(const std::vector<glm::vec2>& keys, const float t) -> float
{
    if (keys.empty()) {
        return 1.0f;
    }
    if (t <= keys.front().x) {
        return keys.front().y;
    }
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        if (t <= keys[i + 1].x) {
            const float span = keys[i + 1].x - keys[i].x;
            const float f    = (span > 1.0e-9f) ? (t - keys[i].x) / span : 0.0f;
            return glm::mix(keys[i].y, keys[i + 1].y, f);
        }
    }
    return keys.back().y;
}

} // anonymous namespace

void make_sweep(GEO::Mesh& mesh, const Sweep_parameters& parameters)
{
    ERHE_PROFILE_FUNCTION();

    const std::size_t profile_count = parameters.profile.size();
    const int         spine_steps   = std::max(1, parameters.spine_steps);
    if ((profile_count < 3) || (parameters.spine.size() < 2)) {
        return;
    }
    const bool morph = parameters.profile_end.size() == profile_count;

    // Station frames: parallel transport along the sampled spine.
    const int station_count = spine_steps + 1;
    std::vector<glm::vec3> positions(station_count);
    std::vector<glm::vec3> tangents (station_count);
    std::vector<glm::vec3> ups      (station_count);
    std::vector<glm::vec3> sides    (station_count);
    std::vector<float>     scales   (station_count);
    for (int i = 0; i < station_count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(spine_steps);
        positions[i] = bezier_point(parameters.spine, t);
        tangents [i] = bezier_tangent(parameters.spine, t);
        scales   [i] = evaluate_taper(parameters.taper, t);
    }
    {
        // Initial up: world +Y projected off the tangent; +X when vertical.
        const glm::vec3 t0 = tangents[0];
        glm::vec3 up = glm::vec3{0.0f, 1.0f, 0.0f} - t0 * t0.y;
        if (glm::length(up) < 1.0e-4f) {
            up = glm::vec3{1.0f, 0.0f, 0.0f} - t0 * t0.x;
        }
        ups[0] = glm::normalize(up);
    }
    for (int i = 1; i < station_count; ++i) {
        // Rotate the previous up by the minimal rotation between tangents.
        const glm::vec3 prev = tangents[i - 1];
        const glm::vec3 next = tangents[i];
        const glm::vec3 axis = glm::cross(prev, next);
        const float     s    = glm::length(axis);
        const float     c    = std::clamp(glm::dot(prev, next), -1.0f, 1.0f);
        if (s < 1.0e-8f) {
            ups[i] = ups[i - 1];
        } else {
            const glm::vec3 a     = axis / s;
            const float     angle = std::atan2(s, c);
            const glm::vec3 u     = ups[i - 1];
            // Rodrigues rotation
            ups[i] = u * std::cos(angle) + glm::cross(a, u) * std::sin(angle) + a * glm::dot(a, u) * (1.0f - std::cos(angle));
        }
        // Re-orthonormalize against drift.
        ups[i] = glm::normalize(ups[i] - tangents[i] * glm::dot(tangents[i], ups[i]));
    }
    for (int i = 0; i < station_count; ++i) {
        // side x up = tangent: CCW profiles face outward.
        sides[i] = glm::normalize(glm::cross(ups[i], tangents[i]));
    }

    // A final scale of ~zero collapses the last ring into one tip vertex.
    const bool tip_collapse = scales[station_count - 1] < 1.0e-4f;
    const int  ring_count   = tip_collapse ? station_count - 1 : station_count;

    const GEO::index_t ring_vertices = static_cast<GEO::index_t>(ring_count) * static_cast<GEO::index_t>(profile_count);
    const GEO::index_t total_vertices = ring_vertices + (tip_collapse ? 1u : 0u);
    const GEO::index_t first_vertex = mesh.vertices.create_vertices(total_vertices);

    auto ring_vertex = [&](const int ring, const std::size_t j) -> GEO::index_t {
        return first_vertex + static_cast<GEO::index_t>(ring) * static_cast<GEO::index_t>(profile_count) + static_cast<GEO::index_t>(j);
    };

    for (int i = 0; i < ring_count; ++i) {
        const float t          = static_cast<float>(i) / static_cast<float>(spine_steps);
        const float angle      = parameters.twist * t;
        const float sin_a      = std::sin(angle);
        const float cos_a      = std::cos(angle);
        for (std::size_t j = 0; j < profile_count; ++j) {
            const glm::vec2 p0 = parameters.profile[j];
            const glm::vec2 p  = morph ? glm::mix(p0, parameters.profile_end[j], t) : p0;
            const float x = scales[i] * (p.x * cos_a - p.y * sin_a);
            const float y = scales[i] * (p.x * sin_a + p.y * cos_a);
            const glm::vec3 world = positions[i] + x * sides[i] + y * ups[i];
            set_pointf(mesh.vertices, ring_vertex(i, j), GEO::vec3f{world.x, world.y, world.z});
        }
    }
    const GEO::index_t tip_vertex = tip_collapse ? (first_vertex + ring_vertices) : GEO::index_t{0};
    if (tip_collapse) {
        const glm::vec3 tip = positions[station_count - 1];
        set_pointf(mesh.vertices, tip_vertex, GEO::vec3f{tip.x, tip.y, tip.z});
    }

    // Side walls between consecutive rings.
    for (int i = 0; i + 1 < ring_count; ++i) {
        for (std::size_t j = 0; j < profile_count; ++j) {
            const std::size_t jn = (j + 1) % profile_count;
            mesh.facets.create_quad(
                ring_vertex(i,     j),
                ring_vertex(i,     jn),
                ring_vertex(i + 1, jn),
                ring_vertex(i + 1, j)
            );
        }
    }
    if (tip_collapse) {
        const int last = ring_count - 1;
        for (std::size_t j = 0; j < profile_count; ++j) {
            const std::size_t jn = (j + 1) % profile_count;
            const GEO::index_t facet = mesh.facets.create_triangles(1);
            mesh.facets.set_vertex(facet, 0, ring_vertex(last, j));
            mesh.facets.set_vertex(facet, 1, ring_vertex(last, jn));
            mesh.facets.set_vertex(facet, 2, tip_vertex);
        }
    }

    if (parameters.start_cap) {
        // Outward normal is -tangent: reverse the CCW profile order.
        const GEO::index_t facet = mesh.facets.create_polygon(static_cast<GEO::index_t>(profile_count));
        for (std::size_t j = 0; j < profile_count; ++j) {
            mesh.facets.set_vertex(facet, static_cast<GEO::index_t>(j), ring_vertex(0, profile_count - 1 - j));
        }
    }
    if (parameters.end_cap && !tip_collapse) {
        const GEO::index_t facet = mesh.facets.create_polygon(static_cast<GEO::index_t>(profile_count));
        for (std::size_t j = 0; j < profile_count; ++j) {
            mesh.facets.set_vertex(facet, static_cast<GEO::index_t>(j), ring_vertex(ring_count - 1, j));
        }
    }
}

} // namespace erhe::geometry::shapes
