#pragma once

#include "erhe_physics/null/null_collision_shape.hpp"

namespace erhe::physics {

// Wrapper that shifts the center of mass of the wrapped shape by offset
class Null_offset_center_of_mass_shape : public Null_collision_shape
{
public:
    Null_offset_center_of_mass_shape(const std::shared_ptr<ICollision_shape>& shape, const glm::vec3 offset)
        : m_inner_shape{shape}
        , m_offset     {offset}
    {
    }

    ~Null_offset_center_of_mass_shape() noexcept override = default;

    [[nodiscard]] auto is_convex      () const -> bool                              override { return m_inner_shape->is_convex(); }
    [[nodiscard]] auto get_shape_type () const -> Collision_shape_type              override { return Collision_shape_type::e_offset_center_of_mass; }
    [[nodiscard]] auto get_offset     () const -> std::optional<glm::vec3>          override { return m_offset; }
    [[nodiscard]] auto get_inner_shape() const -> std::shared_ptr<ICollision_shape> override { return m_inner_shape; }

private:
    std::shared_ptr<ICollision_shape> m_inner_shape;
    glm::vec3                         m_offset;
};

} // namespace erhe::physics
