#include "erhe/primitive/property_maps.hpp"
#include "erhe/primitive/log.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/graphics/configuration.hpp"

#include <glm/glm.hpp>

namespace erhe::primitive
{

using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;

Property_maps::Property_maps(
    const erhe::geometry::Geometry& geometry,
    const Format_info&              format_info
)
{
    ERHE_PROFILE_FUNCTION

    log_primitive_builder.trace("Property_maps::Property_maps() for geometry = {}\n", geometry.name);
    const erhe::log::Indenter indenter;

    polygon_normals      = geometry.polygon_attributes().find<vec3>(erhe::geometry::c_polygon_normals     );
    polygon_centroids    = geometry.polygon_attributes().find<vec3>(erhe::geometry::c_polygon_centroids   );
    corner_normals       = geometry.corner_attributes ().find<vec3>(erhe::geometry::c_corner_normals      );
    corner_tangents      = geometry.corner_attributes ().find<vec4>(erhe::geometry::c_corner_tangents     );
    corner_bitangents    = geometry.corner_attributes ().find<vec4>(erhe::geometry::c_corner_bitangents   );
    corner_texcoords     = geometry.corner_attributes ().find<vec2>(erhe::geometry::c_corner_texcoords    );
    corner_colors        = geometry.corner_attributes ().find<vec4>(erhe::geometry::c_corner_colors       );
    point_locations      = geometry.point_attributes  ().find<vec3>(erhe::geometry::c_point_locations     );
    point_normals        = geometry.point_attributes  ().find<vec3>(erhe::geometry::c_point_normals       );
    point_normals_smooth = geometry.point_attributes  ().find<vec3>(erhe::geometry::c_point_normals_smooth);
    point_tangents       = geometry.point_attributes  ().find<vec4>(erhe::geometry::c_point_tangents      );
    point_bitangents     = geometry.point_attributes  ().find<vec4>(erhe::geometry::c_point_bitangents    );
    point_texcoords      = geometry.point_attributes  ().find<vec2>(erhe::geometry::c_point_texcoords     );
    point_colors         = geometry.point_attributes  ().find<vec4>(erhe::geometry::c_point_colors        );

    if (point_locations == nullptr)
    {
        log_primitive_builder.error("geometry has no point locations\n");
        return;
    }

    if (format_info.features.id)
    {
        polygon_ids_vector3 = polygon_attributes.create<vec3>(erhe::geometry::c_polygon_ids_vec3);
        log_primitive_builder.trace("-created polygon_ids_vec3\n");

        if (erhe::graphics::Instance::info.use_integer_polygon_ids)
        {
            polygon_ids_uint32 = polygon_attributes.create<unsigned int>(erhe::geometry::c_polygon_ids_uint);
            log_primitive_builder.trace("-created polygon_ids_uint\n");
        }
    }

    if (format_info.features.normal)
    {
        if (polygon_normals == nullptr)
        {
            polygon_normals = polygon_attributes.create<vec3>(erhe::geometry::c_polygon_normals);
        }
        if (!geometry.has_polygon_normals())
        {
            geometry.for_each_polygon_const(
                [this, &geometry](auto& i)
                {
                    if (!polygon_normals->has(i.polygon_id))
                    {
                        i.polygon.compute_normal(i.polygon_id, geometry, *polygon_normals, *point_locations);
                    }
                }
            );
        }
        if ((corner_normals == nullptr) && (point_normals == nullptr) && (point_normals_smooth == nullptr))
        {
            corner_normals = corner_attributes.create<vec3>(erhe::geometry::c_corner_normals);
            geometry.smooth_normalize(*corner_normals, *polygon_normals, *polygon_normals, 0.0f);
        }
    }

    if (format_info.features.normal_smooth && (point_normals_smooth == nullptr))
    {
        log_primitive_builder.trace("-computing point_normals_smooth\n");
        point_normals_smooth = point_attributes.create<vec3>(erhe::geometry::c_point_normals_smooth);
        geometry.for_each_point_const(
            [this, &geometry](auto& i)
            {
                vec3 normal_sum{0.0f, 0.0f, 0.0f};
                i.point.for_each_corner_const(
                    geometry,
                    [this, &geometry, &normal_sum](auto& j)
                    {
                        const erhe::geometry::Polygon_id polygon_id = j.corner.polygon_id;
                        if (polygon_normals->has(polygon_id))
                        {
                            normal_sum += polygon_normals->get(polygon_id);
                        }
                        else
                        {
                            //log_primitive_builder.warn("{} - smooth normals have been requested, but polygon normals have missing polygons\n", __func__);
                            const auto& polygon = geometry.polygons[polygon_id];
                            const vec3  normal  = polygon.compute_normal(geometry, *point_locations);
                            normal_sum += normal;
                        }
                    }
                );
                point_normals_smooth->put(i.point_id, normalize(normal_sum));
            }
        );
    }

    if (format_info.features.centroid_points)
    {
        if (polygon_centroids == nullptr)
        {
            polygon_centroids = polygon_attributes.create<vec3>(erhe::geometry::c_polygon_centroids);
        }
        if (!geometry.has_polygon_centroids())
        {
            geometry.for_each_polygon_const(
                [this, &geometry](auto& i)
                {
                    if (!polygon_centroids->has(i.polygon_id))
                    {
                        i.polygon.compute_centroid(i.polygon_id, geometry, *polygon_centroids, *point_locations);
                    }
                }
            );
        }
    }

    corner_indices = corner_attributes.create<unsigned int>(erhe::geometry::c_corner_indices);
}

} // namespace erhe::primitive
