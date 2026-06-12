#pragma once

#include "erhe_physics/jolt/jolt_collision_shape.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>

namespace erhe::physics {

// Wrapper that shifts the center of mass of the wrapped shape by offset
class Jolt_offset_center_of_mass_shape : public Jolt_collision_shape
{
public:
    Jolt_offset_center_of_mass_shape(const std::shared_ptr<ICollision_shape>& shape, glm::vec3 offset);

    // Implements ICollision_shape
    [[nodiscard]] auto is_convex         () const -> bool                              override;
    [[nodiscard]] auto get_shape_settings() -> JPH::ShapeSettings&                     override;
    [[nodiscard]] auto describe          () const -> std::string                       override;
    [[nodiscard]] auto get_shape_type    () const -> Collision_shape_type              override;
    [[nodiscard]] auto get_offset        () const -> std::optional<glm::vec3>          override;
    [[nodiscard]] auto get_inner_shape   () const -> std::shared_ptr<ICollision_shape> override;

private:
    JPH::Ref<JPH::OffsetCenterOfMassShapeSettings> m_shape_settings;
    std::shared_ptr<ICollision_shape>              m_inner_shape;
    glm::vec3                                      m_offset;
};

} // namespace erhe::physics
