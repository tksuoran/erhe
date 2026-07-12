#include "parsers/gltf_physics_export.hpp"

#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "geometry_graph/geometry_graph_mesh.hpp"
#include "scene/node_joint.hpp"
#include "scene/node_physics.hpp"

#include "erhe_physics/collision_filter.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_physics/physics_joint_settings.hpp"
#include "erhe_physics/physics_material.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/trs_transform.hpp"

#include <fmt/format.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <unordered_map>
#include <vector>

namespace editor {

namespace {

constexpr float k_scale_epsilon     = 1.0e-4f;
constexpr float k_transform_epsilon = 1.0e-5f;

[[nodiscard]] auto to_gltf_combine_mode(const erhe::physics::Combine_mode mode) -> erhe::gltf::Physics_combine_mode
{
    switch (mode) {
        case erhe::physics::Combine_mode::e_average:  return erhe::gltf::Physics_combine_mode::e_average;
        case erhe::physics::Combine_mode::e_minimum:  return erhe::gltf::Physics_combine_mode::e_minimum;
        case erhe::physics::Combine_mode::e_maximum:  return erhe::gltf::Physics_combine_mode::e_maximum;
        case erhe::physics::Combine_mode::e_multiply: return erhe::gltf::Physics_combine_mode::e_multiply;
        default:                                      return erhe::gltf::Physics_combine_mode::e_average;
    }
}

// Rotation taking the Y axis (the glTF implicit shape axis) onto the erhe
// shape axis. Inverse of the axis handling in the Jolt shape wrappers.
[[nodiscard]] auto axis_rotation(const erhe::physics::Axis axis) -> glm::quat
{
    switch (axis) {
        case erhe::physics::Axis::X: return glm::angleAxis(-glm::half_pi<float>(), glm::vec3{0.0f, 0.0f, 1.0f});
        case erhe::physics::Axis::Z: return glm::angleAxis( glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f});
        case erhe::physics::Axis::Y:
        default:                     return glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
    }
}

[[nodiscard]] auto is_identity_rotation(const glm::quat& q) -> bool
{
    return std::abs(1.0f - std::abs(q.w)) < k_transform_epsilon;
}

[[nodiscard]] auto same_shape(const erhe::gltf::Physics_shape& a, const erhe::gltf::Physics_shape& b) -> bool
{
    return
        (a.type          == b.type)          &&
        (a.radius        == b.radius)        &&
        (a.size          == b.size)          &&
        (a.height        == b.height)        &&
        (a.radius_bottom == b.radius_bottom) &&
        (a.radius_top    == b.radius_top);
}

// One collider geometry contributed by a body's collision shape after
// unwrapping: the wrapper-free base shape plus the transform of its
// (possibly synthesized) collider node relative to the body node. The
// accumulated wrapper scale is what import re-applies from the collider
// node's world scale.
class Flat_shape_entry
{
public:
    const erhe::physics::ICollision_shape* base{nullptr};
    glm::quat                               rotation     {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3                               translation  {0.0f};
    glm::vec3                               wrapper_scale{1.0f};
};

// Unwraps scale wrappers (uniform / non-uniform), accumulating their scale.
// Center-of-mass offset wrappers do not appear here: the body-level shape has
// its OCOM wrapper removed before flattening (the offset is exported as
// motion.centerOfMass), and inner shapes never carry one.
[[nodiscard]] auto unwrap_scale(const erhe::physics::ICollision_shape* shape, glm::vec3& scale) -> const erhe::physics::ICollision_shape*
{
    std::shared_ptr<erhe::physics::ICollision_shape> inner = shape->get_inner_shape();
    while (inner) {
        const std::optional<glm::vec3> wrapper_scale = shape->get_scale();
        if (wrapper_scale.has_value()) {
            scale *= wrapper_scale.value();
        }
        shape = inner.get();
        inner = shape->get_inner_shape();
    }
    return shape;
}

class Gltf_physics_builder
{
public:
    erhe::gltf::Gltf_physics_data data;

