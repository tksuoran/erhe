#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/property_map.hpp"
#include "erhe/geometry/geometry_log.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/profile.hpp"

#include <fmt/format.h>

#include <cmath>

namespace erhe::geometry
{

using glm::vec3;

auto Polygon::compute_normal(
    const Geometry&                          geometry,
    const Property_map<Point_id, glm::vec3>& point_locations
) const -> glm::vec3
{
    ERHE_PROFILE_FUNCTION();

    if (corner_count < 3) {
        return {};
    }

    vec3 newell_normal{0.0f};
    for_each_corner_neighborhood_const(
        geometry,
        [&newell_normal, &point_locations](const Polygon_corner_neighborhood_context_const& i)
        {
            const Point_id a     = i.corner     .point_id;
            const Point_id b     = i.next_corner.point_id;
            const auto     pos_a = point_locations.get(a);
            const auto     pos_b = point_locations.get(b);
            newell_normal += glm::cross(pos_a, pos_b);
        }
    );

    newell_normal = glm::normalize(newell_normal);
    return newell_normal;
}

void Polygon::compute_normal(
    const Polygon_id                    this_polygon_id,
    const Geometry&                     geometry,
    Property_map<Polygon_id, vec3>&     polygon_normals,
    const Property_map<Point_id, vec3>& point_locations
) const
{
    ERHE_PROFILE_FUNCTION();

    if (corner_count < 3) {
        return;
    }

    glm::vec3 normal = compute_normal(geometry, point_locations);
    polygon_normals.put(this_polygon_id, normal);
}

auto Polygon::compute_centroid(
    const Geometry&                     geometry,
    const Property_map<Point_id, vec3>& point_locations
) const -> glm::vec3
{
    vec3 centroid{0.0f, 0.0f, 0.0f};
    int  count{0};

    for_each_corner_const(
        geometry,
        [&geometry, &centroid, &count, &point_locations](const Polygon_corner_context_const& i)
        {
            const Point_id point_id = geometry.corners[i.corner_id].point_id;
            const auto     pos0     = point_locations.get(point_id);
            centroid += pos0;
            ++count;
        }
    );

    return centroid /= static_cast<float>(count);
}

auto Polygon::compute_edge_midpoint(
    const Geometry&                          geometry,
    const Property_map<Point_id, glm::vec3>& point_locations,
    const uint32_t                           corner_offset
) const -> glm::vec3
{
    if (corner_count >= 2) {
        const auto      polygon_corner_id_a = first_polygon_corner_id + (corner_offset     % corner_count);
        const auto      polygon_corner_id_b = first_polygon_corner_id + (corner_offset + 1 % corner_count);
        const Corner_id corner0_id          = geometry.polygon_corners[polygon_corner_id_a];
        const Corner_id corner1_id          = geometry.polygon_corners[polygon_corner_id_b];
        const Corner&   corner0             = geometry.corners[corner0_id];
        const Corner&   corner1             = geometry.corners[corner1_id];
        const Point_id  a                   = corner0.point_id;
        const Point_id  b                   = corner1.point_id;
        if (point_locations.has(a) && point_locations.has(b)) {
            const glm::vec3 pos_a    = point_locations.get(a);
            const glm::vec3 pos_b    = point_locations.get(b);
            const glm::vec3 midpoint = (pos_a + pos_b) / 2.0f;
            return midpoint;
        }
    }
    log_geometry->warn("Could not compute edge midpoint for polygon with less than three corners");
    return glm::vec3{0.0f, 0.0f, 0.0f};
}

void Polygon::compute_centroid(
    const Polygon_id                    this_polygon_id,
    const Geometry&                     geometry,
    Property_map<Polygon_id, vec3>&     polygon_centroids,
    const Property_map<Point_id, vec3>& point_locations
) const
{
    if (corner_count < 1) {
        return;
    }

    const auto centroid = compute_centroid(geometry, point_locations);
    polygon_centroids.put(this_polygon_id, centroid);
}

auto Polygon::corner(const Geometry& geometry, const Point_id point) const -> Corner_id
{
    std::optional<Corner_id> result;
    for_each_corner_const(geometry, [&](auto& i)
    {
        if (point == i.corner.point_id) {
            result = i.corner_id;
            return i.break_iteration();
        }
    });
    if (result.has_value()) {
        return result.value();
    }
    ERHE_FATAL("corner not found");
    // unreachable return {};
}

auto Polygon::next_corner(const Geometry& geometry, const Corner_id anchor_corner_id) const -> Corner_id
{
    for (uint32_t i = 0; i < corner_count; ++i) {
        const Polygon_corner_id polygon_corner_id = first_polygon_corner_id + i;
        const Corner_id         corner_id         = geometry.polygon_corners[polygon_corner_id];
        if (corner_id == anchor_corner_id) {
            const Polygon_corner_id next_polygon_corner_id = first_polygon_corner_id + (i + 1) % corner_count;
            const Corner_id         next_corner_id         = geometry.polygon_corners[next_polygon_corner_id];
            return next_corner_id;
        }
    }
    ERHE_FATAL("corner not found");
    // unreachable return {};
}

auto Polygon::prev_corner(const Geometry& geometry, const Corner_id anchor_corner_id) const -> Corner_id
{
    for (uint32_t i = 0; i < corner_count; ++i) {
        const Polygon_corner_id polygon_corner_id = first_polygon_corner_id + i;
        const Corner_id         corner_id         = geometry.polygon_corners[polygon_corner_id];
        if (corner_id == anchor_corner_id) {
            const Polygon_corner_id prev_polygon_corner_id = first_polygon_corner_id + (corner_count + i - 1) % corner_count;
            const Corner_id         prev_corner_id         = geometry.polygon_corners[prev_polygon_corner_id];
            return prev_corner_id;
        }
    }
    ERHE_FATAL("corner not found");
    // unreachable return {};
}

void Polygon::reverse(Geometry& geometry)
{
    const auto last_polygon_corner_id = first_polygon_corner_id + corner_count;
    std::reverse(
        &geometry.polygon_corners[first_polygon_corner_id],
        &geometry.polygon_corners[last_polygon_corner_id]
    );
}

auto Polygon::format_points(const Geometry& geometry) const -> std::string
{
    std::stringstream ss;
    for (uint32_t i = 0; i < corner_count; ++i) {
        const Polygon_corner_id polygon_corner_id = first_polygon_corner_id + i;
        const Corner_id         corner_id         = geometry.polygon_corners[polygon_corner_id];
        const Corner&           corner            = geometry.corners[corner_id];
        ss << fmt::format("{} ", corner.point_id);
    }
    return ss.str();
}

auto Polygon::format_corners(const Geometry& geometry) const -> std::string
{
    std::stringstream ss;
    for (uint32_t i = 0; i < corner_count; ++i) {
        const Polygon_corner_id polygon_corner_id = first_polygon_corner_id + i;
        const Corner_id         corner_id         = geometry.polygon_corners[polygon_corner_id];
        ss << fmt::format("{} ", corner_id);
    }
    return ss.str();
}

void Polygon::compute_planar_texture_coordinates(
    const Polygon_id                           this_polygon_id,
    const Geometry&                            geometry,
    Property_map<Corner_id, glm::vec2>&        corner_texcoords,
    const Property_map<Polygon_id, glm::vec3>& polygon_centroids,
    const Property_map<Polygon_id, glm::vec3>& polygon_normals,
    const Property_map<Point_id, glm::vec3>&   point_locations,
    const bool                                 overwrite
) const
{
    ERHE_PROFILE_FUNCTION();

    if (corner_count < 3)
{
        return;
    }

    const Polygon_corner_id p0_polygon_corner_id = first_polygon_corner_id;
    const Corner_id         p0_corner_id         = geometry.polygon_corners[p0_polygon_corner_id];
    const Corner&           p0_corner            = geometry.corners[p0_corner_id];
    const Point_id          p0_point_id          = p0_corner.point_id;
    const glm::vec3         p0_position          = point_locations.get(p0_point_id);
    const glm::vec3         centroid             = polygon_centroids.get(this_polygon_id);
    const glm::vec3         normal               = polygon_normals.get(this_polygon_id);

    // normal = des
    const glm::vec3 view = normal;
    const glm::vec3 edge = glm::normalize(p0_position - centroid);
    const glm::vec3 side = glm::normalize(glm::cross(view, edge));
    glm::mat4 transform;
    // X+ axis is column 0
    transform[0][0] = edge[0];
    transform[0][1] = edge[1];
    transform[0][2] = edge[2];
    transform[0][3] = 0.0f;
    // Y+ axis is column 1
    transform[1][0] = side[0];
    transform[1][1] = side[1];
    transform[1][2] = side[2];
    transform[1][3] = 0.0f;
    // Z+ axis is column 2
    transform[2][0] = view[0];
    transform[2][1] = view[1];
    transform[2][2] = view[2];
    transform[2][3] = 0.0f;
    // Translation is column 3
    transform[3][0] = centroid[0];
    transform[3][1] = centroid[1];
    transform[3][2] = centroid[2];
    transform[3][3] = 1.0f;

    const glm::mat4 inverse_transform = glm::inverse(transform);

    // First pass - for scale
    float max_distance = 0.0f;
    std::vector<std::pair<Corner_id, glm::vec2>> unscaled_uvs;
    for (uint32_t i = 0; i < corner_count; ++i) {
        const Polygon_corner_id polygon_corner_id = first_polygon_corner_id + i;
        const Corner_id         corner_id         = geometry.polygon_corners[polygon_corner_id];
        const Corner&           corner            = geometry.corners[corner_id];
        const Point_id          point_id          = corner.point_id;
        const glm::vec3         position          = point_locations.get(point_id);
        const glm::vec3         planar_position   = glm::vec3{inverse_transform * glm::vec4{position, 1.0f}};
        SPDLOG_LOGGER_TRACE(
            log_polygon_texcoords,
            "polygon {:2} corner {:2} point {:2} "
            "{} -> "
            "{}",
            this_polygon_id, corner_id, point_id,
            position,
            planar_position
        );
        const glm::vec2 uv = glm::vec2{planar_position};
        unscaled_uvs.emplace_back(corner_id, uv);
        const float distance = glm::length(uv);
        max_distance = std::max(distance, max_distance);
    }

    // Second pass - generate texture coordinates
    for (const auto& unscaled_uv : unscaled_uvs) {
        if (overwrite || !corner_texcoords.has(unscaled_uv.first)) {
            corner_texcoords.put(unscaled_uv.first, unscaled_uv.second);
        }
    }
}

} // namespace erhe::geometry
