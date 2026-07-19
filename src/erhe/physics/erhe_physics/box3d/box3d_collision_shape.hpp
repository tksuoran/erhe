#pragma once

#include "erhe_physics/box3d/box3d_hull_builder.hpp"
#include "erhe_physics/icollision_shape.hpp"

#include <glm/glm.hpp>

#include <box3d/box3d.h>

#include <vector>

namespace erhe::physics {

// Box3D has no standalone collision shape object: shapes are created onto a
// body (b3CreateSphereShape(bodyId, ...)), and its baked compound shape is
// rejected for anything but a static, non-sensor body (box3d src/shape.c). An
// erhe ICollision_shape, by contrast, is created before any body exists and is
// shared by every body that uses it.
//
// So a Box3d_collision_shape is a DESCRIPTOR: it eagerly owns whatever Box3D
// resource it can (a b3HullData, a b3MeshData) and materializes one or more
// Box3D shapes onto a body on demand via attach_to_body(). A compound shape
// simply attaches each of its children to the same body, which is exactly what
// Box3D's own documentation recommends for runtime compounds.

// One materialized convex primitive of a body, expressed in body local space.
// Collected during attach so the trial-placement overlap queries can run
// pairwise narrow-phase tests at a hypothetical transform without touching the
// world.
class Collision_primitive
{
public:
    enum class Kind : int {
        sphere,
        capsule,
        hull,
        mesh
    };

    Kind              kind     {Kind::sphere};
    b3Sphere          sphere   {};
    b3Capsule         capsule  {};
    const b3HullData* hull     {nullptr};
    const b3MeshData* mesh     {nullptr};
    b3Transform       transform{};            // shape local -> body local
    glm::vec3         scale    {1.0f};        // applied to the shape's own geometry before transform
};

// Everything attach_to_body() needs, and everything it reports back. The out
// vectors are owned by the rigid body being built.
class Shape_attach_context
{
public:
    b3BodyId                          body                     {};
    const b3ShapeDef*                 shape_def                {nullptr};
    std::vector<b3ShapeId>*           shape_ids                {nullptr};
    std::vector<Box3d_hull>*          derived_hulls            {nullptr};
    std::vector<Collision_primitive>* primitives               {nullptr};
    const char*                       debug_label              {"<unnamed>"};

    // Reported back to the body after all shapes have been attached.
    glm::vec3                         center_of_mass_offset    {0.0f};
    bool                              has_center_of_mass_offset{false};
    bool                              contains_mesh            {false};
};

// Composes a child's placement under a parent that itself carries a transform
// and a scale. Box3D applies scale before rotation (p' = R * (S * p) + t), so
// the composition is exact when the parent scale is uniform, or when the child
// rotation maps the scale axes onto themselves. Otherwise the true result is a
// shear, which no rigid body engine represents; the caller warns and the
// componentwise scale product is used. Jolt has the same limitation.
class Composed_placement
{
public:
    b3Transform transform{};
    glm::vec3   scale    {1.0f};
    bool        is_exact {true};
};

[[nodiscard]] auto compose_placement(
    const b3Transform& parent_transform,
    const glm::vec3&   parent_scale,
    const b3Transform& child_transform,
    const glm::vec3&   child_scale
) -> Composed_placement;

[[nodiscard]] auto is_uniform_scale    (const glm::vec3& scale) -> bool;
[[nodiscard]] auto uniform_scale_factor(const glm::vec3& scale) -> float;

// Rotation carrying erhe's Axis onto Box3D's canonical +Y axis of revolution.
[[nodiscard]] auto axis_rotation(Axis axis) -> b3Quat;

class Box3d_collision_shape : public ICollision_shape
{
public:
    ~Box3d_collision_shape() noexcept override = default;

