#include "erhe_physics/jolt/jolt_collision_shape.hpp"
#include "erhe_physics/jolt/glm_conversions.hpp"
#include "erhe_verify/verify.hpp"

#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CollisionDispatch.h>
#include <Jolt/Physics/Collision/Shape/CompoundShape.h>
#include <Jolt/Physics/Collision/Shape/DecoratedShape.h>
#include <Jolt/Physics/Collision/Shape/SubShapeID.h>

#include <algorithm>

namespace erhe::physics {

class Jolt_empty_shape : public Jolt_collision_shape
{
public:
    ~Jolt_empty_shape() noexcept override = default;

    Jolt_empty_shape()
    {
        auto result = m_shape_settings.Create();
        ERHE_VERIFY(result.IsValid());
        m_jolt_shape = result.Get();
    }

    auto get_shape_settings() -> JPH::ShapeSettings& override
    {
        return m_shape_settings;
    }

    [[nodiscard]] auto get_shape_type() const -> Collision_shape_type override
    {
        return Collision_shape_type::e_empty;
    }

private:
    JPH::EmptyShapeSettings m_shape_settings;
};

auto ICollision_shape::create_empty_shape() -> ICollision_shape*
{
    return new Jolt_empty_shape();
}

auto ICollision_shape::create_empty_shape_shared() -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_empty_shape>();
}

auto ICollision_shape::create_box_shape(const glm::vec3 half_extents) -> ICollision_shape*
{
    return new Jolt_box_shape(half_extents);
}

auto ICollision_shape::create_box_shape_shared(const glm::vec3 half_extents) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_box_shape>(half_extents);
}

auto ICollision_shape::create_capsule_shape(const Axis axis, const float radius, const float length) -> ICollision_shape*
{
    return new Jolt_capsule_shape(axis, radius, length);
}

auto ICollision_shape::create_capsule_shape_shared(const Axis axis, const float radius, const float length) -> std::shared_ptr<ICollision_shape>
{
     return std::make_shared<Jolt_capsule_shape>(axis, radius, length);
}

#if 0
auto ICollision_shape::create_cone_shape(
    const Axis  axis,
    const float base_radius,
    const float height
) -> ICollision_shape*
{
    return new Jolt_cone_shape(axis, base_radius, height);
}

auto ICollision_shape::create_cone_shape_shared(
    const Axis  axis,
    const float base_radius,
    const float height
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_cone_shape>(axis, base_radius, height);
}
#endif

auto ICollision_shape::create_tapered_capsule_shape(const Axis axis, const float bottom_radius, const float top_radius, const float length) -> ICollision_shape*
{
    return new Jolt_tapered_capsule_shape(axis, bottom_radius, top_radius, length);
}

auto ICollision_shape::create_tapered_capsule_shape_shared(const Axis axis, const float bottom_radius, const float top_radius, const float length) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_tapered_capsule_shape>(axis, bottom_radius, top_radius, length);
}

auto ICollision_shape::create_tapered_cylinder_shape(const Axis axis, const float bottom_radius, const float top_radius, const float length) -> ICollision_shape*
{
    return new Jolt_tapered_cylinder_shape(axis, bottom_radius, top_radius, length);
}

auto ICollision_shape::create_tapered_cylinder_shape_shared(const Axis axis, const float bottom_radius, const float top_radius, const float length) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_tapered_cylinder_shape>(axis, bottom_radius, top_radius, length);
}

auto ICollision_shape::create_cylinder_shape(const Axis axis, const glm::vec3 half_extents) -> ICollision_shape*
{
    return new Jolt_cylinder_shape(axis, half_extents);
}

auto ICollision_shape::create_cylinder_shape_shared(const Axis axis, const glm::vec3 half_extents) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_cylinder_shape>(axis, half_extents);
}

auto ICollision_shape::create_sphere_shape(const float radius) -> ICollision_shape*
{
    return new Jolt_sphere_shape(radius);
}

auto ICollision_shape::create_sphere_shape_shared(const float radius) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_sphere_shape>(radius);
}

void Jolt_collision_shape::calculate_local_inertia(const float mass, glm::mat4& inertia) const
{
    JPH::MassProperties mass_properties = m_jolt_shape->GetMassProperties();
    mass_properties.ScaleToMass(mass);
    inertia = from_jolt(mass_properties.mInertia);
}

auto Jolt_collision_shape::is_convex() const -> bool
{
    return true;
}

auto Jolt_collision_shape::get_jolt_shape() const -> JPH::ShapeRefC
{
    return m_jolt_shape;
}

auto Jolt_collision_shape::get_center_of_mass() const -> glm::vec3
{
    const JPH::Vec3 jolt_center_of_mass = m_jolt_shape->GetCenterOfMass();
    const glm::vec3 center_of_mass = from_jolt(jolt_center_of_mass);
    return center_of_mass;
}

