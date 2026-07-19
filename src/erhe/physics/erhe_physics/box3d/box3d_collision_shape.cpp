#include "erhe_physics/box3d/box3d_collision_shape.hpp"
#include "erhe_physics/box3d/glm_conversions.hpp"
#include "erhe_physics/physics_log.hpp"

#include <fmt/format.h>

#include <glm/gtc/constants.hpp>

#include <cmath>

namespace erhe::physics {

auto is_uniform_scale(const glm::vec3& scale) -> bool
{
    const float tolerance = 1e-4f;
    return (std::abs(scale.x - scale.y) <= tolerance) && (std::abs(scale.x - scale.z) <= tolerance);
}

auto uniform_scale_factor(const glm::vec3& scale) -> float
{
    return (scale.x + scale.y + scale.z) / 3.0f;
}

auto axis_rotation(const Axis axis) -> b3Quat
{
    // Generated hulls and capsules are built with +Y as the axis of revolution,
    // so the erhe Axis becomes a rotation of the shape's placement.
    switch (axis) {
        case Axis::X: return to_box3d(glm::angleAxis(-glm::half_pi<float>(), glm::vec3{0.0f, 0.0f, 1.0f}));
        case Axis::Z: return to_box3d(glm::angleAxis( glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f}));
        default:      return b3Quat_identity;
    }
}

auto compose_placement(
    const b3Transform& parent_transform,
    const glm::vec3&   parent_scale,
    const b3Transform& child_transform,
    const glm::vec3&   child_scale
) -> Composed_placement
{
    Composed_placement result{};
    const glm::vec3 scaled_child_origin = parent_scale * from_box3d(child_transform.p);
    result.transform.p = to_box3d(from_box3d(parent_transform.p) + (from_box3d(parent_transform.q) * scaled_child_origin));
    result.transform.q = b3MulQuat(parent_transform.q, child_transform.q);
    result.scale       = parent_scale * child_scale;
    // Exact when the parent scale is uniform, or when the child adds no
    // rotation for a non-uniform parent scale to shear.
    result.is_exact =
        is_uniform_scale(parent_scale) ||
        (std::abs(child_transform.q.s) >= (1.0f - 1e-5f));
    return result;
}

// -----------------------------------------------------------------------------
// Box3d_collision_shape
// -----------------------------------------------------------------------------

auto Box3d_collision_shape::compute_mass_data(float) const -> b3MassData
{
    b3MassData mass_data{};
    mass_data.mass    = 0.0f;
    mass_data.center  = b3Vec3_zero;
    mass_data.inertia = b3Matrix3{b3Vec3_zero, b3Vec3_zero, b3Vec3_zero};
    return mass_data;
}

auto Box3d_collision_shape::contains_mesh() const -> bool
{
    return false;
}

void Box3d_collision_shape::calculate_local_inertia(const float mass, glm::mat4& inertia) const
{
    const b3MassData mass_data = compute_mass_data(1.0f);
    if (mass_data.mass <= 0.0f) {
        inertia = glm::mat4{0.0f};
        return;
    }
    // The inertia tensor is linear in density, so scaling it to the requested
    // mass is a single factor.
    inertia = inertia_from_box3d(mass_data.inertia) * (mass / mass_data.mass);
}

auto Box3d_collision_shape::is_convex() const -> bool
{
    return true;
}

auto Box3d_collision_shape::get_center_of_mass() const -> glm::vec3
{
    return from_box3d(compute_mass_data(1.0f).center);
}

auto Box3d_collision_shape::get_mass_properties() const -> Mass_properties
{
    const b3MassData mass_data = compute_mass_data(1.0f);
    return Mass_properties{
        .mass           = mass_data.mass,
        .inertia_tensor = inertia_from_box3d(mass_data.inertia)
    };
}

auto Box3d_collision_shape::describe() const -> std::string
{
    return "Box3d_collision_shape";
}

auto Box3d_collision_shape::get_shape_type() const -> Collision_shape_type
{
    return Collision_shape_type::e_empty;
}

// -----------------------------------------------------------------------------
// Shared hull attach
// -----------------------------------------------------------------------------

void attach_hull_to_body(
    Shape_attach_context& context,
    const b3HullData*     hull,
    const b3Transform&    local_transform,
    const glm::vec3&      scale,
    const char*           shape_description
)
{
    if (hull == nullptr) {
        log_physics->error(
            "box3d body '{}': {} shape has no valid hull and was not attached",
            context.debug_label, shape_description
        );
        return;
    }
    const b3ShapeId shape_id = b3CreateTransformedHullShape(
        context.body,
        context.shape_def,
        hull,
        local_transform,
        to_box3d(scale)
    );
    context.shape_ids->push_back(shape_id);

    Collision_primitive primitive{};
    primitive.kind      = Collision_primitive::Kind::hull;
    primitive.hull      = hull;
    primitive.transform = local_transform;
    primitive.scale     = scale;
    context.primitives->push_back(primitive);
}

