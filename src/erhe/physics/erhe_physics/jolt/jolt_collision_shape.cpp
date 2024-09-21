#include "erhe_physics/jolt/jolt_collision_shape.hpp"
#include "erhe_physics/jolt/glm_conversions.hpp"
#include "erhe_verify/verify.hpp"

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

auto Jolt_collision_shape::get_jolt_shape() -> JPH::ShapeRefC
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

//

Jolt_capsule_shape::Jolt_capsule_shape(const Axis axis, const float radius, const float length)
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

//

Jolt_cylinder_shape::Jolt_cylinder_shape(const Axis axis, const glm::vec3 half_extents)
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

//

Jolt_sphere_shape::Jolt_sphere_shape(const float radius)
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

} // namespace erhe::physics
