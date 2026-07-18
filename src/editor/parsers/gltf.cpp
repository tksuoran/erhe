// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "parsers/gltf.hpp"

#include "parsers/gltf_extensions_export.hpp"
#include "parsers/gltf_extensions_import.hpp"
#include "parsers/gltf_physics_export.hpp"
#include "parsers/gltf_physics_import.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_scenes.hpp"
#include "assets/asset_manager.hpp"
#include "content_library/content_library.hpp"
#include "scene/scene_root.hpp"
#include "operations/async_raytrace_kickoff_operation.hpp"
#include "operations/compound_operation.hpp"
#include "operations/content_library_attach_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/operation_stack.hpp"
#include "prefabs/prefab_library.hpp"

#include "scene/generated/gltf_source_reference.hpp"

#include "items.hpp"

#include "erhe_file/file.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_gltf/gltf.hpp"
#include "erhe_gltf/image_transfer.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"

#include "erhe_math/math_util.hpp"

#include "scene/generated/scene_settings_serialization.hpp"

#include <fmt/format.h>
#include <simdjson.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace editor {

namespace {

void color_graph(
    erhe::scene::Node*                           node,
    std::unordered_map<erhe::scene::Node*, int>& node_colors,
    const std::unordered_set<int>&               available_colors
) {
    std::unordered_set<int> colors_for_node = available_colors;
    auto parent = node->get_parent_node();
    if (parent) {
        auto i = node_colors.find(parent.get());
        if (i != node_colors.end()) {
            int parent_color = i->second;
            colors_for_node.erase(parent_color);
        }
    }
    for (const auto& child : node->get_children()) {
        auto child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
        if (!child_node) {
            continue;
        }
        auto i = node_colors.find(child_node.get());
        if (i != node_colors.end()) {
            int child_color = i->second;
            colors_for_node.erase(child_color);
        }
    }

    int node_color = *colors_for_node.begin();
    node_colors.emplace(node, node_color);

    for (auto& child : node->get_children()) {
        auto child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
        if (!child_node) {
            continue;
        }
        color_graph(child_node.get(), node_colors, available_colors);
    }
}

// Content-library attach operations for everything the parsed glTF carries
// besides the node tree: textures (with retained source image bytes),
// materials, skins and animations, each tagged with a Gltf_source_reference.
// Shared by the undoable import compound (make_import_gltf_operation) and
// the not-undoable scene open path (open_scene_gltf), which executes them
// inline and drops them.
void append_content_library_attach_operations(
    const std::shared_ptr<Content_library>&  content_library,
    const erhe::gltf::Gltf_data&             gltf_data,
    const std::string&                       gltf_path_str,
    std::vector<std::shared_ptr<Operation>>& operations
)
{
    log_parsers->info("Processing {} textures", gltf_data.images.size());
    for (size_t i = 0; i < gltf_data.images.size(); ++i) {
        const std::shared_ptr<erhe::graphics::Texture>& image = gltf_data.images[i];
        if (image) {
            operations.push_back(
                std::make_shared<Content_library_attach_operation<erhe::graphics::Texture>>(
                    content_library,
                    content_library->textures,
                    image,
                    Gltf_source_reference{
                        .gltf_path  = gltf_path_str,
                        .item_name  = image->get_name(),
                        .item_index = static_cast<int>(i),
                        .item_type  = "texture",
                    },
                    (i < gltf_data.image_sources.size()) ? gltf_data.image_sources[i] : std::shared_ptr<erhe::gltf::Gltf_image_source>{}
                )
            );
        }
    }

    log_parsers->info("Processing {} materials", gltf_data.materials.size());
    for (size_t i = 0; i < gltf_data.materials.size(); ++i) {
        const std::shared_ptr<erhe::primitive::Material>& material = gltf_data.materials[i];
        if (material) {
            operations.push_back(
                std::make_shared<Content_library_attach_operation<erhe::primitive::Material>>(
                    content_library,
                    content_library->materials,
                    material,
                    Gltf_source_reference{
                        .gltf_path  = gltf_path_str,
                        .item_name  = material->get_name(),
                        .item_index = static_cast<int>(i),
                        .item_type  = "material",
                    }
                )
            );
        }
    }

    log_parsers->info("Processing {} skins", gltf_data.skins.size());
    for (size_t i = 0; i < gltf_data.skins.size(); ++i) {
        const std::shared_ptr<erhe::scene::Skin>& skin = gltf_data.skins[i];
        if (skin) {
            operations.push_back(
                std::make_shared<Content_library_attach_operation<erhe::scene::Skin>>(
                    content_library,
                    content_library->skins,
                    skin,
                    Gltf_source_reference{
                        .gltf_path  = gltf_path_str,
                        .item_name  = skin->get_name(),
                        .item_index = static_cast<int>(i),
                        .item_type  = "skin",
                    }
                )
            );
        }
    }

    log_parsers->info("Processing {} animations", gltf_data.animations.size());
    for (size_t i = 0; i < gltf_data.animations.size(); ++i) {
        const std::shared_ptr<erhe::scene::Animation>& animation = gltf_data.animations[i];
        if (animation) {
            operations.push_back(
                std::make_shared<Content_library_attach_operation<erhe::scene::Animation>>(
                    content_library,
                    content_library->animations,
                    animation,
                    Gltf_source_reference{
                        .gltf_path  = gltf_path_str,
                        .item_name  = animation->get_name(),
                        .item_index = static_cast<int>(i),
                        .item_type  = "animation",
                    }
                )
            );
        }
    }
}

}

