#include "tools/screen_snap.hpp"

#include "scene/viewport_scene_view.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_math/math_util.hpp"

#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <limits>
#include <optional>

namespace editor {

using erhe::geometry::get_pointf;
using erhe::geometry::to_glm_vec3;

auto Screen_snap::get_snap_radius(const float base_radius_px, const std::size_t nearby_vertex_count) -> float
{
    if (nearby_vertex_count < 1) {
        return base_radius_px;
    }
    const float density = static_cast<float>(nearby_vertex_count) * 0.5f;
    return std::min(base_radius_px, base_radius_px / density);
}

auto Screen_snap::snap(const Screen_snap_query& query, Screen_snap_result& out_result) -> bool
{
    out_result = Screen_snap_result{};
    if ((query.view == nullptr) || (query.geometry == nullptr)) {
        return false;
    }
    const GEO::Mesh& mesh = query.geometry->get_mesh();
    if (query.facet >= mesh.facets.nb()) {
        return false;
    }
    const GEO::index_t corner_count = mesh.facets.nb_vertices(query.facet);
    if (corner_count < 3) {
        return false;
    }
    const auto is_excluded = [&query](const GEO::index_t vertex) -> bool {
        return (vertex < query.excluded_vertices.size()) && (query.excluded_vertices[vertex] != 0);
    };
    const auto position_in_mesh = [&mesh](const GEO::index_t vertex) -> glm::vec3 {
        return to_glm_vec3(get_pointf(mesh.vertices, vertex));
    };
    const auto to_world = [&query](const glm::vec3& p) -> glm::vec3 {
        return glm::vec3{query.world_from_node * glm::vec4{p, 1.0f}};
    };

    m_projected.clear();
    m_projected_valid.clear();
    for (GEO::index_t local = 0; local < corner_count; ++local) {
        const GEO::index_t             vertex    = mesh.facets.vertex(query.facet, local);
        const std::optional<glm::vec3> projected = query.view->project_to_viewport(to_world(position_in_mesh(vertex)));
        m_projected.push_back(projected.has_value() ? glm::vec2{projected.value()} : glm::vec2{0.0f, 0.0f});
        m_projected_valid.push_back(projected.has_value() ? 1 : 0);
    }

    // Nearest vertex.
    float        best_vertex_distance = std::numeric_limits<float>::max();
    GEO::index_t best_vertex_local    = GEO::NO_INDEX;
    for (GEO::index_t local = 0; local < corner_count; ++local) {
        if ((m_projected_valid[local] == 0) || is_excluded(mesh.facets.vertex(query.facet, local))) {
            continue;
        }
        const float distance = glm::distance(m_projected[local], query.cursor);
        if (distance < best_vertex_distance) {
            best_vertex_distance = distance;
            best_vertex_local    = local;
        }
    }
    if ((best_vertex_local != GEO::NO_INDEX) && (best_vertex_distance <= query.vertex_radius_px)) {
        const GEO::index_t vertex = mesh.facets.vertex(query.facet, best_vertex_local);
        out_result.kind                 = Screen_snap_kind::vertex;
        out_result.vertex               = vertex;
        out_result.position_in_mesh     = position_in_mesh(vertex);
        out_result.position_in_world    = to_world(out_result.position_in_mesh);
        out_result.position_in_viewport = m_projected[best_vertex_local];
        out_result.distance_px          = best_vertex_distance;
        return true;
    }

    // Nearest edge (point to screen segment).
    float        best_edge_distance = std::numeric_limits<float>::max();
    GEO::index_t best_edge_local    = GEO::NO_INDEX;
    for (GEO::index_t local = 0; local < corner_count; ++local) {
        const GEO::index_t next = (local + 1) % corner_count;
        if ((m_projected_valid[local] == 0) || (m_projected_valid[next] == 0)) {
            continue;
        }
        if (is_excluded(mesh.facets.vertex(query.facet, local)) || is_excluded(mesh.facets.vertex(query.facet, next))) {
            continue;
        }
        const glm::vec2 a              = m_projected[local];
        const glm::vec2 b              = m_projected[next];
        const glm::vec2 ab             = b - a;
        const float     length_squared = glm::dot(ab, ab);
        const float     t              = (length_squared > 0.0f) ? std::clamp(glm::dot(query.cursor - a, ab) / length_squared, 0.0f, 1.0f) : 0.0f;
        const float     distance       = glm::distance(a + (t * ab), query.cursor);
        if (distance < best_edge_distance) {
            best_edge_distance = distance;
            best_edge_local    = local;
        }
    }
    if ((best_edge_local == GEO::NO_INDEX) || (best_edge_distance > query.edge_radius_px)) {
        return false;
    }
    const GEO::index_t v0 = mesh.facets.vertex(query.facet, best_edge_local);
    const GEO::index_t v1 = mesh.facets.vertex(query.facet, (best_edge_local + 1) % corner_count);
    const glm::vec3    p0 = position_in_mesh(v0);
    const glm::vec3    p1 = position_in_mesh(v1);
    float              t  = 0.5f;
    if (query.edge_point == Screen_snap_edge_point::nearest) {
        // The edge point nearest to the pointer ray (perspective-correct,
        // unlike the screen-space parameter).
        const std::optional<glm::vec3> ray_near = query.view->unproject_to_world(glm::vec3{query.cursor, 0.0f});
        const std::optional<glm::vec3> ray_far  = query.view->unproject_to_world(glm::vec3{query.cursor, 1.0f});
        const glm::vec3                w0       = to_world(p0);
        const glm::vec3                w1       = to_world(p1);
        std::optional<erhe::math::Closest_points<float>> closest{};
        if (ray_near.has_value() && ray_far.has_value()) {
            closest = erhe::math::closest_points<float>(w0, w1, ray_near.value(), ray_far.value());
        }
        if (closest.has_value()) {
            const glm::vec3 d              = w1 - w0;
            const float     length_squared = glm::dot(d, d);
            t = (length_squared > 0.0f) ? std::clamp(glm::dot(closest.value().P - w0, d) / length_squared, 0.0f, 1.0f) : 0.0f;
        } else {
            const glm::vec2 ab             = m_projected[(best_edge_local + 1) % corner_count] - m_projected[best_edge_local];
            const float     length_squared = glm::dot(ab, ab);
            t = (length_squared > 0.0f) ? std::clamp(glm::dot(query.cursor - m_projected[best_edge_local], ab) / length_squared, 0.0f, 1.0f) : 0.0f;
        }
    }
    out_result.kind              = Screen_snap_kind::edge;
    out_result.edge_v0           = v0;
    out_result.edge_v1           = v1;
    out_result.edge_t            = t;
    out_result.position_in_mesh  = glm::mix(p0, p1, t);
    out_result.position_in_world = to_world(out_result.position_in_mesh);
    const std::optional<glm::vec3> projected = query.view->project_to_viewport(out_result.position_in_world);
    out_result.position_in_viewport = projected.has_value() ? glm::vec2{projected.value()} : query.cursor;
    out_result.distance_px          = best_edge_distance;
    return true;
}

} // namespace editor