    // Implements ICollision_shape
    void calculate_local_inertia(float mass, glm::mat4& inertia) const override;
    auto is_convex              () const -> bool                 override;
    auto get_center_of_mass     () const -> glm::vec3            override;
    auto get_mass_properties    () const -> Mass_properties      override;
    auto describe               () const -> std::string          override;
    auto get_shape_type         () const -> Collision_shape_type override;

    // Materializes this shape onto context.body. local_transform places the
    // shape in body local space; scale is applied to the shape's own geometry
    // first.
    virtual void attach_to_body(
        Shape_attach_context& context,
        const b3Transform&    local_transform,
        const glm::vec3&      scale
    ) const = 0;

    // Mass, center and inertia in the shape's own local frame at the given
    // density. Mesh shapes report zero mass: Box3D has no b3ComputeMeshMass,
    // and a triangle mesh has no volume to integrate.
    [[nodiscard]] virtual auto compute_mass_data(float density) const -> b3MassData;

    // True when this shape, or anything it wraps, is a triangle mesh. Box3D
    // cannot derive mass from a mesh, so a dynamic body containing one is
    // reported and created as static instead.
    [[nodiscard]] virtual auto contains_mesh() const -> bool;
};

class Box3d_empty_shape : public Box3d_collision_shape
{
public:
    ~Box3d_empty_shape() noexcept override = default;

    void attach_to_body(Shape_attach_context& context, const b3Transform& local_transform, const glm::vec3& scale) const override;

    [[nodiscard]] auto get_shape_type() const -> Collision_shape_type override { return Collision_shape_type::e_empty; }
    [[nodiscard]] auto describe      () const -> std::string          override;
};

class Box3d_box_shape : public Box3d_collision_shape
{
public:
    explicit Box3d_box_shape(glm::vec3 half_extents);
    ~Box3d_box_shape() noexcept override = default;

    void attach_to_body(Shape_attach_context& context, const b3Transform& local_transform, const glm::vec3& scale) const override;

    [[nodiscard]] auto compute_mass_data(float density) const -> b3MassData               override;
    [[nodiscard]] auto get_shape_type   () const -> Collision_shape_type                  override { return Collision_shape_type::e_box; }
    [[nodiscard]] auto get_half_extents () const -> std::optional<glm::vec3>              override { return m_half_extents; }
    [[nodiscard]] auto describe         () const -> std::string                           override;

private:
    glm::vec3  m_half_extents;
    Box3d_hull m_hull;
};

class Box3d_sphere_shape : public Box3d_collision_shape
{
public:
    explicit Box3d_sphere_shape(float radius);
    ~Box3d_sphere_shape() noexcept override = default;

    void attach_to_body(Shape_attach_context& context, const b3Transform& local_transform, const glm::vec3& scale) const override;

    [[nodiscard]] auto compute_mass_data(float density) const -> b3MassData  override;
    [[nodiscard]] auto get_shape_type   () const -> Collision_shape_type     override { return Collision_shape_type::e_sphere; }
    [[nodiscard]] auto get_radius       () const -> std::optional<float>     override { return m_radius; }
    [[nodiscard]] auto describe         () const -> std::string              override;

private:
    float m_radius;
};

class Box3d_capsule_shape : public Box3d_collision_shape
{
public:
    Box3d_capsule_shape(Axis axis, float radius, float length);
    ~Box3d_capsule_shape() noexcept override = default;

    void attach_to_body(Shape_attach_context& context, const b3Transform& local_transform, const glm::vec3& scale) const override;

    [[nodiscard]] auto compute_mass_data(float density) const -> b3MassData  override;
    [[nodiscard]] auto get_shape_type   () const -> Collision_shape_type     override { return Collision_shape_type::e_capsule; }
    [[nodiscard]] auto get_radius       () const -> std::optional<float>     override { return m_radius; }
    [[nodiscard]] auto get_axis         () const -> std::optional<Axis>      override { return m_axis; }
    [[nodiscard]] auto get_length       () const -> std::optional<float>     override { return m_length; }
    [[nodiscard]] auto describe         () const -> std::string              override;

private:
    Axis  m_axis;
    float m_radius;
    float m_length; // axial distance between the two cap sphere centers
};

class Box3d_tapered_capsule_shape : public Box3d_collision_shape
{
public:
    Box3d_tapered_capsule_shape(Axis axis, float bottom_radius, float top_radius, float length);
    ~Box3d_tapered_capsule_shape() noexcept override = default;

