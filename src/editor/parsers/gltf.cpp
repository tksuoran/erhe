// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "parsers/gltf.hpp"

#include "scene/content_library.hpp"
#include "scene/scene_root.hpp"

#include "erhe_file/file.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_gltf/gltf.hpp"
#include "erhe_gltf/image_transfer.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"

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
    erhe::graphics::Device&      graphics_device,
    erhe::primitive::Build_info  build_info,
    Scene_root&                  scene_root,
    const std::filesystem::path& path
)
{
    erhe::scene::Scene_message_bus temp_scene_message_bus;
    erhe::scene::Scene temp_scene{temp_scene_message_bus, "temp scene", nullptr};
    const auto temp_scene_root_node = temp_scene.get_root_node();
    auto root_node = std::make_shared<erhe::scene::Node>(erhe::file::to_string(path.filename()));
    root_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    root_node->set_parent(temp_scene_root_node); // Will be moved to final scene later

    erhe::gltf::Image_transfer image_transfer{graphics_device};
    erhe::gltf::Gltf_parse_arguments parse_arguments{
        .graphics_device = graphics_device,
        .image_transfer  = image_transfer,
        .root_node       = root_node,
        .mesh_layer_id   = scene_root.layers().content()->id,
        .path            = path,
    };
    erhe::gltf::Gltf_data gltf_data = erhe::gltf::parse_gltf(parse_arguments);

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root.item_host_mutex};

    erhe::scene::Scene* scene = scene_root.get_hosted_scene();
    auto& layers              = scene_root.layers();
    layers.light()->ambient_light = glm::vec4{0.17f, 0.17f, 0.17f, 0.0f};

    const auto scene_root_node = scene->get_root_node();

    // TODO Make importing an operation

    std::shared_ptr<Content_library> content_library = scene_root.content_library();
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
    bool add_default_light = false;
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
            std::vector<erhe::primitive::Primitive>& primitives = mesh->get_mutable_primitives();
            primitive_count += primitives.size();
        }
    }
    log_parsers->info("Processing {} nodes, {} meshes, {} primitives", gltf_data.nodes.size(), mesh_count, primitive_count);

    for (const auto& node : gltf_data.nodes) {
        if (!node) {
            continue;
        }
        // Apply primitive data, attach node raytrace
        auto camera = erhe::scene::get_camera(node.get());
        if (camera) {
            //content_library->cameras.add(camera);
            add_default_camera = false;
        }

        auto light = erhe::scene::get_light(node.get());
        if (light) {
            //content_library->lights.add(light);
            add_default_light = false;
        }

        auto mesh = erhe::scene::get_mesh(node.get());
        if (mesh) {
            // TODO Defer geometry / raytrace / renderable mesh generation
            std::vector<erhe::primitive::Primitive>& primitives = mesh->get_mutable_primitives();
            for (erhe::primitive::Primitive& primitive : primitives) {
                // Ensure geometry exists
                const bool geometry_ok = primitive.make_geometry();
                if (!geometry_ok) {
                    continue;
                }

                // Ensure raytrace exists
                ERHE_VERIFY(primitive.make_raytrace());

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
        auto node   = std::make_shared<erhe::scene::Node>("Default Camera Node");
        auto camera = std::make_shared<erhe::scene::Camera>("Default Camera"); //content_library->cameras.make("Default Camera");
        camera->projection()->fov_y           = glm::radians(35.0f);
        camera->projection()->projection_type = erhe::scene::Projection::Type::perspective_vertical;
        camera->projection()->z_near          = 0.03f;
        camera->projection()->z_far           = 80.0f;
        camera->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
        node->attach(camera);
        node->set_parent(scene_root_node);

        const glm::mat4 m = erhe::math::create_look_at(
            glm::vec3{0.0f, 1.80f, -8.0f}, // eye
            glm::vec3{0.0f, 1.80f,  0.0f}, // center
            glm::vec3{0.0f, 1.00f,  0.0f}  // up
        );
        node->set_parent_from_node(m);
        node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    }

    if (add_default_light) {
        auto node  = std::make_shared<erhe::scene::Node>("Default Light Node");
        auto light = std::make_shared<erhe::scene::Light>("Default Light"); //content_library->lights.make("Default Light");
        light->type      = erhe::scene::Light::Type::directional;
        light->color     = glm::vec3{1.0f, 1.0f, 1.0};
        light->intensity = 1.0f;
        light->range     = 0.0f;
        light->layer_id  = scene_root.layers().light()->id;
        light->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
        node->attach          (light);
        node->set_parent      (scene_root_node);
        node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);

        const glm::mat4 m = erhe::math::create_look_at(
            glm::vec3{0.0f, 8.0f, 0.0f}, // eye
            glm::vec3{0.0f, 0.0f, 0.0f}, // center
            glm::vec3{1.0f, 0.0f, 0.0f}  // up
        );
        node->set_parent_from_node(m);
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
        scene_root.content_library()->animations->add(animation);
        //animation->apply(0.0f);
    }
}

auto scan_gltf(const std::filesystem::path& path) -> std::vector<std::string>
{
    std::vector<std::string> out;
    erhe::gltf::Gltf_scan scan = erhe::gltf::scan_gltf(path);

    if (!scan.scenes    .empty()) out.push_back(fmt::format("{} scenes",     scan.scenes    .size()));
    if (!scan.meshes    .empty()) out.push_back(fmt::format("{} meshes",     scan.meshes    .size()));
    if (!scan.animations.empty()) out.push_back(fmt::format("{} animations", scan.animations.size()));
    if (!scan.skins     .empty()) out.push_back(fmt::format("{} skins",      scan.skins     .size()));
    if (!scan.materials .empty()) out.push_back(fmt::format("{} materials",  scan.materials .size()));
    if (!scan.nodes     .empty()) out.push_back(fmt::format("{} nodes",      scan.nodes     .size()));
    if (!scan.cameras   .empty()) out.push_back(fmt::format("{} cameras",    scan.cameras   .size()));
    if (!scan.lights    .empty()) out.push_back(fmt::format("{} lights",     scan.lights    .size()));
    if (!scan.images    .empty()) out.push_back(fmt::format("{} images",     scan.images    .size()));
    if (!scan.samplers  .empty()) out.push_back(fmt::format("{} samplers",   scan.samplers  .size()));
    return out;
}

} // namespace editor
