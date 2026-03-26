#include "scene/scene_serialization.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "parsers/gltf.hpp"
#include "renderers/mesh_memory.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"

#include "scene/generated/scene_file_serialization.hpp"

#include "erhe_file/file.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_gltf/gltf.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_gltf/image_transfer.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/camera.hpp"
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

#include <unordered_map>

using erhe::geometry::make_convex_hull;

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
        case erhe::physics::Collision_shape_type::e_empty:           return Collision_shape_type_serial::e_empty;
        case erhe::physics::Collision_shape_type::e_box:             return Collision_shape_type_serial::e_box;
        case erhe::physics::Collision_shape_type::e_sphere:          return Collision_shape_type_serial::e_sphere;
        case erhe::physics::Collision_shape_type::e_capsule:         return Collision_shape_type_serial::e_capsule;
        case erhe::physics::Collision_shape_type::e_cylinder:        return Collision_shape_type_serial::e_cylinder;
        case erhe::physics::Collision_shape_type::e_compound:        return Collision_shape_type_serial::e_compound;
        case erhe::physics::Collision_shape_type::e_convex_hull:     return Collision_shape_type_serial::e_not_specified; // derived from mesh
        case erhe::physics::Collision_shape_type::e_uniform_scaling: return Collision_shape_type_serial::e_not_specified; // not directly serializable
        default:                                                     return Collision_shape_type_serial::e_not_specified;
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
    std::optional<float>&                  out_length
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

auto serialize_collision_shape(const erhe::physics::ICollision_shape& shape) -> Collision_shape_data
{
    Collision_shape_data data;
    data.type = to_collision_shape_type_serial(shape.get_shape_type());

    serialize_primitive_shape_params(shape, data.type, data.half_extents, data.radius, data.axis, data.length);

    if (data.type == Collision_shape_type_serial::e_compound) {
        for (const auto& child : shape.get_children()) {
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
                child_data.length
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
    const std::optional<float>&         length
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
                child_data.length
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

    return deserialize_primitive_shape(data.type, data.half_extents, data.radius, data.axis, data.length);
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
        };
        scene_file.node_physics.push_back(std::move(physics_data));
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
    erhe::imgui::Imgui_renderer*            imgui_renderer,
    erhe::imgui::Imgui_windows*             imgui_windows,
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
        imgui_renderer,
        imgui_windows,
        context,
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

    // Load meshes and physics
    if (context == nullptr || (scene_file.mesh_references.empty() && scene_file.node_physics.empty())) {
        return scene_root;
    }

    const std::filesystem::path scene_dir = path.parent_path();

    constexpr uint64_t mesh_flags =
        erhe::Item_flags::visible     |
        erhe::Item_flags::content     |
        erhe::Item_flags::opaque      |
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

        erhe::gltf::Image_transfer image_transfer{*context->graphics_device};
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
        .buffer_info = context->mesh_memory->buffer_info,
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

        // Try to recreate collision shape from serialized data
        std::shared_ptr<erhe::physics::ICollision_shape> collision_shape = deserialize_collision_shape(physics_data.collision_shape);

        // Fall back: derive collision shape from mesh geometry attached to this node
        if (!collision_shape) {
            auto mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
            if (mesh) {
                for (const auto& prim : mesh->get_primitives()) {
                    if (prim.primitive && prim.primitive->render_shape) {
                        const auto& geom = prim.primitive->render_shape->get_geometry_const();
                        if (geom && geom->get_mesh().vertices.nb() > 0) {
                            GEO::Mesh convex_hull{};
                            if (make_convex_hull(geom->get_mesh(), convex_hull)) {
                                const auto vertex_count = convex_hull.vertices.nb();
                                std::vector<float> coordinates(3 * vertex_count);
                                for (GEO::index_t v = 0; v < vertex_count; ++v) {
                                    const auto* ptr = convex_hull.vertices.single_precision_point_ptr(v);
                                    coordinates[3 * v + 0] = ptr[0];
                                    coordinates[3 * v + 1] = ptr[1];
                                    coordinates[3 * v + 2] = ptr[2];
                                }
                                collision_shape = erhe::physics::ICollision_shape::create_convex_hull_shape_shared(
                                    coordinates.data(),
                                    static_cast<int>(vertex_count),
                                    static_cast<int>(3 * sizeof(float))
                                );
                            }
                            break; // Use first primitive with geometry
                        }
                    }
                }
            }
        }

        if (!collision_shape) {
            collision_shape = erhe::physics::ICollision_shape::create_empty_shape_shared();
        }

        const erhe::physics::IRigid_body_create_info create_info{
            .friction          = physics_data.friction,
            .restitution       = physics_data.restitution,
            .linear_damping    = physics_data.linear_damping,
            .angular_damping   = physics_data.angular_damping,
            .collision_shape   = collision_shape,
            .mass              = physics_data.mass,
            .enable_collisions = physics_data.enable_collisions,
            .motion_mode       = from_motion_mode_serial(physics_data.motion_mode),
        };
        auto node_physics = std::make_shared<Node_physics>(create_info);
        node->attach(node_physics);
    }

    return scene_root;
}

}
