#include "tools/brushes/reference_frame.hpp"

#include "editor_log.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

using erhe::geometry::Polygon; // Resolve conflict with wingdi.h BOOL Polygon(HDC,const POINT *,int)
using erhe::geometry::c_polygon_centroids;
using erhe::geometry::c_polygon_normals;
using erhe::geometry::c_point_locations;
using erhe::geometry::Corner_id;
using erhe::geometry::Point_id;
using erhe::geometry::Polygon_id;
using glm::mat4;
using glm::vec3;
using glm::vec4;

Reference_frame::Reference_frame() = default;

Reference_frame::Reference_frame(
    const erhe::geometry::Geometry&  geometry,
    const erhe::geometry::Polygon_id in_polygon_id,
    const uint32_t                   face_offset,
    const uint32_t                   in_corner_offset
)
    : face_offset{face_offset}
    , polygon_id {std::min(in_polygon_id, geometry.get_polygon_count() - 1)}
{
    const auto* const polygon_centroids = geometry.polygon_attributes().find<vec3>(c_polygon_centroids);
    const auto* const polygon_normals   = geometry.polygon_attributes().find<vec3>(c_polygon_normals);
    const auto* const point_locations   = geometry.point_attributes  ().find<vec3>(c_point_locations);
    ERHE_VERIFY(point_locations != nullptr);

    const auto& polygon = geometry.polygons[polygon_id];
    if (polygon.corner_count == 0) {
        log_brush->warn("Polygon with 0 corners");
        return;
    }

    this->corner_offset = in_corner_offset % polygon.corner_count;

    centroid = (polygon_centroids != nullptr) && polygon_centroids->has(polygon_id)
        ? polygon_centroids->get(polygon_id)
        : polygon.compute_centroid(geometry, *point_locations);

    N = (polygon_normals != nullptr) && polygon_normals->has(polygon_id)
        ? polygon_normals->get(polygon_id)
        : polygon.compute_normal(geometry, *point_locations);

    corner_id                 = geometry.polygon_corners[polygon.first_polygon_corner_id + corner_offset];
    const auto&     corner    = geometry.corners[corner_id];
    const Point_id  point     = corner.point_id;
    ERHE_VERIFY(point_locations->has(point));
    position = point_locations->get(point);
    const vec3 midpoint = polygon.compute_edge_midpoint(geometry, *point_locations, corner_offset);
    T = normalize(midpoint - centroid);
    B = normalize(cross(N, T));
    N = normalize(cross(T, B));
    T = normalize(cross(B, N));
    corner_count = polygon.corner_count;
}

void Reference_frame::transform_by(const mat4& m)
{
    centroid = m * vec4{centroid, 1.0f};
    position = m * vec4{position, 1.0f};
    B        = m * vec4{B, 0.0f};
    T        = m * vec4{T, 0.0f};
    N        = m * vec4{N, 0.0f};
    B        = glm::normalize(cross(N, T));
    N        = glm::normalize(cross(T, B));
    T        = glm::normalize(cross(B, N));
}

auto Reference_frame::scale() const -> float
{
    return glm::distance(centroid, position);
}

auto Reference_frame::transform() const -> mat4
{
    return mat4{
        vec4{-T, 0.0f},
        vec4{-N, 0.0f},
        vec4{-B, 0.0f},
        vec4{centroid, 1.0f}
    };
}

} // namespace editor
