#include "erhe/physics/jolt/jolt_collision_shape.hpp"

namespace erhe::physics
{

class EmptyShapeSettings final : public JPH::ShapeSettings
{
public:
    //JPH_DECLARE_SERIALIZABLE_VIRTUAL(EmptyShapeSettings)

    EmptyShapeSettings();

    // See: ShapeSettings
    JPH::ShapeSettings::ShapeResult Create() const override;
};

class EmptyShape final : public JPH::Shape
{
public:
    //JPH_OVERRIDE_NEW_DELETE
    //void* operator new     (std::size_t inCount)                               { return JPH::Allocate(inCount); }
    //void  operator delete  (void* inPointer) noexcept                          { JPH::Free(inPointer); }
    //void* operator new[]   (std::size_t inCount)                               { return JPH::Allocate(inCount); }
    //void  operator delete[](void* inPointer) noexcept                          { JPH::Free(inPointer); }
    //void* operator new     (std::size_t inCount, std::align_val_t inAlignment) { return JPH::AlignedAllocate(inCount, static_cast<size_t>(inAlignment)); }
    //void  operator delete  (void* inPointer, std::align_val_t) noexcept        { JPH::AlignedFree(inPointer); }
    //void* operator new   [](std::size_t inCount, std::align_val_t inAlignment) { return JPH::AlignedAllocate(inCount, static_cast<size_t>(inAlignment)); }
    //void  operator delete[](void* inPointer, std::align_val_t) noexcept        { JPH::AlignedFree(inPointer); }

    EmptyShape()
        : JPH::Shape{JPH::EShapeType::User1, JPH::EShapeSubType::User1}
    {
    }

    EmptyShape(
        const JPH::ShapeSettings& inSettings,
        ShapeResult&              outResult
    )
        : JPH::Shape{JPH::EShapeType::User1, JPH::EShapeSubType::User1, inSettings, outResult}
    {
        outResult.Set(this);
    }

    JPH::AABox GetLocalBounds() const override
    {
        return JPH::AABox{JPH::Vec3{0.0f, 0.0f, 0.0f}, 0.0f};
    }

    JPH::uint GetSubShapeIDBitsRecursive() const override
    {
        return 0;
    }

    JPH::AABox GetWorldSpaceBounds(JPH::Mat44Arg inCenterOfMassTransform, JPH::Vec3Arg inScale) const override
    {
        static_cast<void>(inCenterOfMassTransform);
        static_cast<void>(inScale);
        return JPH::AABox{};
    }

    float GetInnerRadius() const override
    {
        return 0.0f;
    }

    JPH::MassProperties GetMassProperties() const override
    {
        JPH::MassProperties p;

        p.mMass = 1.0f;
        p.mInertia = JPH::Mat44::sScale(p.mMass);

        return p;
    }

    const JPH::PhysicsMaterial* GetMaterial(const JPH::SubShapeID& inSubShapeID) const override
    {
        static_cast<void>(inSubShapeID);
        return nullptr;
    }

    JPH::Vec3 GetSurfaceNormal(
        const JPH::SubShapeID& inSubShapeID,
        JPH::Vec3Arg           inLocalSurfacePosition
    ) const override
    {
        static_cast<void>(inSubShapeID);
        static_cast<void>(inLocalSurfacePosition);
        return {};
    }

	void GetSubmergedVolume(
        JPH::Mat44Arg     inCenterOfMassTransform,
        JPH::Vec3Arg      inScale,
        const JPH::Plane& inSurface,
        float&            outTotalVolume,
        float&            outSubmergedVolume,
        JPH::Vec3&        outCenterOfBuoyancy
#ifdef JPH_DEBUG_RENDERER // Not using JPH_IF_DEBUG_RENDERER for Doxygen
		, JPH::RVec3Arg   inBaseOffset
#endif
    ) const override
    {
        static_cast<void>(inCenterOfMassTransform);
        static_cast<void>(inScale);
        static_cast<void>(inSurface);
#ifdef JPH_DEBUG_RENDERER // Not using JPH_IF_DEBUG_RENDERER for Doxygen
		static_cast<void>(inBaseOffset);
#endif
        outTotalVolume = 0.0f;
        outSubmergedVolume = 0.0f;
        outCenterOfBuoyancy = JPH::Vec3{0.0f, 0.0f, 0.0f};
    }

#ifdef JPH_DEBUG_RENDERER
    // See Shape::Draw
    void Draw(
        JPH::DebugRenderer* inRenderer,
        JPH::Mat44Arg       inCenterOfMassTransform,
        JPH::Vec3Arg        inScale,
        JPH::ColorArg       inColor,
        const bool          inUseMaterialColors,
        const bool          inDrawWireframe
    ) const override
    {
static_cast<void>(inRenderer);
        static_cast<void>(inCenterOfMassTransform);
        static_cast<void>(inScale);
        static_cast<void>(inColor);
        static_cast<void>(inUseMaterialColors);
        static_cast<void>(inDrawWireframe);
    }
#endif // JPH_DEBUG_RENDERER

