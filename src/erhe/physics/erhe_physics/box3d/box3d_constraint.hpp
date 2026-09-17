#pragma once

#include "erhe_physics/iconstraint.hpp"

#include <box3d/box3d.h>

namespace erhe::physics {

class Box3d_constraint : public IConstraint
{
public:
    ~Box3d_constraint() noexcept override;

    [[nodiscard]] auto get_box3d_joint() const -> b3JointId { return m_joint; }
    [[nodiscard]] auto is_valid       () const -> bool      { return m_is_valid; }

protected:
    b3JointId m_joint   {};
    bool      m_is_valid{false};
};

} // namespace erhe::physics
