#include "brushes/brush.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "editor_log.hpp"

#include "erhe/geometry/operation/clone.hpp"
#include "erhe/physics/icollision_shape.hpp"
#include "erhe/physics/irigid_body.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/primitive/buffer_sink.hpp"
#include "erhe/raytrace/ibuffer.hpp"
#include "erhe/raytrace/igeometry.hpp"
#include "erhe/raytrace/iscene.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"

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
    const erhe::geometry::Geometry& geometry,
    erhe::geometry::Polygon_id      polygon_id
)
    : polygon_id{polygon_id}
{
    const auto* const polygon_centroids = geometry.polygon_attributes().find<vec3>(c_polygon_centroids);
    const auto* const polygon_normals   = geometry.polygon_attributes().find<vec3>(c_polygon_normals);
    const auto* const point_locations   = geometry.point_attributes().find<vec3>(c_point_locations);
    ERHE_VERIFY(point_locations != nullptr);

    const auto& polygon = geometry.polygons[polygon_id];

    centroid = (polygon_centroids != nullptr) && polygon_centroids->has(polygon_id)
        ? polygon_centroids->get(polygon_id)
        : polygon.compute_centroid(geometry, *point_locations);

    N = (polygon_normals != nullptr) && polygon_normals->has(polygon_id)
        ? polygon_normals->get(polygon_id)
        : polygon.compute_normal(geometry, *point_locations);

    const Corner_id corner_id = geometry.polygon_corners[polygon.first_polygon_corner_id];
    const auto&     corner    = geometry.corners[corner_id];
    const Point_id  point     = corner.point_id;
    ERHE_VERIFY(point_locations->has(point));
    position = point_locations->get(point);
    const vec3 midpoint = polygon.compute_edge_midpoint(geometry, *point_locations);
    T = normalize(midpoint - centroid);
    B = normalize(cross(N, T));
    N = normalize(cross(T, B));
    T = normalize(cross(B, N));
    corner_count = polygon.corner_count;
}

void Reference_frame::transform_by(const mat4 m)
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
        vec4{B, 0.0f},
        vec4{T, 0.0f},
        vec4{N, 0.0f},
        vec4{centroid, 1.0f}
    };
}

Brush::Brush(const Brush_data& create_info)
    : data{create_info}
{
    if (data.name.empty() && data.geometry)
    {
        data.name = data.geometry->name;
    }
}

void Brush::late_initialize()
{
    const auto geometry = get_geometry();
    if (!gl_primitive_geometry)
    {
        ERHE_PROFILE_SCOPE("gl primitive");

        gl_primitive_geometry = std::make_unique<erhe::primitive::Primitive_geometry>(
            make_primitive(
                *geometry.get(),
                data.build_info,
                data.normal_style
            )
        );
    }

    if (!rt_primitive)
    {
        ERHE_PROFILE_SCOPE("rt primitive");
        rt_primitive = std::make_shared<Raytrace_primitive>(geometry);
    }

    if (
        data.physics_enabled &&
        !data.collision_shape &&
        !data.collision_shape_generator
    )
    {
        ERHE_PROFILE_SCOPE("make brush convex hull collision shape");

        data.collision_shape = erhe::physics::ICollision_shape::create_convex_hull_shape_shared(
            reinterpret_cast<const float*>(
                geometry->point_attributes().find<vec3>(c_point_locations)->values.data()
            ),
            static_cast<int>(geometry->get_point_count()),
            static_cast<int>(sizeof(vec3))
        );
    }

    if ((data.volume == 0.0f) && data.collision_volume_calculator)
    {
        ERHE_PROFILE_SCOPE("calculate brush volume");

        data.volume = data.collision_volume_calculator(1.0f);
    }
}

Brush::Brush(Brush&& other) noexcept = default;

auto Brush::get_reference_frame(const uint32_t corner_count) -> Reference_frame
{
    for (const auto& reference_frame : reference_frames)
    {
        if (reference_frame.corner_count == corner_count)
        {
            return reference_frame;
        }
    }

    const auto geometry = get_geometry();

    for (
        Polygon_id polygon_id = 0, end = geometry->get_polygon_count();
        polygon_id < end;
        ++polygon_id
    )
    {
        const auto& polygon = geometry->polygons[polygon_id];
        if (
            (polygon.corner_count == corner_count) ||
            (polygon_id + 1 == end)
        )
        {
            return reference_frames.emplace_back(*geometry.get(), polygon_id);
        }
    }

    log_brush->error("{} invalid code path", __func__);
    return Reference_frame{};
}

