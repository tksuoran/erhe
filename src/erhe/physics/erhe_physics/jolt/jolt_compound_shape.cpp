#include "erhe_physics/jolt/jolt_compound_shape.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::physics {

auto ICollision_shape::create_compound_shape(const Compound_shape_create_info& create_info) -> ICollision_shape*
{
    return new Jolt_compound_shape(create_info);
}

auto ICollision_shape::create_compound_shape_shared(const Compound_shape_create_info& create_info) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_compound_shape>(create_info);
}

Jolt_compound_shape::Jolt_compound_shape(const Compound_shape_create_info& create_info)
{
    m_shape_settings = new JPH::StaticCompoundShapeSettings();
    for (const auto& entry : create_info.children) {
        std::shared_ptr<ICollision_shape>     icollision_shape = entry.shape;
        std::shared_ptr<Jolt_collision_shape> jolt_shape       = std::static_pointer_cast<Jolt_collision_shape>(icollision_shape);
        JPH::ShapeRefC                        shape_ref        = jolt_shape->get_jolt_shape();
        const auto basis = glm::quat{entry.transform.basis};
        m_shape_settings->AddShape(
            to_jolt(entry.transform.origin),
            to_jolt(basis),
            shape_ref
        );
    }
    const auto result = m_shape_settings->Create();
    ERHE_VERIFY(result.IsValid());
    m_jolt_shape = result.Get();
}

auto Jolt_compound_shape::is_convex() const -> bool
{
    return false;
}

auto Jolt_compound_shape::get_shape_settings() -> JPH::ShapeSettings&
{
    return *m_shape_settings.GetPtr();
}

auto Jolt_compound_shape::describe() const -> std::string
{
    return "Jolt_compound_shape";
}

} // namespace erhe::physics
