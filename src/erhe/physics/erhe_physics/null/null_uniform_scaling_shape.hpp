#pragma once

#include "erhe_physics/null/null_collision_shape.hpp"

#include <glm/glm.hpp>

namespace erhe::physics {

class Null_uniform_scaling_shape : public Null_collision_shape
{
public:
    Null_uniform_scaling_shape(const std::shared_ptr<ICollision_shape>& shape, const float scale)
        : m_inner_shape{shape}
        , m_scale      {scale}
    {
    }

    void calculate_local_inertia(float mass, glm::mat4& inertia) const override;
    auto is_convex              () const -> bool                       override;
    auto get_center_of_mass     () const -> glm::vec3                  override;
    auto get_mass_properties    () const -> Mass_properties            override;
    auto get_shape_type         () const -> Collision_shape_type       override { return Collision_shape_type::e_uniform_scaling; }
    auto get_scale              () const -> std::optional<glm::vec3>   override { return glm::vec3{m_scale, m_scale, m_scale}; }
    auto get_inner_shape        () const -> std::shared_ptr<ICollision_shape> override { return m_inner_shape; }

private:
    std::shared_ptr<ICollision_shape> m_inner_shape;
    float                             m_scale;
};

} // namespace erhe::physics