auto Brush::get_scaled(const float scale) -> const Scaled&
{
    late_initialize();
    const int scale_key = static_cast<int>(scale * c_scale_factor);
    for (const auto& scaled : scaled_entries)
    {
        if (scaled.scale_key == scale_key)
        {
            return scaled;
        }
    }
    return scaled_entries.emplace_back(create_scaled(scale_key));
}

auto Brush::create_scaled(const int scale_key) -> Scaled
{
    ERHE_PROFILE_FUNCTION

    const float scale    = static_cast<float>(scale_key) / c_scale_factor;
    const auto  geometry = get_geometry();
    if (scale == 1.0f)
    {
        glm::mat4 local_inertia{0.0f};
        if (data.collision_shape)
        {
            ERHE_VERIFY(data.collision_shape->is_convex());
            const auto mass = data.density * data.volume;
            data.collision_shape->calculate_local_inertia(mass, local_inertia);
            return Scaled{
                .scale_key             = scale_key,
                .geometry              = geometry,
                .gl_primitive_geometry = *gl_primitive_geometry.get(),
                .rt_primitive          = rt_primitive,
                .collision_shape       = data.collision_shape,
                .volume                = data.volume,
                .local_inertia         = local_inertia
            };
        }
        else if (data.collision_shape_generator)
        {
            const auto generated_collision_shape = data.collision_shape_generator(scale);
            const auto mass = data.density * data.volume;
            generated_collision_shape->calculate_local_inertia(mass, local_inertia);
            return Scaled{
                .scale_key             = scale_key,
                .geometry              = geometry,
                .gl_primitive_geometry = *gl_primitive_geometry.get(),
                .rt_primitive          = rt_primitive,
                .collision_shape       = generated_collision_shape,
                .volume                = data.volume,
                .local_inertia         = local_inertia
            };
        }
        else
        {
            return Scaled{
                .scale_key             = scale_key,
                .geometry              = geometry,
                .gl_primitive_geometry = *gl_primitive_geometry.get(),
                .rt_primitive          = rt_primitive,
                .collision_shape       = {},
                .volume                = data.volume,
                .local_inertia         = local_inertia
            };
        }
    }

    log_brush->trace("create_scaled() scale = {}", scale);

    auto scaled_geometry = std::make_shared<erhe::geometry::Geometry>(
        erhe::geometry::operation::clone(
            *geometry.get(),
            erhe::toolkit::create_scale(scale)
        )
    );
    scaled_geometry->name = fmt::format(
        "{} scaled by {}",
        geometry->name,
        scale
    );

    auto scaled_gl_primitive_geometry = make_primitive(
        *scaled_geometry.get(),
        data.build_info,
        data.normal_style
    );

    auto scaled_rt_primitive = std::make_shared<Raytrace_primitive>(scaled_geometry);

    glm::mat4 local_inertia{0.0f};
    if (data.collision_shape)
    {
        ERHE_VERIFY(data.collision_shape->is_convex());
        const auto scaled_volume          = data.volume * scale * scale * scale;
        const auto mass                   = data.density * scaled_volume;
        auto       scaled_collision_shape = erhe::physics::ICollision_shape::create_uniform_scaling_shape_shared(
            data.collision_shape.get(),
            scale
        );
        scaled_collision_shape->calculate_local_inertia(mass, local_inertia);
        return Scaled{
            .scale_key             = scale_key,
            .geometry              = scaled_geometry,
            .gl_primitive_geometry = scaled_gl_primitive_geometry,
            .rt_primitive          = scaled_rt_primitive,
            .collision_shape       = scaled_collision_shape,
            .volume                = scaled_volume,
            .local_inertia         = local_inertia
        };
    }
    else if (data.collision_shape_generator)
    {
        auto       scaled_collision_shape = data.collision_shape_generator(scale);
        const auto scaled_volume          = data.collision_volume_calculator
            ? data.collision_volume_calculator(scale)
            : data.volume * scale * scale * scale;
        const auto mass                   = data.density * scaled_volume;
        scaled_collision_shape->calculate_local_inertia(mass, local_inertia);
        return Scaled{
            .scale_key             = scale_key,
            .geometry              = scaled_geometry,
            .gl_primitive_geometry = scaled_gl_primitive_geometry,
            .rt_primitive          = scaled_rt_primitive,
            .collision_shape       = scaled_collision_shape,
            .volume                = scaled_volume,
            .local_inertia         = local_inertia
        };
    }
    else
    {
        return Scaled{
            .scale_key             = scale_key,
            .geometry              = scaled_geometry,
            .gl_primitive_geometry = scaled_gl_primitive_geometry,
            .rt_primitive          = scaled_rt_primitive,
            .collision_shape       = {},
            .volume                = data.volume * scale * scale * scale,
            .local_inertia         = local_inertia
        };
    }
}

