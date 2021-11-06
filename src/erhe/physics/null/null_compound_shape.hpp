#pragma once

#include "erhe/physics/null/null_collision_shape.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace erhe::physics
{

class Compound_child
{
public:
    Compound_child(
        std::shared_ptr<ICollision_shape> shape,
        const glm::mat3                   basis,
        const glm::vec3                   origin
    )
        : shape{shape}
        , basis{basis}
        , origin{origin}
    {
    }

    std::shared_ptr<ICollision_shape> shape;
    const glm::mat3                   basis;
    const glm::vec3                   origin;
};

class Null_compound_shape
    : public Null_collision_shape
{
public:
    // Implements ICollision_shape
    auto is_convex() const -> bool override
    {
        return false;
    }

    // Implements ICompound_shape
    void add_child_shape(
        std::shared_ptr<ICollision_shape> shape,
        const glm::mat3                   basis,
        const glm::vec3                   origin
    ) override
    {
        m_children.emplace_back(std::make_shared<Compound_child>(shape, basis, origin));
    }

private:
    std::vector<std::shared_ptr<Compound_child>> m_children;
};

} // namespace erhe::physics
