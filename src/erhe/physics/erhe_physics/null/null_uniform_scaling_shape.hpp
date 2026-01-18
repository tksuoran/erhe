#pragma once

#include "erhe_physics/null/null_collision_shape.hpp"

#include <glm/glm.hpp>

namespace erhe::physics {

class Null_uniform_scaling_shape : public Null_collision_shape
{
public:
    Null_uniform_scaling_shape(ICollision_shape* shape, const float scale)
        //: m_shape{shape}
        //, m_scale{scale}
    {
        static_cast<void>(shape);
        static_cast<void>(scale);
    }

    void calculate_local_inertia(float mass, glm::mat4& inertia) const override;
    auto is_convex              () const -> bool                       override;
    auto get_center_of_mass     () const -> glm::vec3                  override;
    auto get_mass_properties    () const -> Mass_properties            override;

private:
    //ICollision_shape* m_shape{nullptr};
    //float             m_scale{1.0f};
};

} // namespace erhe::physics
