// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "parsers/gltf.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "scene/scene_root.hpp"
#include "operations/mesh_operation.hpp"

#include "items.hpp"

#include "erhe_file/file.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_gltf/gltf.hpp"
#include "erhe_gltf/image_transfer.hpp"
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
    App_context&                 context,
    erhe::primitive::Build_info  build_info,
    Scene_root&                  scene_root,
    const std::filesystem::path& path
)
{
    erhe::graphics::Device& graphics_device = *context.graphics_device;
    tf::Executor&           executor        = *context.executor;

    erhe::scene::Scene_message_bus temp_scene_message_bus;
    erhe::scene::Scene temp_scene{temp_scene_message_bus, "temp scene", nullptr};
    const auto temp_scene_root_node = temp_scene.get_root_node();
    auto root_node = std::make_shared<erhe::scene::Node>(erhe::file::to_string(path.filename()));
    root_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    root_node->set_parent(temp_scene_root_node); // Will be moved to final scene later

    erhe::gltf::Image_transfer image_transfer{graphics_device};
    erhe::gltf::Gltf_parse_arguments parse_arguments{
        .graphics_device = graphics_device,
        .executor        = executor,
        .image_transfer  = image_transfer,
        .root_node       = root_node,
        .mesh_layer_id   = scene_root.layers().content()->id,
        .path            = path,
    };
    erhe::gltf::Gltf_data gltf_data = erhe::gltf::parse_gltf(parse_arguments);

    std::vector<std::shared_ptr<erhe::Item_base>> mesh_node_items;
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root.item_host_mutex};

        erhe::scene::Scene* scene = scene_root.get_hosted_scene();
        //auto& layers              = scene_root.layers();
        //layers.light()->ambient_light = glm::vec4{0.17f, 0.17f, 0.17f, 0.0f};

        const auto scene_root_node = scene->get_root_node();

        // TODO Make importing an operation

        std::shared_ptr<Content_library> content_library = scene_root.get_content_library();
        log_parsers->info("Processing {} textures", gltf_data.images.size());
        for (const auto& image : gltf_data.images) {
            if (image) {
                content_library->textures->add(image);
            }
        }

        log_parsers->info("Processing {} materials", gltf_data.materials.size());
        for (const auto& material : gltf_data.materials) {
            if (material) {
                content_library->materials->add(material);
            }
        }

        log_parsers->info("Processing {} skins", gltf_data.skins.size());
        for (const auto& skin : gltf_data.skins) {
            if (skin) {
                content_library->skins->add(skin);
            }
        }

        // Assign node colors
        std::vector<glm::vec4> colors;
        colors.emplace_back(0.0f, 1.0f, 1.0f, 1.0f);
        colors.emplace_back(0.0f, 1.0f, 0.0f, 1.0f);
        colors.emplace_back(1.0f, 1.0f, 0.0f, 1.0f);
        colors.emplace_back(1.0f, 0.0f, 0.0f, 1.0f);
        colors.emplace_back(1.0f, 0.0f, 1.0f, 1.0f);
        std::unordered_set<int> available_colors;
        for (int i = 0; i < colors.size(); ++i) {
            available_colors.insert(i);
        }

        std::unordered_map<erhe::scene::Node*, int> node_colors;

        bool add_default_camera = true;
        bool add_default_light = true;
        log_parsers->info("Processing {} nodes", gltf_data.nodes.size());

        size_t mesh_count = 0;
        size_t primitive_count = 0;
        for (const auto& node : gltf_data.nodes) {
            if (!node) {
                continue;
            }
            auto mesh = erhe::scene::get_mesh(node.get());
            if (mesh) {
                ++mesh_count;
                std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_mutable_primitives();
                primitive_count += mesh_primitives.size();
            }
        }
        log_parsers->info("Processing {} nodes, {} meshes, {} primitives", gltf_data.nodes.size(), mesh_count, primitive_count);

        for (const std::shared_ptr<erhe::scene::Node>& node : gltf_data.nodes) {
            if (!node) {
                continue;
            }
            // Apply primitive data, attach node raytrace
            const std::shared_ptr<erhe::scene::Camera> camera = erhe::scene::get_camera(node.get());
            if (camera) {
                //content_library->cameras.add(camera);
                add_default_camera = false;
            }

            const std::shared_ptr<erhe::scene::Light> light = erhe::scene::get_light(node.get());
            if (light) {
                //content_library->lights.add(light);
                add_default_light = false;
            }

            const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(node.get());
            if (mesh) {
                mesh_node_items.push_back(node);

                // TODO Defer geometry / raytrace / renderable mesh generation
                std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_mutable_primitives();
                for (erhe::scene::Mesh_primitive& mesh_primitive : mesh_primitives) {
                    erhe::primitive::Primitive& primitive = *mesh_primitive.primitive.get();
                    // Ensure geometry exists
                    //// const bool geometry_ok = primitive.make_geometry();
                    //// if (!geometry_ok) {
                    ////     continue;
                    //// }

                    // Ensure raytrace exists
                    //// ERHE_VERIFY(primitive.make_raytrace());

                    // Ensure renderable mesh exists
                    ERHE_VERIFY(primitive.make_renderable_mesh(build_info, erhe::primitive::Normal_style::corner_normals));
                }

                mesh->update_rt_primitives();
            }

            if (node->get_parent_node() == root_node) {
                color_graph(node.get(), node_colors, available_colors);
            }
        }

        if (!scene->get_cameras().empty()) {
            add_default_camera = false;
        }
        for (const auto& layer : scene->get_light_layers()) {
            if (!layer->lights.empty()) {
                add_default_light = false;
                break;
            }
        }

        if (add_default_camera) {
            auto node   = std::make_shared<erhe::scene::Node>("Camera");
            auto camera = std::make_shared<erhe::scene::Camera>("Camera");
            camera->projection()->fov_y           = glm::radians(35.0f);
            camera->projection()->projection_type = erhe::scene::Projection::Type::perspective_vertical;
            camera->projection()->z_near          = 0.03f;
            camera->projection()->z_far           = 80.0f;
            camera->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
            node->attach(camera);
            node->set_parent(scene_root_node);

            const glm::mat4 m = erhe::math::create_look_at(
                glm::vec3{0.0f, 0.00f, 8.0f}, // eye
                glm::vec3{0.0f, 0.00f, 0.0f}, // center
                glm::vec3{0.0f, 1.00f, 0.0f}  // up
            );
            node->set_parent_from_node(m);
            node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
        }

        if (add_default_light) {
            auto key_node   = std::make_shared<erhe::scene::Node>("Key Light");
            auto key_light  = std::make_shared<erhe::scene::Light>("Key Light");
            key_light->type      = erhe::scene::Light::Type::directional;
            key_light->color     = glm::vec3{1.0f, 1.0f, 1.0};
            key_light->intensity = 1.0f;
            key_light->range     = 0.0f;
            key_light->layer_id  = scene_root.layers().light()->id;
            key_light->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
            key_node->attach          (key_light);
            key_node->set_parent      (scene_root_node);
            key_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
            const glm::quat key_quat{0.8535534f, -0.3535534f, -0.353553385f, -0.146446586f};
            key_node->set_parent_from_node(glm::mat4{key_quat});
            auto fill_node  = std::make_shared<erhe::scene::Node>("Fill Light Node");
            auto fill_light = std::make_shared<erhe::scene::Light>("Fill Light");
            fill_light->type      = erhe::scene::Light::Type::directional;
            fill_light->color     = glm::vec3{1.0f, 1.0f, 1.0};
            fill_light->intensity = 0.5f;
            fill_light->range     = 0.0f;
            fill_light->layer_id  = scene_root.layers().light()->id;
            fill_light->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
            fill_node->attach          (fill_light);
            fill_node->set_parent      (scene_root_node);
            fill_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
            const glm::quat fill_quat{-0.353553444f, -0.8535534f, 0.146446645f, -0.353553325f};
            fill_node->set_parent_from_node(glm::mat4{fill_quat});
        }

        // Move to final scene
        root_node->set_parent(scene_root_node);

        //// for (const auto& node : gltf_data.nodes) {
        ////     auto i = node_colors.find(node.get());
        ////     if (i != node_colors.end()) {
        ////         int color = i->second;
        ////         node->set_wireframe_color(colors.at(color));
        ////     }
        //// }

        for (const auto& animation : gltf_data.animations) {
            if (!animation) {
                continue;
            }
            scene_root.get_content_library()->animations->add(animation);
            //animation->apply(0.0f);
        }
    }

    async_for_nodes_with_mesh(
        context,
        mesh_node_items,
        [](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            for (const std::shared_ptr<erhe::Item_base>& item : mesh_operation_parameters.items) {
                std::shared_ptr<erhe::scene::Mesh> scene_mesh = erhe::scene::get_mesh(item);
                ERHE_VERIFY(scene_mesh);
                erhe::Item_host* item_host = item->get_item_host();
                ERHE_VERIFY(item_host != nullptr);
                Scene_root* scene_root = static_cast<Scene_root*>(item_host);
                std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root->item_host_mutex};

                scene_root->begin_mesh_rt_update(scene_mesh);
                std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = scene_mesh->get_mutable_primitives();
                for (erhe::scene::Mesh_primitive& mesh_primitive : mesh_primitives) {
                    erhe::primitive::Primitive& primitive = *mesh_primitive.primitive.get();
                    // Ensure raytrace exists
                    ERHE_VERIFY(primitive.make_raytrace());
                }
                scene_mesh->update_rt_primitives();
                scene_root->end_mesh_rt_update(scene_mesh);
            }
        }
    );
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