auto make_import_build_info(App_context& context) -> erhe::primitive::Build_info
{
    return erhe::primitive::Build_info{
        .primitive_types = {
            .fill_triangles          = true,
            .fill_triangles_expanded = true,
            .edge_lines              = true,
            .corner_points           = true,
            .centroid_points         = true
        },
        .buffer_info = context.mesh_memory->make_primitive_buffer_info()
    };
}

void finalize_imported_meshes(
    App_context&                                   context,
    const erhe::primitive::Build_info&             build_info,
    const erhe::gltf::Gltf_data&                   gltf_data,
    std::vector<std::shared_ptr<erhe::Item_base>>* out_mesh_node_items
)
{
    // Build_info variant for skinned meshes -- same as the caller's
    // build_info but with vertex_format_skinned so the GPU vertex buffer
    // carries joint_indices + joint_weights. Without this, Shader_key::derive
    // won't set USE_SKINNING (it checks the vertex_format for joint
    // attributes), and the standard.vert skinning branch is dead code.
    const erhe::primitive::Build_info skinned_build_info{
        .primitive_types = build_info.primitive_types,
        .buffer_info     = context.mesh_memory->make_skinned_primitive_buffer_info(),
        .constant_color  = build_info.constant_color,
        .keep_geometry   = build_info.keep_geometry,
        .normal_style    = build_info.normal_style,
        .vertex_id_vec3  = build_info.vertex_id_vec3,
        .autocolor       = build_info.autocolor
    };

    for (const std::shared_ptr<erhe::scene::Node>& node : gltf_data.nodes) {
        if (!node) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
        if (!mesh) {
            continue;
        }
        if (out_mesh_node_items != nullptr) {
            out_mesh_node_items->push_back(node);
        }

        // Skinned meshes need joint_indices + joint_weights in the GPU
        // vertex buffer so the standard.vert skinning branch can read
        // them via a_joint_indices_0 / a_joint_weights_0. Pick the
        // build_info accordingly; the rest of the build flow is
        // identical.
        const erhe::primitive::Build_info& mesh_build_info = mesh->skin ? skinned_build_info : build_info;
        std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_mutable_primitives();
        for (erhe::scene::Mesh_primitive& mesh_primitive : mesh_primitives) {
            erhe::primitive::Primitive& primitive = *mesh_primitive.primitive.get();
            // glTF arrives with facets + vertices but no edges. Build edges
            // (and the smooth vertex normals used for wireframe bias) so
            // the content wide-line renderer has something to draw.
            // Geometry restored from the ERHE_geometry extension already
            // carries edges (and every other attribute) byte-exact from the
            // dump - reprocessing would overwrite it and break the
            // bit-exact round-trip, so process only when edges are
            // genuinely missing.
            if (primitive.render_shape) {
                const std::shared_ptr<erhe::geometry::Geometry>& geometry = primitive.render_shape->get_geometry();
                if (geometry && (geometry->get_mesh().edges.nb() == 0)) {
                    geometry->process({.flags =
                        erhe::geometry::Geometry::process_flag_connect                       |
                        erhe::geometry::Geometry::process_flag_build_edges                   |
                        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals
                    });
                }
            }
            ERHE_VERIFY(primitive.make_renderable_mesh(mesh_build_info, erhe::primitive::Normal_style::corner_normals));
        }

        mesh->update_rt_primitives();
    }
}

