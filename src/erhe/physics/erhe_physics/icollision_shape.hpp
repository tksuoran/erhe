#pragma once

#include "erhe_physics/transform.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace erhe::physics {

enum class Axis : int {
    X,
    Y,
    Z
};

enum class Collision_shape_type : int {
    e_empty,
    e_box,
    e_sphere,
    e_capsule,
    e_cylinder,
    e_convex_hull,
    e_compound,
    e_uniform_scaling,
};

class ICollision_shape;

class Compound_child
{
public:
    std::shared_ptr<ICollision_shape> shape;
    Transform                         transform;
};

class Compound_shape_create_info
{
public:
    std::vector<Compound_child> children;
};

class Mass_properties
{
public:
    float     mass          {0.0f};
    glm::mat4 inertia_tensor{0.0f};
};

class ICollision_shape
{
public:
    virtual ~ICollision_shape() noexcept {};

    [[nodiscard]] static auto create_empty_shape                 () -> ICollision_shape*;
    [[nodiscard]] static auto create_empty_shape_shared          () -> std::shared_ptr<ICollision_shape>;

    [[nodiscard]] static auto create_box_shape                   (const glm::vec3 half_extents) -> ICollision_shape*;
    [[nodiscard]] static auto create_box_shape_shared            (const glm::vec3 half_extents) -> std::shared_ptr<ICollision_shape>;

    [[nodiscard]] static auto create_capsule_shape               (Axis axis, float radius, float length) -> ICollision_shape*;
    [[nodiscard]] static auto create_capsule_shape_shared        (Axis axis, float radius, float length) -> std::shared_ptr<ICollision_shape>;

    [[nodiscard]] static auto create_compound_shape              (const Compound_shape_create_info& create_info) -> ICollision_shape*;
    [[nodiscard]] static auto create_compound_shape_shared       (const Compound_shape_create_info& create_info) -> std::shared_ptr<ICollision_shape>;

#if 0
    [[nodiscard]] static auto create_cone_shape                  (Axis axis, float base_radius, float height) -> ICollision_shape*;
    [[nodiscard]] static auto create_cone_shape_shared           (Axis axis, float base_radius, float height) -> std::shared_ptr<ICollision_shape>;
#endif

    [[nodiscard]] static auto create_convex_hull_shape           (const float* points, int point_count, int stride) -> ICollision_shape*;
    [[nodiscard]] static auto create_convex_hull_shape_shared    (const float* points, int point_count, int stride) -> std::shared_ptr<ICollision_shape>;

    [[nodiscard]] static auto create_cylinder_shape              (Axis axis, const glm::vec3 half_extents) -> ICollision_shape*;
    [[nodiscard]] static auto create_cylinder_shape_shared       (Axis axis, const glm::vec3 half_extents) -> std::shared_ptr<ICollision_shape>;

    [[nodiscard]] static auto create_sphere_shape                (float radius) -> ICollision_shape*;
    [[nodiscard]] static auto create_sphere_shape_shared         (float radius) -> std::shared_ptr<ICollision_shape>;

    [[nodiscard]] static auto create_uniform_scaling_shape       (ICollision_shape* shape, float scale) -> ICollision_shape*;
    [[nodiscard]] static auto create_uniform_scaling_shape_shared(ICollision_shape* shape, float scale) -> std::shared_ptr<ICollision_shape>;

                  virtual void calculate_local_inertia           (float mass, glm::mat4& inertia) const = 0;
    [[nodiscard]] virtual auto is_convex                         () const -> bool                       = 0;
    [[nodiscard]] virtual auto get_center_of_mass                () const -> glm::vec3                  = 0;
    [[nodiscard]] virtual auto get_mass_properties               () const -> Mass_properties            = 0;
    [[nodiscard]] virtual auto describe                          () const -> std::string                = 0;

    // Shape introspection for serialization
    [[nodiscard]] virtual auto get_shape_type  () const -> Collision_shape_type                        = 0;
    [[nodiscard]] virtual auto get_half_extents() const -> std::optional<glm::vec3>                    { return std::nullopt; }
    [[nodiscard]] virtual auto get_radius      () const -> std::optional<float>                        { return std::nullopt; }
    [[nodiscard]] virtual auto get_axis        () const -> std::optional<Axis>                         { return std::nullopt; }
    [[nodiscard]] virtual auto get_length      () const -> std::optional<float>                        { return std::nullopt; }
    [[nodiscard]] virtual auto get_children    () const -> const std::vector<Compound_child>&          { static const std::vector<Compound_child> empty; return empty; }

    //virtual void calculate_principal_axis_transform(
    //    const std::vector<float>& child_masses,
    //    Transform&                principal_transform,
    //    glm::mat4&                inertia
    //) = 0;
};

} // namespace erhe::physics
