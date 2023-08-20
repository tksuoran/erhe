#pragma once

#include "erhe_physics/iconstraint.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::physics
{

class IRigid_body;

class Null_point_to_point_constraint
    : public IConstraint
{
public:
    explicit Null_point_to_point_constraint(
        const Point_to_point_constraint_settings& settings
    )
        : m_settings{settings}
    {
    }

private:
    Point_to_point_constraint_settings m_settings;
};

} // namespace erhe::physics