auto make_import_gltf_operation(
    App_context&                       context,
    erhe::primitive::Build_info        build_info,
    const std::shared_ptr<Scene_root>& scene_root,
    const std::filesystem::path&       path
) -> std::shared_ptr<Operation>
{
    ERHE_VERIFY(scene_root);
    erhe::graphics::Device& graphics_device = *context.graphics_device;
    tf::Executor&           executor        = *context.executor;
    ERHE_VERIFY(context.current_command_buffer != nullptr);
    erhe::graphics::Command_buffer& command_buffer = *context.current_command_buffer;

    // R5.7 record adoption (plan resolution 3): when registering scene_root
    // adopted a loaded container record for this path (the scene-open flow:
    // Scene_open_operation registers before building this import), reuse the
    // record's parse instead of parsing the file again - one parse, one set
    // of asset objects, both directions.
    std::shared_ptr<erhe::scene::Node> root_node;
    erhe::gltf::Gltf_data              gltf_data;
    std::optional<Adopted_container_parse> adopted_parse;
    if (context.asset_manager != nullptr) {
        adopted_parse = context.asset_manager->take_adopted_parse(*scene_root, path);
    }
    if (adopted_parse.has_value()) {
        gltf_data = std::move(adopted_parse->gltf_data);
        root_node = adopted_parse->root_node;
        // The container's free root node becomes the import_root wrapper:
        // implicit container, not file content; glTF export writes its
        // children in its place so open/save cycles do not nest wrappers.
        root_node->set_name(erhe::file::to_string(path.filename()));
        root_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui | erhe::Item_flags::import_root);
        // Container parses use mesh layer 0 (their node trees are never
        // rendered); scene content draws the content layer. Walk the node
        // attachments, not gltf_data.meshes: the parse clones the template
        // mesh per instantiating node, and the clones are what enter the
        // scene.
        const erhe::scene::Layer_id content_layer_id = scene_root->layers().content()->id;
        for (const std::shared_ptr<erhe::scene::Node>& node : gltf_data.nodes) {
            if (!node) {
                continue;
            }
            const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
            if (mesh) {
                mesh->layer_id = content_layer_id;
            }
        }
    } else {
        erhe::scene::Scene temp_scene{"temp scene", nullptr};
        const std::shared_ptr<erhe::scene::Node> temp_scene_root_node = temp_scene.get_root_node();
        root_node = std::make_shared<erhe::scene::Node>(erhe::file::to_string(path.filename()));
        // import_root: implicit container, not file content; glTF export writes
        // its children in its place so open/save cycles do not nest wrappers.
        root_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui | erhe::Item_flags::import_root);
        root_node->set_parent(temp_scene_root_node);

        erhe::gltf::Image_transfer image_transfer{graphics_device, command_buffer};
        erhe::gltf::Gltf_parse_arguments parse_arguments{
            .graphics_device = graphics_device,
            .executor        = executor,
            .image_transfer  = image_transfer,
            .root_node       = root_node,
            .mesh_layer_id   = scene_root->layers().content()->id,
            .path            = path,
        };
        gltf_data = erhe::gltf::parse_gltf(parse_arguments);

        // Detach root_node from temp_scene before it goes out of scope. The
        // Item_insert_remove_operation sub-op below will attach root_node to
        // scene_root_node on execute().
        root_node->set_parent({});
    }

    // Color-graph computation (currently unused but preserved as in the
    // previous implementation; the wireframe-color application is commented
    // out below).
    std::vector<glm::vec4> colors;
    colors.emplace_back(0.0f, 1.0f, 1.0f, 1.0f);
    colors.emplace_back(0.0f, 1.0f, 0.0f, 1.0f);
    colors.emplace_back(1.0f, 1.0f, 0.0f, 1.0f);
    colors.emplace_back(1.0f, 0.0f, 0.0f, 1.0f);
    colors.emplace_back(1.0f, 0.0f, 1.0f, 1.0f);
    std::unordered_set<int> available_colors;
    for (int i = 0; i < static_cast<int>(colors.size()); ++i) {
        available_colors.insert(i);
    }
    std::unordered_map<erhe::scene::Node*, int> node_colors;

    bool add_default_camera = true;
    bool add_default_light  = true;

    log_parsers->info("Processing {} nodes", gltf_data.nodes.size());

    size_t mesh_count = 0;
    size_t primitive_count = 0;
    for (const auto& node : gltf_data.nodes) {
        if (!node) {
            continue;
        }
        std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
        if (mesh) {
            ++mesh_count;
            std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_mutable_primitives();
            primitive_count += mesh_primitives.size();
        }
    }
    log_parsers->info("Processing {} nodes, {} meshes, {} primitives", gltf_data.nodes.size(), mesh_count, primitive_count);

    std::vector<std::shared_ptr<erhe::Item_base>> mesh_node_items;
    finalize_imported_meshes(context, build_info, gltf_data, &mesh_node_items);

    // glTF 2.1 external assets: instantiate each referenced asset under its
    // carrier node (recursively resolved and cached by Prefab_library). The
    // scene's content library receives the template resources as reference
    // entries, like the interactive instantiate_prefab.
    if (context.prefab_library != nullptr) {
        resolve_external_assets(*context.prefab_library, gltf_data, scene_root->layers().content()->id, &mesh_node_items, scene_root->get_content_library().get());
    }

    for (const std::shared_ptr<erhe::scene::Node>& node : gltf_data.nodes) {
        if (!node) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Camera> camera = erhe::scene::get_attachment<erhe::scene::Camera>(node.get());
        if (camera) {
            add_default_camera = false;
        }

        const std::shared_ptr<erhe::scene::Light> light = erhe::scene::get_attachment<erhe::scene::Light>(node.get());
        if (light) {
            add_default_light = false;
        }

        if (node->get_parent_node() == root_node) {
            color_graph(node.get(), node_colors, available_colors);
        }
    }

    erhe::scene::Scene* scene = scene_root->get_hosted_scene();
    const std::shared_ptr<erhe::scene::Node> scene_root_node = scene->get_root_node();

    if (!scene->get_cameras().empty()) {
        add_default_camera = false;
    }
    for (const auto& layer : scene->get_light_layers()) {
        if (!layer->lights.empty()) {
            add_default_light = false;
            break;
        }
    }

    // Build default camera and light nodes but do NOT parent them; the
    // Item_insert_remove_operation sub-ops below will parent them to
    // scene_root_node on execute() and detach on undo().
    std::shared_ptr<erhe::scene::Node> default_camera_node;
    std::shared_ptr<erhe::scene::Node> default_key_light_node;
    std::shared_ptr<erhe::scene::Node> default_fill_light_node;

    // The implicit defaults are editor conveniences, not authored content:
    // exclude_from_prefab keeps them out of prefab instances (the flag
    // persists in node extras and instantiation filters flagged items).
    if (add_default_camera) {
        default_camera_node = std::make_shared<erhe::scene::Node>("Camera");
        std::shared_ptr<erhe::scene::Camera> default_camera = std::make_shared<erhe::scene::Camera>("Camera");
        default_camera->projection()->fov_y           = glm::radians(35.0f);
        default_camera->projection()->projection_type = erhe::scene::Projection::Type::perspective_vertical;
        default_camera->projection()->z_near          = 0.03f;
        default_camera->projection()->z_far           = 80.0f;
        default_camera->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui | erhe::Item_flags::exclude_from_prefab);
        default_camera_node->attach(default_camera);

        const glm::mat4 m = erhe::math::create_look_at(
            glm::vec3{0.0f, 0.00f, 8.0f}, // eye
            glm::vec3{0.0f, 0.00f, 0.0f}, // center
            glm::vec3{0.0f, 1.00f, 0.0f}  // up
        );
        default_camera_node->set_parent_from_node(m);
        default_camera_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui | erhe::Item_flags::exclude_from_prefab);
    }

    if (add_default_light) {
        default_key_light_node = std::make_shared<erhe::scene::Node>("Key Light");
        std::shared_ptr<erhe::scene::Light> key_light = std::make_shared<erhe::scene::Light>("Key Light");
        key_light->type      = erhe::scene::Light::Type::directional;
        key_light->color     = glm::vec3{1.0f, 1.0f, 1.0};
        key_light->intensity = 1.0f;
        key_light->range     = 0.0f;
        key_light->layer_id  = scene_root->layers().light()->id;
        key_light->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui | erhe::Item_flags::exclude_from_prefab);
        default_key_light_node->attach          (key_light);
        default_key_light_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui | erhe::Item_flags::exclude_from_prefab);
        const glm::quat key_quat{0.8535534f, -0.3535534f, -0.353553385f, -0.146446586f};
        default_key_light_node->set_parent_from_node(glm::mat4{key_quat});

        default_fill_light_node = std::make_shared<erhe::scene::Node>("Fill Light Node");
        std::shared_ptr<erhe::scene::Light> fill_light = std::make_shared<erhe::scene::Light>("Fill Light");
        fill_light->type      = erhe::scene::Light::Type::directional;
        fill_light->color     = glm::vec3{1.0f, 1.0f, 1.0};
        fill_light->intensity = 0.5f;
        fill_light->range     = 0.0f;
        fill_light->layer_id  = scene_root->layers().light()->id;
        fill_light->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui | erhe::Item_flags::exclude_from_prefab);
        default_fill_light_node->attach          (fill_light);
        default_fill_light_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui | erhe::Item_flags::exclude_from_prefab);
        const glm::quat fill_quat{-0.353553444f, -0.8535534f, 0.146446645f, -0.353553325f};
        default_fill_light_node->set_parent_from_node(glm::mat4{fill_quat});
    }

    std::vector<std::shared_ptr<Operation>> operations;
    append_content_library_attach_operations(scene_root->get_content_library(), gltf_data, path.generic_string(), operations);

    // KHR_physics_rigid_bodies / KHR_implicit_shapes: shared physics items go
    // through content-library attach operations; Node_physics / Node_joint
    // attachments are attached directly to the imported nodes (like meshes)
    // and enter the scene with the insert operation below. Must run after
    // mesh finalization above (mesh-sourced collision shapes need Geometry).
    import_gltf_physics(context, gltf_data, scene_root, path, operations);

    // Editor-domain ERHE_* extensions (doc/gltf-scene-roundtrip-plan.md
    // phase 3): ERHE_layout / ERHE_collections onto the imported nodes,
    // ERHE_brushes / ERHE_node_graphs into the content library. ERHE_scene
    // is deliberately NOT applied here - importing an asset must not
    // clobber the target scene's settings (the phase-4 Open-Scene path
    // consumes it).
    import_gltf_editor_state(context, gltf_data, scene_root, path, operations);

    operations.push_back(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = context,
                .item    = root_node,
                .parent  = scene_root_node,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );

    if (default_camera_node) {
        operations.push_back(
            std::make_shared<Item_insert_remove_operation>(
                Item_insert_remove_operation::Parameters{
                    .context = context,
                    .item    = default_camera_node,
                    .parent  = scene_root_node,
                    .mode    = Item_insert_remove_operation::Mode::insert
                }
            )
        );
    }

    if (default_key_light_node) {
        operations.push_back(
            std::make_shared<Item_insert_remove_operation>(
                Item_insert_remove_operation::Parameters{
                    .context = context,
                    .item    = default_key_light_node,
                    .parent  = scene_root_node,
                    .mode    = Item_insert_remove_operation::Mode::insert
                }
            )
        );
    }

    if (default_fill_light_node) {
        operations.push_back(
            std::make_shared<Item_insert_remove_operation>(
                Item_insert_remove_operation::Parameters{
                    .context = context,
                    .item    = default_fill_light_node,
                    .parent  = scene_root_node,
                    .mode    = Item_insert_remove_operation::Mode::insert
                }
            )
        );
    }

    operations.push_back(
        std::make_shared<Async_raytrace_kickoff_operation>(
            scene_root,
            std::move(mesh_node_items)
        )
    );

    std::shared_ptr<Compound_operation> compound = std::make_shared<Compound_operation>(
        Compound_operation::Parameters{.operations = std::move(operations)}
    );
    compound->set_description(
        fmt::format("[{}] Import glTF {}", compound->get_serial(), erhe::file::to_string(path.filename()))
    );
    return compound;
}

