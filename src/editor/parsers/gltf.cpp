// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "parsers/gltf.hpp"

#include "scene/content_library.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"

#include "erhe/geometry/geometry.hpp"
#include "erhe/gltf/gltf.hpp"
#include "erhe/gltf/image_transfer.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"

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
        auto child_node = as_node(child);
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
        auto child_node = as_node(child);
        if (!child_node) {
            continue;
        }
        color_graph(child_node.get(), node_colors, available_colors);
    }
}

}

void import_gltf(
    erhe::graphics::Instance&    graphics_instance,
    erhe::primitive::Build_info  build_info,
    Scene_root&                  scene_root,
    const std::filesystem::path& path
)
{
    const auto scene_root_node = scene_root.get_hosted_scene()->get_root_node();

    // TODO Make importing an operation
    auto root_node = std::make_shared<erhe::scene::Node>(path.filename().string());
    root_node->enable_flag_bits(erhe::scene::Item_flags::content | erhe::scene::Item_flags::show_in_ui);
    root_node->set_parent(scene_root_node);

    const erhe::scene::Layer_id content_layer_id = scene_root.layers().content()->id;
    erhe::gltf::Image_transfer image_transfer{graphics_instance};
    erhe::gltf::Gltf_data gltf_data = erhe::gltf::parse_gltf(
        graphics_instance,
        image_transfer,
        root_node,
        content_layer_id,
        path
    );

    // m_default_material = std::make_shared<erhe::primitive::Material>(
    //     "Default",
    //     glm::vec3{0.500f, 0.500f, 0.500f},
    //     glm::vec2{1.0f, 1.0f},
    //     0.0f
    // );
    class Geometry_primitive
    {
    public:
        erhe::primitive::Primitive_geometry gl_primitive_geometry;
        std::shared_ptr<Raytrace_primitive> raytrace_primitive;
    };

    std::unordered_map<erhe::geometry::Geometry*, Geometry_primitive> geometry_primitives;

    // Create primitive data for each geometry
    for (const auto& geometry : gltf_data.geometries) {
        geometry_primitives[geometry.get()] = Geometry_primitive{
            .gl_primitive_geometry = erhe::primitive::make_primitive(*geometry.get(), build_info),
            .raytrace_primitive    = std::make_shared<Raytrace_primitive>(geometry)
        };
    }

    for (const auto& image : gltf_data.images) {
        scene_root.content_library()->textures.add(image);
    }

    for (const auto& material : gltf_data.materials) {
        scene_root.content_library()->materials.add(material);
    }

    for (const auto& skin : gltf_data.skins) {
        scene_root.content_library()->skins.add(skin);
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

    for (const auto& node : gltf_data.nodes) {
        // Apply primitive data, attach node raytrace
        auto camera = erhe::scene::get_camera(node.get());
        if (camera) {
            scene_root.content_library()->cameras.add(camera);
        }

        auto light = erhe::scene::get_light(node.get());
        if (light) {
            scene_root.content_library()->lights.add(light);
        }

        auto mesh = erhe::scene::get_mesh(node.get());
        if (mesh) {
            scene_root.content_library()->meshes.add(mesh);
            for (auto& primitive : mesh->mesh_data.primitives) {
                if (!primitive.source_geometry) {
                    continue;
                }
                Geometry_primitive& geometry_primitive = geometry_primitives[primitive.source_geometry.get()];
                primitive.gl_primitive_geometry = geometry_primitive.gl_primitive_geometry;
                primitive.rt_primitive_geometry = geometry_primitive.raytrace_primitive->primitive_geometry;
                auto node_raytrace = std::make_shared<Node_raytrace>( // TODO use content library?
                    primitive.source_geometry,
                    geometry_primitive.raytrace_primitive
                );
                node->attach(node_raytrace);
            }
        }

        if (node->get_parent_node() == root_node) {
            color_graph(node.get(), node_colors, available_colors);
        }
    }
    for (const auto& node : gltf_data.nodes) {
        auto i = node_colors.find(node.get());
        if (i != node_colors.end()) {
            int color = i->second;
            node->set_wireframe_color(colors.at(color));
        }
    }

    for (const auto& animation : gltf_data.animations) {
        scene_root.content_library()->animations.add(animation);
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
