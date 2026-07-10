#pragma once

#include "gltf_physics.hpp"

#include <memory>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace erhe::geometry {
    class Geometry;
}
namespace erhe::graphics {
    class Device;
    class Sampler;
    class Texture;
    class Vertex_format;
}
namespace erhe::primitive {
    class Buffer_sink;
    class Primitive;
    class Material;
}
namespace erhe::scene {
    class Animation;
    class Camera;
    class Light;
    class Mesh;
    class Node;
    class Scene;
    class Skin;
    using Layer_id = uint64_t;
}

namespace tf { class Executor; }

namespace erhe::gltf {

class Image_transfer;

// glTF 2.1 top-level "files" array entry (unified file references,
// KhronosGroup/glTF#2590). URI entries are resolved against the glTF
// directory into resolved_path; entries carried inside the asset (data
// URIs / GLB-packed payloads) have an empty resolved_path and embedded set.
class Gltf_file_reference
{
public:
    std::string           name;
    std::string           mime_type;
    std::filesystem::path resolved_path;
    bool                  embedded{false};
};

// glTF 2.1 top-level "externalAssets" array entry (external assets,
// KhronosGroup/glTF#2586): a glTF asset referenced through the "files"
// array which nodes can instantiate. Neither fastgltf nor erhe::gltf
// parses the referenced asset or detects cross-file cycles; the caller
// (the editor's prefab layer) does.
class Gltf_external_asset
{
public:
    std::string name;
    std::size_t file_index{0};
};

class Gltf_data
{
public:
    std::vector<std::shared_ptr<erhe::scene::Animation>>    animations;
    std::vector<std::shared_ptr<erhe::scene::Camera>>       cameras;
    std::vector<std::shared_ptr<erhe::scene::Light>>        lights;
    std::vector<std::shared_ptr<erhe::scene::Mesh>>         meshes;
    std::vector<std::shared_ptr<erhe::scene::Skin>>         skins;
    std::vector<std::shared_ptr<erhe::scene::Node>>         nodes;
    std::vector<std::shared_ptr<erhe::primitive::Material>> materials;
    std::vector<std::shared_ptr<erhe::graphics::Texture>>   images;
    std::vector<std::shared_ptr<erhe::graphics::Sampler>>   samplers;
    std::vector<std::string>                                extensions;
    Gltf_physics_data                                       physics;

    // glTF 2.1 (see Gltf_file_reference / Gltf_external_asset above).
    // node_external_assets parallels nodes: entry i holds the index into
    // external_assets instantiated by node i, or nullopt. The carrier node
    // itself is parsed as an ordinary (empty) node with its transform;
    // instantiating the referenced asset under it is the caller's job.
    std::vector<Gltf_file_reference>        files;
    std::vector<Gltf_external_asset>        external_assets;
    std::vector<std::optional<std::size_t>> node_external_assets;
};

class Gltf_scan
{
public:
    std::vector<std::string> animations;
    std::vector<std::string> cameras;
    std::vector<std::string> directional_lights;
    std::vector<std::string> point_lights;
    std::vector<std::string> spot_lights;
    std::vector<std::string> meshes;
    std::vector<std::string> skins;
    std::vector<std::string> nodes;
    std::vector<std::string> materials;
    std::vector<std::string> images;
    std::vector<std::string> samplers;
    std::vector<std::string> scenes;
    std::vector<std::string> files;
    std::vector<std::string> external_assets;
    std::vector<std::string> extensions_used;
    std::vector<std::string> extensions_required;
    std::vector<std::string> errors;
};

struct Gltf_parse_arguments
{
    erhe::graphics::Device&                   graphics_device;
    ::tf::Executor&                           executor;
    Image_transfer&                           image_transfer;
    const std::shared_ptr<erhe::scene::Node>& root_node;
    erhe::scene::Layer_id                     mesh_layer_id{};
    std::filesystem::path                     path;
};

[[nodiscard]] auto parse_gltf(const Gltf_parse_arguments& arguments) -> Gltf_data;

[[nodiscard]] auto scan_gltf(std::filesystem::path path) -> Gltf_scan;

// physics_data (optional): KHR_implicit_shapes + KHR_physics_rigid_bodies
// content built by the editor (see editor parsers/gltf_physics_export.hpp).
[[nodiscard]] auto export_gltf(
    const erhe::scene::Node& root_node,
    bool                     binary,
    const Gltf_physics_data* physics_data = nullptr
) -> std::string;

}
