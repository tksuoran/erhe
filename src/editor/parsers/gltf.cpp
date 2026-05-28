// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "parsers/gltf.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "scene/scene_root.hpp"
#include "operations/async_raytrace_kickoff_operation.hpp"
#include "operations/compound_operation.hpp"
#include "operations/content_library_attach_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/operation_stack.hpp"

#include "scene/generated/gltf_source_reference.hpp"

#include "items.hpp"

#include "erhe_file/file.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_gltf/gltf.hpp"
#include "erhe_gltf/image_transfer.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"

#include "erhe_math/math_util.hpp"

#include <fmt/format.h>

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

}

void import_gltf(
    App_context&                       context,
    erhe::primitive::Build_info        build_info,
    const std::shared_ptr<Scene_root>& scene_root,
    const std::filesystem::path&       path
)
{
    ERHE_VERIFY(scene_root);
    erhe::graphics::Device& graphics_device = *context.graphics_device;
    tf::Executor&           executor        = *context.executor;
    ERHE_VERIFY(context.current_command_buffer != nullptr);
    erhe::graphics::Command_buffer& command_buffer = *context.current_command_buffer;

    erhe::scene::Scene temp_scene{"temp scene", nullptr};
    const std::shared_ptr<erhe::scene::Node> temp_scene_root_node = temp_scene.get_root_node();
    std::shared_ptr<erhe::scene::Node> root_node = std::make_shared<erhe::scene::Node>(erhe::file::to_string(path.filename()));
    root_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
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
    erhe::gltf::Gltf_data gltf_data = erhe::gltf::parse_gltf(parse_arguments);

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

        const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
        if (mesh) {
            mesh_node_items.push_back(node);

            std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_mutable_primitives();
            for (erhe::scene::Mesh_primitive& mesh_primitive : mesh_primitives) {
                erhe::primitive::Primitive& primitive = *mesh_primitive.primitive.get();
                ERHE_VERIFY(primitive.make_renderable_mesh(build_info, erhe::primitive::Normal_style::corner_normals));
            }

            mesh->update_rt_primitives();
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

    if (add_default_camera) {
        default_camera_node = std::make_shared<erhe::scene::Node>("Camera");
        std::shared_ptr<erhe::scene::Camera> default_camera = std::make_shared<erhe::scene::Camera>("Camera");
        default_camera->projection()->fov_y           = glm::radians(35.0f);
        default_camera->projection()->projection_type = erhe::scene::Projection::Type::perspective_vertical;
        default_camera->projection()->z_near          = 0.03f;
        default_camera->projection()->z_far           = 80.0f;
        default_camera->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
        default_camera_node->attach(default_camera);

        const glm::mat4 m = erhe::math::create_look_at(
            glm::vec3{0.0f, 0.00f, 8.0f}, // eye
            glm::vec3{0.0f, 0.00f, 0.0f}, // center
            glm::vec3{0.0f, 1.00f, 0.0f}  // up
        );
        default_camera_node->set_parent_from_node(m);
        default_camera_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    }

    if (add_default_light) {
        default_key_light_node = std::make_shared<erhe::scene::Node>("Key Light");
        std::shared_ptr<erhe::scene::Light> key_light = std::make_shared<erhe::scene::Light>("Key Light");
        key_light->type      = erhe::scene::Light::Type::directional;
        key_light->color     = glm::vec3{1.0f, 1.0f, 1.0};
        key_light->intensity = 1.0f;
        key_light->range     = 0.0f;
        key_light->layer_id  = scene_root->layers().light()->id;
        key_light->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
        default_key_light_node->attach          (key_light);
        default_key_light_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
        const glm::quat key_quat{0.8535534f, -0.3535534f, -0.353553385f, -0.146446586f};
        default_key_light_node->set_parent_from_node(glm::mat4{key_quat});

        default_fill_light_node = std::make_shared<erhe::scene::Node>("Fill Light Node");
        std::shared_ptr<erhe::scene::Light> fill_light = std::make_shared<erhe::scene::Light>("Fill Light");
        fill_light->type      = erhe::scene::Light::Type::directional;
        fill_light->color     = glm::vec3{1.0f, 1.0f, 1.0};
        fill_light->intensity = 0.5f;
        fill_light->range     = 0.0f;
        fill_light->layer_id  = scene_root->layers().light()->id;
        fill_light->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
        default_fill_light_node->attach          (fill_light);
        default_fill_light_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
        const glm::quat fill_quat{-0.353553444f, -0.8535534f, 0.146446645f, -0.353553325f};
        default_fill_light_node->set_parent_from_node(glm::mat4{fill_quat});
    }

    // Detach root_node from temp_scene before letting temp_scene go out of
    // scope. The Item_insert_remove_operation sub-op below will attach
    // root_node to scene_root_node on execute().
    root_node->set_parent({});

    std::shared_ptr<Content_library> content_library = scene_root->get_content_library();
    const std::string gltf_path_str = path.generic_string();

    std::vector<std::shared_ptr<Operation>> operations;

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
                    }
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
    context.operation_stack->queue(compound);
}

auto scan_gltf(const std::filesystem::path& path) -> std::vector<std::string>
{
    std::vector<std::string> out;
    erhe::gltf::Gltf_scan scan = erhe::gltf::scan_gltf(path);

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
    return out;
}

}