    std::unordered_map<const erhe::physics::Physics_material*,       std::size_t> material_index_map;
    std::unordered_map<const erhe::physics::Collision_filter*,       std::size_t> filter_index_map;
    std::unordered_map<const erhe::physics::Physics_joint_settings*, std::size_t> joint_index_map;

    [[nodiscard]] auto get_material_index(const std::shared_ptr<erhe::physics::Physics_material>& material) -> std::optional<std::size_t>
    {
        if (!material) {
            return std::nullopt;
        }
        const auto it = material_index_map.find(material.get());
        if (it != material_index_map.end()) {
            return it->second;
        }
        const std::size_t index = data.materials.size();
        material_index_map.emplace(material.get(), index);
        erhe::gltf::Physics_material_description description{};
        description.name                = material->get_name();
        description.static_friction     = material->static_friction;
        description.dynamic_friction    = material->dynamic_friction;
        description.restitution         = material->restitution;
        description.friction_combine    = to_gltf_combine_mode(material->friction_combine);
        description.restitution_combine = to_gltf_combine_mode(material->restitution_combine);
        data.materials.push_back(std::move(description));
        return index;
    }

    [[nodiscard]] auto get_filter_index(const std::shared_ptr<erhe::physics::Collision_filter>& filter) -> std::optional<std::size_t>
    {
        if (!filter) {
            return std::nullopt;
        }
        const auto it = filter_index_map.find(filter.get());
        if (it != filter_index_map.end()) {
            return it->second;
        }
        const std::size_t index = data.collision_filters.size();
        filter_index_map.emplace(filter.get(), index);
        erhe::gltf::Physics_collision_filter_description description{};
        description.name                     = filter->get_name();
        description.collision_systems        = filter->collision_systems;
        description.collide_with_systems     = filter->collide_with_systems;
        description.not_collide_with_systems = filter->not_collide_with_systems;
        data.collision_filters.push_back(std::move(description));
        return index;
    }

    [[nodiscard]] auto get_joint_index(const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings) -> std::optional<std::size_t>
    {
        if (!settings) {
            return std::nullopt;
        }
        const auto it = joint_index_map.find(settings.get());
        if (it != joint_index_map.end()) {
            return it->second;
        }
        const std::size_t index = data.joints.size();
        joint_index_map.emplace(settings.get(), index);
        erhe::gltf::Physics_joint_description description{};
        description.name = settings->get_name();
        description.limits.reserve(settings->limits.size());
        for (const erhe::physics::Joint_limit& limit : settings->limits) {
            erhe::gltf::Physics_joint_limit out_limit{};
            for (int axis = 0; axis < 3; ++axis) {
                if (limit.linear_axes[static_cast<std::size_t>(axis)]) {
                    out_limit.linear_axes.push_back(axis);
                }
                if (limit.angular_axes[static_cast<std::size_t>(axis)]) {
                    out_limit.angular_axes.push_back(axis);
                }
            }
            out_limit.min       = limit.min;
            out_limit.max       = limit.max;
            out_limit.stiffness = limit.stiffness;
            out_limit.damping   = limit.damping;
            description.limits.push_back(std::move(out_limit));
        }
        description.drives.reserve(settings->drives.size());
        for (const erhe::physics::Joint_drive& drive : settings->drives) {
            erhe::gltf::Physics_joint_drive out_drive{};
            out_drive.type = (drive.type == erhe::physics::Drive_type::e_angular)
                ? erhe::gltf::Physics_drive_type::e_angular
                : erhe::gltf::Physics_drive_type::e_linear;
            out_drive.mode = (drive.mode == erhe::physics::Drive_mode::e_acceleration)
                ? erhe::gltf::Physics_drive_mode::e_acceleration
                : erhe::gltf::Physics_drive_mode::e_force;
            out_drive.axis            = drive.axis;
            out_drive.max_force       = drive.max_force;
            out_drive.position_target = drive.position_target;
            out_drive.velocity_target = drive.velocity_target;
            out_drive.stiffness       = drive.stiffness;
            out_drive.damping         = drive.damping;
            description.drives.push_back(out_drive);
        }
        data.joints.push_back(std::move(description));
        return index;
    }

