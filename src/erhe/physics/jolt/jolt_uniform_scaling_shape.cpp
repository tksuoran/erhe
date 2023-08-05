#include "erhe/physics/jolt/jolt_uniform_scaling_shape.hpp"

namespace erhe::physics
{

auto ICollision_shape::create_uniform_scaling_shape(
    ICollision_shape* shape,
    const float       scale
) -> ICollision_shape*
{
    return new Jolt_uniform_scaling_shape(shape, scale);
}

auto ICollision_shape::create_uniform_scaling_shape_shared(
    ICollision_shape* shape,
    const float       scale
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_uniform_scaling_shape>(shape, scale);
}

Jolt_uniform_scaling_shape::Jolt_uniform_scaling_shape(ICollision_shape* const shape, const float scale)
{
    m_shape_settings = new JPH::ScaledShapeSettings(
        &(reinterpret_cast<Jolt_collision_shape*>(shape)->get_shape_settings()),
        JPH::Vec3Arg{scale, scale, scale}
    );
    auto result = m_shape_settings->Create();
    ERHE_VERIFY(result.IsValid());
    m_jolt_shape = result.Get();
}

auto Jolt_uniform_scaling_shape::get_shape_settings() -> JPH::ShapeSettings&
{
    return *m_shape_settings.GetPtr();
}

auto Jolt_uniform_scaling_shape::describe() const -> std::string
{
    return "Jolt_uniform_scaling_shape";
}

} // namespace erhe::physics
