#include "erhe_physics/box3d/box3d_compound_shape.hpp"
#include "erhe_physics/box3d/glm_conversions.hpp"
#include "erhe_physics/physics_log.hpp"

#include <fmt/format.h>

namespace erhe::physics {

Box3d_compound_shape::Box3d_compound_shape(const Compound_shape_create_info& create_info)
    : m_children{create_info.children}
{
}

void Box3d_compound_shape::attach_to_body(
    Shape_attach_context& context,
    const b3Transform&    local_transform,
    const glm::vec3&      scale
) const
{
    for (const Compound_child& child : m_children) {
        const Box3d_collision_shape* child_shape = static_cast<const Box3d_collision_shape*>(child.shape.get());
        if (child_shape == nullptr) {
            continue;
        }
        const Composed_placement placement = compose_placement(
            local_transform,
            scale,
            to_box3d(child.transform),
            glm::vec3{1.0f}
        );
        if (!placement.is_exact) {
            log_physics->warn(
                "box3d body '{}': compound child under non-uniform scale ({}, {}, {}) with a rotation; "
                "the exact result is a shear, which is approximated by a scaled rotation",
                context.debug_label, scale.x, scale.y, scale.z
            );
        }
        child_shape->attach_to_body(context, placement.transform, placement.scale);
    }
}

auto Box3d_compound_shape::compute_mass_data(const float density) const -> b3MassData
{
    // Accumulate the children with the parallel axis theorem, in the compound's
    // own frame.
    float     total_mass = 0.0f;
    glm::vec3 weighted_center{0.0f};
    for (const Compound_child& child : m_children) {
        const Box3d_collision_shape* child_shape = static_cast<const Box3d_collision_shape*>(child.shape.get());
        if (child_shape == nullptr) {
            continue;
        }
        const b3MassData child_mass = child_shape->compute_mass_data(density);
        if (child_mass.mass <= 0.0f) {
            continue;
        }
        const glm::vec3 child_center = (child.transform.basis * from_box3d(child_mass.center)) + child.transform.origin;
        total_mass      += child_mass.mass;
        weighted_center += child_mass.mass * child_center;
    }

    b3MassData result{};
    if (total_mass <= 0.0f) {
        result.mass    = 0.0f;
        result.center  = b3Vec3_zero;
        result.inertia = b3Matrix3{b3Vec3_zero, b3Vec3_zero, b3Vec3_zero};
        return result;
    }

    const glm::vec3 center = weighted_center / total_mass;
    glm::mat3       inertia{0.0f};
    for (const Compound_child& child : m_children) {
        const Box3d_collision_shape* child_shape = static_cast<const Box3d_collision_shape*>(child.shape.get());
        if (child_shape == nullptr) {
            continue;
        }
        const b3MassData child_mass = child_shape->compute_mass_data(density);
        if (child_mass.mass <= 0.0f) {
            continue;
        }
        const glm::mat3 basis         = child.transform.basis;
        const glm::vec3 child_center  = (basis * from_box3d(child_mass.center)) + child.transform.origin;
        const glm::mat3 child_inertia = basis * from_box3d(child_mass.inertia) * glm::transpose(basis);
        const glm::vec3 offset        = child_center - center;
        // m * (|d|^2 * E - d d^T)
        const glm::mat3 parallel_axis =
            (glm::mat3{1.0f} * glm::dot(offset, offset)) - glm::outerProduct(offset, offset);
        inertia += child_inertia + (child_mass.mass * parallel_axis);
    }

    result.mass    = total_mass;
    result.center  = to_box3d(center);
    result.inertia = to_box3d(inertia);
    return result;
}

auto Box3d_compound_shape::contains_mesh() const -> bool
{
    for (const Compound_child& child : m_children) {
        const Box3d_collision_shape* child_shape = static_cast<const Box3d_collision_shape*>(child.shape.get());
        if ((child_shape != nullptr) && child_shape->contains_mesh()) {
            return true;
        }
    }
    return false;
}

auto Box3d_compound_shape::is_convex() const -> bool
{
    return false;
}

auto Box3d_compound_shape::describe() const -> std::string
{
    return fmt::format("Box3d_compound_shape(children = {})", m_children.size());
}

auto ICollision_shape::create_compound_shape(const Compound_shape_create_info& create_info) -> ICollision_shape*
{
    return new Box3d_compound_shape(create_info);
}

auto ICollision_shape::create_compound_shape_shared(
    const Compound_shape_create_info& create_info
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Box3d_compound_shape>(create_info);
}

} // namespace erhe::physics
