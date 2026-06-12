#pragma once

#include "erhe_physics/iconstraint.hpp"

namespace erhe::physics {

class IRigid_body;

class Null_point_to_point_constraint : public IConstraint
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

class Null_six_dof_constraint : public IConstraint
{
public:
    explicit Null_six_dof_constraint(
        const Six_dof_constraint_settings& settings
    )
        : m_settings{settings}
    {
    }

private:
    Six_dof_constraint_settings m_settings;
};

} // namespace erhe::physics
