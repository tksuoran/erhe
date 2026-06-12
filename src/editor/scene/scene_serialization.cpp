#include "scene/scene_serialization.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "parsers/gltf.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "scene/collision_shape_from_mesh.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"

#include "scene/generated/scene_file_serialization.hpp"

#include "scene/node_joint.hpp"

#include "erhe_file/file.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_gltf/gltf.hpp"
#include "erhe_physics/collision_filter.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/physics_joint_settings.hpp"
#include "erhe_physics/physics_material.hpp"
#include "erhe_gltf/image_transfer.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/layout.hpp"
#include "erhe_scene/layout_item.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <simdjson.h>

#include <geogram/basic/geofile.h>
#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_io.h>

#include <cmath>
#include <limits>
#include <unordered_map>

namespace editor {

namespace {

auto serialize_transform(const erhe::scene::Trs_transform& trs) -> Transform_data
{
    const glm::vec3 t = trs.get_translation();
    const glm::quat r = trs.get_rotation();
    const glm::vec3 s = trs.get_scale();
    return Transform_data{
        .translation = t,
        .rotation    = glm::vec4{r.x, r.y, r.z, r.w},
        .scale       = s,
    };
}

auto deserialize_transform(const Transform_data& data) -> erhe::scene::Trs_transform
{
    const glm::quat rotation{data.rotation.w, data.rotation.x, data.rotation.y, data.rotation.z};
    return erhe::scene::Trs_transform{data.translation, rotation, data.scale};
}

auto serialize_projection(const erhe::scene::Projection& proj) -> Projection_data
{
    return Projection_data{
        .projection_type = static_cast<int>(proj.projection_type),
        .z_near          = proj.z_near,
        .z_far           = proj.z_far,
        .fov_x           = proj.fov_x,
        .fov_y           = proj.fov_y,
        .fov_left        = proj.fov_left,
        .fov_right       = proj.fov_right,
        .fov_up          = proj.fov_up,
        .fov_down        = proj.fov_down,
        .ortho_left      = proj.ortho_left,
        .ortho_width     = proj.ortho_width,
        .ortho_bottom    = proj.ortho_bottom,
        .ortho_height    = proj.ortho_height,
        .frustum_left    = proj.frustum_left,
        .frustum_right   = proj.frustum_right,
        .frustum_bottom  = proj.frustum_bottom,
        .frustum_top     = proj.frustum_top,
    };
}

void deserialize_projection(const Projection_data& data, erhe::scene::Projection& proj)
{
    proj.projection_type = static_cast<erhe::scene::Projection::Type>(data.projection_type);
    proj.z_near          = data.z_near;
    proj.z_far           = data.z_far;
    proj.fov_x           = data.fov_x;
    proj.fov_y           = data.fov_y;
    proj.fov_left        = data.fov_left;
    proj.fov_right       = data.fov_right;
    proj.fov_up          = data.fov_up;
    proj.fov_down        = data.fov_down;
    proj.ortho_left      = data.ortho_left;
    proj.ortho_width     = data.ortho_width;
    proj.ortho_bottom    = data.ortho_bottom;
    proj.ortho_height    = data.ortho_height;
    proj.frustum_left    = data.frustum_left;
    proj.frustum_right   = data.frustum_right;
    proj.frustum_bottom  = data.frustum_bottom;
    proj.frustum_top     = data.frustum_top;
}

auto to_light_type_serial(erhe::scene::Light_type type) -> Light_type_serial
{
    switch (type) {
        case erhe::scene::Light_type::directional: return Light_type_serial::directional;
        case erhe::scene::Light_type::point:       return Light_type_serial::point;
        case erhe::scene::Light_type::spot:        return Light_type_serial::spot;
        default:                                   return Light_type_serial::directional;
    }
}

auto from_light_type_serial(Light_type_serial type) -> erhe::scene::Light_type
{
    switch (type) {
        case Light_type_serial::directional: return erhe::scene::Light_type::directional;
        case Light_type_serial::point:       return erhe::scene::Light_type::point;
        case Light_type_serial::spot:        return erhe::scene::Light_type::spot;
        default:                             return erhe::scene::Light_type::directional;
    }
}

auto to_layout_type_serial(erhe::scene::Layout_type type) -> Layout_type_serial
{
    switch (type) {
        case erhe::scene::Layout_type::stack: return Layout_type_serial::stack;
        case erhe::scene::Layout_type::grid:  return Layout_type_serial::grid;
        case erhe::scene::Layout_type::flow:  return Layout_type_serial::flow;
        default:                              return Layout_type_serial::stack;
    }
}

auto from_layout_type_serial(Layout_type_serial type) -> erhe::scene::Layout_type
{
    switch (type) {
        case Layout_type_serial::stack: return erhe::scene::Layout_type::stack;
        case Layout_type_serial::grid:  return erhe::scene::Layout_type::grid;
        case Layout_type_serial::flow:  return erhe::scene::Layout_type::flow;
        default:                        return erhe::scene::Layout_type::stack;
    }
}

auto to_axis_direction_serial(erhe::scene::Axis_direction direction) -> Axis_direction_serial
{
    switch (direction) {
        case erhe::scene::Axis_direction::pos_x: return Axis_direction_serial::pos_x;
        case erhe::scene::Axis_direction::neg_x: return Axis_direction_serial::neg_x;
        case erhe::scene::Axis_direction::pos_y: return Axis_direction_serial::pos_y;
        case erhe::scene::Axis_direction::neg_y: return Axis_direction_serial::neg_y;
        case erhe::scene::Axis_direction::pos_z: return Axis_direction_serial::pos_z;
        case erhe::scene::Axis_direction::neg_z: return Axis_direction_serial::neg_z;
        default:                                 return Axis_direction_serial::pos_x;
    }
}

auto from_axis_direction_serial(Axis_direction_serial direction) -> erhe::scene::Axis_direction
{
    switch (direction) {
        case Axis_direction_serial::pos_x: return erhe::scene::Axis_direction::pos_x;
        case Axis_direction_serial::neg_x: return erhe::scene::Axis_direction::neg_x;
        case Axis_direction_serial::pos_y: return erhe::scene::Axis_direction::pos_y;
        case Axis_direction_serial::neg_y: return erhe::scene::Axis_direction::neg_y;
        case Axis_direction_serial::pos_z: return erhe::scene::Axis_direction::pos_z;
        case Axis_direction_serial::neg_z: return erhe::scene::Axis_direction::neg_z;
        default:                           return erhe::scene::Axis_direction::pos_x;
    }
}

auto to_layout_alignment_serial(erhe::scene::Layout_alignment alignment) -> Layout_alignment_serial
{
    switch (alignment) {
        case erhe::scene::Layout_alignment::negative: return Layout_alignment_serial::negative;
        case erhe::scene::Layout_alignment::positive: return Layout_alignment_serial::positive;
        case erhe::scene::Layout_alignment::stretch:  return Layout_alignment_serial::stretch;
        default:                                      return Layout_alignment_serial::negative;
    }
}

auto from_layout_alignment_serial(Layout_alignment_serial alignment) -> erhe::scene::Layout_alignment
{
    switch (alignment) {
        case Layout_alignment_serial::negative: return erhe::scene::Layout_alignment::negative;
        case Layout_alignment_serial::positive: return erhe::scene::Layout_alignment::positive;
        case Layout_alignment_serial::stretch:  return erhe::scene::Layout_alignment::stretch;
        default:                                return erhe::scene::Layout_alignment::negative;
    }
}

auto to_motion_mode_serial(erhe::physics::Motion_mode mode) -> Motion_mode_serial
{
    switch (mode) {
        case erhe::physics::Motion_mode::e_static:                 return Motion_mode_serial::e_static;
        case erhe::physics::Motion_mode::e_kinematic_non_physical: return Motion_mode_serial::e_kinematic_non_physical;
        case erhe::physics::Motion_mode::e_kinematic_physical:     return Motion_mode_serial::e_kinematic_physical;
        case erhe::physics::Motion_mode::e_dynamic:                return Motion_mode_serial::e_dynamic;
        default:                                                   return Motion_mode_serial::e_dynamic;
    }
}

auto from_motion_mode_serial(Motion_mode_serial mode) -> erhe::physics::Motion_mode
{
    switch (mode) {
        case Motion_mode_serial::e_static:                 return erhe::physics::Motion_mode::e_static;
        case Motion_mode_serial::e_kinematic_non_physical: return erhe::physics::Motion_mode::e_kinematic_non_physical;
        case Motion_mode_serial::e_kinematic_physical:     return erhe::physics::Motion_mode::e_kinematic_physical;
        case Motion_mode_serial::e_dynamic:                return erhe::physics::Motion_mode::e_dynamic;
        default:                                           return erhe::physics::Motion_mode::e_dynamic;
    }
}

auto to_collision_shape_type_serial(erhe::physics::Collision_shape_type type) -> Collision_shape_type_serial
{
    switch (type) {
        case erhe::physics::Collision_shape_type::e_empty:            return Collision_shape_type_serial::e_empty;
        case erhe::physics::Collision_shape_type::e_box:              return Collision_shape_type_serial::e_box;
        case erhe::physics::Collision_shape_type::e_sphere:           return Collision_shape_type_serial::e_sphere;
        case erhe::physics::Collision_shape_type::e_capsule:          return Collision_shape_type_serial::e_capsule;
        case erhe::physics::Collision_shape_type::e_tapered_capsule:  return Collision_shape_type_serial::e_tapered_capsule;
        case erhe::physics::Collision_shape_type::e_tapered_cylinder: return Collision_shape_type_serial::e_tapered_cylinder;
        case erhe::physics::Collision_shape_type::e_cylinder:         return Collision_shape_type_serial::e_cylinder;
        case erhe::physics::Collision_shape_type::e_compound:         return Collision_shape_type_serial::e_compound;
        case erhe::physics::Collision_shape_type::e_convex_hull:      return Collision_shape_type_serial::e_convex_hull; // rebuilt from source node mesh at load
        case erhe::physics::Collision_shape_type::e_mesh:             return Collision_shape_type_serial::e_mesh;        // rebuilt from source node mesh at load
        // Wrapper shapes (uniform scaling / scaled / offset center of mass) are
        // unwrapped by serialize_collision_shape() and never reach this point.
        case erhe::physics::Collision_shape_type::e_uniform_scaling:  return Collision_shape_type_serial::e_not_specified;
        default:                                                      return Collision_shape_type_serial::e_not_specified;
    }
}

auto to_combine_mode_serial(erhe::physics::Combine_mode mode) -> Combine_mode_serial
{
    switch (mode) {
        case erhe::physics::Combine_mode::e_average:  return Combine_mode_serial::e_average;
        case erhe::physics::Combine_mode::e_minimum:  return Combine_mode_serial::e_minimum;
        case erhe::physics::Combine_mode::e_maximum:  return Combine_mode_serial::e_maximum;
        case erhe::physics::Combine_mode::e_multiply: return Combine_mode_serial::e_multiply;
        default:                                      return Combine_mode_serial::e_average;
    }
}

auto from_combine_mode_serial(Combine_mode_serial mode) -> erhe::physics::Combine_mode
{
    switch (mode) {
        case Combine_mode_serial::e_average:  return erhe::physics::Combine_mode::e_average;
        case Combine_mode_serial::e_minimum:  return erhe::physics::Combine_mode::e_minimum;
        case Combine_mode_serial::e_maximum:  return erhe::physics::Combine_mode::e_maximum;
        case Combine_mode_serial::e_multiply: return erhe::physics::Combine_mode::e_multiply;
        default:                              return erhe::physics::Combine_mode::e_average;
    }
}

auto to_drive_type_serial(erhe::physics::Drive_type type) -> Drive_type_serial
{
    switch (type) {
        case erhe::physics::Drive_type::e_linear:  return Drive_type_serial::e_linear;
        case erhe::physics::Drive_type::e_angular: return Drive_type_serial::e_angular;
        default:                                   return Drive_type_serial::e_linear;
    }
}

auto from_drive_type_serial(Drive_type_serial type) -> erhe::physics::Drive_type
{
    switch (type) {
        case Drive_type_serial::e_linear:  return erhe::physics::Drive_type::e_linear;
        case Drive_type_serial::e_angular: return erhe::physics::Drive_type::e_angular;
        default:                           return erhe::physics::Drive_type::e_linear;
    }
}

auto to_drive_mode_serial(erhe::physics::Drive_mode mode) -> Drive_mode_serial
{
    switch (mode) {
        case erhe::physics::Drive_mode::e_force:        return Drive_mode_serial::e_force;
        case erhe::physics::Drive_mode::e_acceleration: return Drive_mode_serial::e_acceleration;
        default:                                        return Drive_mode_serial::e_force;
    }
}

auto from_drive_mode_serial(Drive_mode_serial mode) -> erhe::physics::Drive_mode
{
    switch (mode) {
        case Drive_mode_serial::e_force:        return erhe::physics::Drive_mode::e_force;
        case Drive_mode_serial::e_acceleration: return erhe::physics::Drive_mode::e_acceleration;
        default:                                return erhe::physics::Drive_mode::e_force;
    }
}

auto to_axis_int(erhe::physics::Axis axis) -> int
{
    return static_cast<int>(axis);
}

auto from_axis_int(int value) -> erhe::physics::Axis
{
    switch (value) {
        case 0:  return erhe::physics::Axis::X;
        case 1:  return erhe::physics::Axis::Y;
        case 2:  return erhe::physics::Axis::Z;
        default: return erhe::physics::Axis::Y;
    }
}

void serialize_primitive_shape_params(
    const erhe::physics::ICollision_shape& shape,
    Collision_shape_type_serial            serial_type,
    std::optional<glm::vec3>&              out_half_extents,
    std::optional<float>&                  out_radius,
    std::optional<int>&                    out_axis,
    std::optional<float>&                  out_length,
    std::optional<float>&                  out_top_radius
)
{
    switch (serial_type) {
        case Collision_shape_type_serial::e_box:
            out_half_extents = shape.get_half_extents();
            break;
        case Collision_shape_type_serial::e_sphere:
            out_radius = shape.get_radius();
            break;
        case Collision_shape_type_serial::e_capsule:
            out_radius = shape.get_radius();
            out_length = shape.get_length();
            if (auto a = shape.get_axis()) {
                out_axis = to_axis_int(*a);
            }
            break;
        case Collision_shape_type_serial::e_tapered_capsule:
        case Collision_shape_type_serial::e_tapered_cylinder:
            out_radius     = shape.get_bottom_radius();
            out_top_radius = shape.get_top_radius();
            out_length     = shape.get_length();
            if (auto a = shape.get_axis()) {
                out_axis = to_axis_int(*a);
            }
            break;
        case Collision_shape_type_serial::e_cylinder:
            out_half_extents = shape.get_half_extents();
            if (auto a = shape.get_axis()) {
                out_axis = to_axis_int(*a);
            }
            break;
        default:
            break;
    }
}

auto serialize_collision_shape(const erhe::physics::ICollision_shape& shape_in) -> Collision_shape_data
{
    Collision_shape_data data;

    // Unwrap wrapper shapes into flat fields (the schema stores no shape tree):
    // - scaled / uniform scaling: scale goes to Collision_shape_data::scale and
    //   is re-applied at load (uniform when all components are equal).
    // - offset center of mass: the offset is intentionally NOT serialized here;
    //   it is stored once as Node_physics_data::center_of_mass (captured via
    //   Node_physics::get_center_of_mass_offset()) and re-wrapped at load.
    const erhe::physics::ICollision_shape*           shape = &shape_in;
    std::shared_ptr<erhe::physics::ICollision_shape> inner = shape->get_inner_shape();
    while (inner) {
        const std::optional<glm::vec3> wrapper_scale = shape->get_scale();
        if (wrapper_scale.has_value()) {
            data.scale = data.scale.has_value() ? (data.scale.value() * wrapper_scale.value()) : wrapper_scale.value();
        }
        shape = inner.get();
        inner = shape->get_inner_shape();
    }

    data.type = to_collision_shape_type_serial(shape->get_shape_type());

    serialize_primitive_shape_params(*shape, data.type, data.half_extents, data.radius, data.axis, data.length, data.top_radius);

    // Mesh / convex hull geometry is rebuilt from the owning node's mesh at
    // load; source_node_id stays absent (absent = owning node).

    if (data.type == Collision_shape_type_serial::e_compound) {
        for (const auto& child : shape->get_children()) {
            if (!child.shape) {
                continue;
            }
            Compound_child_data child_data;
            child_data.shape_type = to_collision_shape_type_serial(child.shape->get_shape_type());
            serialize_primitive_shape_params(
                *child.shape,
                child_data.shape_type,
                child_data.half_extents,
                child_data.radius,
                child_data.axis,
                child_data.length,
                child_data.top_radius
            );
            const glm::quat rotation{child.transform.basis};
            child_data.position = child.transform.origin;
            child_data.rotation = glm::vec4{rotation.x, rotation.y, rotation.z, rotation.w};
            data.children.push_back(std::move(child_data));
        }
    }

    return data;
}

auto deserialize_primitive_shape(
    Collision_shape_type_serial         type,
    const std::optional<glm::vec3>&     half_extents,
    const std::optional<float>&         radius,
    const std::optional<int>&           axis,
    const std::optional<float>&         length,
    const std::optional<float>&         top_radius
) -> std::shared_ptr<erhe::physics::ICollision_shape>
{
    switch (type) {
        case Collision_shape_type_serial::e_empty:
            return erhe::physics::ICollision_shape::create_empty_shape_shared();
        case Collision_shape_type_serial::e_box:
            if (half_extents.has_value()) {
                return erhe::physics::ICollision_shape::create_box_shape_shared(*half_extents);
            }
            break;
        case Collision_shape_type_serial::e_sphere:
            if (radius.has_value()) {
                return erhe::physics::ICollision_shape::create_sphere_shape_shared(*radius);
            }
            break;
        case Collision_shape_type_serial::e_capsule:
            if (radius.has_value() && length.has_value()) {
                return erhe::physics::ICollision_shape::create_capsule_shape_shared(
                    axis.has_value() ? from_axis_int(*axis) : erhe::physics::Axis::Y,
                    *radius,
                    *length
                );
            }
            break;
        case Collision_shape_type_serial::e_tapered_capsule:
            if (radius.has_value() && top_radius.has_value() && length.has_value()) {
                return erhe::physics::ICollision_shape::create_tapered_capsule_shape_shared(
                    axis.has_value() ? from_axis_int(*axis) : erhe::physics::Axis::Y,
                    *radius,
                    *top_radius,
                    *length
                );
            }
            break;
        case Collision_shape_type_serial::e_tapered_cylinder:
            if (radius.has_value() && top_radius.has_value() && length.has_value()) {
                return erhe::physics::ICollision_shape::create_tapered_cylinder_shape_shared(
                    axis.has_value() ? from_axis_int(*axis) : erhe::physics::Axis::Y,
                    *radius,
                    *top_radius,
                    *length
                );
            }
            break;
        case Collision_shape_type_serial::e_cylinder:
            if (half_extents.has_value()) {
                return erhe::physics::ICollision_shape::create_cylinder_shape_shared(
                    axis.has_value() ? from_axis_int(*axis) : erhe::physics::Axis::Y,
                    *half_extents
                );
            }
            break;
        default:
            break;
    }
    return {};
}

auto deserialize_collision_shape(const Collision_shape_data& data) -> std::shared_ptr<erhe::physics::ICollision_shape>
{
    if (data.type == Collision_shape_type_serial::e_not_specified) {
        return {}; // Caller falls back to convex hull from mesh
    }

    if (data.type == Collision_shape_type_serial::e_compound) {
        erhe::physics::Compound_shape_create_info create_info;
        for (const auto& child_data : data.children) {
            auto child_shape = deserialize_primitive_shape(
                child_data.shape_type,
                child_data.half_extents,
                child_data.radius,
                child_data.axis,
                child_data.length,
                child_data.top_radius
            );
            if (!child_shape) {
                continue;
            }
            const glm::quat rotation{child_data.rotation.w, child_data.rotation.x, child_data.rotation.y, child_data.rotation.z};
            create_info.children.push_back(erhe::physics::Compound_child{
                .shape     = std::move(child_shape),
                .transform = erhe::physics::Transform{glm::mat3_cast(rotation), child_data.position},
            });
        }
        if (!create_info.children.empty()) {
            return erhe::physics::ICollision_shape::create_compound_shape_shared(create_info);
        }
        return erhe::physics::ICollision_shape::create_empty_shape_shared();
    }

    return deserialize_primitive_shape(data.type, data.half_extents, data.radius, data.axis, data.length, data.top_radius);
}

// A mesh is geometry-normative if any of its primitives has a Geometry object.
// Scene-built meshes always have Geometry. glTF-imported meshes only have
// Triangle_soup unless the geometry was created (make_geometry) and manipulated.
auto is_geometry_normative(const erhe::scene::Mesh& mesh) -> bool
{
    for (const auto& prim : mesh.get_primitives()) {
        if (prim.primitive && prim.primitive->render_shape &&
            prim.primitive->render_shape->get_geometry_const()) {
            return true;
        }
    }
    return false;
}

auto get_node_id(
    const erhe::scene::Node*                                         node,
    const std::unordered_map<const erhe::scene::Node*, uint64_t>&    node_id_map
) -> uint64_t
{
    if (node == nullptr) {
        return 0;
    }
    auto it = node_id_map.find(node);
    return (it != node_id_map.end()) ? it->second : 0;
}

} // anonymous namespace

auto save_scene(
    const Scene_root&            scene_root,
    const std::filesystem::path& path
) -> bool
{
    const erhe::scene::Scene& scene = scene_root.get_scene();
    const auto root_node = scene.get_root_node();
    if (!root_node) {
        log_parsers->error("save_scene: scene has no root node");
        return false;
    }

    const std::filesystem::path scene_dir  = path.parent_path();
    const std::string           scene_stem = path.stem().string();
    const std::string           glb_filename = scene_stem + ".glb";

    // Build node ID map
    std::unordered_map<const erhe::scene::Node*, uint64_t> node_id_map;
    uint64_t next_id = 1;
    const auto& flat_nodes = scene.get_flat_nodes();
    for (const auto& node : flat_nodes) {
        node_id_map[node.get()] = next_id++;
    }

    Scene_file scene_file;
    scene_file.name           = scene.get_name();
    scene_file.enable_physics = true; // TODO: track this properly

    // Serialize nodes
    for (const auto& node : flat_nodes) {
        const uint64_t id = node_id_map[node.get()];
        uint64_t parent_id = 0;
        auto parent_node = node->get_parent_node();
        if (parent_node) {
            auto it = node_id_map.find(parent_node.get());
            if (it != node_id_map.end()) {
                parent_id = it->second;
            }
        }

        const auto& trs = node->parent_from_node_transform();
        Node_data_serial node_data{
            .name      = node->get_name(),
            .id        = id,
            .parent_id = parent_id,
            .transform = serialize_transform(trs),
            .flag_bits = node->get_flag_bits(),
        };
        scene_file.nodes.push_back(std::move(node_data));
    }

    // Serialize cameras
    for (const auto& camera : scene.get_cameras()) {
        const uint64_t node_id = get_node_id(camera->get_node(), node_id_map);
        const erhe::scene::Projection* proj = camera->projection();
        Camera_data camera_data{
            .node_id      = node_id,
            .name         = camera->get_name(),
            .projection   = proj ? serialize_projection(*proj) : Projection_data{},
            .exposure     = camera->get_exposure(),
            .shadow_range = camera->get_shadow_range(),
        };
        scene_file.cameras.push_back(std::move(camera_data));
    }

    // Serialize lights
    for (const auto& light_layer : scene.get_light_layers()) {
        for (const auto& light : light_layer->lights) {
            const uint64_t node_id = get_node_id(light->get_node(), node_id_map);
            Light_data light_data{
                .node_id          = node_id,
                .name             = light->get_name(),
                .type             = to_light_type_serial(light->type),
                .color            = light->color,
                .intensity        = light->intensity,
                .range            = light->range,
                .inner_spot_angle = light->inner_spot_angle,
                .outer_spot_angle = light->outer_spot_angle,
                .cast_shadow      = light->cast_shadow,
            };
            scene_file.lights.push_back(std::move(light_data));
        }
    }

    // Shared physics asset collection: referenced Physics_material /
    // Collision_filter / Physics_joint_settings items get file-local 1-based
    // ids in encounter order and are written to the top-level arrays.
    // Library items not referenced by any body / joint are appended after
    // the node passes so that editor-authored assets survive save / load.
    std::unordered_map<const erhe::physics::Physics_material*,       uint64_t> material_id_map;
    std::unordered_map<const erhe::physics::Collision_filter*,       uint64_t> filter_id_map;
    std::unordered_map<const erhe::physics::Physics_joint_settings*, uint64_t> joint_id_map;

    auto get_material_id = [&scene_file, &material_id_map](const std::shared_ptr<erhe::physics::Physics_material>& material) -> uint64_t {
        const auto it = material_id_map.find(material.get());
        if (it != material_id_map.end()) {
            return it->second;
        }
        const uint64_t id = static_cast<uint64_t>(scene_file.physics_materials.size() + 1);
        material_id_map.emplace(material.get(), id);
        scene_file.physics_materials.push_back(
            Physics_material_data{
                .id                  = id,
                .name                = material->get_name(),
                .static_friction     = material->static_friction,
                .dynamic_friction    = material->dynamic_friction,
                .restitution         = material->restitution,
                .friction_combine    = to_combine_mode_serial(material->friction_combine),
                .restitution_combine = to_combine_mode_serial(material->restitution_combine),
            }
        );
        return id;
    };
    auto get_filter_id = [&scene_file, &filter_id_map](const std::shared_ptr<erhe::physics::Collision_filter>& filter) -> uint64_t {
        const auto it = filter_id_map.find(filter.get());
        if (it != filter_id_map.end()) {
            return it->second;
        }
        const uint64_t id = static_cast<uint64_t>(scene_file.collision_filters.size() + 1);
        filter_id_map.emplace(filter.get(), id);
        scene_file.collision_filters.push_back(
            Collision_filter_data{
                .id                       = id,
                .name                     = filter->get_name(),
                .collision_systems        = filter->collision_systems,
                .collide_with_systems     = filter->collide_with_systems,
                .not_collide_with_systems = filter->not_collide_with_systems,
            }
        );
        return id;
    };
    auto get_joint_id = [&scene_file, &joint_id_map](const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings) -> uint64_t {
        const auto it = joint_id_map.find(settings.get());
        if (it != joint_id_map.end()) {
            return it->second;
        }
        const uint64_t id = static_cast<uint64_t>(scene_file.physics_joints.size() + 1);
        joint_id_map.emplace(settings.get(), id);
        Physics_joint_data joint_data{
            .id   = id,
            .name = settings->get_name(),
        };
        for (const erhe::physics::Joint_limit& limit : settings->limits) {
            Joint_limit_data limit_data{
                .min       = limit.min,
                .max       = limit.max,
                .stiffness = limit.stiffness,
                .damping   = limit.damping,
            };
            for (int axis = 0; axis < 3; ++axis) {
                if (limit.linear_axes[static_cast<std::size_t>(axis)]) {
                    limit_data.linear_axes.push_back(axis);
                }
                if (limit.angular_axes[static_cast<std::size_t>(axis)]) {
                    limit_data.angular_axes.push_back(axis);
                }
            }
            joint_data.limits.push_back(std::move(limit_data));
        }
        for (const erhe::physics::Joint_drive& drive : settings->drives) {
            joint_data.drives.push_back(
                Joint_drive_data{
                    .type            = to_drive_type_serial(drive.type),
                    .mode            = to_drive_mode_serial(drive.mode),
                    .axis            = drive.axis,
                    .max_force       = std::isfinite(drive.max_force) ? std::optional<float>{drive.max_force} : std::nullopt,
                    .position_target = drive.position_target,
                    .velocity_target = drive.velocity_target,
                    .stiffness       = drive.stiffness,
                    .damping         = drive.damping,
                }
            );
        }
        scene_file.physics_joints.push_back(std::move(joint_data));
        return id;
    };

    // Serialize node physics
    for (const auto& node : flat_nodes) {
        auto node_physics = erhe::scene::get_attachment<Node_physics>(node.get());
        if (!node_physics) {
            continue;
        }
        const uint64_t node_id = node_id_map[node.get()];
        const auto* rigid_body = node_physics->get_rigid_body();
        const auto& collision_shape = node_physics->get_collision_shape();
        Node_physics_data physics_data{
            .node_id           = node_id,
            .motion_mode       = to_motion_mode_serial(node_physics->get_motion_mode()),
            .friction          = rigid_body ? rigid_body->get_friction()        : 0.5f,
            .restitution       = rigid_body ? rigid_body->get_restitution()     : 0.2f,
            .linear_damping    = rigid_body ? rigid_body->get_linear_damping()  : 0.05f,
            .angular_damping   = rigid_body ? rigid_body->get_angular_damping() : 0.05f,
            .mass              = rigid_body ? std::optional<float>{rigid_body->get_mass()} : std::nullopt,
            .enable_collisions = true,
            .collision_shape   = collision_shape ? serialize_collision_shape(*collision_shape) : Collision_shape_data{},
            .is_trigger        = node_physics->is_trigger(),
        };
        const glm::vec3 linear_velocity = node_physics->get_initial_linear_velocity();
        if (linear_velocity != glm::vec3{0.0f}) {
            physics_data.linear_velocity = linear_velocity;
        }
        const glm::vec3 angular_velocity = node_physics->get_initial_angular_velocity();
        if (angular_velocity != glm::vec3{0.0f}) {
            physics_data.angular_velocity = angular_velocity;
        }
        const float gravity_factor = node_physics->get_gravity_factor();
        if (gravity_factor != 1.0f) {
            physics_data.gravity_factor = gravity_factor;
        }
        const glm::vec3 center_of_mass = node_physics->get_center_of_mass_offset();
        if (center_of_mass != glm::vec3{0.0f}) {
            physics_data.center_of_mass = center_of_mass;
        }
        const std::shared_ptr<erhe::physics::Physics_material>& physics_material = node_physics->get_physics_material();
        if (physics_material) {
            physics_data.material_id = get_material_id(physics_material);
        }
        const std::shared_ptr<erhe::physics::Collision_filter>& collision_filter = node_physics->get_collision_filter();
        if (collision_filter) {
            physics_data.filter_id = get_filter_id(collision_filter);
        }
        scene_file.node_physics.push_back(std::move(physics_data));
    }

    // Serialize node joints (a node may carry multiple Node_joint attachments)
    for (const auto& node : flat_nodes) {
        for (const auto& attachment : node->get_attachments()) {
            const auto node_joint = std::dynamic_pointer_cast<Node_joint>(attachment);
            if (!node_joint) {
                continue;
            }
            const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings = node_joint->get_settings();
            scene_file.node_joints.push_back(
                Node_joint_data{
                    .node_id           = node_id_map[node.get()],
                    .connected_node_id = get_node_id(node_joint->get_connected_node().get(), node_id_map),
                    .joint_id          = settings ? get_joint_id(settings) : uint64_t{0},
                    .enable_collision  = node_joint->get_enable_collision(),
                }
            );
        }
    }

    // Serialize content-library physics items no body / joint references,
    // so that assets created in the editor are not lost on save / load.
    {
        const std::shared_ptr<Content_library> content_library = scene_root.get_content_library();
        if (content_library) {
            for (const std::shared_ptr<erhe::physics::Physics_material>& material : content_library->physics_materials->get_all<erhe::physics::Physics_material>()) {
                static_cast<void>(get_material_id(material));
            }
            for (const std::shared_ptr<erhe::physics::Collision_filter>& filter : content_library->collision_filters->get_all<erhe::physics::Collision_filter>()) {
                static_cast<void>(get_filter_id(filter));
            }
            for (const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings : content_library->physics_joints->get_all<erhe::physics::Physics_joint_settings>()) {
                static_cast<void>(get_joint_id(settings));
            }
        }
    }

    // Serialize layouts and layout items
    for (const auto& node : flat_nodes) {
        const auto layout = erhe::scene::get_attachment<erhe::scene::Layout>(node.get());
        if (layout) {
            Layout_data layout_data{
                .node_id             = node_id_map[node.get()],
                .name                = layout->get_name(),
                .type                = to_layout_type_serial(layout->type),
                .volume_min          = layout->volume.min,
                .volume_max          = layout->volume.max,
                .primary             = to_axis_direction_serial(layout->primary),
                .secondary           = to_axis_direction_serial(layout->secondary),
                .tertiary            = to_axis_direction_serial(layout->tertiary),
                .gap                 = layout->gap,
                .grid_track_count    = layout->grid_track_count,
                .grid_track_extent_x = layout->grid_track_extent[0],
                .grid_track_extent_y = layout->grid_track_extent[1],
                .grid_track_extent_z = layout->grid_track_extent[2],
            };
            scene_file.layouts.push_back(std::move(layout_data));
        }
        const auto layout_item = erhe::scene::get_attachment<erhe::scene::Layout_item>(node.get());
        if (layout_item) {
            Layout_item_data item_data{
                .node_id        = node_id_map[node.get()],
                .name           = layout_item->get_name(),
                .align_x        = to_layout_alignment_serial(layout_item->alignment[0]),
                .align_y        = to_layout_alignment_serial(layout_item->alignment[1]),
                .align_z        = to_layout_alignment_serial(layout_item->alignment[2]),
                .margin_min     = layout_item->margin_min,
                .margin_max     = layout_item->margin_max,
                .grid_cell_auto = layout_item->grid_cell_auto,
                .grid_cell      = layout_item->grid_cell,
                .grid_span      = layout_item->grid_span,
            };
            scene_file.layout_items.push_back(std::move(item_data));
        }
    }

    // Serialize mesh references and save geometry files
    bool has_any_meshes  = false;
    int  mesh_index      = 0;

    GEO::MeshIOFlags geo_ioflags;
    geo_ioflags.set_dimension(3);
    geo_ioflags.set_attributes(GEO::MeshAttributesFlags::MESH_ALL_ATTRIBUTES);
    geo_ioflags.set_elements(GEO::MeshElementsFlags::MESH_ALL_ELEMENTS);

    for (const auto& mesh_layer : scene.get_mesh_layers()) {
        for (const auto& mesh : mesh_layer->meshes) {
            const uint64_t node_id = get_node_id(mesh->get_node(), node_id_map);
            const auto&    primitives = mesh->get_primitives();
            const bool     geom_normative = is_geometry_normative(*mesh);

            has_any_meshes = true;

            // Save geometry files for geometry-normative meshes
            std::string geometry_base;
            if (geom_normative) {
                geometry_base = fmt::format("{}_mesh_{}", scene_stem, mesh_index);
                for (size_t p = 0; p < primitives.size(); ++p) {
                    const auto& prim = primitives[p];
                    if (!prim.primitive || !prim.primitive->render_shape) {
                        continue;
                    }
                    const auto& geom = prim.primitive->render_shape->get_geometry_const();
                    if (!geom) {
                        continue;
                    }
                    const std::string filename = fmt::format("{}_p{}.geogram", geometry_base, p);
                    const std::filesystem::path full_path = scene_dir / filename;
                    GEO::OutputGeoFile geofile{full_path.string(), 3};
                    if (!GEO::mesh_save(geom->get_mesh(), geofile, geo_ioflags)) {
                        log_parsers->error("save_scene: failed to save geometry: {}", full_path.string());
                    }
                }
            }

            // Material references (stored by name for both source types)
            std::vector<Gltf_source_reference> material_refs;
            int prim_index = 0;
            for (const auto& prim : primitives) {
                if (prim.material) {
                    material_refs.push_back(Gltf_source_reference{
                        .gltf_path  = glb_filename,
                        .item_name  = prim.material->get_name(),
                        .item_index = prim_index,
                        .item_type  = "material",
                    });
                }
                ++prim_index;
            }

            Mesh_reference mesh_ref{
                .node_id       = node_id,
                .gltf_source   = geom_normative
                    ? Gltf_source_reference{}
                    : Gltf_source_reference{
                        .gltf_path  = glb_filename,
                        .item_name  = mesh->get_name(),
                        .item_index = mesh_index,
                        .item_type  = "mesh",
                    },
                .material_refs   = std::move(material_refs),
                .source_type     = geom_normative ? Mesh_source_type::geometry : Mesh_source_type::gltf,
                .geometry_path   = geometry_base,
                .mesh_name       = mesh->get_name(),
                .primitive_count = static_cast<int>(primitives.size()),
            };
            scene_file.mesh_references.push_back(std::move(mesh_ref));
            ++mesh_index;
        }
    }

    // Export companion .glb (meshes and materials)
    if (has_any_meshes) {
        const std::filesystem::path glb_path = scene_dir / glb_filename;
        const std::string glb_data = erhe::gltf::export_gltf(*root_node, true);
        if (!erhe::file::write_file(glb_path, glb_data)) {
            log_parsers->error("save_scene: failed to write glb: {}", glb_path.string());
            return false;
        }
    }

    // Write scene JSON
    const std::string json = serialize(scene_file);
    return erhe::file::write_file(path, json);
}

auto load_scene(
    App_context*                            context,
    App_message_bus*                        app_message_bus,
    App_scenes*                             app_scenes,
    const std::shared_ptr<Content_library>& content_library,
    const std::filesystem::path&            path
) -> std::shared_ptr<Scene_root>
{
    // Read file
    std::optional<std::string> file_content = erhe::file::read("scene file", path);
    const bool file_has_content = file_content.has_value();
    if (file_has_content) {
        log_parsers->debug("Path A");
    } else {
        log_parsers->debug("Path B");
        return {};
    }

    // Parse JSON
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded{file_content.value()};
    simdjson::ondemand::document doc;
    auto error = parser.iterate(padded).get(doc);
    if (error != simdjson::SUCCESS) {
        log_parsers->error("Failed to parse scene JSON: {}", simdjson::error_message(error));
        return {};
    }

    simdjson::ondemand::object root_obj;
    error = doc.get_object().get(root_obj);
    if (error != simdjson::SUCCESS) {
        log_parsers->error("Scene JSON is not an object");
        return {};
    }

    Scene_file scene_file;
    error = deserialize(root_obj, scene_file);
    if (error != simdjson::SUCCESS) {
        log_parsers->error("Failed to deserialize scene file");
        return {};
    }

    // Create Scene_root
    auto scene_root = std::make_shared<Scene_root>(
        app_message_bus,
        content_library,
        scene_file.name,
        scene_file.enable_physics
    );
    if (app_scenes != nullptr) {
        scene_root->register_to_editor_scenes(*app_scenes);
    }

    erhe::scene::Scene& scene = scene_root->get_scene();
    const auto scene_root_node = scene.get_root_node();

    // Build node map: serial id -> shared_ptr<Node>
    std::unordered_map<uint64_t, std::shared_ptr<erhe::scene::Node>> node_map;
    for (const auto& node_data : scene_file.nodes) {
        auto node = std::make_shared<erhe::scene::Node>(node_data.name);
        node->enable_flag_bits(node_data.flag_bits);
        node_map[node_data.id] = node;
    }

    // Set parent-child relationships and transforms
    for (const auto& node_data : scene_file.nodes) {
        auto& node = node_map[node_data.id];
        if (node_data.parent_id != 0) {
            auto parent_it = node_map.find(node_data.parent_id);
            if (parent_it != node_map.end()) {
                node->set_parent(parent_it->second.get());
            } else {
                node->set_parent(scene_root_node.get());
            }
        } else {
            node->set_parent(scene_root_node.get());
        }

        auto trs = deserialize_transform(node_data.transform);
        node->set_parent_from_node(trs);
    }

    // Create cameras
    for (const auto& camera_data : scene_file.cameras) {
        auto camera = std::make_shared<erhe::scene::Camera>(camera_data.name);
        deserialize_projection(camera_data.projection, *camera->projection());
        camera->set_exposure(camera_data.exposure);
        camera->set_shadow_range(camera_data.shadow_range);
        camera->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);

        if (camera_data.node_id != 0) {
            auto node_it = node_map.find(camera_data.node_id);
            if (node_it != node_map.end()) {
                node_it->second->attach(camera);
            }
        }
    }

