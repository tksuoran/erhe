#pragma once

#include "erhe_physics/jolt/jolt_collision_shape.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

namespace erhe::physics {

class Jolt_compound_shape : public Jolt_collision_shape
{
public:
    explicit Jolt_compound_shape(const Compound_shape_create_info& create_info);

    // Implements ICollision_shape
    [[nodiscard]] auto is_convex         () const -> bool                             override;
    [[nodiscard]] auto get_shape_settings() -> JPH::ShapeSettings&                    override;
    [[nodiscard]] auto describe          () const -> std::string                      override;
    [[nodiscard]] auto get_shape_type    () const -> Collision_shape_type             override;
    [[nodiscard]] auto get_children      () const -> const std::vector<Compound_child>& override;

private:
    JPH::Ref<JPH::StaticCompoundShapeSettings> m_shape_settings;
    std::vector<Compound_child>                m_children;
};

} // namespace erhe::physics
