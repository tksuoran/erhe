#include "scene/brush.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_manager.hpp"
#include "log.hpp"
#include "erhe/geometry/operation/clone.hpp"
#include "erhe/physics/rigid_body.hpp"
#include "erhe/physics/world.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/tracy_client.hpp"

#include "BulletCollision/CollisionShapes/btConvexHullShape.h"
#include "BulletCollision/CollisionShapes/btUniformScalingShape.h"

namespace editor
{

using namespace erhe::geometry;
using erhe::geometry::Polygon; // Resolve conflict with wingdi.h BOOL Polygon(HDC,const POINT *,int)
using namespace erhe::primitive;
using namespace erhe::scene;
using namespace std;
using namespace glm;

Reference_frame::Reference_frame(const erhe::geometry::Geometry& geometry,
                                 erhe::geometry::Polygon_id      polygon_id)
    : polygon_id(polygon_id)
{
    const auto* const polygon_centroids = geometry.polygon_attributes().find<vec3>(c_polygon_centroids);
    const auto* const polygon_normals   = geometry.polygon_attributes().find<vec3>(c_polygon_normals);
    const auto* const point_locations   = geometry.point_attributes().find<vec3>(c_point_locations);
    VERIFY(point_locations != nullptr);

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
    VERIFY(point_locations->has(point));
    position = point_locations->get(point);
    const vec3 midpoint = polygon.compute_edge_midpoint(geometry, *point_locations);
    T = normalize(midpoint - centroid);
    B = normalize(cross(N, T));
    N = normalize(cross(T, B));
    T = normalize(cross(B, N));
    corner_count = polygon.corner_count;
}

void Reference_frame::transform_by(mat4 m)
{
    centroid = m * vec4(centroid, 1.0f);
    position = m * vec4(position, 1.0f);
    B = m * vec4(B, 0.0f);
    T = m * vec4(T, 0.0f);
    N = m * vec4(N, 0.0f);
    B = normalize(cross(N, T));
    N = normalize(cross(T, B));
    T = normalize(cross(B, N));
}

auto Reference_frame::scale() const -> float
{
    return glm::distance(centroid, position);
}

auto Reference_frame::transform() const -> mat4
{
    vec3 C = centroid;
    return mat4(vec4(B, 0.0f),
                vec4(T, 0.0f),
                vec4(N, 0.0f),
                vec4(centroid, 1.0f));
}

Brush::~Brush()
{
}

Brush_create_info::~Brush_create_info()
{
}

Brush_create_info::Brush_create_info(const shared_ptr<Geometry>&         geometry,
                                     Primitive_build_context&            context,
                                     erhe::primitive::Normal_style       normal_style,
                                     float                               density,
                                     float                               volume,
                                     const shared_ptr<btCollisionShape>& collision_shape)
    : geometry       {geometry}
    , context        {context}
    , normal_style   {normal_style}
    , density        {density}
    , volume         {volume}
    , collision_shape{collision_shape}
{
}

Brush_create_info::Brush_create_info(const shared_ptr<Geometry>&   geometry,
                                     Primitive_build_context&      context,
                                     erhe::primitive::Normal_style normal_style,
                                     float                         density,
                                     Collision_volume_calculator   collision_volume_calculator,
                                     Collision_shape_generator     collision_shape_generator)
    : geometry                   {geometry}
    , context                    {context}
    , normal_style               {normal_style}
    , density                    {density}
    , collision_volume_calculator{collision_volume_calculator}
    , collision_shape_generator  {collision_shape_generator}
{
}

Brush::Brush(const erhe::primitive::Primitive_build_context& context)
    : context{context}
{
}

void Brush::initialize(const Create_info& create_info)
{
    ZoneScoped;

    geometry                    = create_info.geometry;
    normal_style                = create_info.normal_style;
    density                     = create_info.density;
    volume                      = create_info.volume;
    collision_shape             = create_info.collision_shape;
    collision_volume_calculator = create_info.collision_volume_calculator;
    collision_shape_generator   = create_info.collision_shape_generator;

    VERIFY(geometry.get() != nullptr);

    {
        ZoneScopedN("make brush primitive");

        primitive_geometry = make_primitive_shared(*create_info.geometry.get(), context, normal_style);
        primitive_geometry->source_geometry = create_info.geometry;
    }

    if (!collision_shape && !collision_shape_generator)
    {
        ZoneScopedN("make brush concex hull collision shape");
    
        auto convex_hull_shape = make_shared<btConvexHullShape>(
            reinterpret_cast<const btScalar*>(geometry->point_attributes().find<vec3>(c_point_locations)->values.data()),
            static_cast<int>(geometry->point_count()),
            static_cast<int>(sizeof(vec3))
        );
        convex_hull_shape->initializePolyhedralFeatures();
        this->collision_shape = convex_hull_shape;
    }
    
    if (this->collision_volume_calculator)
    {
        ZoneScopedN("calculate brush volume");
    
        volume = this->collision_volume_calculator(1.0f);
    }
}

Brush::Brush(const Create_info& create_info)
    : geometry                   {create_info.geometry}
    , context                    {create_info.context}
    , normal_style               {create_info.normal_style}
    , density                    {create_info.density}
    , volume                     {create_info.volume}
    , collision_shape            {create_info.collision_shape}
    , collision_volume_calculator{create_info.collision_volume_calculator}
    , collision_shape_generator  {create_info.collision_shape_generator}
{
    ZoneScoped;

    VERIFY(geometry.get() != nullptr);

    {
        ZoneScopedN("make brush primitive");

        primitive_geometry = make_primitive_shared(*create_info.geometry.get(),
                                                   context);

        primitive_geometry->source_geometry = create_info.geometry;
    }

    if (!collision_shape && !collision_shape_generator)
    {
        ZoneScopedN("make brush concex hull collision shape");

        auto convex_hull_shape = make_shared<btConvexHullShape>(
            reinterpret_cast<const btScalar*>(geometry->point_attributes().find<vec3>(c_point_locations)->values.data()),
            static_cast<int>(geometry->point_count()),
            static_cast<int>(sizeof(vec3))
        );
        convex_hull_shape->initializePolyhedralFeatures();
        this->collision_shape = convex_hull_shape;
    }

    if (this->collision_volume_calculator)
    {
        ZoneScopedN("calculate brush volume");

        volume = this->collision_volume_calculator(1.0f);
    }
}

Brush::Brush(Brush&& other) noexcept
    : geometry                     {std::move(other.geometry)}
    , primitive_geometry           {std::move(other.primitive_geometry)}
    , context                      {other.context}
    , normal_style                 {other.normal_style}
    , collision_shape              {std::move(other.collision_shape)}
    , collision_volume_calculator  {std::move(other.collision_volume_calculator)}
    , collision_shape_generator    {std::move(other.collision_shape_generator)}
    , volume                       {other.volume}
    , density                      {other.density}
    , reference_frames             {std::move(other.reference_frames)}
    , scaled_entries               {std::move(other.scaled_entries)}
{
}

auto Brush::get_reference_frame(const uint32_t corner_count)
-> Reference_frame
{
    VERIFY(primitive_geometry->source_geometry != nullptr);
    const auto& geometry = *primitive_geometry->source_geometry;

    for (const auto& reference_frame : reference_frames)
    {
        if (reference_frame.corner_count == corner_count)
        {
            return reference_frame;
        }
    }

    for (Polygon_id polygon_id = 0, end = geometry.polygon_count();
         polygon_id < end;
         ++polygon_id)
    {
        const auto& polygon = geometry.polygons[polygon_id];
        if (polygon.corner_count == corner_count || (polygon_id + 1 == end))
        {
            return reference_frames.emplace_back(geometry, polygon_id);
        }
    }

    log_brush.error("{} invalid code path\n", __func__);
    return Reference_frame{};
}

auto Brush::get_scaled(const float scale)
-> const Scaled&
{
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

auto Brush::create_scaled(const int scale_key)
-> Scaled
{
    ZoneScoped;

    VERIFY(primitive_geometry);

    const float scale = static_cast<float>(scale_key) / c_scale_factor;

    if (scale == 1.0f)
    {
        if (collision_shape)
        {
            VERIFY(collision_shape->isConvex());
            const auto mass = density * volume;
            btVector3  local_inertia{1.0f, 1.0f, 1.0f};
            {
                ZoneScopedN("calculateLocalInertia");
                collision_shape->calculateLocalInertia(mass, local_inertia);
            }
            return Scaled{scale_key,
                          primitive_geometry,
                          collision_shape,
                          volume,
                          local_inertia};
        }
        else if (collision_shape_generator)
        {
            const auto collision_shape = collision_shape_generator(scale);
            const auto mass            = density * volume;
            btVector3  local_inertia{1.0f, 1.0f, 1.0f};
            {
                ZoneScopedN("calculateLocalInertia");
                collision_shape->calculateLocalInertia(mass, local_inertia);
            }
            return Scaled{scale_key,
                          primitive_geometry,
                          collision_shape,
                          volume,
                          local_inertia};
        }
        else
        {
            return Scaled{scale_key, primitive_geometry, {}, volume};
        }
    }

    log_brush.trace("create_scaled() scale = {}\n", scale);

    const mat4 scale_transform = erhe::toolkit::create_scale(scale);
    Geometry   scaled_geometry = erhe::geometry::operation::clone(*primitive_geometry->source_geometry.get(), scale_transform);
    scaled_geometry.name = fmt::format("{} scaled by {}", primitive_geometry->source_geometry->name, scale);
    auto scaled_primitive_geometry = make_primitive_shared(scaled_geometry, context, normal_style);
    scaled_primitive_geometry->source_geometry     = std::make_shared<Geometry>(std::move(scaled_geometry));
    scaled_primitive_geometry->source_normal_style = primitive_geometry->source_normal_style;

    if (collision_shape)
    {
        VERIFY(collision_shape->isConvex());
        const auto scaled_volume          = volume * scale * scale * scale;
        const auto mass                   = density * scaled_volume;
        const auto convex_shape           = static_cast<btConvexShape*>(collision_shape.get());
        auto       scaled_collision_shape = std::make_shared<btUniformScalingShape>(convex_shape, scale);
        btVector3  local_inertia{1.0f, 1.0f, 1.0f};
        {
            ZoneScopedN("calculateLocalInertia");
            scaled_collision_shape->calculateLocalInertia(mass, local_inertia);
        }
        return Scaled{scale_key,
                      scaled_primitive_geometry,
                      scaled_collision_shape,
                      scaled_volume,
                      local_inertia};
    }
    else if (collision_shape_generator)
    {
        auto       scaled_collision_shape = collision_shape_generator(scale);
        const auto scaled_volume          = collision_volume_calculator ? collision_volume_calculator(scale)
                                                                       : volume * scale * scale * scale;
        const auto mass                   = density * scaled_volume;
        btVector3  local_inertia{1.0f, 1.0f, 1.0f};
        {
            ZoneScopedN("calculateLocalInertia");
            scaled_collision_shape->calculateLocalInertia(mass, local_inertia);
        }
        return Scaled{scale_key,
                      scaled_primitive_geometry,
                      scaled_collision_shape,
                      scaled_volume,
                      local_inertia};
    }
    else
    {
        return Scaled{scale_key, scaled_primitive_geometry, {}, volume * scale * scale * scale};
    }

}

auto Brush::make_instance(erhe::scene::Layer&                               layer,
                          erhe::scene::Scene&                               scene,
                          erhe::physics::World&                             physics_world,
                          const std::shared_ptr<erhe::scene::Node>&         parent,
                          const glm::mat4                                   local_to_parent,
                          const std::shared_ptr<erhe::primitive::Material>& material,
                          const float                                       scale)
-> Instance
{
    ZoneScoped;

    const auto& scaled = get_scaled(scale);
    auto        mesh   = std::make_shared<Mesh>(scaled.primitive_geometry->source_geometry ? scaled.primitive_geometry->source_geometry->name
                                                                                     : "");
    mesh->primitives.emplace_back(scaled.primitive_geometry, material);
    layer.meshes.push_back(mesh);

    auto node = make_shared<Node>();
    node->parent = parent.get();
    node->transforms.parent_from_node.set(local_to_parent);
    node->update();
    
    if (collision_shape || collision_shape_generator)
    {
        ZoneScopedN("make brush node physics");
        erhe::physics::Rigid_body::Create_info create_info{density * scaled.volume,
                                                           scaled.collision_shape,
                                                           scaled.local_inertia};
        auto node_physics = make_shared<Node_physics>(create_info);
        return { node, mesh, node_physics };
    }
    return { node, mesh, {} };
}

}
