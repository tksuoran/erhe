#pragma once

#include "erhe_physics/null/null_collision_shape.hpp"

namespace erhe::physics {

class Null_compound_shape : public Null_collision_shape
{
public:
    explicit Null_compound_shape(const Compound_shape_create_info& create_info)
        : m_children{create_info.children}
    {
    }

    // Implements ICollision_shape
    auto is_convex     () const -> bool                              override { return false; }
    auto get_shape_type() const -> Collision_shape_type              override { return Collision_shape_type::e_compound; }
    auto get_children  () const -> const std::vector<Compound_child>& override { return m_children; }

private:
    std::vector<Compound_child> m_children;
};

} // namespace erhe::physics
