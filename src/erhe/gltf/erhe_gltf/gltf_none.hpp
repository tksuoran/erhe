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

// See gltf_fastgltf.hpp for documentation; this header mirrors the API for
// the ERHE_GLTF_LIBRARY=none configuration.
class Gltf_file_reference
{
public:
    std::string           name;
    std::string           mime_type;
    std::filesystem::path resolved_path;
    bool                  embedded{false};
};

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

[[nodiscard]] auto export_gltf(
    const erhe::scene::Node& root_node,
    bool                     binary,
    const Gltf_physics_data* physics_data = nullptr
) -> std::string;

}
