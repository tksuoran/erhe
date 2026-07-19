#pragma once

#include "erhe_physics/box3d/box3d_collision_shape.hpp"

#include <cstdint>

namespace erhe::physics {

// Triangle mesh shape. Usable with static / kinematic bodies only: Box3D has no
// b3ComputeMeshMass (only sphere, capsule and hull mass functions exist), so a
// mesh contributes no mass and cannot drive a dynamic body's inertia. The world
// reports and downgrades any dynamic body whose shape tree contains one.
class Box3d_mesh_shape : public Box3d_collision_shape
{
public:
    Box3d_mesh_shape(
        const float*    points,
        int             point_count,
        int             point_stride_bytes,
        const uint32_t* indices,
        int             triangle_count
    );
    ~Box3d_mesh_shape() noexcept override;

    Box3d_mesh_shape           (const Box3d_mesh_shape&) = delete;
    Box3d_mesh_shape& operator=(const Box3d_mesh_shape&) = delete;

    void attach_to_body(Shape_attach_context& context, const b3Transform& local_transform, const glm::vec3& scale) const override;

    [[nodiscard]] auto is_convex     () const -> bool                 override;
    [[nodiscard]] auto contains_mesh () const -> bool                 override;
    [[nodiscard]] auto get_shape_type() const -> Collision_shape_type override { return Collision_shape_type::e_mesh; }
    [[nodiscard]] auto describe      () const -> std::string          override;

private:
    int         m_point_count   {0};
    int         m_triangle_count{0};
    b3MeshData* m_mesh          {nullptr};
};

} // namespace erhe::physics