const std::string empty_string = {};

auto Brush::name() const -> const std::string&
{
    return data.name;
}

auto Brush::get_geometry() -> std::shared_ptr<erhe::geometry::Geometry>
{
    if (!data.geometry)
    {
        data.geometry = data.geometry_generator();
        data.geometry_generator = {};
    }
    return data.geometry;
}

auto Brush::make_instance(
    const Instance_create_info& instance_create_info
) -> Instance
{
    ERHE_PROFILE_FUNCTION

    late_initialize();

    const auto& scaled = get_scaled(instance_create_info.scale);

    const auto& name = scaled.geometry
        ? scaled.geometry->name
        : empty_string;

    ERHE_VERIFY(scaled.rt_primitive);

    log_scene->trace(
        "creating {} with material index {} : {}",
        name,
        instance_create_info.material->index,
        instance_create_info.material->name
    );

    auto mesh = std::make_shared<erhe::scene::Mesh>(name);
    mesh->mesh_data.primitives.push_back(
        erhe::primitive::Primitive{
            .material              = instance_create_info.material,
            .gl_primitive_geometry = scaled.gl_primitive_geometry,
            .rt_primitive_geometry = scaled.rt_primitive->primitive_geometry,
            .rt_vertex_buffer      = scaled.rt_primitive->vertex_buffer,
            .rt_index_buffer       = scaled.rt_primitive->index_buffer,
            .source_geometry       = scaled.geometry,
            .normal_style          = data.normal_style
        }
    );
    mesh->set_visibility_mask(instance_create_info.node_visibility_flags);
    mesh->set_world_from_node(instance_create_info.world_from_node);

    std::shared_ptr<Node_physics>  node_physics;
    std::shared_ptr<Node_raytrace> node_raytrace;
    if (data.collision_shape || data.collision_shape_generator)
    {
        ERHE_PROFILE_SCOPE("make brush node physics");

        const erhe::physics::IRigid_body_create_info rigid_body_create_info{
            .world            = instance_create_info.scene_root->physics_world(),
            .collision_shape  = scaled.collision_shape,
            .mass             = data.density * scaled.volume,
            .inertia_override = scaled.local_inertia,
            .debug_label      = name.c_str()
        };
        node_physics = std::make_shared<Node_physics>(rigid_body_create_info);
        mesh->attach(node_physics);
    }

    if (scaled.rt_primitive)
    {
        node_raytrace = std::make_shared<Node_raytrace>(
            scaled.geometry,
            scaled.rt_primitive
        );
        mesh->attach(node_raytrace);
    }

    return Instance{
        mesh,
        node_physics,
        node_raytrace
    };
}

[[nodiscard]] auto Brush::get_bounding_box() -> erhe::toolkit::Bounding_box
{
    if (!gl_primitive_geometry)
    {
        ERHE_PROFILE_SCOPE("gl primitive");

        gl_primitive_geometry = std::make_unique<erhe::primitive::Primitive_geometry>(
            make_primitive(
                *get_geometry().get(),
                data.build_info,
                data.normal_style
            )
        );
    }
    return gl_primitive_geometry->bounding_box;
}

}