void import_gltf(
    App_context&                       context,
    erhe::primitive::Build_info        build_info,
    const std::shared_ptr<Scene_root>& scene_root,
    const std::filesystem::path&       path
)
{
    context.operation_stack->queue(
        make_import_gltf_operation(context, std::move(build_info), scene_root, path)
    );
}

auto scan_gltf(const std::filesystem::path& path) -> Gltf_scan_summary
{
    Gltf_scan_summary summary;
    std::vector<std::string>& out = summary.contents;
    erhe::gltf::Gltf_scan scan = erhe::gltf::scan_gltf(path);
    summary.bounding_box = scan.bounding_box;

    if (!scan.errors.empty()) {
        out.push_back("Errors:");
        for (const std::string& error : scan.errors) {
            out.push_back(" - " + error);
        }
    }

    if (!scan.scenes            .empty()) out.push_back(fmt::format("{} scenes",             scan.scenes            .size()));
    if (!scan.meshes            .empty()) out.push_back(fmt::format("{} meshes",             scan.meshes            .size()));
    if (!scan.animations        .empty()) out.push_back(fmt::format("{} animations",         scan.animations        .size()));
    if (!scan.skins             .empty()) out.push_back(fmt::format("{} skins",              scan.skins             .size()));
    if (!scan.materials         .empty()) out.push_back(fmt::format("{} materials",          scan.materials         .size()));
    if (!scan.nodes             .empty()) out.push_back(fmt::format("{} nodes",              scan.nodes             .size()));
    if (!scan.cameras           .empty()) out.push_back(fmt::format("{} cameras",            scan.cameras           .size()));
    if (!scan.directional_lights.empty()) out.push_back(fmt::format("{} directional lights", scan.directional_lights.size()));
    if (!scan.point_lights      .empty()) out.push_back(fmt::format("{} point lights",       scan.point_lights      .size()));
    if (!scan.spot_lights       .empty()) out.push_back(fmt::format("{} spot lights",        scan.spot_lights       .size()));
    if (!scan.images            .empty()) out.push_back(fmt::format("{} images",             scan.images            .size()));
    if (!scan.samplers          .empty()) out.push_back(fmt::format("{} samplers",           scan.samplers          .size()));
    if (!scan.files             .empty()) out.push_back(fmt::format("{} files",              scan.files             .size()));
    if (!scan.external_assets.empty()) {
        out.push_back(fmt::format("{} external assets:", scan.external_assets.size()));
        for (const std::string& external_asset : scan.external_assets) {
            out.push_back(" - " + external_asset);
        }
    }
    if (!scan.extensions_used.empty()) {
        out.push_back("Extensions used:");
        for (const std::string& extension : scan.extensions_used) {
            out.push_back(" - " + extension);
        }
    }
    if (!scan.extensions_required.empty()) {
        out.push_back("Extensions required:");
        for (const std::string& extension : scan.extensions_required) {
            out.push_back(" - " + extension);
        }
    }
    if (scan.bounding_box.has_value() && scan.bounding_box->is_valid()) {
        const glm::vec3 size = scan.bounding_box->diagonal();
        out.push_back(fmt::format("size: {:.2f} x {:.2f} x {:.2f}", size.x, size.y, size.z));
    }
    summary.extensions_used = std::move(scan.extensions_used);
    return summary;
}

