#pragma once

#include "erhe_physics/box3d/box3d_collision_shape.hpp"

#include <memory>

namespace erhe::physics {

// The three wrapper shapes. None of them materializes a Box3D shape of its own:
// b3CreateTransformedHullShape and b3CreateMeshShape both take a scale, and
// spheres and capsules bake their placement into their geometry, so a scale
// wrapper folds into the recursion. An offset-center-of-mass wrapper has no
// geometric effect at all -- it is reported to the body, which adjusts its mass
// data after all shapes have been attached.

class Box3d_scaled_shape : public Box3d_collision_shape
{
public:
    Box3d_scaled_shape(const std::shared_ptr<ICollision_shape>& shape, glm::vec3 scale);
    ~Box3d_scaled_shape() noexcept override = default;

    void attach_to_body(Shape_attach_context& context, const b3Transform& local_transform, const glm::vec3& scale) const override;

    [[nodiscard]] auto compute_mass_data(float density) const -> b3MassData        override;
    [[nodiscard]] auto contains_mesh    () const -> bool                           override;
    [[nodiscard]] auto is_convex        () const -> bool                           override;
    [[nodiscard]] auto get_shape_type   () const -> Collision_shape_type           override { return Collision_shape_type::e_scaled; }
    [[nodiscard]] auto get_scale        () const -> std::optional<glm::vec3>       override { return m_scale; }
    [[nodiscard]] auto get_inner_shape  () const -> std::shared_ptr<ICollision_shape> override { return m_shape; }
    [[nodiscard]] auto describe         () const -> std::string                    override;

private:
    std::shared_ptr<ICollision_shape> m_shape;
    glm::vec3                         m_scale;
};

class Box3d_uniform_scaling_shape : public Box3d_collision_shape
{
public:
    Box3d_uniform_scaling_shape(const std::shared_ptr<ICollision_shape>& shape, float scale);
    ~Box3d_uniform_scaling_shape() noexcept override = default;

    void attach_to_body(Shape_attach_context& context, const b3Transform& local_transform, const glm::vec3& scale) const override;

    [[nodiscard]] auto compute_mass_data(float density) const -> b3MassData        override;
    [[nodiscard]] auto contains_mesh    () const -> bool                           override;
    [[nodiscard]] auto is_convex        () const -> bool                           override;
    [[nodiscard]] auto get_shape_type   () const -> Collision_shape_type           override { return Collision_shape_type::e_uniform_scaling; }
    [[nodiscard]] auto get_scale        () const -> std::optional<glm::vec3>       override { return glm::vec3{m_scale}; }
    [[nodiscard]] auto get_inner_shape  () const -> std::shared_ptr<ICollision_shape> override { return m_shape; }
    [[nodiscard]] auto describe         () const -> std::string                    override;

private:
    std::shared_ptr<ICollision_shape> m_shape;
    float                             m_scale;
};

class Box3d_offset_center_of_mass_shape : public Box3d_collision_shape
{
public:
    Box3d_offset_center_of_mass_shape(const std::shared_ptr<ICollision_shape>& shape, glm::vec3 offset);
    ~Box3d_offset_center_of_mass_shape() noexcept override = default;

    void attach_to_body(Shape_attach_context& context, const b3Transform& local_transform, const glm::vec3& scale) const override;

    [[nodiscard]] auto compute_mass_data(float density) const -> b3MassData        override;
    [[nodiscard]] auto contains_mesh    () const -> bool                           override;
    [[nodiscard]] auto is_convex        () const -> bool                           override;
    [[nodiscard]] auto get_shape_type   () const -> Collision_shape_type           override { return Collision_shape_type::e_offset_center_of_mass; }
    [[nodiscard]] auto get_offset       () const -> std::optional<glm::vec3>       override { return m_offset; }
    [[nodiscard]] auto get_inner_shape  () const -> std::shared_ptr<ICollision_shape> override { return m_shape; }
    [[nodiscard]] auto describe         () const -> std::string                    override;

private:
    std::shared_ptr<ICollision_shape> m_shape;
    glm::vec3                         m_offset;
};

} // namespace erhe::physics
