#pragma once

#include <memory>
#include <filesystem>
#include <vector>

namespace igl {
    class IDevice;
    class ITexture;
    class ISamplerState;
}

namespace erhe::geometry {
    class Geometry;
}
namespace erhe::graphics {
    class Vertex_format;
}
namespace erhe::primitive {
    class Buffer_sink;
    class Geometry_primitive;
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

namespace erhe::gltf {

class Image_transfer;

class Gltf_data
{
public:
    std::vector<std::shared_ptr<erhe::scene::Animation>>              animations;
    std::vector<std::shared_ptr<erhe::scene::Camera>>                 cameras;
    std::vector<std::shared_ptr<erhe::scene::Light>>                  lights;
    std::vector<std::shared_ptr<erhe::scene::Mesh>>                   meshes;
    std::vector<std::shared_ptr<erhe::scene::Skin>>                   skins;
    std::vector<std::shared_ptr<erhe::scene::Node>>                   nodes;
    std::vector<std::shared_ptr<erhe::geometry::Geometry>>            geometries;
    std::vector<std::shared_ptr<erhe::primitive::Geometry_primitive>> geometry_primitives;
    std::vector<std::shared_ptr<erhe::primitive::Material>>           materials;
    std::vector<std::shared_ptr<igl::ITexture>>                       images;
    std::vector<std::shared_ptr<igl::ISamplerState>>                  samplers;
};

class Gltf_scan
{
public:
    std::vector<std::string> animations;
    std::vector<std::string> cameras;
    std::vector<std::string> lights;
    std::vector<std::string> meshes;
    std::vector<std::string> skins;
    std::vector<std::string> nodes;
    std::vector<std::string> materials;
    std::vector<std::string> images;
    std::vector<std::string> samplers;
    std::vector<std::string> scenes;
};

enum class Coordinate_system : unsigned int {
    Y_up = 0,
    Z_up = 1
};

struct Gltf_parse_arguments
{
    igl::IDevice&                             device,
    Image_transfer&                           image_transfer;
    const std::shared_ptr<erhe::scene::Node>& root_node;
    erhe::scene::Layer_id                     mesh_layer_id;
    std::filesystem::path                     path;
    Coordinate_system                         coordinate_system{Coordinate_system::Y_up};
};

[[nodiscard]] auto parse_gltf(const Gltf_parse_arguments& arguments) -> Gltf_data;

[[nodiscard]] auto scan_gltf(std::filesystem::path path) -> Gltf_scan;

}
