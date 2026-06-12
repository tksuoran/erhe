#pragma once

#include "erhe_physics/null/null_collision_shape.hpp"

namespace erhe::physics {

// Wrapper that applies non-uniform scale to the wrapped shape
class Null_scaled_shape : public Null_collision_shape
{
public:
    Null_scaled_shape(const std::shared_ptr<ICollision_shape>& shape, const glm::vec3 scale)
        : m_inner_shape{shape}
        , m_scale      {scale}
    {
    }

    ~Null_scaled_shape() noexcept override = default;

    [[nodiscard]] auto is_convex      () const -> bool                              override { return m_inner_shape->is_convex(); }
    [[nodiscard]] auto get_shape_type () const -> Collision_shape_type              override { return Collision_shape_type::e_scaled; }
    [[nodiscard]] auto get_scale      () const -> std::optional<glm::vec3>          override { return m_scale; }
    [[nodiscard]] auto get_inner_shape() const -> std::shared_ptr<ICollision_shape> override { return m_inner_shape; }

private:
    std::shared_ptr<ICollision_shape> m_inner_shape;
    glm::vec3                         m_scale;
};

} // namespace erhe::physics
