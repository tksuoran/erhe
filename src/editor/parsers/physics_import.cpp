#include "parsers/physics_import.hpp"

#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "operations/library_attach_operation.hpp"
#include "scene/collision_shape_from_mesh.hpp"
#include "scene/node_joint.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"

#include "scene/generated/gltf_source_reference.hpp"

#include "erhe_gltf/gltf_item_flags.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_string.hpp"
#include "erhe_physics/collision_filter.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_physics/physics_joint_settings.hpp"
#include "erhe_physics/physics_material.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/physics_description.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_scene/xform.hpp"

#include <fmt/format.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace editor {

namespace {

[[nodiscard]] auto to_combine_mode(const erhe::scene::Physics_combine_mode mode) -> erhe::physics::Combine_mode
{
    switch (mode) {
        case erhe::scene::Physics_combine_mode::e_average:  return erhe::physics::Combine_mode::e_average;
        case erhe::scene::Physics_combine_mode::e_minimum:  return erhe::physics::Combine_mode::e_minimum;
        case erhe::scene::Physics_combine_mode::e_maximum:  return erhe::physics::Combine_mode::e_maximum;
        case erhe::scene::Physics_combine_mode::e_multiply: return erhe::physics::Combine_mode::e_multiply;
        default:                                           return erhe::physics::Combine_mode::e_average;
    }
}

// KHR_implicit_shapes shape entry -> erhe collision shape. glTF implicit
// shapes are centered at the origin and aligned along the Y axis.
[[nodiscard]] auto make_implicit_shape(const erhe::scene::Physics_shape& shape) -> std::shared_ptr<erhe::physics::ICollision_shape>
{
    using erhe::physics::Axis;
    using erhe::physics::ICollision_shape;
    switch (shape.type) {
        case erhe::scene::Physics_shape_type::e_sphere: {
            return ICollision_shape::create_sphere_shape_shared(shape.radius);
        }
        case erhe::scene::Physics_shape_type::e_box: {
            // glTF box size is full extents.
            return ICollision_shape::create_box_shape_shared(shape.size * 0.5f);
        }
        case erhe::scene::Physics_shape_type::e_capsule: {
            // KHR_implicit_shapes capsule height is "the distance between the
            // centers of the two capping spheres" (shape.capsule schema),
            // which is exactly the erhe capsule length convention - the value
            // passes through unmodified.
            if (shape.radius_bottom == shape.radius_top) {
                return ICollision_shape::create_capsule_shape_shared(Axis::Y, shape.radius_bottom, shape.height);
            }
            return ICollision_shape::create_tapered_capsule_shape_shared(Axis::Y, shape.radius_bottom, shape.radius_top, shape.height);
        }
        case erhe::scene::Physics_shape_type::e_cylinder: {
            // KHR_implicit_shapes cylinder height is the full axial height.
            if (shape.radius_bottom == shape.radius_top) {
                return ICollision_shape::create_cylinder_shape_shared(
                    Axis::Y,
                    glm::vec3{shape.radius_bottom, shape.height * 0.5f, shape.radius_bottom}
                );
            }
            return ICollision_shape::create_tapered_cylinder_shape_shared(Axis::Y, shape.radius_bottom, shape.radius_top, shape.height);
        }
        default: {
            return {};
        }
    }
}

// Applies the node's world scale to a collider shape per the spec: negative
// scale components use their absolute value; an all-zero scale disables the
// shape (returns nullptr); uniform scale != 1 uses the uniform scaling
// wrapper; non-uniform scale uses the scaled-shape wrapper.
[[nodiscard]] auto apply_node_scale(
    const std::shared_ptr<erhe::physics::ICollision_shape>& shape,
    const erhe::scene::Node&                                node
) -> std::shared_ptr<erhe::physics::ICollision_shape>
{
    const glm::vec3 scale = glm::abs(node.world_from_node_transform().get_scale());
    if (scale == glm::vec3{0.0f}) {
        log_parsers->info("physics import: node '{}' has all-zero scale - collision shape disabled", node.get_name());
        return {};
    }
    if ((scale.x == scale.y) && (scale.y == scale.z)) {
        if (scale.x == 1.0f) {
            return shape;
        }
        return erhe::physics::ICollision_shape::create_uniform_scaling_shape_shared(shape, scale.x);
    }
    return erhe::physics::ICollision_shape::create_scaled_shape_shared(shape, scale);
}

// One collision shape contributed to a body, with the transform of the
// contributing collider node relative to the body root node (rotation and
// translation only; scale is baked into the shape).
class Body_shape_entry
{
public:
    const erhe::scene::Node*                         node{nullptr};
    std::shared_ptr<erhe::physics::ICollision_shape> shape;
};

// Rotation / translation of child_node expressed in the (unscaled) node space
// of root_node - the frame the rigid body is created with (Node_physics uses
// world rotation + translation only).
[[nodiscard]] auto relative_body_transform(
    const erhe::scene::Node& root_node,
    const erhe::scene::Node& child_node
) -> erhe::physics::Transform
{
    const erhe::scene::Trs_transform& root_world  = root_node.world_from_node_transform();
    const erhe::scene::Trs_transform& child_world = child_node.world_from_node_transform();
    const glm::quat root_rotation_inverse = glm::inverse(root_world.get_rotation());
    const glm::quat relative_rotation     = root_rotation_inverse * child_world.get_rotation();
    const glm::vec3 relative_translation  = root_rotation_inverse * (child_world.get_translation() - root_world.get_translation());
    return erhe::physics::Transform{glm::mat3_cast(relative_rotation), relative_translation};
}

class Physics_importer
{
public:
    const erhe::scene::Physics_description&                             physics;
    std::vector<std::shared_ptr<erhe::physics::Physics_material>>       material_items{};
    std::vector<std::shared_ptr<erhe::physics::Collision_filter>>       filter_items{};
    std::vector<std::shared_ptr<erhe::physics::Physics_joint_settings>> joint_items{};

