#include "parsers/gltf_physics_import.hpp"

#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "operations/content_library_attach_operation.hpp"
#include "scene/collision_shape_from_mesh.hpp"
#include "scene/node_joint.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"

#include "scene/generated/gltf_source_reference.hpp"

#include "erhe_gltf/gltf.hpp"
#include "erhe_physics/collision_filter.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_physics/physics_joint_settings.hpp"
#include "erhe_physics/physics_material.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/trs_transform.hpp"

#include <fmt/format.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <unordered_map>
#include <unordered_set>

namespace editor {

namespace {

[[nodiscard]] auto to_combine_mode(const erhe::gltf::Physics_combine_mode mode) -> erhe::physics::Combine_mode
{
    switch (mode) {
        case erhe::gltf::Physics_combine_mode::e_average:  return erhe::physics::Combine_mode::e_average;
        case erhe::gltf::Physics_combine_mode::e_minimum:  return erhe::physics::Combine_mode::e_minimum;
        case erhe::gltf::Physics_combine_mode::e_maximum:  return erhe::physics::Combine_mode::e_maximum;
        case erhe::gltf::Physics_combine_mode::e_multiply: return erhe::physics::Combine_mode::e_multiply;
        default:                                           return erhe::physics::Combine_mode::e_average;
    }
}

// KHR_implicit_shapes shape entry -> erhe collision shape. glTF implicit
// shapes are centered at the origin and aligned along the Y axis.
[[nodiscard]] auto make_implicit_shape(const erhe::gltf::Physics_shape& shape) -> std::shared_ptr<erhe::physics::ICollision_shape>
{
    using erhe::physics::Axis;
    using erhe::physics::ICollision_shape;
    switch (shape.type) {
        case erhe::gltf::Physics_shape_type::e_sphere: {
            return ICollision_shape::create_sphere_shape_shared(shape.radius);
        }
        case erhe::gltf::Physics_shape_type::e_box: {
            // glTF box size is full extents.
            return ICollision_shape::create_box_shape_shared(shape.size * 0.5f);
        }
        case erhe::gltf::Physics_shape_type::e_capsule: {
            // KHR_implicit_shapes capsule height is "the distance between the
            // centers of the two capping spheres" (shape.capsule schema),
            // which is exactly the erhe capsule length convention - the value
            // passes through unmodified.
            if (shape.radius_bottom == shape.radius_top) {
                return ICollision_shape::create_capsule_shape_shared(Axis::Y, shape.radius_bottom, shape.height);
            }
            return ICollision_shape::create_tapered_capsule_shape_shared(Axis::Y, shape.radius_bottom, shape.radius_top, shape.height);
        }
        case erhe::gltf::Physics_shape_type::e_cylinder: {
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
        log_parsers->info("gltf physics: node '{}' has all-zero scale - collision shape disabled", node.get_name());
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

class Gltf_physics_importer
{
public:
    const erhe::gltf::Gltf_physics_data&                                physics;
    std::vector<std::shared_ptr<erhe::physics::Physics_material>>       material_items;
    std::vector<std::shared_ptr<erhe::physics::Collision_filter>>       filter_items;
    std::vector<std::shared_ptr<erhe::physics::Physics_joint_settings>> joint_items;

    std::unordered_map<const erhe::scene::Node*, const erhe::gltf::Physics_node_description*> description_by_node;

    [[nodiscard]] auto get_material(const std::optional<std::size_t>& index) const -> std::shared_ptr<erhe::physics::Physics_material>
    {
        if (!index.has_value()) {
            return {};
        }
        if (index.value() >= material_items.size()) {
            log_parsers->warn("gltf physics: physics material index {} out of range", index.value());
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
            log_parsers->warn("gltf physics: collision filter index {} out of range", index.value());
            return {};
        }
        return filter_items[index.value()];
    }

    // Builds the (node-scale adjusted) collision shape for one collider /
    // trigger geometry. Returns nullptr when the geometry cannot be built
    // (degenerate scale, missing mesh geometry, unsupported geometry form);
    // failures are logged.
    [[nodiscard]] auto build_geometry_shape(
        const erhe::gltf::Physics_node_geometry& geometry,
        const erhe::scene::Node&                 collider_node,
        const bool                               dynamic_body
    ) const -> std::shared_ptr<erhe::physics::ICollision_shape>
    {
        std::shared_ptr<erhe::physics::ICollision_shape> shape{};
        if (geometry.shape_index.has_value()) {
            const std::size_t shape_index = geometry.shape_index.value();
            if (shape_index >= physics.shapes.size()) {
                log_parsers->warn("gltf physics: node '{}' shape index {} out of range", collider_node.get_name(), shape_index);
                return {};
            }
            shape = make_implicit_shape(physics.shapes[shape_index]);
        } else if (geometry.mesh || geometry.node) {
            // Jolt restriction: triangle mesh shapes are usable with static /
            // kinematic bodies only; dynamic bodies fall back to a convex hull.
            bool convex_hull = geometry.convex_hull;
            if (dynamic_body && !convex_hull) {
                log_parsers->warn(
                    "gltf physics: node '{}' uses a triangle mesh collider on a dynamic body - using convex hull instead (Jolt restriction)",
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
                    "gltf physics: node '{}' {} collider could not be built from {} '{}' geometry",
                    collider_node.get_name(),
                    convex_hull ? "convex hull" : "mesh",
                    geometry.mesh ? "mesh" : "node",
                    geometry.mesh ? geometry.mesh->get_name() : geometry.node->get_name()
                );
                return {};
            }
        } else {
            log_parsers->warn(
                "gltf physics: node '{}' collider geometry has neither shape nor mesh nor node - skipping",
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
        const erhe::scene::Node&                                node,
        const std::optional<erhe::gltf::Physics_node_motion>&   motion,
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

} // anonymous namespace

void import_gltf_physics(
    App_context&                             context,
    const erhe::gltf::Gltf_data&             gltf_data,
    const std::shared_ptr<Scene_root>&       scene_root,
    const std::filesystem::path&             path,
    std::vector<std::shared_ptr<Operation>>& operations
)
{
    static_cast<void>(context);

    const erhe::gltf::Gltf_physics_data& physics = gltf_data.physics;
    if (physics.materials.empty() && physics.collision_filters.empty() && physics.joints.empty() && physics.node_physics.empty()) {
        return;
    }

    std::shared_ptr<Content_library> content_library = scene_root->get_content_library();
    const std::string gltf_path_str = path.generic_string();

    Gltf_physics_importer importer{.physics = physics};

    // 1. Shared content-library items (1:1 with the glTF top-level arrays),
    //    attached through undoable operations like materials / textures.
    importer.material_items.reserve(physics.materials.size());
    for (std::size_t i = 0; i < physics.materials.size(); ++i) {
        const erhe::gltf::Physics_material_description& description = physics.materials[i];
        const std::string name = description.name.empty() ? fmt::format("Physics material {}", i) : description.name;
        auto item = std::make_shared<erhe::physics::Physics_material>(name);
        item->static_friction     = description.static_friction;
        item->dynamic_friction    = description.dynamic_friction;
        item->restitution         = description.restitution;
        item->friction_combine    = to_combine_mode(description.friction_combine);
        item->restitution_combine = to_combine_mode(description.restitution_combine);
        importer.material_items.push_back(item);
        operations.push_back(
            std::make_shared<Content_library_attach_operation<erhe::physics::Physics_material>>(
                content_library,
                content_library->physics_materials,
                item,
                Gltf_source_reference{
                    .gltf_path  = gltf_path_str,
                    .item_name  = name,
                    .item_index = static_cast<int>(i),
                    .item_type  = "physics_material",
                }
            )
        );
    }

    importer.filter_items.reserve(physics.collision_filters.size());
    for (std::size_t i = 0; i < physics.collision_filters.size(); ++i) {
        const erhe::gltf::Physics_collision_filter_description& description = physics.collision_filters[i];
        const std::string name = description.name.empty() ? fmt::format("Collision filter {}", i) : description.name;
        auto item = std::make_shared<erhe::physics::Collision_filter>(name);
        item->collision_systems        = description.collision_systems;
        item->collide_with_systems     = description.collide_with_systems;
        item->not_collide_with_systems = description.not_collide_with_systems;
        importer.filter_items.push_back(item);
        operations.push_back(
            std::make_shared<Content_library_attach_operation<erhe::physics::Collision_filter>>(
                content_library,
                content_library->collision_filters,
                item,
                Gltf_source_reference{
                    .gltf_path  = gltf_path_str,
                    .item_name  = name,
                    .item_index = static_cast<int>(i),
                    .item_type  = "collision_filter",
                }
            )
        );
    }

    importer.joint_items.reserve(physics.joints.size());
    for (std::size_t i = 0; i < physics.joints.size(); ++i) {
        const erhe::gltf::Physics_joint_description& description = physics.joints[i];
        const std::string name = description.name.empty() ? fmt::format("Physics joint {}", i) : description.name;
        auto item = std::make_shared<erhe::physics::Physics_joint_settings>(name);
        item->limits.reserve(description.limits.size());
        for (const erhe::gltf::Physics_joint_limit& limit : description.limits) {
            erhe::physics::Joint_limit out_limit{};
            for (const int axis : limit.linear_axes) {
                if ((axis >= 0) && (axis < 3)) {
                    out_limit.linear_axes[axis] = true;
                }
            }
            for (const int axis : limit.angular_axes) {
                if ((axis >= 0) && (axis < 3)) {
                    out_limit.angular_axes[axis] = true;
                }
            }
            out_limit.min       = limit.min;
            out_limit.max       = limit.max;
            out_limit.stiffness = limit.stiffness;
            out_limit.damping   = limit.damping;
            item->limits.push_back(out_limit);
        }
        item->drives.reserve(description.drives.size());
        for (const erhe::gltf::Physics_joint_drive& drive : description.drives) {
            erhe::physics::Joint_drive out_drive{};
            out_drive.type = (drive.type == erhe::gltf::Physics_drive_type::e_angular)
                ? erhe::physics::Drive_type::e_angular
                : erhe::physics::Drive_type::e_linear;
            out_drive.mode = (drive.mode == erhe::gltf::Physics_drive_mode::e_acceleration)
                ? erhe::physics::Drive_mode::e_acceleration
                : erhe::physics::Drive_mode::e_force;
            out_drive.axis            = drive.axis;
            out_drive.max_force       = drive.max_force;
            out_drive.position_target = drive.position_target;
            out_drive.velocity_target = drive.velocity_target;
            out_drive.stiffness       = drive.stiffness;
            out_drive.damping         = drive.damping;
            item->drives.push_back(out_drive);
        }
        importer.joint_items.push_back(item);
        operations.push_back(
            std::make_shared<Content_library_attach_operation<erhe::physics::Physics_joint_settings>>(
                content_library,
                content_library->physics_joints,
                item,
                Gltf_source_reference{
                    .gltf_path  = gltf_path_str,
                    .item_name  = name,
                    .item_index = static_cast<int>(i),
                    .item_type  = "physics_joint",
                }
            )
        );
    }

    // 2. Per-node bodies / triggers.
    for (const erhe::gltf::Physics_node_description& description : physics.node_physics) {
        if (description.node) {
            importer.description_by_node.emplace(description.node.get(), &description);
        }
    }

    // Nodes listed in a compound trigger get no Node_physics of their own;
    // their trigger geometries fold into the compound trigger body.
    std::unordered_set<const erhe::scene::Node*> compound_trigger_members;
    for (const erhe::gltf::Physics_node_description& description : physics.node_physics) {
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
                const erhe::gltf::Physics_node_description* description = it->second;
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
    std::unordered_map<erhe::scene::Node*, std::vector<const erhe::gltf::Physics_node_description*>> colliders_by_root;
    std::vector<erhe::scene::Node*> body_roots;
    for (const erhe::gltf::Physics_node_description& description : physics.node_physics) {
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

    for (erhe::scene::Node* root : body_roots) {
        const std::vector<const erhe::gltf::Physics_node_description*>& collider_descriptions = colliders_by_root[root];
        const auto root_description_it = importer.description_by_node.find(root);
        const erhe::gltf::Physics_node_description* root_description =
            (root_description_it != importer.description_by_node.end()) ? root_description_it->second : nullptr;
        const std::optional<erhe::gltf::Physics_node_motion> motion =
            (root_description != nullptr) ? root_description->motion : std::optional<erhe::gltf::Physics_node_motion>{};
        const bool dynamic_body = motion.has_value() && !motion->is_kinematic;

        // Build the per-collider shapes (root's own collider first) and pick
        // the body material / filter (root's collider preferred, else first
        // contributing collider; differing assignments are reported - erhe
        // bodies carry a single material / filter).
        std::vector<Body_shape_entry> entries;
        std::shared_ptr<erhe::physics::Physics_material> body_material{};
        std::shared_ptr<erhe::physics::Collision_filter> body_filter{};
        bool material_conflict = false;
        bool filter_conflict   = false;
        for (int pass = 0; pass < 2; ++pass) {
            for (const erhe::gltf::Physics_node_description* description : collider_descriptions) {
                const bool is_root = (description->node.get() == root);
                if ((pass == 0) != is_root) {
                    continue;
                }
                const erhe::gltf::Physics_node_collider& collider = description->collider.value();
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
            }
        }
        if (material_conflict) {
            log_parsers->warn("gltf physics: body '{}' compound colliders use differing physics materials - using the first", root->get_name());
        }
        if (filter_conflict) {
            log_parsers->warn("gltf physics: body '{}' compound colliders use differing collision filters - using the first", root->get_name());
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
            log_parsers->warn("gltf physics: body '{}' has motion but no usable collider geometry - creating body with empty shape", root->get_name());
            body_shape = erhe::physics::ICollision_shape::create_empty_shape_shared();
        } else {
            log_parsers->warn("gltf physics: static collider '{}' has no usable geometry - skipping", root->get_name());
            continue;
        }

        const erhe::physics::IRigid_body_create_info create_info =
            importer.make_body_create_info(*root, motion, body_shape, body_material, body_filter, false);
        // The imported subtree arrives pre-attached (like meshes); rigid
        // bodies are created when the insert operation gives the nodes a
        // scene host.
        auto node_physics = std::make_shared<Node_physics>(create_info);
        root->attach(node_physics);
        nodes_with_body.insert(root);
        ++body_count;
    }

    // Triggers (sensors). Compound triggers fold the listed descendant
    // trigger geometries into one sensor body; other geometry triggers get
    // their own sensor body. Static triggers are created with the user-facing
    // static motion mode; Node_physics maps them to kinematic non-physical
    // internally (Jolt sensors must be non-static to detect static bodies).
    for (const erhe::gltf::Physics_node_description& description : physics.node_physics) {
        if (!description.node || !description.trigger.has_value()) {
            continue;
        }
        erhe::scene::Node* node = description.node.get();
        if (compound_trigger_members.contains(node)) {
            continue; // folded into the owning compound trigger body
        }
        if (nodes_with_body.contains(node)) {
            log_parsers->warn("gltf physics: node '{}' has both a collider body and a trigger - trigger skipped", node->get_name());
            continue;
        }
        const erhe::gltf::Physics_node_trigger& trigger = description.trigger.value();

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
                        "gltf physics: compound trigger '{}' member '{}' has no trigger geometry - skipping member",
                        node->get_name(),
                        member->get_name()
                    );
                    continue;
                }
                const erhe::gltf::Physics_node_trigger& member_trigger = member_it->second->trigger.value();
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
            }
            if (compound_create_info.children.empty()) {
                log_parsers->warn("gltf physics: compound trigger '{}' has no usable member geometry - skipping", node->get_name());
                continue;
            }
            trigger_shape = erhe::physics::ICollision_shape::create_compound_shape_shared(compound_create_info);
        } else if (trigger.geometry.has_value()) {
            trigger_shape = importer.build_geometry_shape(trigger.geometry.value(), *node, false);
            if (!trigger_shape) {
                continue;
            }
        } else {
            log_parsers->warn("gltf physics: trigger '{}' has neither geometry nor compound nodes - skipping", node->get_name());
            continue;
        }

        const erhe::physics::IRigid_body_create_info create_info =
            importer.make_body_create_info(*node, description.motion, trigger_shape, {}, trigger_filter, true);
        auto node_physics = std::make_shared<Node_physics>(create_info);
        node->attach(node_physics);
        nodes_with_body.insert(node);
        ++trigger_count;
    }

    // Motion-only bodies (no collider anywhere, no trigger): keep the rigid
    // body so initial velocities / gravity factor still apply.
    for (const erhe::gltf::Physics_node_description& description : physics.node_physics) {
        if (!description.node || !description.motion.has_value()) {
            continue;
        }
        erhe::scene::Node* node = description.node.get();
        if (nodes_with_body.contains(node)) {
            continue;
        }
        const erhe::physics::IRigid_body_create_info create_info = importer.make_body_create_info(
            *node,
            description.motion,
            erhe::physics::ICollision_shape::create_empty_shape_shared(),
            {},
            {},
            false
        );
        auto node_physics = std::make_shared<Node_physics>(create_info);
        node->attach(node_physics);
        nodes_with_body.insert(node);
        ++body_count;
    }

    // 3. Joints.
    for (const erhe::gltf::Physics_node_description& description : physics.node_physics) {
        if (!description.node || !description.joint.has_value()) {
            continue;
        }
        const erhe::gltf::Physics_node_joint& joint = description.joint.value();
        std::shared_ptr<erhe::physics::Physics_joint_settings> settings{};
        if (joint.joint_index < importer.joint_items.size()) {
            settings = importer.joint_items[joint.joint_index];
        } else {
            log_parsers->warn(
                "gltf physics: node '{}' joint index {} out of range - attaching joint without settings",
                description.node->get_name(),
                joint.joint_index
            );
        }
        auto node_joint = std::make_shared<Node_joint>(joint.connected_node, settings, joint.enable_collision);
        description.node->attach(node_joint);
        ++joint_count;
    }

    log_parsers->info(
        "gltf physics: {} materials, {} collision filters, {} joint settings, {} bodies, {} triggers, {} joints imported",
        importer.material_items.size(),
        importer.filter_items.size(),
        importer.joint_items.size(),
        body_count,
        trigger_count,
        joint_count
    );
}

}