    // Returns the deduplicated index into data.shapes for an implicit shape
    // (Y-aligned, per KHR_implicit_shapes), or nullopt when the base shape is
    // not an implicit shape type. out_axis receives the erhe shape axis (Y
    // when the shape type has none).
    [[nodiscard]] auto get_implicit_shape_index(
        const erhe::physics::ICollision_shape& base,
        erhe::physics::Axis&                   out_axis
    ) -> std::optional<std::size_t>
    {
        using erhe::physics::Collision_shape_type;
        erhe::gltf::Physics_shape shape{};
        out_axis = base.get_axis().value_or(erhe::physics::Axis::Y);
        switch (base.get_shape_type()) {
            case Collision_shape_type::e_sphere: {
                shape.type   = erhe::gltf::Physics_shape_type::e_sphere;
                shape.radius = base.get_radius().value_or(0.5f);
                break;
            }
            case Collision_shape_type::e_box: {
                shape.type = erhe::gltf::Physics_shape_type::e_box;
                shape.size = 2.0f * base.get_half_extents().value_or(glm::vec3{0.5f});
                break;
            }
            case Collision_shape_type::e_capsule: {
                const float radius  = base.get_radius().value_or(0.25f);
                shape.type          = erhe::gltf::Physics_shape_type::e_capsule;
                shape.height        = base.get_length().value_or(0.5f);
                shape.radius_bottom = radius;
                shape.radius_top    = radius;
                break;
            }
            case Collision_shape_type::e_tapered_capsule: {
                shape.type          = erhe::gltf::Physics_shape_type::e_capsule;
                shape.height        = base.get_length().value_or(0.5f);
                shape.radius_bottom = base.get_bottom_radius().value_or(0.25f);
                shape.radius_top    = base.get_top_radius().value_or(0.25f);
                break;
            }
            case Collision_shape_type::e_cylinder: {
                // Jolt cylinder half extents: x = radius, y = half height.
                const glm::vec3 half_extents = base.get_half_extents().value_or(glm::vec3{0.25f});
                shape.type          = erhe::gltf::Physics_shape_type::e_cylinder;
                shape.height        = 2.0f * half_extents.y;
                shape.radius_bottom = half_extents.x;
                shape.radius_top    = half_extents.x;
                break;
            }
            case Collision_shape_type::e_tapered_cylinder: {
                shape.type          = erhe::gltf::Physics_shape_type::e_cylinder;
                shape.height        = base.get_length().value_or(0.5f);
                shape.radius_bottom = base.get_bottom_radius().value_or(0.25f);
                shape.radius_top    = base.get_top_radius().value_or(0.25f);
                break;
            }
            default: {
                return std::nullopt;
            }
        }
        for (std::size_t i = 0, end = data.shapes.size(); i < end; ++i) {
            if (same_shape(data.shapes[i], shape)) {
                return i;
            }
        }
        data.shapes.push_back(shape);
        return data.shapes.size() - 1;
    }