    // Create lights
    for (const auto& light_data : scene_file.lights) {
        auto light = std::make_shared<erhe::scene::Light>(light_data.name);
        light->type             = from_light_type_serial(light_data.type);
        light->color            = light_data.color;
        light->intensity        = light_data.intensity;
        light->range            = light_data.range;
        light->inner_spot_angle = light_data.inner_spot_angle;
        light->outer_spot_angle = light_data.outer_spot_angle;
        light->cast_shadow      = light_data.cast_shadow;
        light->layer_id         = scene_root->layers().light()->id;
        light->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);

        if (light_data.node_id != 0) {
            auto node_it = node_map.find(light_data.node_id);
            if (node_it != node_map.end()) {
                node_it->second->attach(light);
            }
        }
    }

    // Create layouts
    for (const auto& layout_data : scene_file.layouts) {
        auto layout = std::make_shared<erhe::scene::Layout>(layout_data.name);
        layout->type                 = from_layout_type_serial(layout_data.type);
        layout->volume.min           = layout_data.volume_min;
        layout->volume.max           = layout_data.volume_max;
        layout->primary              = from_axis_direction_serial(layout_data.primary);
        layout->secondary            = from_axis_direction_serial(layout_data.secondary);
        layout->tertiary             = from_axis_direction_serial(layout_data.tertiary);
        layout->gap                  = layout_data.gap;
        layout->grid_track_count     = layout_data.grid_track_count;
        layout->grid_track_extent[0] = layout_data.grid_track_extent_x;
        layout->grid_track_extent[1] = layout_data.grid_track_extent_y;
        layout->grid_track_extent[2] = layout_data.grid_track_extent_z;
        layout->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui | erhe::Item_flags::show_debug_visualizations);

        if (layout_data.node_id != 0) {
            auto node_it = node_map.find(layout_data.node_id);
            if (node_it != node_map.end()) {
                node_it->second->attach(layout);
            }
        }
    }

    // Create layout items
    for (const auto& item_data : scene_file.layout_items) {
        auto layout_item = std::make_shared<erhe::scene::Layout_item>(item_data.name);
        layout_item->alignment[0]   = from_layout_alignment_serial(item_data.align_x);
        layout_item->alignment[1]   = from_layout_alignment_serial(item_data.align_y);
        layout_item->alignment[2]   = from_layout_alignment_serial(item_data.align_z);
        layout_item->margin_min     = item_data.margin_min;
        layout_item->margin_max     = item_data.margin_max;
        layout_item->grid_cell_auto = item_data.grid_cell_auto;
        layout_item->grid_cell      = item_data.grid_cell;
        layout_item->grid_span      = item_data.grid_span;
        layout_item->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);

        if (item_data.node_id != 0) {
            auto node_it = node_map.find(item_data.node_id);
            if (node_it != node_map.end()) {
                node_it->second->attach(layout_item);
            }
        }
    }

    // Shared physics assets (scene file v3+): construct items and register
    // them into the content library. Loaded items are always added as new
    // items (the loading scene gets its own Content_library instance).
    std::unordered_map<uint64_t, std::shared_ptr<erhe::physics::Physics_material>> material_map;
    for (const Physics_material_data& material_data : scene_file.physics_materials) {
        auto material = std::make_shared<erhe::physics::Physics_material>(material_data.name);
        material->static_friction     = material_data.static_friction;
        material->dynamic_friction    = material_data.dynamic_friction;
        material->restitution         = material_data.restitution;
        material->friction_combine    = from_combine_mode_serial(material_data.friction_combine);
        material->restitution_combine = from_combine_mode_serial(material_data.restitution_combine);
        if (material_data.id != 0) {
            material_map[material_data.id] = material;
        }
        content_library->physics_materials->add(material);
    }

    std::unordered_map<uint64_t, std::shared_ptr<erhe::physics::Collision_filter>> filter_map;
    for (const Collision_filter_data& filter_data : scene_file.collision_filters) {
        auto filter = std::make_shared<erhe::physics::Collision_filter>(filter_data.name);
        filter->collision_systems        = filter_data.collision_systems;
        filter->collide_with_systems     = filter_data.collide_with_systems;
        filter->not_collide_with_systems = filter_data.not_collide_with_systems;
        if (filter_data.id != 0) {
            filter_map[filter_data.id] = filter;
        }
        content_library->collision_filters->add(filter);
    }

    std::unordered_map<uint64_t, std::shared_ptr<erhe::physics::Physics_joint_settings>> joint_settings_map;
    for (const Physics_joint_data& joint_data : scene_file.physics_joints) {
        auto settings = std::make_shared<erhe::physics::Physics_joint_settings>(joint_data.name);
        for (const Joint_limit_data& limit_data : joint_data.limits) {
            erhe::physics::Joint_limit limit{};
            for (const int axis : limit_data.linear_axes) {
                if ((axis >= 0) && (axis < 3)) {
                    limit.linear_axes[static_cast<std::size_t>(axis)] = true;
                }
            }
            for (const int axis : limit_data.angular_axes) {
                if ((axis >= 0) && (axis < 3)) {
                    limit.angular_axes[static_cast<std::size_t>(axis)] = true;
                }
            }
            limit.min       = limit_data.min;
            limit.max       = limit_data.max;
            limit.stiffness = limit_data.stiffness;
            limit.damping   = limit_data.damping;
            settings->limits.push_back(limit);
        }
        for (const Joint_drive_data& drive_data : joint_data.drives) {
            erhe::physics::Joint_drive drive{};
            drive.type            = from_drive_type_serial(drive_data.type);
            drive.mode            = from_drive_mode_serial(drive_data.mode);
            drive.axis            = drive_data.axis;
            drive.max_force       = drive_data.max_force.value_or(std::numeric_limits<float>::infinity());
            drive.position_target = drive_data.position_target;
            drive.velocity_target = drive_data.velocity_target;
            drive.stiffness       = drive_data.stiffness;
            drive.damping         = drive_data.damping;
            settings->drives.push_back(drive);
        }
        if (joint_data.id != 0) {
            joint_settings_map[joint_data.id] = settings;
        }
        content_library->physics_joints->add(settings);
    }

    if (!scene_file.physics_materials.empty() || !scene_file.collision_filters.empty() || !scene_file.physics_joints.empty() || !scene_file.node_joints.empty()) {
        log_parsers->info(
            "load_scene: {} physics materials, {} collision filters, {} physics joints, {} node joints",
            scene_file.physics_materials.size(),
            scene_file.collision_filters.size(),
            scene_file.physics_joints.size(),
            scene_file.node_joints.size()
        );
    }

    // Load meshes and physics
    if (context == nullptr || (scene_file.mesh_references.empty() && scene_file.node_physics.empty() && scene_file.node_joints.empty())) {
        return scene_root;
    }

    const std::filesystem::path scene_dir = path.parent_path();

    constexpr uint64_t mesh_flags =
        erhe::Item_flags::visible     |
        erhe::Item_flags::content     |
        erhe::Item_flags::shadow_cast |
        erhe::Item_flags::id          |
        erhe::Item_flags::show_in_ui;

    // Collect unique glTF paths from all mesh references (both types need the .glb for materials)
    std::unordered_map<std::string, erhe::gltf::Gltf_data> gltf_cache;
    auto cache_gltf = [&](const std::string& glb_path_str) {
        if (glb_path_str.empty() || gltf_cache.contains(glb_path_str)) {
            return;
        }
        const std::filesystem::path glb_path = scene_dir / glb_path_str;
        if (!std::filesystem::exists(glb_path)) {
            log_parsers->error("load_scene: glTF file not found: {}", glb_path.string());
            return;
        }
        erhe::scene::Scene temp_scene{"temp", nullptr};
        auto temp_root = std::make_shared<erhe::scene::Node>("temp_root");
        temp_root->set_parent(temp_scene.get_root_node());

        ERHE_VERIFY(context->current_command_buffer != nullptr);
        erhe::gltf::Image_transfer image_transfer{*context->graphics_device, *context->current_command_buffer};
        erhe::gltf::Gltf_parse_arguments parse_args{
            .graphics_device = *context->graphics_device,
            .executor        = *context->executor,
            .image_transfer  = image_transfer,
            .root_node       = temp_root,
            .mesh_layer_id   = scene_root->layers().content()->id,
            .path            = glb_path,
        };
        gltf_cache[glb_path_str] = erhe::gltf::parse_gltf(parse_args);
    };
    for (const auto& mesh_ref : scene_file.mesh_references) {
        // Cache glTF source path (for glTF-normative meshes)
        cache_gltf(mesh_ref.gltf_source.gltf_path);
        // Cache material reference paths (for all mesh types)
        for (const auto& mat_ref : mesh_ref.material_refs) {
            cache_gltf(mat_ref.gltf_path);
        }
    }

    // Process each mesh reference
    erhe::primitive::Build_info build_info{
        .primitive_types = {
            .fill_triangles  = true,
            .edge_lines      = true,
            .corner_points   = true,
            .centroid_points = true,
        },
        .buffer_info = context->mesh_memory->make_primitive_buffer_info()
    };

    for (const auto& mesh_ref : scene_file.mesh_references) {
        if (mesh_ref.source_type == Mesh_source_type::geometry) {
            // --- Load geometry-normative mesh from geogram files ---
            const int num_primitives = (mesh_ref.primitive_count > 0)
                ? mesh_ref.primitive_count
                : 1;

            auto mesh = std::make_shared<erhe::scene::Mesh>(mesh_ref.mesh_name);

            for (int p = 0; p < num_primitives; ++p) {
                const std::string filename = fmt::format("{}_p{}.geogram", mesh_ref.geometry_path, p);
                const std::filesystem::path geom_path = scene_dir / filename;

                if (!std::filesystem::exists(geom_path)) {
                    log_parsers->warn("load_scene: geometry file not found: {}", geom_path.string());
                    continue;
                }

                auto geometry = std::make_shared<erhe::geometry::Geometry>();
                // Unbind erhe Mesh_attributes before loading, because mesh_load
                // will try to bind attributes with the same names and assert.
                geometry->get_attributes().unbind();
                GEO::Mesh& geo_mesh = geometry->get_mesh();
                geo_mesh.clear(false, false);
                GEO::MeshIOFlags ioflags;
                ioflags.set_dimension(3);
                ioflags.set_attributes(GEO::MeshAttributesFlags::MESH_ALL_ATTRIBUTES);
                ioflags.set_elements(GEO::MeshElementsFlags::MESH_ALL_ELEMENTS);

                if (!GEO::mesh_load(geom_path.string().c_str(), geo_mesh, ioflags)) {
                    log_parsers->error("load_scene: failed to load geometry: {}", geom_path.string());
                    continue;
                }
                // Re-bind erhe Mesh_attributes after loading
                geometry->get_attributes().bind();

                if (geo_mesh.vertices.nb() == 0 || geo_mesh.facets.nb() == 0) {
                    log_parsers->warn("load_scene: empty geometry: {}", geom_path.string());
                    continue;
                }

                constexpr uint64_t process_flags =
                    erhe::geometry::Geometry::process_flag_connect                       |
                    erhe::geometry::Geometry::process_flag_build_edges                   |
                    erhe::geometry::Geometry::process_flag_compute_facet_centroids       |
                    erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
                    erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;

                geo_mesh.vertices.set_single_precision();
                geometry->process(process_flags);

                auto primitive = std::make_shared<erhe::primitive::Primitive>(
                    geometry,
                    build_info,
                    erhe::primitive::Normal_style::corner_normals
                );
                static_cast<void>(primitive->make_raytrace());

                // Find material from companion .glb or content library
                std::shared_ptr<erhe::primitive::Material> material;
                for (const auto& mat_ref : mesh_ref.material_refs) {
                    if (mat_ref.item_index == p) {
                        // First try the glTF cache (companion .glb)
                        if (!mat_ref.gltf_path.empty()) {
                            auto cache_it = gltf_cache.find(mat_ref.gltf_path);
                            if (cache_it != gltf_cache.end()) {
                                for (const auto& m : cache_it->second.materials) {
                                    if (m && m->get_name() == mat_ref.item_name) {
                                        material = m;
                                        content_library->materials->add(material);
                                        break;
                                    }
                                }
                            }
                        }
                        // Fall back to content library
                        if (!material) {
                            content_library->materials->for_each<Content_library_node>(
                                [&material, &mat_ref](const Content_library_node& node) {
                                    auto entry = std::dynamic_pointer_cast<erhe::primitive::Material>(node.item);
                                    if (entry && entry->get_name() == mat_ref.item_name) {
                                        material = entry;
                                        return false;
                                    }
                                    return true;
                                }
                            );
                        }
                        break;
                    }
                }

                mesh->add_primitive(primitive, material);
            }

            mesh->layer_id = scene_root->layers().content()->id;
            mesh->enable_flag_bits(mesh_flags);
            mesh->update_rt_primitives();

            if (mesh_ref.node_id != 0) {
                auto node_it = node_map.find(mesh_ref.node_id);
                if (node_it != node_map.end()) {
                    node_it->second->attach(mesh);
                }
            }
        } else {
            // --- Load glTF-normative mesh from companion .glb ---
            const std::string& glb_path_str = mesh_ref.gltf_source.gltf_path;
            auto cache_it = gltf_cache.find(glb_path_str);
            if (cache_it == gltf_cache.end()) {
                continue;
            }

            const erhe::gltf::Gltf_data& gltf_data = cache_it->second;

            // Find the mesh by index (preferred) or name
            std::shared_ptr<erhe::scene::Mesh> matched_mesh;
            const int mesh_index = mesh_ref.gltf_source.item_index;
            if (mesh_index >= 0 && mesh_index < static_cast<int>(gltf_data.meshes.size())) {
                matched_mesh = gltf_data.meshes[mesh_index];
            }
            if (!matched_mesh) {
                for (const auto& m : gltf_data.meshes) {
                    if (m && m->get_name() == mesh_ref.gltf_source.item_name) {
                        matched_mesh = m;
                        break;
                    }
                }
            }

            if (!matched_mesh) {
                log_parsers->warn(
                    "load_scene: could not find mesh '{}' (index {}) in {}",
                    mesh_ref.gltf_source.item_name,
                    mesh_ref.gltf_source.item_index,
                    glb_path_str
                );
                continue;
            }

            auto mesh_clone = std::make_shared<erhe::scene::Mesh>(*matched_mesh, erhe::for_clone{});
            mesh_clone->layer_id = scene_root->layers().content()->id;
            mesh_clone->enable_flag_bits(mesh_flags);

            for (auto& mesh_primitive : mesh_clone->get_mutable_primitives()) {
                if (mesh_primitive.primitive) {
                    static_cast<void>(mesh_primitive.primitive->make_renderable_mesh(
                        build_info,
                        erhe::primitive::Normal_style::corner_normals
                    ));
                }
            }
            mesh_clone->update_rt_primitives();

            if (mesh_ref.node_id != 0) {
                auto node_it = node_map.find(mesh_ref.node_id);
                if (node_it != node_map.end()) {
                    node_it->second->attach(mesh_clone);
                }
            }

            // Add materials to content library
            for (const auto& prim : mesh_clone->get_primitives()) {
                if (prim.material) {
                    content_library->materials->add(prim.material);
                }
            }
        }
    }

    // Create node physics
    for (const auto& physics_data : scene_file.node_physics) {
        if (physics_data.node_id == 0) {
            continue;
        }
        auto node_it = node_map.find(physics_data.node_id);
        if (node_it == node_map.end()) {
            continue;
        }
        const auto& node = node_it->second;
        const Collision_shape_data& shape_data = physics_data.collision_shape;

        // Recreate the collision shape from serialized data; mesh and convex
        // hull shapes are rebuilt from the source node's mesh geometry
        // (absent source_node_id = the owning node).
        std::shared_ptr<erhe::physics::ICollision_shape> collision_shape{};
        if ((shape_data.type == Collision_shape_type_serial::e_convex_hull) || (shape_data.type == Collision_shape_type_serial::e_mesh)) {
            const erhe::scene::Node* source_node = node.get();
            if (shape_data.source_node_id.has_value()) {
                auto source_it = node_map.find(shape_data.source_node_id.value());
                if (source_it != node_map.end()) {
                    source_node = source_it->second.get();
                } else {
                    log_parsers->warn("load_scene: collision shape source node {} not found, using owning node", shape_data.source_node_id.value());
                }
            }
            const bool convex_hull = (shape_data.type == Collision_shape_type_serial::e_convex_hull);
            collision_shape = build_shape_from_node_mesh(source_node, convex_hull);
            if (!collision_shape) {
                log_parsers->warn("load_scene: could not rebuild {} shape for node '{}'", convex_hull ? "convex hull" : "mesh", node->get_name());
            }
        } else {
            collision_shape = deserialize_collision_shape(shape_data);
        }

        // Legacy fall back (e_not_specified): derive convex hull from the
        // mesh geometry attached to this node
        if (!collision_shape) {
            collision_shape = build_shape_from_node_mesh(node.get(), true);
        }

        if (!collision_shape) {
            collision_shape = erhe::physics::ICollision_shape::create_empty_shape_shared();
        }

        // Re-apply wrapper shapes serialized as flat fields: scale first
        // (closest to the base shape), then the center of mass offset (the
        // order Node_physics::set_center_of_mass_offset() maintains).
        if (shape_data.scale.has_value()) {
            const glm::vec3 scale = shape_data.scale.value();
            if ((scale.x == scale.y) && (scale.y == scale.z)) {
                collision_shape = erhe::physics::ICollision_shape::create_uniform_scaling_shape_shared(collision_shape, scale.x);
            } else {
                collision_shape = erhe::physics::ICollision_shape::create_scaled_shape_shared(collision_shape, scale);
            }
        }
        if (physics_data.center_of_mass.has_value() && (physics_data.center_of_mass.value() != glm::vec3{0.0f})) {
            collision_shape = erhe::physics::ICollision_shape::create_offset_center_of_mass_shape_shared(collision_shape, physics_data.center_of_mass.value());
        }

        erhe::physics::IRigid_body_create_info create_info{
            .friction          = physics_data.friction,
            .restitution       = physics_data.restitution,
            .linear_damping    = physics_data.linear_damping,
            .angular_damping   = physics_data.angular_damping,
            .collision_shape   = collision_shape,
            .mass              = physics_data.mass,
            .enable_collisions = physics_data.enable_collisions,
            .motion_mode       = from_motion_mode_serial(physics_data.motion_mode),
            .linear_velocity   = physics_data.linear_velocity.value_or(glm::vec3{0.0f}),
            .angular_velocity  = physics_data.angular_velocity.value_or(glm::vec3{0.0f}),
            .gravity_factor    = physics_data.gravity_factor.value_or(1.0f),
            .is_sensor         = physics_data.is_trigger,
        };
        if (physics_data.material_id.has_value()) {
            auto material_it = material_map.find(physics_data.material_id.value());
            if (material_it != material_map.end()) {
                create_info.physics_material = material_it->second;
            } else {
                log_parsers->warn("load_scene: physics material {} not found", physics_data.material_id.value());
            }
        }
        if (physics_data.filter_id.has_value()) {
            auto filter_it = filter_map.find(physics_data.filter_id.value());
            if (filter_it != filter_map.end()) {
                create_info.collision_filter = filter_it->second;
            } else {
                log_parsers->warn("load_scene: collision filter {} not found", physics_data.filter_id.value());
            }
        }
        auto node_physics = std::make_shared<Node_physics>(create_info);
        node->attach(node_physics);
    }

    // Create node joints after all node physics: constraint creation needs
    // the rigid bodies (Scene_root retries pending joints as bodies register,
    // so strict ordering is not required, but attaching joints last avoids
    // pointless pending retries).
    for (const Node_joint_data& joint_data : scene_file.node_joints) {
        if (joint_data.node_id == 0) {
            continue;
        }
        auto node_it = node_map.find(joint_data.node_id);
        if (node_it == node_map.end()) {
            continue;
        }
        std::shared_ptr<erhe::scene::Node> connected_node{};
        if (joint_data.connected_node_id != 0) {
            auto connected_it = node_map.find(joint_data.connected_node_id);
            if (connected_it != node_map.end()) {
                connected_node = connected_it->second;
            } else {
                log_parsers->warn("load_scene: node joint connected node {} not found", joint_data.connected_node_id);
            }
        }
        std::shared_ptr<erhe::physics::Physics_joint_settings> settings{};
        if (joint_data.joint_id != 0) {
            auto settings_it = joint_settings_map.find(joint_data.joint_id);
            if (settings_it != joint_settings_map.end()) {
                settings = settings_it->second;
            } else {
                log_parsers->warn("load_scene: physics joint {} not found", joint_data.joint_id);
            }
        }
        auto node_joint = std::make_shared<Node_joint>(connected_node, settings, joint_data.enable_collision);
        node_it->second->attach(node_joint);
    }

    return scene_root;
}

}