auto Jolt_collision_shape::get_mass_properties() const -> Mass_properties
{
    const auto jolt_mass_properties = m_jolt_shape->GetMassProperties();
    return Mass_properties{
        .mass           = jolt_mass_properties.mMass,
        .inertia_tensor = from_jolt(jolt_mass_properties.mInertia)
    };
}

auto Jolt_collision_shape::describe() const -> std::string
{
    return "Jolt_collision_shape";
}

//

Jolt_box_shape::Jolt_box_shape(const glm::vec3 half_extents)
    : m_half_extents{half_extents}
{
    m_shape_settings = new JPH::BoxShapeSettings(to_jolt(half_extents));
    auto result = m_shape_settings->Create();
    ERHE_VERIFY(result.IsValid());
    m_jolt_shape = result.Get();
}

auto Jolt_box_shape::get_shape_settings() -> JPH::ShapeSettings&
{
    return *m_shape_settings.GetPtr();
}

auto Jolt_box_shape::describe() const -> std::string
{
    return "Jolt_box_shape";
}

auto Jolt_box_shape::get_shape_type() const -> Collision_shape_type
{
    return Collision_shape_type::e_box;
}

auto Jolt_box_shape::get_half_extents() const -> std::optional<glm::vec3>
{
    return m_half_extents;
}

//

Jolt_capsule_shape::Jolt_capsule_shape(const Axis axis, const float radius, const float length)
    : m_axis  {axis}
    , m_radius{radius}
    , m_length{length}
{
    m_capsule_shape_settings = new JPH::CapsuleShapeSettings(length * 0.5f, radius);
    const JPH::Vec3 x_axis{1.0f, 0.0f, 0.0f};
    const JPH::Vec3 y_axis{0.0f, 1.0f, 0.0f};
    const JPH::Vec3 z_axis{0.0f, 0.0f, 1.0f};
    m_shape_settings = new JPH::RotatedTranslatedShapeSettings{
        JPH::Vec3{0.0f, 0.0f, 0.0f},
        (axis == Axis::Y)
            ? JPH::Quat::sIdentity()
            : (axis == Axis::X)
                ? JPH::Quat::sFromTo(y_axis, x_axis)
                : JPH::Quat::sFromTo(y_axis, z_axis),
        m_capsule_shape_settings
    };
    auto result = m_shape_settings->Create();
    ERHE_VERIFY(result.IsValid());
    m_jolt_shape = result.Get();
}

Jolt_capsule_shape::~Jolt_capsule_shape() noexcept
{
}

auto Jolt_capsule_shape::get_shape_settings() -> JPH::ShapeSettings&
{
    return *m_shape_settings.GetPtr();
}

auto Jolt_capsule_shape::describe() const -> std::string
{
    return "Jolt_capsule_shape";
}

auto Jolt_capsule_shape::get_shape_type() const -> Collision_shape_type
{
    return Collision_shape_type::e_capsule;
}

auto Jolt_capsule_shape::get_radius() const -> std::optional<float>
{
    return m_radius;
}

auto Jolt_capsule_shape::get_axis() const -> std::optional<Axis>
{
    return m_axis;
}

auto Jolt_capsule_shape::get_length() const -> std::optional<float>
{
    return m_length;
}

//

Jolt_tapered_capsule_shape::Jolt_tapered_capsule_shape(const Axis axis, const float bottom_radius, const float top_radius, const float length)
    : m_axis         {axis}
    , m_bottom_radius{bottom_radius}
    , m_top_radius   {top_radius}
    , m_length       {length}
{
    // Jolt centers the shape around the origin with the bottom cap sphere at
    // (0, -half_height, 0) and the top cap sphere at (0, +half_height, 0),
    // matching erhe::geometry::shapes::make_capsule().
    m_tapered_capsule_shape_settings = new JPH::TaperedCapsuleShapeSettings(length * 0.5f, top_radius, bottom_radius);
    const JPH::Vec3 x_axis{1.0f, 0.0f, 0.0f};
    const JPH::Vec3 y_axis{0.0f, 1.0f, 0.0f};
    const JPH::Vec3 z_axis{0.0f, 0.0f, 1.0f};
    m_shape_settings = new JPH::RotatedTranslatedShapeSettings{
        JPH::Vec3{0.0f, 0.0f, 0.0f},
        (axis == Axis::Y)
            ? JPH::Quat::sIdentity()
            : (axis == Axis::X)
                ? JPH::Quat::sFromTo(y_axis, x_axis)
                : JPH::Quat::sFromTo(y_axis, z_axis),
        m_tapered_capsule_shape_settings
    };
    auto result = m_shape_settings->Create();
    ERHE_VERIFY(result.IsValid());
    m_jolt_shape = result.Get();
}