    std::unordered_map<const erhe::scene::Node*, const erhe::scene::Physics_node_description*> description_by_node{};

    [[nodiscard]] auto get_material(const std::optional<std::size_t>& index) const -> std::shared_ptr<erhe::physics::Physics_material>
    {
        if (!index.has_value()) {
            return {};
        }
        if (index.value() >= material_items.size()) {
            log_parsers->warn("physics import: physics material index {} out of range", index.value());
            return {};
        }
        return material_items[index.value()];
    }

    [[nodiscard]] auto get_filter(const std::optional<std::size_t>& index) const -> std::shared_ptr<erhe::physics::Collision_filter>
    {
        if (!index.has_value()) {
            return {};
        }
        if (index.value() >= filter_items.size()) {
            log_parsers->warn("physics import: collision filter index {} out of range", index.value());
            return {};
        }
        return filter_items[index.value()];
    }

    // Builds the (node-scale adjusted) collision shape for one collider /
    // trigger geometry. Returns nullptr when the geometry cannot be built
    // (degenerate scale, missing mesh geometry, unsupported geometry form);
    // failures are logged.
    [[nodiscard]] auto build_geometry_shape(
        const erhe::scene::Physics_node_geometry& geometry,
        const erhe::scene::Node&                  collider_node,
        const bool                                dynamic_body
    ) const -> std::shared_ptr<erhe::physics::ICollision_shape>
    {
        std::shared_ptr<erhe::physics::ICollision_shape> shape{};
        if (geometry.shape_index.has_value()) {
            const std::size_t shape_index = geometry.shape_index.value();
            if (shape_index >= physics.shapes.size()) {
                log_parsers->warn("physics import: node '{}' shape index {} out of range", collider_node.get_name(), shape_index);
                return {};
            }
            shape = make_implicit_shape(physics.shapes[shape_index]);
        } else if (geometry.mesh || geometry.node) {
            // Jolt restriction: triangle mesh shapes are usable with static /
            // kinematic bodies only; dynamic bodies fall back to a convex hull.
            bool convex_hull = geometry.convex_hull;
            if (dynamic_body && !convex_hull) {
                log_parsers->warn(
                    "physics import: node '{}' uses a triangle mesh collider on a dynamic body - using convex hull instead (Jolt restriction)",
                    collider_node.get_name()
                );
                convex_hull = true;
            }
            // Current spec keys collider geometry by mesh; the older spec
            // revision keyed it by a mesh-providing node.
            shape = geometry.mesh
                ? build_shape_from_mesh(geometry.mesh.get(), convex_hull)
                : build_shape_from_node_mesh(geometry.node.get(), convex_hull);
            if (!shape) {
                log_parsers->warn(
                    "physics import: node '{}' {} collider could not be built from {} '{}' geometry",
                    collider_node.get_name(),
                    convex_hull ? "convex hull" : "mesh",
                    geometry.mesh ? "mesh" : "node",
                    geometry.mesh ? geometry.mesh->get_name() : geometry.node->get_name()
                );
                return {};
            }
        } else {
            log_parsers->warn(
                "physics import: node '{}' collider geometry has neither shape nor mesh nor node - skipping",
                collider_node.get_name()
            );
            return {};
        }
        if (!shape) {
            return {};
        }
        return apply_node_scale(shape, collider_node);
    }

