#pragma once

#include "gltf_physics.hpp"

#include "erhe_math/aabb.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace erhe::geometry {
    class Geometry;
}
namespace erhe::graphics {
    class Command_buffer;
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
    std::string           uid;
    std::string           mime_type;
    std::filesystem::path resolved_path;
    bool                  embedded{false};
};

class Gltf_external_asset
{
public:
    std::string name;
    std::string uid;
    std::size_t file_index{0};
};

class Gltf_image_source
{
public:
    std::vector<std::byte> encoded_bytes;
    std::string            mime_type;
};

class Gltf_raw_extensions
{
public:
    std::vector<std::pair<std::string, std::string>> entries;
};

// Mirrors the fastgltf backend's residency split (see gltf_fastgltf.hpp):
// with no parser there is never anything to make resident, so every entry
// point is a no-op.
class Gltf_data;

class Gltf_image_residency
{
public:
    [[nodiscard]] auto get_pending_image_count() const -> std::size_t { return 0; }
    [[nodiscard]] auto get_pending_byte_count () const -> std::size_t { return 0; }

    auto process_next_image(Gltf_data&, erhe::graphics::Device&, Image_transfer&) -> bool { return false; }
    void create_samplers(Gltf_data&, erhe::graphics::Device&) const {}
    auto process_next_image_into_frame(Gltf_data&, erhe::graphics::Device&, Image_transfer&, erhe::graphics::Command_buffer&, std::size_t&) -> bool { return false; }
    void bind_material_textures(Gltf_data&) const {}
    void drain(Gltf_data&, erhe::graphics::Device&, Image_transfer&) {}
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
    std::vector<std::shared_ptr<Gltf_image_source>>         image_sources;
    Gltf_image_residency                                    image_residency;
    std::vector<std::shared_ptr<erhe::graphics::Sampler>>   samplers;
    std::vector<std::string>                                extensions;
    Gltf_physics_data                                       physics;

    std::vector<Gltf_file_reference>        files;
    std::vector<Gltf_external_asset>        external_assets;
    std::vector<std::optional<std::size_t>> node_external_assets;

    Gltf_raw_extensions                           asset_extensions;
    Gltf_raw_extensions                           scene_extensions;
    std::vector<Gltf_raw_extensions>              node_extensions;
    std::vector<Gltf_raw_extensions>              camera_extensions;
    std::vector<Gltf_raw_extensions>              material_extensions;
    std::vector<Gltf_raw_extensions>              mesh_extensions;
    std::vector<std::vector<Gltf_raw_extensions>> mesh_primitive_extensions;
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

    std::vector<std::string> animation_uids;
    std::vector<std::string> camera_uids;
    std::vector<std::string> mesh_uids;
    std::vector<std::string> skin_uids;
    std::vector<std::string> node_uids;
    std::vector<std::string> material_uids;
    std::vector<std::string> image_uids;
    std::vector<std::string> sampler_uids;
    std::vector<std::string> scene_uids;
    std::vector<std::string> file_uids;
    std::vector<std::string> external_asset_uids;

    std::optional<erhe::math::Aabb> bounding_box;
};

class Gltf_device_options
{
public:
    unsigned int transcode_format_preference{0};
    float        max_sampler_anisotropy{1.0f};
};

[[nodiscard]] auto query_gltf_device_options(erhe::graphics::Device& graphics_device) -> Gltf_device_options;

struct Gltf_parse_arguments
{
    ::tf::Executor&                           executor;
    Gltf_device_options                       device_options{};
    const std::shared_ptr<erhe::scene::Node>& root_node;
    erhe::scene::Layer_id                     mesh_layer_id{};
    std::filesystem::path                     path;
    bool                                      parallel{true};
    bool                                      fix_spot_lights{false};
    std::span<const std::byte>                glb_data{};
};

[[nodiscard]] auto parse_gltf(const Gltf_parse_arguments& arguments) -> Gltf_data;

[[nodiscard]] auto scan_gltf(std::filesystem::path path) -> Gltf_scan;

[[nodiscard]] auto sniff_image_mime_type(const std::vector<std::byte>& bytes) -> std::string;

class Gltf_export_external_asset
{
public:
    std::string uri;
    std::string mime_type;
    std::string name;
};

class Gltf_export_asset_reference
{
public:
    std::string uri;
    std::string mime_type;
    std::string uid;
};

class Gltf_export_extension_payloads
{
public:
    std::string                                                             asset;
    std::string                                                             scene;
    std::map<const erhe::scene::Node*, std::string>                         nodes;
    std::map<const erhe::scene::Camera*, std::string>                       cameras;
    std::map<const erhe::primitive::Material*, std::string>                 materials;
    std::map<const erhe::scene::Mesh*, std::string>                         meshes;
    std::map<std::pair<const erhe::scene::Mesh*, std::size_t>, std::string> mesh_primitives;
};

class Gltf_export_extra_mesh
{
public:
    std::string                                name;
    std::shared_ptr<erhe::geometry::Geometry>  geometry;
    std::shared_ptr<erhe::primitive::Material> material;
};

class Gltf_export_index_lookup
{
public:
    std::unordered_map<const erhe::scene::Node*, std::size_t>         node_indices;
    std::unordered_map<const erhe::primitive::Material*, std::size_t> material_indices;
    std::unordered_map<const erhe::scene::Mesh*, std::size_t>         mesh_indices;
    std::vector<std::optional<std::size_t>>                           extra_mesh_indices;
};

class Gltf_export_arguments
{
public:
    const erhe::scene::Node& root_node;
    bool                     binary{true};
    const Gltf_physics_data* physics_data{nullptr};
    std::map<const erhe::scene::Node*, Gltf_export_external_asset> external_assets;
    std::map<const erhe::primitive::Material*, Gltf_export_asset_reference> material_asset_references;
    std::function<std::shared_ptr<const Gltf_image_source>(const erhe::graphics::Texture*)> image_source_provider;
    std::vector<std::shared_ptr<erhe::scene::Animation>> animations;
    Gltf_export_extension_payloads extension_payloads;
    std::vector<std::string> extensions_used;
    std::unordered_set<const erhe::scene::Mesh*> excluded_meshes;
    std::vector<Gltf_export_extra_mesh> extra_meshes;
    std::vector<std::shared_ptr<erhe::primitive::Material>> extra_materials;
    std::function<std::vector<std::pair<std::string, std::string>>(const Gltf_export_index_lookup&)> asset_extensions_builder;
};

[[nodiscard]] auto export_gltf(const Gltf_export_arguments& arguments) -> std::string;

[[nodiscard]] auto export_gltf(
    const erhe::scene::Node& root_node,
    bool                     binary,
    const Gltf_physics_data* physics_data = nullptr
) -> std::string;

}