Jolt_tapered_capsule_shape::~Jolt_tapered_capsule_shape() noexcept = default;

auto Jolt_tapered_capsule_shape::get_shape_settings() -> JPH::ShapeSettings&
{
    return *m_shape_settings.GetPtr();
}

auto Jolt_tapered_capsule_shape::describe() const -> std::string
{
    return "Jolt_tapered_capsule_shape";
}

auto Jolt_tapered_capsule_shape::get_shape_type() const -> Collision_shape_type
{
    return Collision_shape_type::e_tapered_capsule;
}

auto Jolt_tapered_capsule_shape::get_bottom_radius() const -> std::optional<float>
{
    return m_bottom_radius;
}

auto Jolt_tapered_capsule_shape::get_top_radius() const -> std::optional<float>
{
    return m_top_radius;
}

auto Jolt_tapered_capsule_shape::get_axis() const -> std::optional<Axis>
{
    return m_axis;
}

auto Jolt_tapered_capsule_shape::get_length() const -> std::optional<float>
{
    return m_length;
}

//

Jolt_tapered_cylinder_shape::Jolt_tapered_cylinder_shape(const Axis axis, const float bottom_radius, const float top_radius, const float length)
    : m_axis         {axis}
    , m_bottom_radius{bottom_radius}
    , m_top_radius   {top_radius}
    , m_length       {length}
{
    // Jolt centers the shape around the origin with the bottom cap disc at
    // (0, -half_height, 0) and the top cap disc at (0, +half_height, 0).
    // length is the full axial height; a cone is a tapered cylinder with one
    // radius ~0 (Jolt clamps the convex radius to the smaller radius internally).
    m_tapered_cylinder_shape_settings = new JPH::TaperedCylinderShapeSettings(length * 0.5f, top_radius, bottom_radius);
    const JPH::Vec3 x_axis{1.0f, 0.0f, 0.0f};
    const JPH::Vec3 y_axis{0.0f, 1.0f, 0.0f};
    const JPH::Vec3 z_axis{0.0f, 0.0f, 1.0f};
    m_shape_settings = new JPH::RotatedTranslatedShapeSettings{
        JPH::Vec3{0.0f, 0.0f, 0.0f},
        (axis == Axis::Y)
            ? JPH::Quat::sIdentity()
            : (axis == Axis::X)
                ? JPH::Quat::sFromTo(y_axis, x_axis)
                : JPH::Quat::sFromTo(y_axis, z_axis),
        m_tapered_cylinder_shape_settings
    };
    auto result = m_shape_settings->Create();
    ERHE_VERIFY(result.IsValid());
    m_jolt_shape = result.Get();
}

Jolt_tapered_cylinder_shape::~Jolt_tapered_cylinder_shape() noexcept = default;

auto Jolt_tapered_cylinder_shape::get_shape_settings() -> JPH::ShapeSettings&
{
    return *m_shape_settings.GetPtr();
}

auto Jolt_tapered_cylinder_shape::describe() const -> std::string
{
    return "Jolt_tapered_cylinder_shape";
}

auto Jolt_tapered_cylinder_shape::get_shape_type() const -> Collision_shape_type
{
    return Collision_shape_type::e_tapered_cylinder;
}

auto Jolt_tapered_cylinder_shape::get_bottom_radius() const -> std::optional<float>
{
    return m_bottom_radius;
}

auto Jolt_tapered_cylinder_shape::get_top_radius() const -> std::optional<float>
{
    return m_top_radius;
}

auto Jolt_tapered_cylinder_shape::get_axis() const -> std::optional<Axis>
{
    return m_axis;
}

auto Jolt_tapered_cylinder_shape::get_length() const -> std::optional<float>
{
    return m_length;
}

//

Jolt_cylinder_shape::Jolt_cylinder_shape(const Axis axis, const glm::vec3 half_extents)
    : m_axis        {axis}
    , m_half_extents{half_extents}
{
    m_cylinder_shape_settings = new JPH::CylinderShapeSettings(half_extents.x, half_extents.y);
    const JPH::Vec3 x_axis{1.0f, 0.0f, 0.0f};
    const JPH::Vec3 y_axis{0.0f, 1.0f, 0.0f};
    const JPH::Vec3 z_axis{0.0f, 0.0f, 1.0f};
    m_shape_settings = new JPH::RotatedTranslatedShapeSettings(
        JPH::Vec3{0.0f, 0.0f, 0.0f},
        (axis == Axis::Y)
            ? JPH::Quat::sIdentity()
            : (axis == Axis::X)
                ? JPH::Quat::sFromTo(y_axis, x_axis)
                : JPH::Quat::sFromTo(y_axis, z_axis),
        m_cylinder_shape_settings
    );
    auto result = m_shape_settings->Create();
    ERHE_VERIFY(result.IsValid());
    m_jolt_shape = result.Get();
}