// -----------------------------------------------------------------------------
// Box3d_empty_shape
// -----------------------------------------------------------------------------

void Box3d_empty_shape::attach_to_body(Shape_attach_context&, const b3Transform&, const glm::vec3&) const
{
    // An empty shape contributes no geometry. Bodies created from it exist in
    // the world (so joints and transforms still work) but collide with nothing.
}

auto Box3d_empty_shape::describe() const -> std::string
{
    return "Box3d_empty_shape";
}

// -----------------------------------------------------------------------------
// Box3d_box_shape
// -----------------------------------------------------------------------------

Box3d_box_shape::Box3d_box_shape(const glm::vec3 half_extents)
    : m_half_extents{half_extents}
    , m_hull        {create_box_hull(half_extents)}
{
}

void Box3d_box_shape::attach_to_body(
    Shape_attach_context& context,
    const b3Transform&    local_transform,
    const glm::vec3&      scale
) const
{
    attach_hull_to_body(context, m_hull.get(), local_transform, scale, "box");
}

auto Box3d_box_shape::compute_mass_data(const float density) const -> b3MassData
{
    if (!m_hull.is_valid()) {
        return Box3d_collision_shape::compute_mass_data(density);
    }
    return b3ComputeHullMass(m_hull.get(), density);
}

auto Box3d_box_shape::describe() const -> std::string
{
    return fmt::format("Box3d_box_shape(half_extents = {}, {}, {})", m_half_extents.x, m_half_extents.y, m_half_extents.z);
}

// -----------------------------------------------------------------------------
// Box3d_sphere_shape
// -----------------------------------------------------------------------------

Box3d_sphere_shape::Box3d_sphere_shape(const float radius)
    : m_radius{radius}
{
}

void Box3d_sphere_shape::attach_to_body(
    Shape_attach_context& context,
    const b3Transform&    local_transform,
    const glm::vec3&      scale
) const
{
    if (is_uniform_scale(scale)) {
        b3Sphere sphere{};
        sphere.center = local_transform.p;
        sphere.radius = m_radius * uniform_scale_factor(scale);

        const b3ShapeId shape_id = b3CreateSphereShape(context.body, context.shape_def, &sphere);
        context.shape_ids->push_back(shape_id);

        Collision_primitive primitive{};
        primitive.kind      = Collision_primitive::Kind::sphere;
        primitive.sphere    = sphere;
        primitive.transform = b3Transform_identity;
        context.primitives->push_back(primitive);
        return;
    }

    // A non-uniformly scaled sphere is an ellipsoid, which Box3D cannot
    // represent, so it is promoted to a tessellated hull owned by the body.
    log_physics->debug(
        "box3d body '{}': sphere under non-uniform scale ({}, {}, {}) promoted to a convex hull",
        context.debug_label, scale.x, scale.y, scale.z
    );
    context.derived_hulls->push_back(create_sphere_hull(m_radius));
    attach_hull_to_body(context, context.derived_hulls->back().get(), local_transform, scale, "scaled sphere");
}

auto Box3d_sphere_shape::compute_mass_data(const float density) const -> b3MassData
{
    b3Sphere sphere{};
    sphere.center = b3Vec3_zero;
    sphere.radius = m_radius;
    return b3ComputeSphereMass(&sphere, density);
}

auto Box3d_sphere_shape::describe() const -> std::string
{
    return fmt::format("Box3d_sphere_shape(radius = {})", m_radius);
}

// -----------------------------------------------------------------------------
// Box3d_capsule_shape
// -----------------------------------------------------------------------------

Box3d_capsule_shape::Box3d_capsule_shape(const Axis axis, const float radius, const float length)
    : m_axis  {axis}
    , m_radius{radius}
    , m_length{length}
{
}

