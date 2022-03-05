#pragma once

#include "erhe/physics/bullet/bullet_collision_shape.hpp"

#include <BulletCollision/CollisionShapes/btCompoundShape.h>
#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace erhe::physics
{


class Bullet_compound_shape
    : public Bullet_collision_shape
{
public:
    Bullet_compound_shape(const Compound_shape_create_info& create_info);
    ~Bullet_compound_shape() override;

    // Implements ICollision_shape
    [[nodiscard]] auto is_convex() const -> bool override;

    void calculate_principal_axis_transform(
        const std::vector<float>& child_masses,
        Transform&                principal_transform,
        glm::vec3&                inertia
    ) override;

private:
    btCompoundShape                                m_compound_shape;
    std::vector<std::shared_ptr<ICollision_shape>> m_children;
};

} // namespace erhe::physics
