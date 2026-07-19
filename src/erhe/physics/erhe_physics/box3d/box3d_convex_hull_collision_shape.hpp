#pragma once

#include "erhe_physics/box3d/box3d_collision_shape.hpp"
#include "erhe_physics/box3d/box3d_hull_builder.hpp"

namespace erhe::physics {

class Box3d_convex_hull_collision_shape : public Box3d_collision_shape
{
public:
    Box3d_convex_hull_collision_shape(const float* points, int point_count, int stride);
    ~Box3d_convex_hull_collision_shape() noexcept override = default;

    void attach_to_body(Shape_attach_context& context, const b3Transform& local_transform, const glm::vec3& scale) const override;

    [[nodiscard]] auto compute_mass_data(float density) const -> b3MassData override;
    [[nodiscard]] auto get_shape_type   () const -> Collision_shape_type    override { return Collision_shape_type::e_convex_hull; }
    [[nodiscard]] auto describe         () const -> std::string             override;

private:
    int        m_source_point_count{0};
    Box3d_hull m_hull;
};

} // namespace erhe::physics