void Box3d_capsule_shape::attach_to_body(
    Shape_attach_context& context,
    const b3Transform&    local_transform,
    const glm::vec3&      scale
) const
{
    // The capsule is canonically built along +Y; the erhe Axis becomes part of
    // its placement.
    const b3Transform axis_transform{b3Vec3_zero, axis_rotation(m_axis)};
    const Composed_placement placement = compose_placement(local_transform, scale, axis_transform, glm::vec3{1.0f});

    if (is_uniform_scale(scale)) {
        const float      factor      = uniform_scale_factor(scale);
        const float      half_length = 0.5f * m_length * factor;
        // A capsule bakes its transform into the two cap centers, so any rigid
        // placement is represented exactly.
        const glm::quat  rotation    = from_box3d(placement.transform.q);
        const glm::vec3  origin      = from_box3d(placement.transform.p);
        const glm::vec3  axis_vector = rotation * glm::vec3{0.0f, half_length, 0.0f};

        b3Capsule capsule{};
        capsule.center1 = to_box3d(origin - axis_vector);
        capsule.center2 = to_box3d(origin + axis_vector);
        capsule.radius  = m_radius * factor;

        const b3ShapeId shape_id = b3CreateCapsuleShape(context.body, context.shape_def, &capsule);
        context.shape_ids->push_back(shape_id);

        Collision_primitive primitive{};
        primitive.kind      = Collision_primitive::Kind::capsule;
        primitive.capsule   = capsule;
        primitive.transform = b3Transform_identity;
        context.primitives->push_back(primitive);
        return;
    }

    log_physics->debug(
        "box3d body '{}': capsule under non-uniform scale ({}, {}, {}) promoted to a convex hull",
        context.debug_label, scale.x, scale.y, scale.z
    );
    context.derived_hulls->push_back(create_capsule_hull(m_radius, 0.5f * m_length));
    attach_hull_to_body(context, context.derived_hulls->back().get(), placement.transform, placement.scale, "scaled capsule");
}

auto Box3d_capsule_shape::compute_mass_data(const float density) const -> b3MassData
{
    b3Capsule capsule{};
    capsule.center1 = b3Vec3{0.0f, -0.5f * m_length, 0.0f};
    capsule.center2 = b3Vec3{0.0f,  0.5f * m_length, 0.0f};
    capsule.radius  = m_radius;
    return b3ComputeCapsuleMass(&capsule, density);
}

auto Box3d_capsule_shape::describe() const -> std::string
{
    return fmt::format("Box3d_capsule_shape(radius = {}, length = {})", m_radius, m_length);
}

// -----------------------------------------------------------------------------
// Box3d_tapered_capsule_shape
// -----------------------------------------------------------------------------

Box3d_tapered_capsule_shape::Box3d_tapered_capsule_shape(
    const Axis  axis,
    const float bottom_radius,
    const float top_radius,
    const float length
)
    : m_axis         {axis}
    , m_bottom_radius{bottom_radius}
    , m_top_radius   {top_radius}
    , m_length       {length}
    , m_hull         {create_tapered_capsule_hull(bottom_radius, top_radius, 0.5f * length)}
{
}

void Box3d_tapered_capsule_shape::attach_to_body(
    Shape_attach_context& context,
    const b3Transform&    local_transform,
    const glm::vec3&      scale
) const
{
    const b3Transform axis_transform{b3Vec3_zero, axis_rotation(m_axis)};
    const Composed_placement placement = compose_placement(local_transform, scale, axis_transform, glm::vec3{1.0f});
    attach_hull_to_body(context, m_hull.get(), placement.transform, placement.scale, "tapered capsule");
}

auto Box3d_tapered_capsule_shape::compute_mass_data(const float density) const -> b3MassData
{
    if (!m_hull.is_valid()) {
        return Box3d_collision_shape::compute_mass_data(density);
    }
    return b3ComputeHullMass(m_hull.get(), density);
}

auto Box3d_tapered_capsule_shape::describe() const -> std::string
{
    return fmt::format(
        "Box3d_tapered_capsule_shape(bottom_radius = {}, top_radius = {}, length = {})",
        m_bottom_radius, m_top_radius, m_length
    );
}

// -----------------------------------------------------------------------------
// Box3d_cylinder_shape
// -----------------------------------------------------------------------------

Box3d_cylinder_shape::Box3d_cylinder_shape(const Axis axis, const glm::vec3 half_extents)
    : m_axis        {axis}
    , m_half_extents{half_extents}
    , m_hull        {create_cylinder_hull(half_extents.y, half_extents.x)}
{
}

void Box3d_cylinder_shape::attach_to_body(
    Shape_attach_context& context,
    const b3Transform&    local_transform,
    const glm::vec3&      scale
) const
{
    const b3Transform axis_transform{b3Vec3_zero, axis_rotation(m_axis)};
    const Composed_placement placement = compose_placement(local_transform, scale, axis_transform, glm::vec3{1.0f});
    attach_hull_to_body(context, m_hull.get(), placement.transform, placement.scale, "cylinder");
}

auto Box3d_cylinder_shape::compute_mass_data(const float density) const -> b3MassData
{
    if (!m_hull.is_valid()) {
        return Box3d_collision_shape::compute_mass_data(density);
    }
    return b3ComputeHullMass(m_hull.get(), density);
}

