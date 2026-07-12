#pragma once

#include "gltf_physics.hpp"

#include "erhe_math/aabb.hpp"

#include <cstddef>
#include <functional>
#include <map>
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

// Compressed source image stream retained at import so a later export can
// re-embed the image verbatim, byte-exact (doc/gltf-scene-roundtrip-plan.md
// phase 0). GPU-only textures (e.g. editor graph-texture bakes) have no
// Gltf_image_source and are never exported as images.
class Gltf_image_source
{
public:
    std::vector<std::byte> encoded_bytes; // original PNG / JPEG / ... stream
    std::string            mime_type;     // "image/png", "image/jpeg", ...
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
    // Parallel to images: the retained encoded source stream of each loaded
    // image (null for images that failed to load or were never referenced).
    std::vector<std::shared_ptr<Gltf_image_source>>         image_sources;
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

    // Combined default-scene AABB computed from POSITION accessor min/max
    // (required by the glTF spec) transformed through the node hierarchy;
    // no buffer data is read. Bind-pose bounds: skinning, morph targets and
    // GPU instancing are not applied, and quantized (KHR_mesh_quantization)
    // positions are read as stored. nullopt when the file declares no
    // usable position bounds.
    std::optional<erhe::math::Aabb> bounding_box;
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

// Sniff the media type of an encoded image stream from its magic bytes
// ("image/png", "image/jpeg", ...); empty when unrecognized. Used for
// Gltf_image_source retention and export-side fallbacks.
[[nodiscard]] auto sniff_image_mime_type(const std::vector<std::byte>& bytes) -> std::string;

// A glTF 2.1 external-asset reference to write on export: nodes mapped to
// one of these are written with "externalAsset" (children and attachments
// are not exported - the instantiated content comes from the referenced
// file), creating deduplicated "files" / "externalAssets" entries.
class Gltf_export_external_asset
{
public:
    std::string uri;       // written into the files array as-is
    std::string mime_type; // "model/gltf+json" or "model/gltf-binary"
    std::string name;      // externalAssets entry name
};

class Gltf_export_arguments
{
public:
    const erhe::scene::Node& root_node;
    bool                     binary{true};
    // Optional KHR_implicit_shapes + KHR_physics_rigid_bodies content built
    // by the editor (see editor parsers/gltf_physics_export.hpp).
    const Gltf_physics_data* physics_data{nullptr};
    // Nodes to export as glTF 2.1 externalAsset instances. When any entry
    // is emitted, the asset is written with version + minVersion "2.1";
    // otherwise the exporter keeps writing plain glTF 2.0.
    std::map<const erhe::scene::Node*, Gltf_export_external_asset> external_assets;
    // Returns the retained encoded source stream for a texture (see
    // Gltf_image_source), or null when the texture has no exportable
    // source - such texture slots are skipped on export. When the provider
    // itself is empty, no images / textures / samplers are exported at all
    // (pre-phase-0 behavior).
    std::function<std::shared_ptr<const Gltf_image_source>(const erhe::graphics::Texture*)> image_source_provider;
    // Animations to export (the editor passes the content-library
    // animations). Channels targeting nodes outside the exported subtree
    // are skipped with a warning.
    std::vector<std::shared_ptr<erhe::scene::Animation>> animations;
};

[[nodiscard]] auto export_gltf(const Gltf_export_arguments& arguments) -> std::string;

// Convenience wrapper without glTF 2.1 external assets.
[[nodiscard]] auto export_gltf(
    const erhe::scene::Node& root_node,
    bool                     binary,
    const Gltf_physics_data* physics_data = nullptr
) -> std::string;

}