auto is_erhe_scene(const std::vector<std::string>& extensions_used) -> bool
{
    return std::find(extensions_used.begin(), extensions_used.end(), "ERHE_scene") != extensions_used.end();
}

auto make_gltf_image_source_provider(const std::shared_ptr<Content_library>& content_library)
    -> std::function<std::shared_ptr<const erhe::gltf::Gltf_image_source>(const erhe::graphics::Texture*)>
{
    // Snapshot the retained sources under the library mutex; the returned
    // provider then runs lock-free inside export_gltf().
    using Source_map = std::unordered_map<const erhe::graphics::Texture*, std::shared_ptr<const erhe::gltf::Gltf_image_source>>;
    std::shared_ptr<Source_map> sources = std::make_shared<Source_map>();
    if (content_library && content_library->textures) {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{content_library->mutex};
        content_library->textures->for_each_const<Content_library_node>(
            [&sources](const Content_library_node& node) -> bool {
                const std::shared_ptr<erhe::graphics::Texture> texture = std::dynamic_pointer_cast<erhe::graphics::Texture>(node.item);
                if (texture && node.image_source) {
                    (*sources)[texture.get()] = node.image_source;
                }
                return true;
            }
        );
    }
    return [sources](const erhe::graphics::Texture* texture) -> std::shared_ptr<const erhe::gltf::Gltf_image_source> {
        if (texture == nullptr) {
            return {};
        }
        const auto it = sources->find(texture);
        if (it != sources->end()) {
            return it->second;
        }
        // Fallback for textures imported before retention landed: re-read a
        // standalone source image file. Images embedded in a .glb/.gltf
        // cannot be re-extracted here; the exporter warns and skips the slot.
        const std::filesystem::path* source_path = texture->get_source_path();
        if ((source_path != nullptr) && !source_path->empty()) {
            const std::string extension = source_path->extension().generic_string();
            if ((extension != ".glb") && (extension != ".gltf")) {
                const std::optional<std::string> file_content = erhe::file::read("gltf export image source fallback", *source_path);
                if (file_content.has_value() && !file_content->empty()) {
                    std::shared_ptr<erhe::gltf::Gltf_image_source> image_source = std::make_shared<erhe::gltf::Gltf_image_source>();
                    const std::byte* start = reinterpret_cast<const std::byte*>(file_content->data());
                    image_source->encoded_bytes.assign(start, start + file_content->size());
                    image_source->mime_type = erhe::gltf::sniff_image_mime_type(image_source->encoded_bytes);
                    (*sources)[texture] = image_source;
                    return image_source;
                }
            }
        }
        return {};
    };
}

