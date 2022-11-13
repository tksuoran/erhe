#include "erhe/physics/jolt/jolt_compound_shape.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::physics
{

auto ICollision_shape::create_compound_shape(
    const Compound_shape_create_info& create_info
) -> ICollision_shape*
{
    return new Jolt_compound_shape(create_info);
}

auto ICollision_shape::create_compound_shape_shared(
    const Compound_shape_create_info& create_info
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_compound_shape>(create_info);
}

namespace
{

auto make_static_compound_shape_settings(
    const Compound_shape_create_info& create_info
) -> JPH::StaticCompoundShapeSettings
{
    JPH::StaticCompoundShapeSettings shape_settings;
    for (const auto& entry : create_info.children)
    {
        std::shared_ptr<ICollision_shape>     icollision_shape = entry.shape;
        std::shared_ptr<Jolt_collision_shape> jolt_shape       = dynamic_pointer_cast<Jolt_collision_shape>(icollision_shape);
        JPH::ShapeRefC                        shape_ref        = jolt_shape->get_jolt_shape();
        const auto basis = glm::quat{entry.transform.basis};
        shape_settings.AddShape(
            to_jolt(entry.transform.origin),
            to_jolt(basis),
            shape_ref
        );
    }
    return shape_settings;
}

}

Jolt_compound_shape::Jolt_compound_shape(
    const Compound_shape_create_info& create_info
)
    : m_shape_settings{make_static_compound_shape_settings(create_info)}
{
    const auto result = m_shape_settings.Create();
    ERHE_VERIFY(result.IsValid());
    m_jolt_shape = result.Get();
}


} // namespace erhe::physics
