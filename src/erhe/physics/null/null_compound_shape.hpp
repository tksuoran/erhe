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
        const std::shared_ptr<ICollision_shape>& shape,
        const Transform                          transform
    )
        : shape    {shape}
        , transform{transform}
    {
    }

    std::shared_ptr<ICollision_shape> shape;
    const Transform                   transform;
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
        const std::shared_ptr<ICollision_shape>& shape,
        const Transform                          transform
    ) override
    {
        m_children.emplace_back(std::make_shared<Compound_child>(shape, transform));
    }

private:
    std::vector<std::shared_ptr<Compound_child>> m_children;
};

} // namespace erhe::physics
