#include "erhe_physics/jolt/jolt_scaled_shape.hpp"
#include "erhe_physics/jolt/glm_conversions.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::physics {

auto ICollision_shape::create_scaled_shape(const std::shared_ptr<ICollision_shape>& shape, const glm::vec3 scale) -> ICollision_shape*
{
    return new Jolt_scaled_shape(shape, scale);
}

auto ICollision_shape::create_scaled_shape_shared(const std::shared_ptr<ICollision_shape>& shape, const glm::vec3 scale) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_scaled_shape>(shape, scale);
}

Jolt_scaled_shape::Jolt_scaled_shape(const std::shared_ptr<ICollision_shape>& shape, const glm::vec3 scale)
    : m_inner_shape{shape}
    , m_scale      {scale}
{
    Jolt_collision_shape* jolt_shape = static_cast<Jolt_collision_shape*>(shape.get());
    m_shape_settings = new JPH::ScaledShapeSettings(
        &jolt_shape->get_shape_settings(),
        to_jolt(scale)
    );
    auto result = m_shape_settings->Create();
    ERHE_VERIFY(result.IsValid());
    m_jolt_shape = result.Get();
}

auto Jolt_scaled_shape::is_convex() const -> bool
{
    return m_inner_shape->is_convex();
}

auto Jolt_scaled_shape::get_shape_settings() -> JPH::ShapeSettings&
{
    return *m_shape_settings.GetPtr();
}

auto Jolt_scaled_shape::describe() const -> std::string
{
    return "Jolt_scaled_shape";
}

auto Jolt_scaled_shape::get_shape_type() const -> Collision_shape_type
{
    return Collision_shape_type::e_scaled;
}

auto Jolt_scaled_shape::get_scale() const -> std::optional<glm::vec3>
{
    return m_scale;
}

auto Jolt_scaled_shape::get_inner_shape() const -> std::shared_ptr<ICollision_shape>
{
    return m_inner_shape;
}

} // namespace erhe::physics
