#include "erhe_physics/jolt/jolt_offset_center_of_mass_shape.hpp"
#include "erhe_physics/jolt/glm_conversions.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::physics {

auto ICollision_shape::create_offset_center_of_mass_shape(const std::shared_ptr<ICollision_shape>& shape, const glm::vec3 offset) -> ICollision_shape*
{
    return new Jolt_offset_center_of_mass_shape(shape, offset);
}

auto ICollision_shape::create_offset_center_of_mass_shape_shared(const std::shared_ptr<ICollision_shape>& shape, const glm::vec3 offset) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_offset_center_of_mass_shape>(shape, offset);
}

Jolt_offset_center_of_mass_shape::Jolt_offset_center_of_mass_shape(const std::shared_ptr<ICollision_shape>& shape, const glm::vec3 offset)
    : m_inner_shape{shape}
    , m_offset     {offset}
{
    Jolt_collision_shape* jolt_shape = static_cast<Jolt_collision_shape*>(shape.get());
    m_shape_settings = new JPH::OffsetCenterOfMassShapeSettings(
        to_jolt(offset),
        &jolt_shape->get_shape_settings()
    );
    auto result = m_shape_settings->Create();
    ERHE_VERIFY(result.IsValid());
    m_jolt_shape = result.Get();
}

auto Jolt_offset_center_of_mass_shape::is_convex() const -> bool
{
    return m_inner_shape->is_convex();
}

auto Jolt_offset_center_of_mass_shape::get_shape_settings() -> JPH::ShapeSettings&
{
    return *m_shape_settings.GetPtr();
}

auto Jolt_offset_center_of_mass_shape::describe() const -> std::string
{
    return "Jolt_offset_center_of_mass_shape";
}

auto Jolt_offset_center_of_mass_shape::get_shape_type() const -> Collision_shape_type
{
    return Collision_shape_type::e_offset_center_of_mass;
}

auto Jolt_offset_center_of_mass_shape::get_offset() const -> std::optional<glm::vec3>
{
    return m_offset;
}

auto Jolt_offset_center_of_mass_shape::get_inner_shape() const -> std::shared_ptr<ICollision_shape>
{
    return m_inner_shape;
}

} // namespace erhe::physics
