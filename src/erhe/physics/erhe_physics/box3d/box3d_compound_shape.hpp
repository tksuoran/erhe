#pragma once

#include "erhe_physics/box3d/box3d_collision_shape.hpp"

#include <vector>

namespace erhe::physics {

// Box3D's baked compound shape (b3CreateBakedCompoundShape) is rejected for
// anything but a static, non-sensor body, so it cannot back erhe's compound
// shape. Instead each child is attached to the body as its own Box3D shape --
// which is what Box3D's own header recommends for runtime compounds.
class Box3d_compound_shape : public Box3d_collision_shape
{
public:
    explicit Box3d_compound_shape(const Compound_shape_create_info& create_info);
    ~Box3d_compound_shape() noexcept override = default;

    void attach_to_body(Shape_attach_context& context, const b3Transform& local_transform, const glm::vec3& scale) const override;

    [[nodiscard]] auto compute_mass_data(float density) const -> b3MassData          override;
    [[nodiscard]] auto contains_mesh    () const -> bool                             override;
    [[nodiscard]] auto is_convex        () const -> bool                             override;
    [[nodiscard]] auto get_shape_type   () const -> Collision_shape_type             override { return Collision_shape_type::e_compound; }
    [[nodiscard]] auto get_children     () const -> const std::vector<Compound_child>& override { return m_children; }
    [[nodiscard]] auto describe         () const -> std::string                      override;

private:
    std::vector<Compound_child> m_children;
};

} // namespace erhe::physics
