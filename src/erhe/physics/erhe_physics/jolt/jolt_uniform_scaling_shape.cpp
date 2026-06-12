#include "erhe_physics/jolt/jolt_uniform_scaling_shape.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::physics {

auto ICollision_shape::create_uniform_scaling_shape(const std::shared_ptr<ICollision_shape>& shape, const float scale) -> ICollision_shape*
{
    return new Jolt_uniform_scaling_shape(shape, scale);
}

auto ICollision_shape::create_uniform_scaling_shape_shared(const std::shared_ptr<ICollision_shape>& shape, const float scale) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_uniform_scaling_shape>(shape, scale);
}

Jolt_uniform_scaling_shape::Jolt_uniform_scaling_shape(const std::shared_ptr<ICollision_shape>& shape, const float scale)
    : m_inner_shape{shape}
    , m_scale      {scale}
{
    m_shape_settings = new JPH::ScaledShapeSettings(
        &(static_cast<Jolt_collision_shape*>(shape.get())->get_shape_settings()),
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

auto Jolt_uniform_scaling_shape::get_shape_type() const -> Collision_shape_type
{
    return Collision_shape_type::e_uniform_scaling;
}

auto Jolt_uniform_scaling_shape::get_scale() const -> std::optional<glm::vec3>
{
    return glm::vec3{m_scale, m_scale, m_scale};
}

auto Jolt_uniform_scaling_shape::get_inner_shape() const -> std::shared_ptr<ICollision_shape>
{
    return m_inner_shape;
}

} // namespace erhe::physics
