#include "tools/brushes/brush.hpp"

#include "editor_settings.hpp"
#include "scene/content_library.hpp"
#include "scene/node_physics.hpp"
#include "editor_log.hpp"

#include "erhe_geometry/operation/clone.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"

namespace editor
{

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

    const Corner_id corner_id = geometry.polygon_corners[polygon.first_polygon_corner_id + corner_offset];
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

auto Brush_data::get_name() const -> const std::string&
{
    if (name.empty() && geometry) {
        return geometry->name;
    }
    return name;
}

auto Brush::get_static_type() -> uint64_t
{
    return erhe::Item_type::brush;
}

auto Brush::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Brush::get_type_name() const -> std::string_view
{
    return static_type_name;
}

Brush::Brush(const Brush_data& create_info)
    : Item{create_info.get_name()}
    , data{create_info}
{
}

void Brush::late_initialize()
{
    const auto geometry = get_geometry();
    ERHE_VERIFY(geometry);
    if (!geometry_primitive) {
        ERHE_PROFILE_SCOPE("geometry primitive");

        geometry_primitive = std::make_unique<erhe::primitive::Geometry_primitive>(
            geometry,
            data.build_info,
            data.normal_style
        );
    }

    if (
        data.editor_settings.physics.static_enable &&
        !data.collision_shape &&
        !data.collision_shape_generator
    ) {
        ERHE_PROFILE_SCOPE("make brush convex hull collision shape");

        data.collision_shape = erhe::physics::ICollision_shape::create_convex_hull_shape_shared(
            reinterpret_cast<const float*>(
                geometry->point_attributes().find<vec3>(c_point_locations)->values.data()
            ),
            static_cast<int>(geometry->get_point_count()),
            static_cast<int>(sizeof(vec3))
        );
    }

    if ((data.volume == 0.0f) && data.collision_volume_calculator) {
        ERHE_PROFILE_SCOPE("calculate brush volume");

        data.volume = data.collision_volume_calculator(1.0f);
    }
}

Brush::Brush(Brush&&) noexcept = default;

auto Brush::get_reference_frame(
    const uint32_t corner_count,
    const uint32_t in_face_offset,
    const uint32_t corner_offset
) -> Reference_frame
{
    for (const auto& reference_frame : reference_frames) {
        if (
            (reference_frame.corner_count  == corner_count  ) &&
            (reference_frame.face_offset   == in_face_offset) &&
            (reference_frame.corner_offset == corner_offset )
        ) {
            return reference_frame;
        }
    }

    const auto geometry = get_geometry();

    uint32_t face_offset = 0;
    Polygon_id selected_polygon = 0;
    for (
        Polygon_id polygon_id = 0, end = geometry->get_polygon_count();
        polygon_id < end;
        ++polygon_id
    ) {
        const auto& polygon = geometry->polygons[polygon_id];
        if (
            (corner_count == 0) ||
            (polygon.corner_count == corner_count) ||
            (polygon_id + 1 == end)
        ) {
            selected_polygon = polygon_id;
            if (face_offset == in_face_offset) {
                break;
            }
            ++face_offset;
        }
    }
    return reference_frames.emplace_back(*geometry.get(), selected_polygon, in_face_offset, corner_offset);
}

auto Brush::get_reference_frame(
    const uint32_t face_offset,
    const uint32_t corner_offset
) -> Reference_frame
{
    //for (const auto& reference_frame : reference_frames) {
    //    if (
    //        (reference_frame.corner_count  == 0            ) &&
    //        (reference_frame.face_offset   == face_offset  ) &&
    //        (reference_frame.corner_offset == corner_offset)
    //    ) {
    //        return reference_frame;
    //    }
    //}

    const auto geometry = get_geometry();

    //return reference_frames.emplace_back(
    //    *geometry.get(),
    //    face_offset,
    //    face_offset,
    //    corner_offset
    //);
    return Reference_frame{
        *geometry.get(),
        face_offset,
        face_offset,
        corner_offset
    };
}

auto Brush::get_scaled(const float scale) -> const Scaled&
{
    late_initialize();
    const int scale_key = static_cast<int>(scale * c_scale_factor);
    for (const auto& scaled : scaled_entries) {
        if (scaled.scale_key == scale_key) {
            return scaled;
        }
    }
    return scaled_entries.emplace_back(create_scaled(scale_key));
}

auto Brush::create_scaled(const int scale_key) -> Scaled
{
    ERHE_PROFILE_FUNCTION();

    const float scale    = static_cast<float>(scale_key) / c_scale_factor;
    const auto  geometry = get_geometry();
    if (scale == 1.0f) {
        glm::mat4 local_inertia{0.0f};
        if (data.editor_settings.physics.static_enable) {
            if (data.collision_shape) {
                ERHE_VERIFY(data.collision_shape->is_convex());
                const auto mass = data.density * data.volume;
                data.collision_shape->calculate_local_inertia(mass, local_inertia);
                return Scaled{
                    .scale_key          = scale_key,
                    .geometry           = geometry,
                    .geometry_primitive = geometry_primitive,
                    .collision_shape    = data.collision_shape,
                    .volume             = data.volume,
                    .local_inertia      = local_inertia
                };
            } else if (data.collision_shape_generator) {
                const auto generated_collision_shape = data.collision_shape_generator(scale);
                const auto mass = data.density * data.volume;
                generated_collision_shape->calculate_local_inertia(mass, local_inertia);
                return Scaled{
                    .scale_key          = scale_key,
                    .geometry           = geometry,
                    .geometry_primitive = geometry_primitive,
                    .collision_shape    = generated_collision_shape,
                    .volume             = data.volume,
                    .local_inertia      = local_inertia
                };
            }
        }
        return Scaled{
            .scale_key          = scale_key,
            .geometry           = geometry,
            .geometry_primitive = geometry_primitive,
            .collision_shape    = {},
            .volume             = data.volume,
            .local_inertia      = local_inertia
        };
    }

    log_brush->trace("create_scaled() scale = {}", scale);

    auto scaled_geometry = std::make_shared<erhe::geometry::Geometry>(
        erhe::geometry::operation::clone(
            *geometry.get(),
            erhe::math::create_scale(scale)
        )
    );
    scaled_geometry->name = fmt::format("{} scaled by {}", geometry->name, scale);

    auto scaled_geometry_primitive = std::make_shared<erhe::primitive::Geometry_primitive>(
        scaled_geometry,
        data.build_info,
        data.normal_style
    );

    glm::mat4 local_inertia{0.0f};
    if (data.editor_settings.physics.static_enable) {
        if (data.collision_shape) {
            ERHE_VERIFY(data.collision_shape->is_convex());
            const auto scaled_volume          = data.volume * scale * scale * scale;
            const auto mass                   = data.density * scaled_volume;
            auto       scaled_collision_shape = erhe::physics::ICollision_shape::create_uniform_scaling_shape_shared(
                data.collision_shape.get(),
                scale
            );
            scaled_collision_shape->calculate_local_inertia(mass, local_inertia);
            return Scaled{
                .scale_key          = scale_key,
                .geometry           = scaled_geometry,
                .geometry_primitive = scaled_geometry_primitive,
                .collision_shape    = scaled_collision_shape,
                .volume             = scaled_volume,
                .local_inertia      = local_inertia
            };
        } else if (data.collision_shape_generator) {
            auto       scaled_collision_shape = data.collision_shape_generator(scale);
            const auto scaled_volume          = data.collision_volume_calculator
                ? data.collision_volume_calculator(scale)
                : data.volume * scale * scale * scale;
            const auto mass                   = data.density * scaled_volume;
            scaled_collision_shape->calculate_local_inertia(mass, local_inertia);
            return Scaled{
                .scale_key          = scale_key,
                .geometry           = scaled_geometry,
                .geometry_primitive = scaled_geometry_primitive,
                .collision_shape    = scaled_collision_shape,
                .volume             = scaled_volume,
                .local_inertia      = local_inertia
            };
        }
    }
    return Scaled{
        .scale_key          = scale_key,
        .geometry           = scaled_geometry,
        .geometry_primitive = scaled_geometry_primitive,
        .collision_shape    = {},
        .volume             = data.volume * scale * scale * scale,
        .local_inertia      = local_inertia
    };
}

const std::string empty_string = {};

auto Brush::get_geometry() -> std::shared_ptr<erhe::geometry::Geometry>
{
    if (!data.geometry) {
        data.geometry = data.geometry_generator();
        data.geometry_generator = {};
    }
    return data.geometry;
}

auto Brush::make_instance(
    const Instance_create_info& instance_create_info
) -> std::shared_ptr<erhe::scene::Node>
{
    ERHE_PROFILE_FUNCTION();

    late_initialize();

    const auto& scaled = get_scaled(instance_create_info.scale);

    const auto& name = scaled.geometry
        ? scaled.geometry->name
        : empty_string;

    log_scene->trace(
        "creating {} with material {} (material buffer index {})",
        name,
        instance_create_info.material->get_name(),
        instance_create_info.material->material_buffer_index
    );

    auto node = std::make_shared<erhe::scene::Node>(name);
    auto mesh = std::make_shared<erhe::scene::Mesh>(name);
    mesh->add_primitive(
        erhe::primitive::Primitive{
            .material           = instance_create_info.material,
            .geometry_primitive = scaled.geometry_primitive,
        }
    );

    ERHE_VERIFY(instance_create_info.scene_root != nullptr);

    mesh->layer_id = instance_create_info.scene_root->layers().content()->id;
    mesh->enable_flag_bits   (instance_create_info.mesh_flags);
    node->set_world_from_node(instance_create_info.world_from_node);
    node->attach             (mesh);
    node->enable_flag_bits   (instance_create_info.node_flags);

    if (data.editor_settings.physics.static_enable) {
        if (data.collision_shape || data.collision_shape_generator) {
            ERHE_PROFILE_SCOPE("make brush node physics");

            const erhe::physics::IRigid_body_create_info rigid_body_create_info{
                .collision_shape  = scaled.collision_shape,
                .mass             = data.density * scaled.volume,
                .inertia_override = scaled.local_inertia,
                .debug_label      = name.c_str(),
                .motion_mode      = instance_create_info.motion_mode,
            };
            auto node_physics = std::make_shared<Node_physics>(rigid_body_create_info); // TODO use content library?
            node->attach(node_physics);
        }
    }

    return node;
}

[[nodiscard]] auto Brush::get_bounding_box() -> erhe::math::Bounding_box
{
    if (!geometry_primitive) {
        ERHE_PROFILE_SCOPE("geometry primitive");

        geometry_primitive = std::make_unique<erhe::primitive::Geometry_primitive>(
            get_geometry(),
            data.build_info,
            data.normal_style
        );
    }
    erhe::primitive::Renderable_mesh& renderable_mesh = geometry_primitive->get_geometry_mesh();
    return renderable_mesh.bounding_box;
}

}
