#include "scene/scene_serialization.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "parsers/gltf.hpp"
#include "renderers/mesh_memory.hpp"
#include "scene/scene_root.hpp"

#include "scene/generated/scene_file.hpp"

#include "erhe_file/file.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_gltf/gltf.hpp"
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

    // Serialize mesh references and save geometry files
    bool has_gltf_meshes = false;
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
            } else {
                has_gltf_meshes = true;
            }

            // Material references (stored by name for both source types)
            std::vector<Gltf_source_reference> material_refs;
            int prim_index = 0;
            for (const auto& prim : primitives) {
                if (prim.material) {
                    material_refs.push_back(Gltf_source_reference{
                        .gltf_path  = geom_normative ? std::string{} : glb_filename,
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

    // Export glTF-normative meshes to companion .glb
    if (has_gltf_meshes) {
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

    // Load meshes
    if (context == nullptr || scene_file.mesh_references.empty()) {
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

    // Collect unique glTF paths for glTF-normative meshes
    std::unordered_map<std::string, erhe::gltf::Gltf_data> gltf_cache;
    for (const auto& mesh_ref : scene_file.mesh_references) {
        if (mesh_ref.source_type != Mesh_source_type::gltf) {
            continue;
        }
        const std::string& glb_path_str = mesh_ref.gltf_source.gltf_path;
        if (glb_path_str.empty() || gltf_cache.contains(glb_path_str)) {
            continue;
        }

        const std::filesystem::path glb_path = scene_dir / glb_path_str;
        if (!std::filesystem::exists(glb_path)) {
            log_parsers->error("load_scene: glTF file not found: {}", glb_path.string());
            continue;
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

                // Try to find material by name from content library
                std::shared_ptr<erhe::primitive::Material> material;
                for (const auto& mat_ref : mesh_ref.material_refs) {
                    if (mat_ref.item_index == p) {
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

    return scene_root;
}

}