    // Flattens a body-level collision shape (OCOM wrapper already removed)
    // into per-collider entries: compound children become individual entries
    // with their transforms; everything else is a single identity-transform
    // entry. Scale wrappers are unwrapped into Flat_shape_entry::wrapper_scale.
    [[nodiscard]] auto flatten_shape(const erhe::physics::ICollision_shape* shape) -> std::vector<Flat_shape_entry>
    {
        std::vector<Flat_shape_entry> entries;
        glm::vec3 top_scale{1.0f};
        const erhe::physics::ICollision_shape* base = unwrap_scale(shape, top_scale);
        if (base->get_shape_type() == erhe::physics::Collision_shape_type::e_compound) {
            for (const erhe::physics::Compound_child& child : base->get_children()) {
                if (!child.shape) {
                    continue;
                }
                Flat_shape_entry entry{};
                entry.wrapper_scale = top_scale;
                entry.base          = unwrap_scale(child.shape.get(), entry.wrapper_scale);
                entry.rotation      = glm::quat_cast(child.transform.basis);
                entry.translation   = child.transform.origin;
                entries.push_back(entry);
            }
        } else {
            Flat_shape_entry entry{};
            entry.wrapper_scale = top_scale;
            entry.base          = base;
            entries.push_back(entry);
        }
        return entries;
    }
};

} // anonymous namespace

auto build_gltf_physics_data(const erhe::scene::Scene& scene, const Content_library* content_library) -> erhe::gltf::Gltf_physics_data
{
    Gltf_physics_builder builder{};

    for (const std::shared_ptr<erhe::scene::Node>& node : scene.get_flat_nodes()) {
        std::shared_ptr<Node_physics> node_physics = erhe::scene::get_attachment<Node_physics>(node.get());
        // A Node_physics controlled by a Geometry Graph Mesh attachment is a
        // baked artifact the graph rebuilds on load - persisting it would
        // duplicate the rigid body on every save/load round-trip (same check
        // as save_scene; doc/gltf-scene-roundtrip-plan.md phase 3 exclusion
        // hook).
        if (node_physics) {
            const std::shared_ptr<Geometry_graph_mesh> graph_mesh_attachment = erhe::scene::get_attachment<Geometry_graph_mesh>(node.get());
            if (graph_mesh_attachment && (graph_mesh_attachment->get_controlled_node_physics() == node_physics)) {
                node_physics.reset();
            }
        }
        std::shared_ptr<Node_joint>         node_joint{};
        std::size_t                         joint_count = 0;
        for (const std::shared_ptr<erhe::scene::Node_attachment>& attachment : node->get_attachments()) {
            const std::shared_ptr<Node_joint> joint = std::dynamic_pointer_cast<Node_joint>(attachment);
            if (joint) {
                if (!node_joint) {
                    node_joint = joint;
                }
                ++joint_count;
            }
        }
        if (!node_physics && !node_joint) {
            continue;
        }

        erhe::gltf::Physics_node_description description{};
        description.node = node;
        bool has_content = false;

        if (node_physics) {
            const erhe::physics::Motion_mode motion_mode = node_physics->get_motion_mode();
            const bool is_trigger = node_physics->is_trigger();

            if (motion_mode != erhe::physics::Motion_mode::e_static) {
                erhe::gltf::Physics_node_motion motion{};
                motion.is_kinematic = (motion_mode != erhe::physics::Motion_mode::e_dynamic);
                const erhe::physics::IRigid_body* rigid_body = node_physics->get_rigid_body();
                if (!motion.is_kinematic && (rigid_body != nullptr)) {
                    motion.mass = rigid_body->get_mass();
                }
                motion.center_of_mass = node_physics->get_center_of_mass_offset();
                // Initial velocities are stored in world space; the spec wants
                // them in node space.
                const glm::quat world_rotation_inverse = glm::inverse(node->world_from_node_transform().get_rotation());
                motion.linear_velocity  = world_rotation_inverse * node_physics->get_initial_linear_velocity();
                motion.angular_velocity = world_rotation_inverse * node_physics->get_initial_angular_velocity();
                motion.gravity_factor   = node_physics->get_gravity_factor();
                description.motion = motion;
                has_content = true;
            }

            const std::optional<std::size_t> material_index = is_trigger
                ? std::optional<std::size_t>{}
                : builder.get_material_index(node_physics->get_physics_material());
            const std::optional<std::size_t> filter_index = builder.get_filter_index(node_physics->get_collision_filter());

            // The center-of-mass offset wrapper is the outermost wrapper when
            // present; it is exported as motion.centerOfMass above.
            const std::shared_ptr<erhe::physics::ICollision_shape>& body_shape = node_physics->get_collision_shape();
            const erhe::physics::ICollision_shape* shape = body_shape.get();
            if ((shape != nullptr) && (shape->get_shape_type() == erhe::physics::Collision_shape_type::e_offset_center_of_mass)) {
                shape = shape->get_inner_shape().get();
            }

            if ((shape != nullptr) && (shape->get_shape_type() != erhe::physics::Collision_shape_type::e_empty)) {
                const glm::vec3 node_world_scale = glm::abs(node->world_from_node_transform().get_scale());
                std::vector<Flat_shape_entry> entries = builder.flatten_shape(shape);

                // Build the geometry for each entry; decide whether it can sit
                // directly on the body node (single entry, identity transform,
                // Y axis, wrapper scale matching the node world scale - the
                // form import produces for simple colliders) or needs a
                // synthesized child node.
                std::vector<erhe::gltf::Physics_synthesized_collider> synthesized;
                std::optional<erhe::gltf::Physics_node_geometry>      direct_geometry;

                for (std::size_t entry_index = 0; entry_index < entries.size(); ++entry_index) {
                    const Flat_shape_entry& entry = entries[entry_index];
                    erhe::physics::Axis axis = erhe::physics::Axis::Y;
                    erhe::gltf::Physics_node_geometry geometry{};
                    const erhe::physics::Collision_shape_type base_type = entry.base->get_shape_type();
                    const std::optional<std::size_t> shape_index = builder.get_implicit_shape_index(*entry.base, axis);
                    if (shape_index.has_value()) {
                        geometry.shape_index = shape_index;
                    } else if (
                        (base_type == erhe::physics::Collision_shape_type::e_convex_hull) ||
                        (base_type == erhe::physics::Collision_shape_type::e_mesh)
                    ) {
                        // erhe convention: hull / mesh shapes are built from
                        // the owning node's mesh (compound children carry no
                        // source mesh reference and cannot be exported).
                        if (entries.size() > 1) {
                            log_parsers->warn(
                                "gltf physics export: body '{}' compound child {} is a {} shape with no source mesh reference - skipping child",
                                node->get_name(),
                                entry_index,
                                (base_type == erhe::physics::Collision_shape_type::e_convex_hull) ? "convex hull" : "triangle mesh"
                            );
                            continue;
                        }
                        const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
                        if (!mesh) {
                            log_parsers->warn(
                                "gltf physics export: body '{}' has a {} shape but no mesh attachment - skipping collider",
                                node->get_name(),
                                (base_type == erhe::physics::Collision_shape_type::e_convex_hull) ? "convex hull" : "triangle mesh"
                            );
                            continue;
                        }
                        geometry.mesh        = mesh;
                        geometry.convex_hull = (base_type == erhe::physics::Collision_shape_type::e_convex_hull);
                    } else if (base_type == erhe::physics::Collision_shape_type::e_empty) {
                        continue;
                    } else {
                        log_parsers->warn(
                            "gltf physics export: body '{}' has unsupported collision shape type {} - skipping collider",
                            node->get_name(),
                            static_cast<int>(base_type)
                        );
                        continue;
                    }

                    const glm::quat rotation = entry.rotation * axis_rotation(axis);
                    const glm::vec3 scale_ratio{
                        (node_world_scale.x > 0.0f) ? (entry.wrapper_scale.x / node_world_scale.x) : 1.0f,
                        (node_world_scale.y > 0.0f) ? (entry.wrapper_scale.y / node_world_scale.y) : 1.0f,
                        (node_world_scale.z > 0.0f) ? (entry.wrapper_scale.z / node_world_scale.z) : 1.0f
                    };
                    const bool identity_transform =
                        is_identity_rotation(rotation) &&
                        (glm::length(entry.translation) < k_transform_epsilon);
                    const bool unit_scale_ratio =
                        (std::abs(scale_ratio.x - 1.0f) < k_scale_epsilon) &&
                        (std::abs(scale_ratio.y - 1.0f) < k_scale_epsilon) &&
                        (std::abs(scale_ratio.z - 1.0f) < k_scale_epsilon);

                    if ((entries.size() == 1) && identity_transform && unit_scale_ratio) {
                        direct_geometry = geometry;
                    } else {
                        erhe::gltf::Physics_synthesized_collider collider{};
                        collider.parent         = node;
                        collider.name           = fmt::format("{}_collider_{}", node->get_name(), entry_index);
                        collider.rotation       = rotation;
                        collider.translation    = entry.translation;
                        collider.scale          = scale_ratio;
                        collider.geometry       = geometry;
                        collider.material_index = material_index;
                        collider.filter_index   = filter_index;
                        collider.is_trigger     = is_trigger;
                        synthesized.push_back(std::move(collider));
                    }
                }

                if (is_trigger) {
                    if (direct_geometry.has_value()) {
                        erhe::gltf::Physics_node_trigger trigger{};
                        trigger.geometry     = direct_geometry;
                        trigger.filter_index = filter_index;
                        description.trigger  = std::move(trigger);
                        has_content = true;
                    } else if (!synthesized.empty()) {
                        // Compound trigger: the node gets a node-list trigger
                        // over its synthesized trigger children (the exporter
                        // resolves the children; see gltf_physics.hpp).
                        description.trigger = erhe::gltf::Physics_node_trigger{};
                        has_content = true;
                    } else {
                        log_parsers->warn("gltf physics export: trigger '{}' has no exportable geometry - skipping trigger", node->get_name());
                    }
                } else if (direct_geometry.has_value()) {
                    erhe::gltf::Physics_node_collider collider{};
                    collider.geometry       = direct_geometry.value();
                    collider.material_index = material_index;
                    collider.filter_index   = filter_index;
                    description.collider    = std::move(collider);
                    has_content = true;
                }
                // Bodies represented purely by synthesized child colliders
                // (static compounds without motion) need no rigid-body entry
                // on the body node itself.

                for (erhe::gltf::Physics_synthesized_collider& collider : synthesized) {
                    builder.data.synthesized_colliders.push_back(std::move(collider));
                }
            } else if (description.motion.has_value()) {
                // Motion-only body (empty or missing shape): exported without
                // collider; import recreates it with an empty shape.
            } else {
                log_parsers->warn("gltf physics export: static body '{}' has no collision shape - node not exported as physics", node->get_name());
            }
        }

        if (node_joint) {
            if (joint_count > 1) {
                log_parsers->warn(
                    "gltf physics export: node '{}' has {} joints - glTF supports one joint per node, exporting the first",
                    node->get_name(),
                    joint_count
                );
            }
            const std::shared_ptr<erhe::scene::Node> connected_node = node_joint->get_connected_node();
            const std::optional<std::size_t> joint_index = builder.get_joint_index(node_joint->get_settings());
            if (!connected_node) {
                log_parsers->warn(
                    "gltf physics export: node '{}' joint has no connected node (world attachment is not representable) - skipping joint",
                    node->get_name()
                );
            } else if (!joint_index.has_value()) {
                log_parsers->warn("gltf physics export: node '{}' joint has no settings - skipping joint", node->get_name());
            } else {
                erhe::gltf::Physics_node_joint joint{};
                joint.connected_node   = connected_node;
                joint.joint_index      = joint_index.value();
                joint.enable_collision = node_joint->get_enable_collision();
                description.joint = std::move(joint);
                has_content = true;
            }
        }

        if (has_content) {
            builder.data.node_physics.push_back(std::move(description));
        }
    }

    if (!builder.data.node_physics.empty() || !builder.data.synthesized_colliders.empty()) {
        log_parsers->info(
            "gltf physics export: {} shapes, {} materials, {} collision filters, {} joint settings, {} physics nodes, {} synthesized colliders",
            builder.data.shapes.size(),
            builder.data.materials.size(),
            builder.data.collision_filters.size(),
            builder.data.joints.size(),
            builder.data.node_physics.size(),
            builder.data.synthesized_colliders.size()
        );
    }

    // Append content-library physics items no body / joint references, so
    // editor-authored assets survive save / load (parity with scene.json
    // v3+). get_*_index() deduplicates against the referenced entries above.
    if (content_library != nullptr) {
        if (content_library->physics_materials) {
            for (const std::shared_ptr<erhe::physics::Physics_material>& material : content_library->physics_materials->get_all<erhe::physics::Physics_material>()) {
                static_cast<void>(builder.get_material_index(material));
            }
        }
        if (content_library->collision_filters) {
            for (const std::shared_ptr<erhe::physics::Collision_filter>& filter : content_library->collision_filters->get_all<erhe::physics::Collision_filter>()) {
                static_cast<void>(builder.get_filter_index(filter));
            }
        }
        if (content_library->physics_joints) {
            for (const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings : content_library->physics_joints->get_all<erhe::physics::Physics_joint_settings>()) {
                static_cast<void>(builder.get_joint_index(settings));
            }
        }
    }

    return builder.data;
}

}