auto collect_gltf_export_animations(const std::shared_ptr<Content_library>& content_library)
    -> std::vector<std::shared_ptr<erhe::scene::Animation>>
{
    if (!content_library || !content_library->animations) {
        return {};
    }
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{content_library->mutex};
    return content_library->animations->get_all<erhe::scene::Animation>();
}

auto save_scene_gltf(Scene_root& scene_root, const std::filesystem::path& path) -> bool
{
    const erhe::scene::Scene& scene = scene_root.get_scene();
    const std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
    if (!root_node) {
        log_parsers->error("save_scene_gltf: scene '{}' has no root node", scene_root.get_name());
        return false;
    }
    const erhe::gltf::Gltf_physics_data physics_data = build_gltf_physics_data(scene, scene_root.get_content_library().get());
    erhe::gltf::Gltf_export_arguments export_arguments{
        .root_node             = *root_node,
        .binary                = path.extension() != std::filesystem::path{".gltf"},
        .physics_data          = &physics_data,
        .external_assets       = collect_prefab_external_assets(*root_node, path.parent_path()),
        .image_source_provider = make_gltf_image_source_provider(scene_root.get_content_library()),
        .animations            = collect_gltf_export_animations(scene_root.get_content_library())
    };
    // Editor-domain ERHE_* extensions + baked graph-mesh exclusion: this is
    // what makes the file a full scene save instead of an interchange export
    // (ERHE_scene in extensionsUsed is the erhe-authored marker).
    add_gltf_editor_state(export_arguments, scene_root);
    const std::string gltf = erhe::gltf::export_gltf(export_arguments);
    if (!erhe::file::write_file(path, gltf)) {
        log_parsers->error("save_scene_gltf: failed to write '{}'", erhe::file::to_string(path));
        return false;
    }
    return true;
}