    void attach_to_body(Shape_attach_context& context, const b3Transform& local_transform, const glm::vec3& scale) const override;

    [[nodiscard]] auto compute_mass_data (float density) const -> b3MassData override;
    [[nodiscard]] auto get_shape_type    () const -> Collision_shape_type    override { return Collision_shape_type::e_tapered_capsule; }
    [[nodiscard]] auto get_bottom_radius () const -> std::optional<float>    override { return m_bottom_radius; }
    [[nodiscard]] auto get_top_radius    () const -> std::optional<float>    override { return m_top_radius; }
    [[nodiscard]] auto get_axis          () const -> std::optional<Axis>     override { return m_axis; }
    [[nodiscard]] auto get_length        () const -> std::optional<float>    override { return m_length; }
    [[nodiscard]] auto describe          () const -> std::string             override;

private:
    Axis       m_axis;
    float      m_bottom_radius;
    float      m_top_radius;
    float      m_length;
    Box3d_hull m_hull;
};

class Box3d_cylinder_shape : public Box3d_collision_shape
{
public:
    Box3d_cylinder_shape(Axis axis, glm::vec3 half_extents);
    ~Box3d_cylinder_shape() noexcept override = default;

    void attach_to_body(Shape_attach_context& context, const b3Transform& local_transform, const glm::vec3& scale) const override;

    [[nodiscard]] auto compute_mass_data(float density) const -> b3MassData  override;
    [[nodiscard]] auto get_shape_type   () const -> Collision_shape_type     override { return Collision_shape_type::e_cylinder; }
    [[nodiscard]] auto get_half_extents () const -> std::optional<glm::vec3> override { return m_half_extents; }
    [[nodiscard]] auto get_axis         () const -> std::optional<Axis>      override { return m_axis; }
    [[nodiscard]] auto describe         () const -> std::string              override;

private:
    Axis       m_axis;
    glm::vec3  m_half_extents; // x = half height along the axis, y = radius (matches the Jolt backend)
    Box3d_hull m_hull;
};

class Box3d_tapered_cylinder_shape : public Box3d_collision_shape
{
public:
    Box3d_tapered_cylinder_shape(Axis axis, float bottom_radius, float top_radius, float length);
    ~Box3d_tapered_cylinder_shape() noexcept override = default;

    void attach_to_body(Shape_attach_context& context, const b3Transform& local_transform, const glm::vec3& scale) const override;

    [[nodiscard]] auto compute_mass_data (float density) const -> b3MassData override;
    [[nodiscard]] auto get_shape_type    () const -> Collision_shape_type    override { return Collision_shape_type::e_tapered_cylinder; }
    [[nodiscard]] auto get_bottom_radius () const -> std::optional<float>    override { return m_bottom_radius; }
    [[nodiscard]] auto get_top_radius    () const -> std::optional<float>    override { return m_top_radius; }
    [[nodiscard]] auto get_axis          () const -> std::optional<Axis>     override { return m_axis; }
    [[nodiscard]] auto get_length        () const -> std::optional<float>    override { return m_length; }
    [[nodiscard]] auto describe          () const -> std::string             override;

private:
    Axis       m_axis;
    float      m_bottom_radius;
    float      m_top_radius;
    float      m_length; // full axial height
    Box3d_hull m_hull;
};

// Shared helper for the shapes that materialize as a hull.
void attach_hull_to_body(
    Shape_attach_context& context,
    const b3HullData*     hull,
    const b3Transform&    local_transform,
    const glm::vec3&      scale,
    const char*           shape_description
);

} // namespace erhe::physics
