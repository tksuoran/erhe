#include "erhe/geometry/geometry.hpp"

#include "erhe/geometry/property_map.hpp"

#include "Tracy.hpp"

#include <cassert>
#include <cmath>
#include <exception>

namespace erhe::geometry
{

using glm::vec3;

auto Polygon::compute_normal(const Geometry&                          geometry,
                             const Property_map<Point_id, glm::vec3>& point_locations) const -> glm::vec3
{
    ZoneScoped;

    if (corner_count < 3)
    {
        return {};
    }

    Polygon_corner_id polygon_corner_id0 = first_polygon_corner_id;
    Polygon_corner_id polygon_corner_id1 = first_polygon_corner_id + 1;
    Polygon_corner_id polygon_corner_id2 = first_polygon_corner_id + 2;

    Corner_id corner_id0 = geometry.polygon_corners[polygon_corner_id0];
    Corner_id corner_id1 = geometry.polygon_corners[polygon_corner_id1];
    Corner_id corner_id2 = geometry.polygon_corners[polygon_corner_id2];

    const Corner& c0 = geometry.corners[corner_id0];
    const Corner& c1 = geometry.corners[corner_id1];
    const Corner& c2 = geometry.corners[corner_id2];
    Point_id p0 = c0.point_id;
    Point_id p1 = c1.point_id;
    Point_id p2 = c2.point_id;

    // Make sure all points are unique from others
    if ((p0 == p1) || (p0 == p2) || (p1 == p2))
    {
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }
    auto pos0   = point_locations.get(p0);
    auto pos1   = point_locations.get(p1);
    auto pos2   = point_locations.get(p2);
    auto normal = glm::cross((pos2 - pos0), (pos1 - pos0));
    return glm::normalize(normal);
 }

void Polygon::compute_normal(Polygon_id                          this_polygon_id,
                             const Geometry&                     geometry,
                             Property_map<Polygon_id, vec3>&     polygon_normals,
                             const Property_map<Point_id, vec3>& point_locations) const
{
    ZoneScoped;

    if (corner_count < 3)
    {
        return;
    }

    glm::vec3 normal = compute_normal(geometry, point_locations);
    polygon_normals.put(this_polygon_id, normal);
}

auto Polygon::compute_centroid(const Geometry&                     geometry,
                               const Property_map<Point_id, vec3>& point_locations) const -> glm::vec3
{
    ZoneScoped;

    vec3 centroid(0.0f, 0.0f, 0.0f);
    int  count{0};

    for (Polygon_corner_id polygon_corner_id = first_polygon_corner_id,
         end = first_polygon_corner_id + corner_count;
         polygon_corner_id < end;
         ++polygon_corner_id)
    {
        Corner_id corner_id = geometry.polygon_corners[polygon_corner_id];
        Point_id  point_id  = geometry.corners[corner_id].point_id;
        auto      pos0      = point_locations.get(point_id);
        centroid += pos0;
        ++count;
    }

    return centroid /= static_cast<float>(count);
}

auto Polygon::compute_edge_midpoint(const Geometry&                          geometry,
                                    const Property_map<Point_id, glm::vec3>& point_locations) const -> glm::vec3
{
    if (corner_count >= 2)
    {
        Corner_id     corner0_id = geometry.polygon_corners[first_polygon_corner_id];
        Corner_id     corner1_id = geometry.polygon_corners[first_polygon_corner_id + 1];
        const Corner& corner0    = geometry.corners[corner0_id];
        const Corner& corner1    = geometry.corners[corner1_id];
        Point_id      a          = corner0.point_id;
        Point_id      b          = corner1.point_id;
        if (point_locations.has(a) && point_locations.has(b))
        {
            glm::vec3 pos_a    = point_locations.get(a);
            glm::vec3 pos_b    = point_locations.get(b);
            glm::vec3 midpoint = (pos_a + pos_b) / 2.0f;
            return midpoint;
        }
    }
    log.warn("Could not compute edge midpoint for polygon with less than three corners\n");
    return glm::vec3{0.0f, 0.0f, 0.0f};
}

void Polygon::compute_centroid(Polygon_id                          this_polygon_id,
                               const Geometry&                     geometry,
                               Property_map<Polygon_id, vec3>&     polygon_centroids,
                               const Property_map<Point_id, vec3>& point_locations) const
{
    ZoneScoped;

    if (corner_count < 1)
    {
        return;
    }

    auto centroid = compute_centroid(geometry, point_locations);
    polygon_centroids.put(this_polygon_id, centroid);
}

auto Polygon::corner(const Geometry& geometry, Point_id point) -> Corner_id
{
    ZoneScoped;

    for (Polygon_corner_id polygon_corner_id = first_polygon_corner_id,
         end = first_polygon_corner_id + corner_count;
         polygon_corner_id < end;
         ++polygon_corner_id)
    {
        Corner_id corner_id = geometry.polygon_corners[polygon_corner_id];
        if (point == geometry.corners[corner_id].point_id)
        {
            return corner_id;
        }
    }
    FATAL("corner not found");
    return {};
}

auto Polygon::next_corner(const Geometry& geometry, Corner_id anchor_corner_id) -> Corner_id
{
    ZoneScoped;

    for (uint32_t i = 0; i < corner_count; ++i)
    {
        Polygon_corner_id polygon_corner_id = first_polygon_corner_id + i;
        Corner_id corner_id = geometry.polygon_corners[polygon_corner_id];
        if (corner_id == anchor_corner_id)
        {
            Polygon_corner_id next_polygon_corner_id = first_polygon_corner_id + (i + 1) % corner_count;
            Corner_id next_corner_id = geometry.polygon_corners[next_polygon_corner_id];
            return next_corner_id;
        }
    }
    FATAL("corner not found");
    return {};
}

auto Polygon::prev_corner(const Geometry& geometry, Corner_id anchor_corner_id) -> Corner_id
{
    ZoneScoped;

    for (uint32_t i = 0; i < corner_count; ++i)
    {
        Polygon_corner_id polygon_corner_id = first_polygon_corner_id + i;
        Corner_id corner_id = geometry.polygon_corners[polygon_corner_id];
        if (corner_id == anchor_corner_id)
        {
            Polygon_corner_id prev_polygon_corner_id = first_polygon_corner_id + (corner_count + i - 1) % corner_count;
            Corner_id prev_corner_id = geometry.polygon_corners[prev_polygon_corner_id];
            return prev_corner_id;
        }
    }
    FATAL("corner not found");
    return {};
}

void Polygon::reverse(Geometry& geometry)
{
    ZoneScoped;

    std::reverse(&geometry.polygon_corners[first_polygon_corner_id],
                 &geometry.polygon_corners[first_polygon_corner_id + corner_count]);
}

void Polygon::compute_planar_texture_coordinates(Polygon_id                                 this_polygon_id,
                                                 const Geometry&                            geometry,
                                                 Property_map<Corner_id, glm::vec2>&        corner_texcoords,
                                                 const Property_map<Polygon_id, glm::vec3>& polygon_centroids,
                                                 const Property_map<Polygon_id, glm::vec3>& polygon_normals,
                                                 const Property_map<Point_id, glm::vec3>&   point_locations,
                                                 bool                                       overwrite) const
{
    ZoneScoped;

    if (corner_count < 3)
    {
        return;
    }

    Polygon_corner_id p0_polygon_corner_id = first_polygon_corner_id;
    Corner_id         p0_corner_id         = geometry.polygon_corners[p0_polygon_corner_id];
    const Corner&     p0_corner            = geometry.corners[p0_corner_id];
    Point_id          p0_point_id          = p0_corner.point_id;
    glm::vec3         p0_position          = point_locations.get(p0_point_id);
    glm::vec3         centroid             = polygon_centroids.get(this_polygon_id);
    glm::vec3         normal               = polygon_normals.get(this_polygon_id);

    // normal = des
    glm::vec3 view = normal;
    glm::vec3 edge = glm::normalize(p0_position - centroid);
    glm::vec3 side = glm::normalize(glm::cross(view, edge));
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

    glm::mat4 inverse_transform = glm::inverse(transform);

    // First pass - for scale
    float max_distance = 0.0f;
    std::vector<std::pair<Corner_id, glm::vec2>> unscaled_uvs;
    for (uint32_t i = 0; i < corner_count; ++i)
    {
        Polygon_corner_id polygon_corner_id = first_polygon_corner_id + i;
        Corner_id         corner_id         = geometry.polygon_corners[polygon_corner_id];
        const Corner&     corner            = geometry.corners[corner_id];
        Point_id          point_id          = corner.point_id;
        glm::vec3         position          = point_locations.get(point_id);
        glm::vec3         planar_position   = glm::vec3(inverse_transform * glm::vec4(position, 1.0f));
        // log_polygon_texcoords.trace("polygon {:2} corner {:2} point {:2} "
        //                             "{} -> "
        //                             "{}\n",
        //                             this_polygon_id, corner_id, point_id,
        //                             position,
        //                             planar_position);
        glm::vec2 uv = glm::vec2(planar_position);
        unscaled_uvs.emplace_back(corner_id, uv);
        float distance = glm::length(uv);
        max_distance = std::max(distance, max_distance);
    }

    // Second pass - generate texture coordinates
    for (auto unscaled_uv : unscaled_uvs)
    {
        if (overwrite || !corner_texcoords.has(unscaled_uv.first))
        {
            corner_texcoords.put(unscaled_uv.first, unscaled_uv.second);
        }
    }
}

} // namespace erhe::geometry