    // See Shape::CastRay
    bool CastRay(
        const JPH::RayCast&           inRay,
        const JPH::SubShapeIDCreator& inSubShapeIDCreator,
        JPH::RayCastResult&           ioHit
    ) const override
    {
        static_cast<void>(inRay);
        static_cast<void>(inSubShapeIDCreator);
        static_cast<void>(ioHit);
        return false;
    }
    void CastRay(
        const JPH::RayCast&           inRay,
        const JPH::RayCastSettings&   inRayCastSettings,
        const JPH::SubShapeIDCreator& inSubShapeIDCreator,
        JPH::CastRayCollector&        ioCollector,
        const JPH::ShapeFilter&       inShapeFilter = {}
    ) const override
    {
        static_cast<void>(inRay);
        static_cast<void>(inRayCastSettings);
        static_cast<void>(inSubShapeIDCreator);
        static_cast<void>(ioCollector);
        static_cast<void>(inShapeFilter);
    }

    void CollidePoint(
        JPH::Vec3Arg                  inPoint,
        const JPH::SubShapeIDCreator& inSubShapeIDCreator,
        JPH::CollidePointCollector&   ioCollector,
        const JPH::ShapeFilter&       inShapeFilter = {}
    ) const override
    {
        static_cast<void>(inPoint);
        static_cast<void>(inSubShapeIDCreator);
        static_cast<void>(ioCollector);
        static_cast<void>(inShapeFilter);
    }

    void GetTrianglesStart(
        GetTrianglesContext& ioContext,
        const JPH::AABox&    inBox,
        JPH::Vec3Arg         inPositionCOM,
        JPH::QuatArg         inRotation,
        JPH::Vec3Arg         inScale
    ) const override
    {
        static_cast<void>(ioContext);
        static_cast<void>(inBox);
        static_cast<void>(inPositionCOM);
        static_cast<void>(inRotation);
        static_cast<void>(inScale);
    }

    int GetTrianglesNext(
        GetTrianglesContext&         ioContext,
        int                          inMaxTrianglesRequested,
        JPH::Float3*                 outTriangleVertices,
        const JPH::PhysicsMaterial** outMaterials = nullptr
    ) const override
    {
        static_cast<void>(ioContext);
        static_cast<void>(inMaxTrianglesRequested);
        static_cast<void>(outTriangleVertices);
        static_cast<void>(outMaterials);
        return 0;
    }

    Stats GetStats() const override
    {
        return Stats{0, 0};
    }

    float GetVolume() const override
    {
        return 0.0f;
    }
};

EmptyShapeSettings::EmptyShapeSettings() = default;

void register_empty_shape()
{
    JPH::ShapeFunctions& f = JPH::ShapeFunctions::sGet(JPH::EShapeSubType::User1);
    f.mConstruct = []() -> JPH::Shape * { return new EmptyShape; };
    f.mColor = JPH::Color::sOrange;
}

// See: ShapeSettings
JPH::ShapeSettings::ShapeResult EmptyShapeSettings::Create() const
{
    if (mCachedResult.IsEmpty()) {
        static_cast<void>(
            new EmptyShape(*this, mCachedResult)
        );
    }
    return mCachedResult;
}

class Jolt_empty_shape
    : public Jolt_collision_shape
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
    EmptyShapeSettings m_shape_settings;
};

auto ICollision_shape::create_empty_shape()
-> ICollision_shape*
{
    return new Jolt_empty_shape();
}

auto ICollision_shape::create_empty_shape_shared()
-> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_empty_shape>();
}

auto ICollision_shape::create_box_shape(const glm::vec3 half_extents)
    -> ICollision_shape*
{
    return new Jolt_box_shape(half_extents);
}

auto ICollision_shape::create_box_shape_shared(const glm::vec3 half_extents)
    -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_box_shape>(half_extents);
}

auto ICollision_shape::create_capsule_shape(
    const Axis  axis,
    const float radius,
    const float length
) -> ICollision_shape*
{
    return new Jolt_capsule_shape(axis, radius, length);
}

auto ICollision_shape::create_capsule_shape_shared(
    const Axis  axis,
    const float radius,
    const float length
) -> std::shared_ptr<ICollision_shape>
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

auto ICollision_shape::create_cylinder_shape(
    const Axis      axis,
    const glm::vec3 half_extents
) -> ICollision_shape*
{
    return new Jolt_cylinder_shape(axis, half_extents);
}

auto ICollision_shape::create_cylinder_shape_shared(
    const Axis      axis,
    const glm::vec3 half_extents
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_cylinder_shape>(axis, half_extents);
}

auto ICollision_shape::create_sphere_shape(const float radius)
    -> ICollision_shape*
{
    return new Jolt_sphere_shape(radius);
}

auto ICollision_shape::create_sphere_shape_shared(const float radius)
    -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_sphere_shape>(radius);
}

void Jolt_collision_shape::calculate_local_inertia(
    const float mass,
    glm::mat4&  inertia
) const
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