    [[nodiscard]] auto make_body_create_info(
        const erhe::scene::Node&                               node,
        const std::optional<erhe::scene::Physics_node_motion>& motion,
        const std::shared_ptr<erhe::physics::ICollision_shape>& collision_shape,
        const std::shared_ptr<erhe::physics::Physics_material>& material,
        const std::shared_ptr<erhe::physics::Collision_filter>& filter,
        const bool                                              is_sensor
    ) const -> erhe::physics::IRigid_body_create_info
    {
        erhe::physics::IRigid_body_create_info create_info{};
        create_info.collision_shape  = collision_shape;
        create_info.debug_label      = node.get_name();
        create_info.physics_material = material;
        create_info.collision_filter = filter;
        create_info.is_sensor        = is_sensor;
        if (motion.has_value()) {
            create_info.motion_mode = motion->is_kinematic
                ? erhe::physics::Motion_mode::e_kinematic_physical
                : erhe::physics::Motion_mode::e_dynamic;
            if (motion->mass.has_value()) {
                // Exactly 0.0f selects the spec infinite-mass convention in
                // the physics wrapper.
                create_info.mass = motion->mass.value();
            }
            if (motion->inertia_diagonal.has_value()) {
                const glm::vec3 d = motion->inertia_diagonal.value();
                const glm::mat3 rotation = motion->inertia_orientation.has_value()
                    ? glm::mat3_cast(motion->inertia_orientation.value())
                    : glm::mat3{1.0f};
                const glm::mat3 diagonal{
                    glm::vec3{d.x, 0.0f, 0.0f},
                    glm::vec3{0.0f, d.y, 0.0f},
                    glm::vec3{0.0f, 0.0f, d.z}
                };
                create_info.inertia_override = glm::mat4{rotation * diagonal * glm::transpose(rotation)};
            }
            if ((motion->center_of_mass != glm::vec3{0.0f}) && create_info.collision_shape) {
                // Center of mass offset is the outermost wrapper, matching the
                // order scene deserialization applies (scale closest to the
                // base shape, then the offset-center-of-mass wrapper) and what
                // Node_physics::get_center_of_mass_offset() reads back.
                create_info.collision_shape = erhe::physics::ICollision_shape::create_offset_center_of_mass_shape_shared(
                    create_info.collision_shape,
                    motion->center_of_mass
                );
            }
            // Spec initial velocities are in node space; the create info
            // applies them in world space.
            const glm::quat world_rotation = node.world_from_node_transform().get_rotation();
            create_info.linear_velocity  = world_rotation * motion->linear_velocity;
            create_info.angular_velocity = world_rotation * motion->angular_velocity;
            create_info.gravity_factor   = motion->gravity_factor;
        } else {
            create_info.motion_mode = erhe::physics::Motion_mode::e_static;
        }
        return create_info;
    }
};

// Whether a property list may set an object-valued property. A body's object
// references (its physics material, its collision filter) are what the
// collider records already gave the create info by identity - the attachment
// has no item host while it is built, so a name could not resolve - so they
// are kept and the rest of the list applies.
enum class Object_property_handling : int {
    apply,
    keep
};

// The property one record names. A file states a property in the form its own
// layer carries: the glTF payloads name a property of the item's own class by
// its bare name, and a USD `erhe:Owner:name` attribute names the owner too.
// erhe resolves a secondary or attached property by its qualified name and a
// property of the item's own class by its bare name, so a qualified name that
// resolves to nothing is looked up again without its owner component.
[[nodiscard]] auto find_record_property(
    const erhe::Item_base& item,
    const std::string&     name
) -> const erhe::property::Dependency_property*
{
    const erhe::property::Property_registry&         registry = erhe::property::Property_registry::get();
    const erhe::property::Dependency_property* const property = registry.find_for_object(item, name);
    if (property != nullptr) {
        return property;
    }
    const std::size_t separator = name.find('.');
    if (separator == std::string::npos) {
        return nullptr;
    }
    return registry.find_for_object(item, std::string_view{name}.substr(separator + 1));
}

// One value of a record onto the item it became.
void apply_record_property(erhe::Item_base& item, const std::string& name, const std::string& text)
{
    const erhe::property::Dependency_property* const property = find_record_property(item, name);
    if (property == nullptr) {
        log_parsers->warn("physics import: '{}' has no property '{}'", item.get_name(), name);
        return;
    }
    const std::optional<erhe::property::Property_value> parsed = erhe::property::parse_value(item, *property, text);
    if (!parsed.has_value()) {
        log_parsers->warn("physics import: '{}' property '{}' value '{}' does not parse", item.get_name(), name, text);
        return;
    }
    item.set_value(*property, parsed.value());
}

// One record's erhe-only values onto the item it became. A complete local set
// also clears the values it does not name, so a value the file's own fields
// carried from an inherited value inherits again after the reload.
void apply_record_properties(
    erhe::Item_base&                                        item,
    const std::vector<std::pair<std::string, std::string>>& properties,
    const Physics_property_set                              property_set,
    const Object_property_handling                          object_property_handling
)
{
    for (const std::pair<std::string, std::string>& property : properties) {
        if (object_property_handling == Object_property_handling::keep) {
            const erhe::property::Dependency_property* const registered = find_record_property(item, property.first);
            if ((registered != nullptr) && erhe::property::is_object_reference_type(registered->get_type())) {
                continue;
            }
        }
        apply_record_property(item, property.first, property.second);
    }
    if (property_set != Physics_property_set::complete_local_set) {
        return;
    }
    erhe::gltf::clear_local_properties_not_listed(
        item,
        [&properties](const std::string_view property_name) -> bool {
            for (const std::pair<std::string, std::string>& property : properties) {
                const std::size_t      separator = property.first.find('.');
                const std::string_view listed    = (separator == std::string::npos)
                    ? std::string_view{property.first}
                    : std::string_view{property.first}.substr(separator + 1);
                if (listed == property_name) {
                    return true;
                }
            }
            return false;
        }
    );
}

// The shared item of one record enters the scene the way the file placed it:
// under the prim the file gave it, riding that tree's insert, or through a
// content-library attach operation of its own when the file gave it no place.
void place_library_item(
    App_context&                             context,
    const std::shared_ptr<Content_library>&  content_library,
    const std::shared_ptr<erhe::Hierarchy>&  item,
    const Physics_import_item*               source,
    const Gltf_source_reference&             gltf_source,
    std::vector<std::shared_ptr<Operation>>& operations
)
{
    if ((source != nullptr) && source->parent) {
        item->set_parent(source->parent);
        content_library->set_gltf_source(item, gltf_source);
        return;
    }
    operations.push_back(make_library_attach_operation(context, content_library, item, gltf_source));
}

[[nodiscard]] auto record_of(const std::vector<Physics_import_item>& items, const std::size_t index) -> const Physics_import_item*
{
    return (index < items.size()) ? &items[index] : nullptr;
}

[[nodiscard]] auto record_name(
    const Physics_import_item* record,
    const std::string&         description_name,
    const char*                fallback,
    const std::size_t          index
) -> std::string
{
    if ((record != nullptr) && !record->name.empty()) {
        return record->name;
    }
    return description_name.empty() ? fmt::format("{} {}", fallback, index) : description_name;
}

// Each limit entry sets the properties of every axis it names and each drive
// entry those of the axis it names (doc/erhe/property_system.md section
// 4.22). When two entries name one axis the later one wins, which is what the
// per-axis model does with a file that grouped axes; the overwrite is logged.
void apply_joint_description_to_item(
    const erhe::scene::Physics_joint_description& description,
    const std::string&                            joint_name,
    erhe::physics::Physics_joint_settings&        item
)
{
    using erhe::physics::Joint_axis_drive;
    using erhe::physics::Joint_axis_limit;

    std::array<bool, erhe::physics::c_joint_axis_count> axis_limited{};
    std::array<bool, erhe::physics::c_joint_axis_count> axis_driven{};

    const auto apply_limit = [&](const std::size_t axis, const erhe::scene::Physics_joint_limit& limit) {
        if (axis_limited[axis]) {
            log_physics->warn(
                "Joint '{}': more than one limit names axis '{}'; the later entry wins",
                joint_name, erhe::physics::joint_axis_token(axis)
            );
        }
        axis_limited[axis] = true;
        item.set_axis_limit(axis, Joint_axis_limit::limited);
        if (limit.min.has_value()) { item.set_axis_limit_min(axis, limit.min.value()); }
        if (limit.max.has_value()) { item.set_axis_limit_max(axis, limit.max.value()); }
        if (limit.stiffness.has_value()) { item.set_axis_limit_stiffness(axis, limit.stiffness.value()); }
        item.set_axis_limit_damping(axis, limit.damping);
    };

    for (const erhe::scene::Physics_joint_limit& limit : description.limits) {
        for (const int axis : limit.linear_axes) {
            if ((axis >= 0) && (axis < 3)) {
                apply_limit(static_cast<std::size_t>(axis), limit);
            }
        }
        for (const int axis : limit.angular_axes) {
            if ((axis >= 0) && (axis < 3)) {
                apply_limit(static_cast<std::size_t>(axis) + 3, limit);
            }
        }
    }

    for (const erhe::scene::Physics_joint_drive& drive : description.drives) {
        if ((drive.axis < 0) || (drive.axis > 2)) {
            log_physics->warn("Joint '{}': drive axis {} is outside 0..2; the drive is dropped", joint_name, drive.axis);
            continue;
        }
        const std::size_t axis = static_cast<std::size_t>(drive.axis) +
            ((drive.type == erhe::scene::Physics_drive_type::e_angular) ? std::size_t{3} : std::size_t{0});
        if (axis_driven[axis]) {
            log_physics->warn(
                "Joint '{}': more than one drive names axis '{}'; the later entry wins",
                joint_name, erhe::physics::joint_axis_token(axis)
            );
        }
        axis_driven[axis] = true;
        item.set_axis_drive(
            axis,
            (drive.mode == erhe::scene::Physics_drive_mode::e_acceleration) ? Joint_axis_drive::acceleration : Joint_axis_drive::force
        );
        // A file that states no maximum force leaves the property unset, and
        // zero is the unlimited force the mirror substitutes infinity for.
        if (std::isfinite(drive.max_force)) {
            item.set_axis_drive_max_force(axis, drive.max_force);
        }
        item.set_axis_drive_position_target(axis, drive.position_target);
        item.set_axis_drive_velocity_target(axis, drive.velocity_target);
        item.set_axis_drive_stiffness      (axis, drive.stiffness);
        item.set_axis_drive_damping        (axis, drive.damping);
    }
}

} // anonymous namespace