auto save_scene_gltf(App_context& context, Scene_root& scene_root, const std::filesystem::path& path) -> bool
{
    if (!save_scene_gltf(scene_root, path)) {
        return false;
    }
    // R5.8: a successful save clears the scene's container record dirty flag
    // (the file now matches the live asset state).
    if (context.asset_manager != nullptr) {
        context.asset_manager->on_scene_saved(scene_root);
    }
    // Rescan the asset browser so the freshly saved scene appears without a
    // manual Scan (#256).
    context.app_message_bus->scene_saved.send_message(Scene_saved_message{.path = path});
    // Saving over a loaded prefab source refreshes every instance in every
    // scene (this side effect used to be the separate Save Prefab command).
    if (context.prefab_library != nullptr) {
        std::error_code error_code;
        std::filesystem::path canonical_path = std::filesystem::weakly_canonical(path, error_code);
        if (error_code) {
            canonical_path = path;
        }
        if (context.prefab_library->get_prefabs().contains(canonical_path)) {
            context.prefab_library->reload(canonical_path);
        }
    }
    return true;
}

auto default_scene_dir() -> std::filesystem::path
{
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::absolute(std::filesystem::path{"res"} / "editor" / "scenes", ec);
    if (ec) {
        dir = std::filesystem::path{"res"} / "editor" / "scenes";
    }
    static_cast<void>(erhe::file::ensure_directory_exists(dir));
    return dir;
}

auto resolve_scene_save_path(const Scene_root& scene_root) -> std::filesystem::path
{
    const std::filesystem::path& source_path = scene_root.get_source_path();
    if (!source_path.empty()) {
        return source_path;
    }
    return default_scene_dir() / (scene_root.get_name() + ".glb");
}