Jolt_cylinder_shape::~Jolt_cylinder_shape() noexcept = default;

auto Jolt_cylinder_shape::get_shape_settings() -> JPH::ShapeSettings&
{
    return *m_shape_settings.GetPtr();
}

auto Jolt_cylinder_shape::describe() const -> std::string
{
    return "Jolt_cylinder_shape";
}

auto Jolt_cylinder_shape::get_shape_type() const -> Collision_shape_type
{
    return Collision_shape_type::e_cylinder;
}

auto Jolt_cylinder_shape::get_half_extents() const -> std::optional<glm::vec3>
{
    return m_half_extents;
}

auto Jolt_cylinder_shape::get_axis() const -> std::optional<Axis>
{
    return m_axis;
}

//

Jolt_sphere_shape::Jolt_sphere_shape(const float radius)
    : m_radius{radius}
{
    m_shape_settings = new JPH::SphereShapeSettings(radius);
    auto result = m_shape_settings->Create();
    ERHE_VERIFY(result.IsValid());
    m_jolt_shape = result.Get();
}

Jolt_sphere_shape::~Jolt_sphere_shape() noexcept = default;

auto Jolt_sphere_shape::get_shape_settings() -> JPH::ShapeSettings&
{
    return *m_shape_settings.GetPtr();
}

auto Jolt_sphere_shape::describe() const -> std::string
{
    return "Jolt_sphere_shape";
}

auto Jolt_sphere_shape::get_shape_type() const -> Collision_shape_type
{
    return Collision_shape_type::e_sphere;
}

auto Jolt_sphere_shape::get_radius() const -> std::optional<float>
{
    return m_radius;
}

namespace {

// Jolt collides no triangle surface (mesh, height field) with another (CollisionDispatch).
[[nodiscard]] auto has_triangle_surface(const JPH::Shape& shape) -> bool
{
    switch (shape.GetType()) {
        case JPH::EShapeType::Mesh:
        case JPH::EShapeType::HeightField: {
            return true;
        }
        case JPH::EShapeType::Decorated: {
            return has_triangle_surface(*static_cast<const JPH::DecoratedShape&>(shape).GetInnerShape());
        }
        case JPH::EShapeType::Compound: {
            for (const JPH::CompoundShape::SubShape& sub_shape : static_cast<const JPH::CompoundShape&>(shape).GetSubShapes()) {
                if (has_triangle_surface(*sub_shape.mShape)) {
                    return true;
                }
            }
            return false;
        }
        default: {
            return false;
        }
    }
}

// The center of mass transform of the shape placed at its node-space transform.
[[nodiscard]] auto center_of_mass_transform(const JPH::Shape& shape, const Transform& transform) -> JPH::Mat44
{
    glm::mat4 world{transform.basis};
    world[3] = glm::vec4{transform.origin, 1.0f};
    world[3] = world * glm::vec4{from_jolt(shape.GetCenterOfMass()), 1.0f};
    return to_jolt(world);
}

} // anonymous namespace

auto ICollision_shape::get_penetration_depth(
    const ICollision_shape& shape_a, const Transform& transform_a,
    const ICollision_shape& shape_b, const Transform& transform_b
) -> std::optional<float>
{
    const JPH::ShapeRefC jolt_a = static_cast<const Jolt_collision_shape&>(shape_a).get_jolt_shape();
    const JPH::ShapeRefC jolt_b = static_cast<const Jolt_collision_shape&>(shape_b).get_jolt_shape();
    if ((jolt_a == nullptr) || (jolt_b == nullptr)) {
        return std::nullopt;
    }
    if (has_triangle_surface(*jolt_a) && has_triangle_surface(*jolt_b)) {
        return std::nullopt;
    }
    JPH::CollideShapeSettings settings{};
    settings.mMaxSeparationDistance = 0.0f; // only actual overlaps (depth >= 0)

    // ClosestHit keeps the contact of the greatest penetration depth.
    JPH::ClosestHitCollisionCollector<JPH::CollideShapeCollector> collector;
    JPH::CollisionDispatch::sCollideShapeVsShape(
        jolt_a.GetPtr(),
        jolt_b.GetPtr(),
        JPH::Vec3::sReplicate(1.0f),
        JPH::Vec3::sReplicate(1.0f),
        center_of_mass_transform(*jolt_a, transform_a),
        center_of_mass_transform(*jolt_b, transform_b),
        JPH::SubShapeIDCreator{},
        JPH::SubShapeIDCreator{},
        settings,
        collector
    );
    return collector.HadHit() ? std::max(collector.mHit.mPenetrationDepth, 0.0f) : 0.0f;
}

} // namespace erhe::physics