void import_physics(
    App_context&                             context,
    const Physics_import_arguments&          arguments,
    const std::shared_ptr<Scene_root>&       scene_root,
    std::vector<std::shared_ptr<Operation>>& operations
)
{
    if (arguments.description == nullptr) {
        return;
    }
    const erhe::scene::Physics_description& physics = *arguments.description;
    if (physics.materials.empty() && physics.collision_filters.empty() && physics.joints.empty() && physics.node_physics.empty()) {
        return;
    }

    std::shared_ptr<Content_library> content_library = scene_root->get_content_library();
    const std::string source_path_string = arguments.path.generic_string();

    Physics_importer importer{.physics = physics};

    // Rigid-body state the neutral description has no field for, applied onto
    // each body's create info before Node_physics construction and onto the
    // attachment once it is built.
    const auto find_body = [&arguments](const erhe::scene::Node* node) -> const Physics_import_body* {
        const std::unordered_map<const erhe::scene::Node*, Physics_import_body>::const_iterator it = arguments.bodies.find(node);
        return (it == arguments.bodies.end()) ? nullptr : &it->second;
    };
    const auto apply_physics_overrides = [&find_body](erhe::physics::IRigid_body_create_info& create_info, const erhe::scene::Node* node) {
        const Physics_import_body* const body = find_body(node);
        if ((body != nullptr) && body->motion_mode.has_value()) {
            create_info.motion_mode = body->motion_mode.value();
        }
    };
    const auto apply_node_physics_properties = [&find_body](Node_physics& node_physics, const erhe::scene::Node* node) {
        const Physics_import_body* const body = find_body(node);
        if (body == nullptr) {
            return;
        }
        apply_record_properties(node_physics, body->properties, body->property_set, Object_property_handling::keep);
    };

    // 1. Shared content-library items (1:1 with the description's top-level
    //    arrays), placed where the file placed them and attached through
    //    undoable operations where it did not. Names come from the file's own
    //    record, else from the description, else are synthesized.
    importer.material_items.reserve(physics.materials.size());
    for (std::size_t i = 0; i < physics.materials.size(); ++i) {
        const erhe::scene::Physics_material_description& description = physics.materials[i];
        const Physics_import_item* record = record_of(arguments.materials, i);
        const std::string name = record_name(record, description.name, "Physics material", i);
        auto item = std::make_shared<erhe::physics::Physics_material>(name);
        item->set_static_friction    (description.static_friction);
        item->set_dynamic_friction   (description.dynamic_friction);
        item->set_restitution        (description.restitution);
        item->set_friction_combine   (to_combine_mode(description.friction_combine));
        item->set_restitution_combine(to_combine_mode(description.restitution_combine));
        if (record != nullptr) {
            apply_record_properties(*item, record->properties, record->property_set, Object_property_handling::apply);
        }
        importer.material_items.push_back(item);
        place_library_item(
            context, content_library, item, record,
            Gltf_source_reference{
                .gltf_path  = source_path_string,
                .item_name  = name,
                .item_index = static_cast<int>(i),
                .item_type  = "physics_material",
            },
            operations
        );
    }

    importer.filter_items.reserve(physics.collision_filters.size());
    for (std::size_t i = 0; i < physics.collision_filters.size(); ++i) {
        const erhe::scene::Physics_collision_filter_description& description = physics.collision_filters[i];
        const Physics_import_item* record = record_of(arguments.collision_filters, i);
        const std::string name = record_name(record, description.name, "Collision filter", i);
        auto item = std::make_shared<erhe::physics::Collision_filter>(name);
        item->set_collision_systems       (description.collision_systems);
        item->set_collide_with_systems    (description.collide_with_systems);
        item->set_not_collide_with_systems(description.not_collide_with_systems);
        if (record != nullptr) {
            apply_record_properties(*item, record->properties, record->property_set, Object_property_handling::apply);
        }
        importer.filter_items.push_back(item);
        place_library_item(
            context, content_library, item, record,
            Gltf_source_reference{
                .gltf_path  = source_path_string,
                .item_name  = name,
                .item_index = static_cast<int>(i),
                .item_type  = "collision_filter",
            },
            operations
        );
    }

    importer.joint_items.reserve(physics.joints.size());
    for (std::size_t i = 0; i < physics.joints.size(); ++i) {
        const erhe::scene::Physics_joint_description& description = physics.joints[i];
        const Physics_import_item* record = record_of(arguments.joint_settings, i);
        const std::string name = record_name(record, description.name, "Physics joint", i);
        auto item = std::make_shared<erhe::physics::Physics_joint_settings>(name);
        apply_joint_description_to_item(description, name, *item.get());
        if (record != nullptr) {
            apply_record_properties(*item, record->properties, record->property_set, Object_property_handling::apply);
        }
        importer.joint_items.push_back(item);
        place_library_item(
            context, content_library, item, record,
            Gltf_source_reference{
                .gltf_path  = source_path_string,
                .item_name  = name,
                .item_index = static_cast<int>(i),
                .item_type  = "physics_joint",
            },
            operations
        );
    }

    // 2. Per-node bodies / triggers.
    for (const erhe::scene::Physics_node_description& description : physics.node_physics) {
        if (description.node) {
            importer.description_by_node.emplace(description.node.get(), &description);
        }
    }

    // Nodes listed in a compound trigger get no Node_physics of their own;
    // their trigger geometries fold into the compound trigger body.
    std::unordered_set<const erhe::scene::Node*> compound_trigger_members;
    for (const erhe::scene::Physics_node_description& description : physics.node_physics) {
        if (description.trigger.has_value()) {
            for (const std::shared_ptr<erhe::scene::Node>& member : description.trigger->compound_nodes) {
                compound_trigger_members.insert(member.get());
            }
        }
    }

    // Body root of a collider node: the nearest self-or-ancestor with motion
    // (the collider belongs to that rigid body); when no self-or-ancestor has
    // motion, the topmost collider-carrying self-or-ancestor becomes a static
    // body root and everything below it folds into its compound.
    const auto find_body_root = [&importer](erhe::scene::Node* node) -> erhe::scene::Node* {
        erhe::scene::Node* topmost_collider_node = nullptr;
        erhe::scene::Node* current = node;
        while (current != nullptr) {
            const auto it = importer.description_by_node.find(current);
            if (it != importer.description_by_node.end()) {
                const erhe::scene::Physics_node_description* description = it->second;
                if (description->motion.has_value()) {
                    return current;
                }
                if (description->collider.has_value()) {
                    topmost_collider_node = current;
                }
            }
            current = current->get_parent_node().get();
        }
        return topmost_collider_node;
    };

    // Group collider descriptions by body root, keeping first-seen root order
    // for deterministic processing.
    std::unordered_map<erhe::scene::Node*, std::vector<const erhe::scene::Physics_node_description*>> colliders_by_root;
    std::vector<erhe::scene::Node*> body_roots;
    for (const erhe::scene::Physics_node_description& description : physics.node_physics) {
        if (!description.node || !description.collider.has_value()) {
            continue;
        }
        erhe::scene::Node* root = find_body_root(description.node.get());
        auto [it, inserted] = colliders_by_root.try_emplace(root);
        if (inserted) {
            body_roots.push_back(root);
        }
        it->second.push_back(&description);
    }

    std::size_t body_count    = 0;
    std::size_t trigger_count = 0;
    std::size_t joint_count   = 0;
    std::unordered_set<const erhe::scene::Node*> nodes_with_body;
    // The bodies that are the trigger their description states: the trigger
    // pass below has nothing left to build for them.
    std::unordered_set<const erhe::scene::Node*> nodes_with_trigger_body;
    // Nodes whose collider / trigger geometry folded into another node's
    // body; pure shape-carrier nodes among them are removed at the end (see
    // the cleanup pass below).
    std::unordered_set<erhe::scene::Node*> folded_contributor_nodes;

    for (erhe::scene::Node* root : body_roots) {
        const std::vector<const erhe::scene::Physics_node_description*>& collider_descriptions = colliders_by_root[root];
        const auto root_description_it = importer.description_by_node.find(root);
        const erhe::scene::Physics_node_description* root_description =
            (root_description_it != importer.description_by_node.end()) ? root_description_it->second : nullptr;
        const std::optional<erhe::scene::Physics_node_motion> motion =
            (root_description != nullptr) ? root_description->motion : std::optional<erhe::scene::Physics_node_motion>{};
        const bool dynamic_body = motion.has_value() && !motion->is_kinematic;

        // Build the per-collider shapes (root's own collider first) and pick
        // the body material / filter (root's collider preferred, else first
        // contributing collider; differing assignments are reported - erhe
        // bodies carry a single material / filter).
        std::vector<Body_shape_entry> entries;
        // The mesh a hull / triangle shape of this body was built from when
        // that mesh is a prim below the body rather than the body's own
        // (the `Rock/shell` form): the body remembers it, so a save states
        // the collider on that prim again. A body collecting more than one
        // mesh shape has no single source mesh and remembers none.
        std::shared_ptr<erhe::scene::Mesh> collision_mesh{};
        std::size_t                        mesh_geometry_count = 0;
        std::shared_ptr<erhe::physics::Physics_material> body_material{};
        std::shared_ptr<erhe::physics::Collision_filter> body_filter{};
        bool material_conflict = false;
        bool filter_conflict   = false;
        for (int pass = 0; pass < 2; ++pass) {
            for (const erhe::scene::Physics_node_description* description : collider_descriptions) {
                const bool is_root = (description->node.get() == root);
                if ((pass == 0) != is_root) {
                    continue;
                }
                const erhe::scene::Physics_node_collider& collider = description->collider.value();
                const std::shared_ptr<erhe::physics::Physics_material> material = importer.get_material(collider.material_index);
                if (material) {
                    if (!body_material) {
                        body_material = material;
                    } else if (body_material != material) {
                        material_conflict = true;
                    }
                }
                const std::shared_ptr<erhe::physics::Collision_filter> filter = importer.get_filter(collider.filter_index);
                if (filter) {
                    if (!body_filter) {
                        body_filter = filter;
                    } else if (body_filter != filter) {
                        filter_conflict = true;
                    }
                }
                std::shared_ptr<erhe::physics::ICollision_shape> shape =
                    importer.build_geometry_shape(collider.geometry, *description->node.get(), dynamic_body);
                if (!shape) {
                    continue;
                }
                entries.push_back(Body_shape_entry{.node = description->node.get(), .shape = std::move(shape)});
                if (collider.geometry.mesh || collider.geometry.node) {
                    ++mesh_geometry_count;
                    if (description->node.get() != root) {
                        collision_mesh = erhe::scene::get_mesh(description->node.get());
                    }
                }
                if (description->node.get() != root) {
                    // Non-root contributors always fold into the root's
                    // compound below; candidates for the carrier cleanup.
                    folded_contributor_nodes.insert(description->node.get());
                }
            }
        }
        if (material_conflict) {
            log_parsers->warn("physics import: body '{}' compound colliders use differing physics materials - using the first", root->get_name());
        }
        if (filter_conflict) {
            log_parsers->warn("physics import: body '{}' compound colliders use differing collision filters - using the first", root->get_name());
        }

        std::shared_ptr<erhe::physics::ICollision_shape> body_shape{};
        if ((entries.size() == 1) && (entries.front().node == root)) {
            body_shape = entries.front().shape;
        } else if (!entries.empty()) {
            // Descendant colliders fold into a compound on the body root with
            // their transforms relative to the root baked in. Known
            // limitation: editing a child node transform after import does
            // not rebuild the compound.
            erhe::physics::Compound_shape_create_info compound_create_info{};
            compound_create_info.children.reserve(entries.size());
            for (const Body_shape_entry& entry : entries) {
                compound_create_info.children.push_back(
                    erhe::physics::Compound_child{
                        .shape     = entry.shape,
                        .transform = (entry.node == root)
                            ? erhe::physics::Transform{}
                            : relative_body_transform(*root, *entry.node),
                    }
                );
            }
            body_shape = erhe::physics::ICollision_shape::create_compound_shape_shared(compound_create_info);
        } else if (motion.has_value()) {
            // Motion without any usable collider geometry: keep the body so
            // velocities / gravity still apply, with an empty shape.
            log_parsers->warn("physics import: body '{}' has motion but no usable collider geometry - creating body with empty shape", root->get_name());
            body_shape = erhe::physics::ICollision_shape::create_empty_shape_shared();
        } else {
            log_parsers->warn("physics import: static collider '{}' has no usable geometry - skipping", root->get_name());
            continue;
        }

        // A body that states a trigger of its own detects overlaps with the
        // shapes it collected: the colliders below a trigger body are what it
        // senses with, and a trigger carries no physics material.
        const bool body_is_trigger = (root_description != nullptr) && root_description->trigger.has_value();
        if (body_is_trigger && root_description->trigger->filter_index.has_value()) {
            const std::shared_ptr<erhe::physics::Collision_filter> trigger_filter =
                importer.get_filter(root_description->trigger->filter_index);
            if (trigger_filter) {
                body_filter = trigger_filter;
            }
        }
        erhe::physics::IRigid_body_create_info create_info = importer.make_body_create_info(
            *root,
            motion,
            body_shape,
            body_is_trigger ? std::shared_ptr<erhe::physics::Physics_material>{} : body_material,
            body_filter,
            body_is_trigger
        );
        apply_physics_overrides(create_info, root);
        // The imported subtree arrives pre-attached (like meshes); rigid
        // bodies are created when the insert operation gives the nodes a
        // scene host.
        auto node_physics = std::make_shared<Node_physics>(create_info);
        // A trigger senses with the shapes it collected and states them as a
        // trigger of the body prim, which has no place for a source mesh of
        // its own, so only a collider body remembers one.
        if (!body_is_trigger && (mesh_geometry_count == 1) && collision_mesh) {
            node_physics->set_collision_mesh(collision_mesh);
        }
        apply_node_physics_properties(*node_physics, root);
        root->attach(node_physics);
        nodes_with_body.insert(root);
        if (body_is_trigger) {
            nodes_with_trigger_body.insert(root);
            ++trigger_count;
        } else {
            ++body_count;
        }
    }

    // Triggers (sensors). Compound triggers fold the listed descendant
    // trigger geometries into one sensor body; other geometry triggers get
    // their own sensor body. Static triggers are created with the user-facing
    // static motion mode; Node_physics maps them to kinematic non-physical
    // internally (Jolt sensors must be non-static to detect static bodies).
    for (const erhe::scene::Physics_node_description& description : physics.node_physics) {
        if (!description.node || !description.trigger.has_value()) {
            continue;
        }
        erhe::scene::Node* node = description.node.get();
        if (compound_trigger_members.contains(node)) {
            continue; // folded into the owning compound trigger body
        }
        if (nodes_with_trigger_body.contains(node)) {
            continue; // the body built above is this trigger
        }
        if (nodes_with_body.contains(node)) {
            log_parsers->warn("physics import: node '{}' has both a collider body and a trigger - trigger skipped", node->get_name());
            continue;
        }
        const erhe::scene::Physics_node_trigger& trigger = description.trigger.value();

        std::shared_ptr<erhe::physics::ICollision_shape> trigger_shape{};
        std::shared_ptr<erhe::physics::Collision_filter> trigger_filter = importer.get_filter(trigger.filter_index);
        if (!trigger.compound_nodes.empty()) {
            erhe::physics::Compound_shape_create_info compound_create_info{};
            compound_create_info.children.reserve(trigger.compound_nodes.size());
            for (const std::shared_ptr<erhe::scene::Node>& member : trigger.compound_nodes) {
                const auto member_it = importer.description_by_node.find(member.get());
                if (
                    (member_it == importer.description_by_node.end()) ||
                    !member_it->second->trigger.has_value()           ||
                    !member_it->second->trigger->geometry.has_value()
                ) {
                    log_parsers->warn(
                        "physics import: compound trigger '{}' member '{}' has no trigger geometry - skipping member",
                        node->get_name(),
                        member->get_name()
                    );
                    continue;
                }
                const erhe::scene::Physics_node_trigger& member_trigger = member_it->second->trigger.value();
                std::shared_ptr<erhe::physics::ICollision_shape> member_shape =
                    importer.build_geometry_shape(member_trigger.geometry.value(), *member, false);
                if (!member_shape) {
                    continue;
                }
                if (!trigger_filter) {
                    trigger_filter = importer.get_filter(member_trigger.filter_index);
                }
                compound_create_info.children.push_back(
                    erhe::physics::Compound_child{
                        .shape     = std::move(member_shape),
                        .transform = relative_body_transform(*node, *member),
                    }
                );
                if (member.get() != node) {
                    folded_contributor_nodes.insert(member.get());
                }
            }
            if (compound_create_info.children.empty()) {
                log_parsers->warn("physics import: compound trigger '{}' has no usable member geometry - skipping", node->get_name());
                continue;
            }
            trigger_shape = erhe::physics::ICollision_shape::create_compound_shape_shared(compound_create_info);
        } else if (trigger.geometry.has_value()) {
            trigger_shape = importer.build_geometry_shape(trigger.geometry.value(), *node, false);
            if (!trigger_shape) {
                continue;
            }
        } else {
            log_parsers->warn("physics import: trigger '{}' has neither geometry nor compound nodes - skipping", node->get_name());
            continue;
        }

        erhe::physics::IRigid_body_create_info create_info =
            importer.make_body_create_info(*node, description.motion, trigger_shape, {}, trigger_filter, true);
        apply_physics_overrides(create_info, node);
        auto node_physics = std::make_shared<Node_physics>(create_info);
        apply_node_physics_properties(*node_physics, node);
        node->attach(node_physics);
        nodes_with_body.insert(node);
        ++trigger_count;
    }

    // Motion-only bodies (no collider anywhere, no trigger): keep the rigid
    // body so initial velocities / gravity factor still apply.
    for (const erhe::scene::Physics_node_description& description : physics.node_physics) {
        if (!description.node || !description.motion.has_value()) {
            continue;
        }
        erhe::scene::Node* node = description.node.get();
        if (nodes_with_body.contains(node)) {
            continue;
        }
        erhe::physics::IRigid_body_create_info create_info = importer.make_body_create_info(
            *node,
            description.motion,
            erhe::physics::ICollision_shape::create_empty_shape_shared(),
            {},
            {},
            false
        );
        apply_physics_overrides(create_info, node);
        auto node_physics = std::make_shared<Node_physics>(create_info);
        apply_node_physics_properties(*node_physics, node);
        node->attach(node_physics);
        nodes_with_body.insert(node);
        ++body_count;
    }

    // 3. Joints.
    for (const erhe::scene::Physics_node_description& description : physics.node_physics) {
        if (!description.node || !description.joint.has_value()) {
            continue;
        }
        const erhe::scene::Physics_node_joint& joint = description.joint.value();
        std::shared_ptr<erhe::physics::Physics_joint_settings> settings{};
        if (joint.joint_index < importer.joint_items.size()) {
            settings = importer.joint_items[joint.joint_index];
        } else {
            log_parsers->warn(
                "physics import: node '{}' joint index {} out of range - attaching joint without settings",
                description.node->get_name(),
                joint.joint_index
            );
        }
        auto node_joint = std::make_shared<Node_joint>(joint.connected_node, settings, joint.enable_collision);
        description.node->attach(node_joint);
        ++joint_count;
    }

    // 4. Remove pure shape-carrier nodes. The erhe exporter synthesizes
    // "<body>_collider_<i>" child nodes for compound shape entries that
    // cannot sit on the body node itself; once such a node's geometry is
    // folded into the owning body's compound above, the node has served its
    // whole purpose. Left in the scene it would be re-exported as a content
    // node while the exporter synthesizes a fresh carrier next to it, growing
    // the node set on every save/open cycle. Only a prim whose whole content
    // was that shape is removed: a plain `Xform`, which carries nothing of
    // its own (a `Mesh` prim IS its geometry and stays, and so does every
    // other typed prim), with no attachments (light, joint, own body, ...)
    // and no children (external assets instantiate before physics import, so
    // carrier nodes with instanced content have children here).
    for (erhe::scene::Node* node : folded_contributor_nodes) {
        if (nodes_with_body.contains(node)) {
            continue;
        }
        if (!erhe::is<erhe::scene::Xform>(node)) {
            continue;
        }
        if (!node->get_attachments().empty() || !node->get_children().empty()) {
            continue;
        }
        log_parsers->info("physics import: removing folded shape-carrier node '{}'", node->get_name());
        node->set_parent({});
    }

    log_parsers->info(
        "physics import: {} materials, {} collision filters, {} joint settings, {} bodies, {} triggers, {} joints imported",
        importer.material_items.size(),
        importer.filter_items.size(),
        importer.joint_items.size(),
        body_count,
        trigger_count,
        joint_count
    );
}

}