auto Box3d_cylinder_shape::describe() const -> std::string
{
    return fmt::format("Box3d_cylinder_shape(half_height = {}, radius = {})", m_half_extents.x, m_half_extents.y);
}

// -----------------------------------------------------------------------------
// Box3d_tapered_cylinder_shape
// -----------------------------------------------------------------------------

Box3d_tapered_cylinder_shape::Box3d_tapered_cylinder_shape(
    const Axis  axis,
    const float bottom_radius,
    const float top_radius,
    const float length
)
    : m_axis         {axis}
    , m_bottom_radius{bottom_radius}
    , m_top_radius   {top_radius}
    , m_length       {length}
    , m_hull         {create_tapered_cylinder_hull(bottom_radius, top_radius, 0.5f * length)}
{
}

void Box3d_tapered_cylinder_shape::attach_to_body(
    Shape_attach_context& context,
    const b3Transform&    local_transform,
    const glm::vec3&      scale
) const
{
    const b3Transform axis_transform{b3Vec3_zero, axis_rotation(m_axis)};
    const Composed_placement placement = compose_placement(local_transform, scale, axis_transform, glm::vec3{1.0f});
    attach_hull_to_body(context, m_hull.get(), placement.transform, placement.scale, "tapered cylinder");
}

auto Box3d_tapered_cylinder_shape::compute_mass_data(const float density) const -> b3MassData
{
    if (!m_hull.is_valid()) {
        return Box3d_collision_shape::compute_mass_data(density);
    }
    return b3ComputeHullMass(m_hull.get(), density);
}

auto Box3d_tapered_cylinder_shape::describe() const -> std::string
{
    return fmt::format(
        "Box3d_tapered_cylinder_shape(bottom_radius = {}, top_radius = {}, length = {})",
        m_bottom_radius, m_top_radius, m_length
    );
}

// -----------------------------------------------------------------------------
// ICollision_shape factories for the primitive shapes
// -----------------------------------------------------------------------------

auto ICollision_shape::create_empty_shape() -> ICollision_shape*
{
    return new Box3d_empty_shape();
}

auto ICollision_shape::create_empty_shape_shared() -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Box3d_empty_shape>();
}

auto ICollision_shape::create_box_shape(const glm::vec3 half_extents) -> ICollision_shape*
{
    return new Box3d_box_shape(half_extents);
}

auto ICollision_shape::create_box_shape_shared(const glm::vec3 half_extents) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Box3d_box_shape>(half_extents);
}

auto ICollision_shape::create_capsule_shape(const Axis axis, const float radius, const float length) -> ICollision_shape*
{
    return new Box3d_capsule_shape(axis, radius, length);
}

auto ICollision_shape::create_capsule_shape_shared(
    const Axis  axis,
    const float radius,
    const float length
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Box3d_capsule_shape>(axis, radius, length);
}

auto ICollision_shape::create_cylinder_shape(const Axis axis, const glm::vec3 half_extents) -> ICollision_shape*
{
    return new Box3d_cylinder_shape(axis, half_extents);
}

auto ICollision_shape::create_cylinder_shape_shared(
    const Axis      axis,
    const glm::vec3 half_extents
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Box3d_cylinder_shape>(axis, half_extents);
}

auto ICollision_shape::create_sphere_shape(const float radius) -> ICollision_shape*
{
    return new Box3d_sphere_shape(radius);
}

auto ICollision_shape::create_sphere_shape_shared(const float radius) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Box3d_sphere_shape>(radius);
}

auto ICollision_shape::create_tapered_capsule_shape(
    const Axis  axis,
    const float bottom_radius,
    const float top_radius,
    const float length
) -> ICollision_shape*
{
    return new Box3d_tapered_capsule_shape(axis, bottom_radius, top_radius, length);
}

auto ICollision_shape::create_tapered_capsule_shape_shared(
    const Axis  axis,
    const float bottom_radius,
    const float top_radius,
    const float length
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Box3d_tapered_capsule_shape>(axis, bottom_radius, top_radius, length);
}

auto ICollision_shape::create_tapered_cylinder_shape(
    const Axis  axis,
    const float bottom_radius,
    const float top_radius,
    const float length
) -> ICollision_shape*
{
    return new Box3d_tapered_cylinder_shape(axis, bottom_radius, top_radius, length);
}

auto ICollision_shape::create_tapered_cylinder_shape_shared(
    const Axis  axis,
    const float bottom_radius,
    const float top_radius,
    const float length
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Box3d_tapered_cylinder_shape>(axis, bottom_radius, top_radius, length);
}

} // namespace erhe::physics
