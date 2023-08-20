#include "erhe_primitive/property_maps.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_profile/profile.hpp"

namespace erhe::primitive
{

using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using uvec4 = glm::uvec4;

Property_maps::Property_maps(
    const erhe::geometry::Geometry&      geometry,
    const Primitive_types&               primitive_types,
    const erhe::graphics::Vertex_format& vertex_format 
)
{
    ERHE_PROFILE_FUNCTION();

    log_primitive_builder->trace(
        "Property_maps::Property_maps() for geometry = {}",
        geometry.name
    );
    //const erhe::log::Indenter indenter;

    polygon_normals       = geometry.polygon_attributes().find<vec3 >(erhe::geometry::c_polygon_normals      );
    polygon_centroids     = geometry.polygon_attributes().find<vec3 >(erhe::geometry::c_polygon_centroids    );
    polygon_colors        = geometry.polygon_attributes().find<vec4 >(erhe::geometry::c_polygon_colors       );
    polygon_aniso_control = geometry.polygon_attributes().find<vec2 >(erhe::geometry::c_polygon_aniso_control);
    corner_normals        = geometry.corner_attributes ().find<vec3 >(erhe::geometry::c_corner_normals       );
    corner_tangents       = geometry.corner_attributes ().find<vec4 >(erhe::geometry::c_corner_tangents      );
    corner_bitangents     = geometry.corner_attributes ().find<vec4 >(erhe::geometry::c_corner_bitangents    );
    corner_texcoords      = geometry.corner_attributes ().find<vec2 >(erhe::geometry::c_corner_texcoords     );
    corner_colors         = geometry.corner_attributes ().find<vec4 >(erhe::geometry::c_corner_colors        );
    corner_aniso_control  = geometry.corner_attributes ().find<vec2 >(erhe::geometry::c_corner_aniso_control );
    point_locations       = geometry.point_attributes  ().find<vec3 >(erhe::geometry::c_point_locations      );
    point_normals         = geometry.point_attributes  ().find<vec3 >(erhe::geometry::c_point_normals        );
    point_normals_smooth  = geometry.point_attributes  ().find<vec3 >(erhe::geometry::c_point_normals_smooth );
    point_tangents        = geometry.point_attributes  ().find<vec4 >(erhe::geometry::c_point_tangents       );
    point_bitangents      = geometry.point_attributes  ().find<vec4 >(erhe::geometry::c_point_bitangents     );
    point_texcoords       = geometry.point_attributes  ().find<vec2 >(erhe::geometry::c_point_texcoords      );
    point_colors          = geometry.point_attributes  ().find<vec4 >(erhe::geometry::c_point_colors         );
    point_aniso_control   = geometry.point_attributes  ().find<vec2 >(erhe::geometry::c_point_aniso_control  );
    point_joint_indices   = geometry.point_attributes  ().find<uvec4>(erhe::geometry::c_point_joint_indices  );
    point_joint_weights   = geometry.point_attributes  ().find<vec4 >(erhe::geometry::c_point_joint_weights  );

    if (point_locations == nullptr) {
        log_primitive_builder->error("geometry has no point locations");
        return;
    }

    using Usage_type = erhe::graphics::Vertex_attribute::Usage_type;
    if (vertex_format.find_attribute_maybe(Usage_type::id) != nullptr) {
        polygon_ids_vector3 = polygon_attributes.create<vec3>(erhe::geometry::c_polygon_ids_vec3);
        log_primitive_builder->trace("created polygon_ids_vec3");

        //// TODO
        //// if (erhe::graphics::g_instance->info.use_integer_polygon_ids) {
        ////     polygon_ids_uint32 = polygon_attributes.create<unsigned int>(erhe::geometry::c_polygon_ids_uint);
        ////     log_primitive_builder->trace("created polygon_ids_uint");
        //// }
    }

    // TODO This should be done externally before calling primitive builder
#if 1
    if (vertex_format.find_attribute_maybe(Usage_type::normal) != nullptr) {
        if (polygon_normals == nullptr) {
            polygon_normals = polygon_attributes.create<vec3>(erhe::geometry::c_polygon_normals);
        }
        if (!geometry.has_polygon_normals()) {
            geometry.for_each_polygon_const(
                [this, &geometry](auto& i)
                {
                    if (!polygon_normals->has(i.polygon_id)) {
                        i.polygon.compute_normal(i.polygon_id, geometry, *polygon_normals, *point_locations);
                    }
                }
            );
        }
        if ((corner_normals == nullptr) && (point_normals == nullptr) && (point_normals_smooth == nullptr)) {
            corner_normals = corner_attributes.create<vec3>(erhe::geometry::c_corner_normals);
            geometry.smooth_normalize(*corner_normals, *polygon_normals, *polygon_normals, 0.0f);
        }
    }

    // if (
    //     vertex_format.find_attribute_maybe(Usage_type::n) != nullptr) {
    //     && (point_normals_smooth == nullptr)) {
    //     log_primitive_builder->trace("computing point_normals_smooth");
    //     point_normals_smooth = point_attributes.create<vec3>(erhe::geometry::c_point_normals_smooth);
    //     geometry.for_each_point_const(
    //         [this, &geometry](auto& i) {
    //             vec3 normal_sum{0.0f, 0.0f, 0.0f};
    //             i.point.for_each_corner_const(
    //                 geometry,
    //                 [this, &geometry, &normal_sum](auto& j) {
    //                     const erhe::geometry::Polygon_id polygon_id = j.corner.polygon_id;
    //                     if (polygon_normals->has(polygon_id)) {
    //                         normal_sum += polygon_normals->get(polygon_id);
    //                     } else {
    //                         //log_primitive_builder.warn("{} - smooth normals have been requested, but polygon normals have missing polygons", __func__);
    //                         const auto& polygon = geometry.polygons[polygon_id];
    //                         const vec3  normal  = polygon.compute_normal(geometry, *point_locations);
    //                         normal_sum += normal;
    //                     }
    //                 }
    //             );
    //             point_normals_smooth->put(i.point_id, normalize(normal_sum));
    //         }
    //     );
    // }

    if (primitive_types.centroid_points) {
        if (polygon_centroids == nullptr) {
            polygon_centroids = polygon_attributes.create<vec3>(erhe::geometry::c_polygon_centroids);
        }
        if (!geometry.has_polygon_centroids()) {
            geometry.for_each_polygon_const(
                [this, &geometry](auto& i) {
                    if (!polygon_centroids->has(i.polygon_id)) {
                        i.polygon.compute_centroid(i.polygon_id, geometry, *polygon_centroids, *point_locations);
                    }
                }
            );
        }
    }
#endif

    corner_indices = corner_attributes.create<unsigned int>(erhe::geometry::c_corner_indices);

    // if ((point_joint_indices == nullptr) && format_info.features.joint_indices) {
    //     point_joint_indices = polygon_attributes.create<uvec4>(erhe::geometry::c_point_joint_indices);
    //     log_primitive_builder->trace("created point_joint_indices");
    // }
    // if ((point_joint_weights == nullptr) && format_info.features.joint_weights) {
    //     point_joint_weights = polygon_attributes.create<vec4>(erhe::geometry::c_point_joint_weights);
    //     log_primitive_builder->trace("created point_joint_weights");
    // }

}

} // namespace erhe::primitive
