#include "erhe_physics/box3d/box3d_wrapper_shapes.hpp"
#include "erhe_physics/box3d/glm_conversions.hpp"
#include "erhe_physics/physics_log.hpp"

#include <fmt/format.h>

namespace erhe::physics {

namespace {

[[nodiscard]] auto as_box3d_shape(const std::shared_ptr<ICollision_shape>& shape) -> const Box3d_collision_shape*
{
    return static_cast<const Box3d_collision_shape*>(shape.get());
}

// Scaling a shape by s scales its volume (and so its mass at fixed density) by
// s^3, and its inertia tensor by s^5. Only meaningful for uniform scale; a
// non-uniform scale would need the tensor transformed per axis, which is
// reported by the caller.
[[nodiscard]] auto scale_mass_data(const b3MassData& mass_data, const float factor) -> b3MassData
{
    const float mass_factor    = factor * factor * factor;
    const float inertia_factor = mass_factor * factor * factor;
    b3MassData result{};
    result.mass    = mass_data.mass * mass_factor;
    result.center  = to_box3d(from_box3d(mass_data.center) * factor);
    result.inertia = to_box3d(from_box3d(mass_data.inertia) * inertia_factor);
    return result;
}

} // anonymous namespace

// -----------------------------------------------------------------------------
// Box3d_scaled_shape
// -----------------------------------------------------------------------------

Box3d_scaled_shape::Box3d_scaled_shape(const std::shared_ptr<ICollision_shape>& shape, const glm::vec3 scale)
    : m_shape{shape}
    , m_scale{scale}
{
}

void Box3d_scaled_shape::attach_to_body(
    Shape_attach_context& context,
    const b3Transform&    local_transform,
    const glm::vec3&      scale
) const
{
    const Box3d_collision_shape* inner = as_box3d_shape(m_shape);
    if (inner == nullptr) {
        return;
    }
    inner->attach_to_body(context, local_transform, scale * m_scale);
}

auto Box3d_scaled_shape::compute_mass_data(const float density) const -> b3MassData
{
    const Box3d_collision_shape* inner = as_box3d_shape(m_shape);
    if (inner == nullptr) {
        return Box3d_collision_shape::compute_mass_data(density);
    }
    const b3MassData inner_mass = inner->compute_mass_data(density);
    if (!is_uniform_scale(m_scale)) {
        // A non-uniform scale does not act on the inertia tensor as a single
        // factor. The geometric-mean volume factor keeps the mass right, which
        // is what the editor reads; the tensor is approximate.
        const float factor = std::cbrt(std::abs(m_scale.x * m_scale.y * m_scale.z));
        return scale_mass_data(inner_mass, factor);
    }
    return scale_mass_data(inner_mass, uniform_scale_factor(m_scale));
}

auto Box3d_scaled_shape::contains_mesh() const -> bool
{
    const Box3d_collision_shape* inner = as_box3d_shape(m_shape);
    return (inner != nullptr) && inner->contains_mesh();
}

auto Box3d_scaled_shape::is_convex() const -> bool
{
    const Box3d_collision_shape* inner = as_box3d_shape(m_shape);
    return (inner != nullptr) && inner->is_convex();
}

auto Box3d_scaled_shape::describe() const -> std::string
{
    return fmt::format("Box3d_scaled_shape(scale = {}, {}, {})", m_scale.x, m_scale.y, m_scale.z);
}

// -----------------------------------------------------------------------------
// Box3d_uniform_scaling_shape
// -----------------------------------------------------------------------------

Box3d_uniform_scaling_shape::Box3d_uniform_scaling_shape(const std::shared_ptr<ICollision_shape>& shape, const float scale)
    : m_shape{shape}
    , m_scale{scale}
{
}

void Box3d_uniform_scaling_shape::attach_to_body(
    Shape_attach_context& context,
    const b3Transform&    local_transform,
    const glm::vec3&      scale
) const
{
    const Box3d_collision_shape* inner = as_box3d_shape(m_shape);
    if (inner == nullptr) {
        return;
    }
    inner->attach_to_body(context, local_transform, scale * m_scale);
}

auto Box3d_uniform_scaling_shape::compute_mass_data(const float density) const -> b3MassData
{
    const Box3d_collision_shape* inner = as_box3d_shape(m_shape);
    if (inner == nullptr) {
        return Box3d_collision_shape::compute_mass_data(density);
    }
    return scale_mass_data(inner->compute_mass_data(density), m_scale);
}

auto Box3d_uniform_scaling_shape::contains_mesh() const -> bool
{
    const Box3d_collision_shape* inner = as_box3d_shape(m_shape);
    return (inner != nullptr) && inner->contains_mesh();
}

auto Box3d_uniform_scaling_shape::is_convex() const -> bool
{
    const Box3d_collision_shape* inner = as_box3d_shape(m_shape);
    return (inner != nullptr) && inner->is_convex();
}

auto Box3d_uniform_scaling_shape::describe() const -> std::string
{
    return fmt::format("Box3d_uniform_scaling_shape(scale = {})", m_scale);
}

// -----------------------------------------------------------------------------
// Box3d_offset_center_of_mass_shape
// -----------------------------------------------------------------------------

Box3d_offset_center_of_mass_shape::Box3d_offset_center_of_mass_shape(
    const std::shared_ptr<ICollision_shape>& shape,
    const glm::vec3                          offset
)
    : m_shape {shape}
    , m_offset{offset}
{
}

void Box3d_offset_center_of_mass_shape::attach_to_body(
    Shape_attach_context& context,
    const b3Transform&    local_transform,
    const glm::vec3&      scale
) const
{
    const Box3d_collision_shape* inner = as_box3d_shape(m_shape);
    if (inner == nullptr) {
        return;
    }
    // Box3D carries the center of mass on the body, not on a shape, so this
    // wrapper is only representable at the root of a body's shape tree. Nested
    // occurrences are reported instead of being silently applied to the whole
    // body, which would move the wrong thing.
    if (context.has_center_of_mass_offset) {
        log_physics->error(
            "box3d body '{}': more than one offset-center-of-mass shape in the shape tree; "
            "only the outermost is applied",
            context.debug_label
        );
    } else {
        context.center_of_mass_offset     = m_offset;
        context.has_center_of_mass_offset = true;
    }
    inner->attach_to_body(context, local_transform, scale);
}

auto Box3d_offset_center_of_mass_shape::compute_mass_data(const float density) const -> b3MassData
{
    const Box3d_collision_shape* inner = as_box3d_shape(m_shape);
    if (inner == nullptr) {
        return Box3d_collision_shape::compute_mass_data(density);
    }
    b3MassData mass_data = inner->compute_mass_data(density);
    mass_data.center = to_box3d(from_box3d(mass_data.center) + m_offset);
    return mass_data;
}

auto Box3d_offset_center_of_mass_shape::contains_mesh() const -> bool
{
    const Box3d_collision_shape* inner = as_box3d_shape(m_shape);
    return (inner != nullptr) && inner->contains_mesh();
}

auto Box3d_offset_center_of_mass_shape::is_convex() const -> bool
{
    const Box3d_collision_shape* inner = as_box3d_shape(m_shape);
    return (inner != nullptr) && inner->is_convex();
}

auto Box3d_offset_center_of_mass_shape::describe() const -> std::string
{
    return fmt::format("Box3d_offset_center_of_mass_shape(offset = {}, {}, {})", m_offset.x, m_offset.y, m_offset.z);
}

// -----------------------------------------------------------------------------
// ICollision_shape factories
// -----------------------------------------------------------------------------

auto ICollision_shape::create_scaled_shape(
    const std::shared_ptr<ICollision_shape>& shape,
    const glm::vec3                          scale
) -> ICollision_shape*
{
    return new Box3d_scaled_shape(shape, scale);
}

auto ICollision_shape::create_scaled_shape_shared(
    const std::shared_ptr<ICollision_shape>& shape,
    const glm::vec3                          scale
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Box3d_scaled_shape>(shape, scale);
}

auto ICollision_shape::create_uniform_scaling_shape(
    const std::shared_ptr<ICollision_shape>& shape,
    const float                              scale
) -> ICollision_shape*
{
    return new Box3d_uniform_scaling_shape(shape, scale);
}

auto ICollision_shape::create_uniform_scaling_shape_shared(
    const std::shared_ptr<ICollision_shape>& shape,
    const float                              scale
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Box3d_uniform_scaling_shape>(shape, scale);
}

auto ICollision_shape::create_offset_center_of_mass_shape(
    const std::shared_ptr<ICollision_shape>& shape,
    const glm::vec3                          offset
) -> ICollision_shape*
{
    return new Box3d_offset_center_of_mass_shape(shape, offset);
}

auto ICollision_shape::create_offset_center_of_mass_shape_shared(
    const std::shared_ptr<ICollision_shape>& shape,
    const glm::vec3                          offset
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Box3d_offset_center_of_mass_shape>(shape, offset);
}

} // namespace erhe::physics