auto open_scene_gltf(
    App_context&                 context,
    const std::filesystem::path& path
) -> std::shared_ptr<Scene_root>
{
    ERHE_VERIFY(context.current_command_buffer != nullptr);

    // R5.7 record adoption (plan resolution 3): a container already loaded
    // for this path lends its parse - read in place, because the ERHE_scene
    // payload (enable_physics) must be known before the Scene_root can be
    // constructed. Registering the scene below adopts the record; the
    // take_adopted_parse call at the end severs the record's structure pins
    // once the open succeeded. Until then the record stays intact, so a
    // failed open leaves the loaded container exactly as it was.
    std::shared_ptr<Asset_container_record> adoptable_record;
    if (context.asset_manager != nullptr) {
        adoptable_record = context.asset_manager->find_adoptable_container(path);
    }

    // Parse into a temporary container when there is nothing to adopt. Mesh
    // layer ids are editor-wide constants (Mesh_layer_id), so parsing before
    // the destination scene exists is safe.
    erhe::scene::Scene temp_scene{"temp scene", nullptr};
    std::shared_ptr<erhe::scene::Node> container_node;
    erhe::gltf::Gltf_data              parsed_gltf_data;
    if (adoptable_record) {
        container_node = adoptable_record->root_node;
        log_parsers->info("open_scene_gltf: adopting loaded container record for '{}'", erhe::file::to_string(path));
    } else {
        const std::shared_ptr<erhe::scene::Node> temp_scene_root_node = temp_scene.get_root_node();
        container_node = std::make_shared<erhe::scene::Node>("open scene container");
        container_node->set_parent(temp_scene_root_node);

        erhe::gltf::Image_transfer image_transfer{*context.graphics_device, *context.current_command_buffer};
        erhe::gltf::Gltf_parse_arguments parse_arguments{
            .graphics_device = *context.graphics_device,
            .executor        = *context.executor,
            .image_transfer  = image_transfer,
            .root_node       = container_node,
            .mesh_layer_id   = Mesh_layer_id::content,
            .path            = path,
        };
        parsed_gltf_data = erhe::gltf::parse_gltf(parse_arguments);
    }
    const erhe::gltf::Gltf_data& gltf_data = adoptable_record ? adoptable_record->gltf_data : parsed_gltf_data;

    // Container parses use mesh layer 0 (their node trees are never
    // rendered); scene content draws the content layer. Walk the node
    // attachments, not gltf_data.meshes: the parse clones the template mesh
    // per instantiating node, and the clones are what enter the scene.
    if (adoptable_record) {
        for (const std::shared_ptr<erhe::scene::Node>& node : gltf_data.nodes) {
            if (!node) {
                continue;
            }
            const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
            if (mesh) {
                mesh->layer_id = Mesh_layer_id::content;
            }
        }
    }

    const std::optional<Gltf_scene_state> scene_state = parse_gltf_scene_state(gltf_data);
    if (!scene_state.has_value()) {
        // The callers route only ERHE_scene-marked files here, so a missing
        // payload means the file could not be parsed at all.
        log_parsers->error("open_scene_gltf: '{}' has no ERHE_scene payload - not an erhe-authored scene or parse failed", erhe::file::to_string(path));
        return {};
    }

    // Fresh, EMPTY content library: the file carries the scene's own brushes /
    // materials / textures (a create_scene-style library pre-populated with
    // the standard brushes would duplicate the saved ones).
    std::shared_ptr<Content_library> content_library = std::make_shared<Content_library>();
    std::shared_ptr<Scene_root> scene_root = std::make_shared<Scene_root>(
        context.app_message_bus,
        content_library,
        erhe::file::to_string(path.stem()),
        scene_state->enable_physics
    );
    // Remember where the scene came from (same as Scene_open_operation does
    // for foreign glTF): Save Scene writes back here, without confirmation.
    {
        std::error_code error_code;
        const std::filesystem::path canonical_path = std::filesystem::weakly_canonical(path, error_code);
        scene_root->set_source_path(error_code ? path : canonical_path);
    }

    // Apply the ERHE_scene payload - the one thing the import path
    // deliberately leaves alone (importing an asset must not clobber the
    // target scene's settings).
    erhe::scene::Scene& scene = scene_root->get_scene();
    scene.ambient_light = scene_state->ambient_light;
    if (!scene_state->settings_json.empty()) {
        simdjson::ondemand::parser settings_parser;
        simdjson::padded_string    settings_padded{scene_state->settings_json};
        simdjson::ondemand::document settings_document;
        simdjson::ondemand::object   settings_object;
        if (
            (settings_parser.iterate(settings_padded).get(settings_document) == simdjson::SUCCESS) &&
            (settings_document.get_object().get(settings_object) == simdjson::SUCCESS) &&
            (deserialize(settings_object, scene_root->get_scene_settings()) == simdjson::SUCCESS)
        ) {
            log_parsers->info("open_scene_gltf: applied per-scene setting overrides");
        } else {
            log_parsers->error("open_scene_gltf: failed to deserialize ERHE_scene settings payload");
        }
    }

    scene_root->register_to_editor_scenes(*context.app_scenes);

    std::vector<std::shared_ptr<erhe::Item_base>> mesh_node_items;
    finalize_imported_meshes(context, make_import_build_info(context), gltf_data, &mesh_node_items);

    // glTF 2.1 external assets: instantiate each referenced prefab under its
    // carrier node. The scene's content library receives the template
    // resources as reference entries, like the interactive
    // instantiate_prefab - without them, register_mesh would mis-adopt the
    // unhosted template materials as scene-owned when the nodes move under
    // the scene root below.
    if (context.prefab_library != nullptr) {
        resolve_external_assets(*context.prefab_library, gltf_data, scene_root->layers().content()->id, &mesh_node_items, content_library.get());
    }

    // Content-library attaches (textures / materials / skins / animations),
    // physics items and editor-domain ERHE_* state. These build undoable
    // operations for the import path; here they are executed inline and
    // dropped - opening a scene is not undoable. Same ordering as the import
    // compound: everything executes before the nodes enter the scene.
    std::vector<std::shared_ptr<Operation>> operations;
    append_content_library_attach_operations(content_library, gltf_data, path.generic_string(), operations);
    import_gltf_physics(context, gltf_data, scene_root, path, operations);
    import_gltf_editor_state(context, gltf_data, scene_root, path, operations);
    for (const std::shared_ptr<Operation>& operation : operations) {
        operation->execute(context);
    }

    // Move the parsed top-level nodes directly under the new scene's root:
    // the saved file carries the scene's children in place (import_root
    // wrappers are never written), so open adds no wrapper either. No
    // default camera / lights are injected - an erhe-authored scene carries
    // exactly the cameras and lights it was saved with. Copy the child list:
    // reparenting mutates it.
    const std::shared_ptr<erhe::scene::Node> scene_root_node = scene.get_root_node();
    const std::vector<std::shared_ptr<erhe::Hierarchy>> children = container_node->get_children();
    for (const std::shared_ptr<erhe::Hierarchy>& child : children) {
        child->set_parent(scene_root_node);
    }

    // Raytrace kickoff, mirroring the import compound's final sub-operation.
    Async_raytrace_kickoff_operation raytrace_kickoff{scene_root, std::move(mesh_node_items)};
    raytrace_kickoff.execute(context);

    // Adopted open succeeded: sever the record's structure pins (the parse
    // was consumed in place; the nodes now live in - and must die with -
    // this scene). The record became the scene's record at registration.
    if (adoptable_record && (context.asset_manager != nullptr)) {
        static_cast<void>(context.asset_manager->take_adopted_parse(*scene_root, path));
    }

    log_parsers->info("open_scene_gltf: opened scene '{}' from '{}'", scene_root->get_name(), erhe::file::to_string(path));
    return scene_root;
}

}
