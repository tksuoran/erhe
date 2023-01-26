#pragma once

#include "erhe/physics/jolt/jolt_collision_shape.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace erhe::physics
{

class Jolt_compound_shape
    : public Jolt_collision_shape
{
public:
    explicit Jolt_compound_shape(const Compound_shape_create_info& create_info);

    // Implements ICollision_shape
    auto is_convex() const -> bool override
    {
        return false;
    }

    auto get_shape_settings() -> JPH::ShapeSettings& override
    {
        return *m_shape_settings.GetPtr();
    }

private:
    JPH::Ref<JPH::StaticCompoundShapeSettings> m_shape_settings;
};

} // namespace erhe::physics
