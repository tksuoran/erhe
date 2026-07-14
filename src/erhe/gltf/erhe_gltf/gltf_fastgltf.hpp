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
#include <unordered_map>
#include <unordered_set>
#include <utility>
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

// Generic ERHE_* vendor-extension passthrough
// (doc/gltf-scene-roundtrip-plan.md phases 1 + 3): raw JSON payloads of
// extensions erhe::gltf has no typed support for, captured per object at
// parse time. Only `ERHE_`-prefixed extension names are captured; typed
// extensions (KHR_*, EXT_*) keep their typed paths.
class Gltf_raw_extensions
{
public:
    // (extension name, minified extension JSON value) pairs.
    std::vector<std::pair<std::string, std::string>> entries;
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

    // Captured ERHE_* extension payloads (see Gltf_raw_extensions). The
    // per-object vectors parallel the object vectors above;
    // mesh_primitive_extensions is indexed [mesh][primitive].
    Gltf_raw_extensions                           asset_extensions;
    Gltf_raw_extensions                           scene_extensions; // first scene (only one is parsed)
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

// Raw JSON members to splice into exported objects' "extensions" objects
// (doc/gltf-scene-roundtrip-plan.md phase 3). Each string holds one or more
// comma-separated members, e.g. R"("ERHE_node":{"flags":["hidden"]})" - no
// surrounding braces. Keyed by the erhe objects the exporter maps to glTF
// indices; payloads for objects that do not end up in the export are
// skipped with a warning.
class Gltf_export_extension_payloads
{
public:
    std::string                                                             asset;
    std::string                                                             scene;
    std::map<const erhe::scene::Node*, std::string>                         nodes;
    std::map<const erhe::scene::Camera*, std::string>                       cameras;
    std::map<const erhe::primitive::Material*, std::string>                 materials;
    std::map<const erhe::scene::Mesh*, std::string>                         meshes;
    std::map<std::pair<const erhe::scene::Mesh*, std::size_t>, std::string> mesh_primitives; // (mesh, primitive index)
};

// An extra glTF mesh to export that no node references, carrying one
// geometry-normative primitive (the ERHE_geometry accessor/dump path).
// Used by the editor for brush geometry (doc/gltf-scene-roundtrip-plan.md
// phase 3, ERHE_brushes).
class Gltf_export_extra_mesh
{
public:
    std::string                                name;
    std::shared_ptr<erhe::geometry::Geometry>  geometry;
    std::shared_ptr<erhe::primitive::Material> material; // optional primitive material
};

// glTF indices assigned during export, handed to asset_extensions_builder
// so asset-root extension payloads (ERHE_brushes, ERHE_node_graphs,
// ERHE_collections) can reference exported objects by index - the indices
// only exist once the export passes have run.
class Gltf_export_index_lookup
{
public:
    std::unordered_map<const erhe::scene::Node*, std::size_t>         node_indices;
    std::unordered_map<const erhe::primitive::Material*, std::size_t> material_indices;
    std::unordered_map<const erhe::scene::Mesh*, std::size_t>         mesh_indices;
    // Parallels Gltf_export_arguments::extra_meshes; nullopt for entries
    // that could not be exported (missing geometry).
    std::vector<std::optional<std::size_t>>                           extra_mesh_indices;
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
    std::map<const erhe::scene::Node*, Gltf_export_external_asset> external_assets{};
    // Returns the retained encoded source stream for a texture (see
    // Gltf_image_source), or null when the texture has no exportable
    // source - such texture slots are skipped on export. When the provider
    // itself is empty, no images / textures / samplers are exported at all
    // (pre-phase-0 behavior).
    std::function<std::shared_ptr<const Gltf_image_source>(const erhe::graphics::Texture*)> image_source_provider{};
    // Animations to export (the editor passes the content-library
    // animations). Channels targeting nodes outside the exported subtree
    // are skipped with a warning.
    std::vector<std::shared_ptr<erhe::scene::Animation>> animations{};
    // ERHE_* extension payloads to attach to exported objects; the caller
    // must also list each extension name in extensions_used.
    Gltf_export_extension_payloads extension_payloads{};
    // Extension names to declare in the asset's extensionsUsed (for the
    // extension_payloads above).
    std::vector<std::string> extensions_used{};
    // Mesh attachments to skip in the node pass (the node exports without
    // its mesh): baked artifacts that are rebuilt on load, e.g. graph-mesh
    // controlled meshes (doc/gltf-scene-roundtrip-plan.md phase 3
    // exclusion hook).
    std::unordered_set<const erhe::scene::Mesh*> excluded_meshes{};
    // Extra unreferenced meshes to export (see Gltf_export_extra_mesh).
    std::vector<Gltf_export_extra_mesh> extra_meshes{};
    // Called after all objects are emitted (glTF indices known); returns
    // (extension name, extension JSON value) pairs to attach to the asset
    // root, e.g. ("ERHE_brushes", "{\"brushes\":[...]}"). Each returned
    // name is declared in extensionsUsed automatically.
    std::function<std::vector<std::pair<std::string, std::string>>(const Gltf_export_index_lookup&)> asset_extensions_builder{};
};

[[nodiscard]] auto export_gltf(const Gltf_export_arguments& arguments) -> std::string;

// Convenience wrapper without glTF 2.1 external assets.
[[nodiscard]] auto export_gltf(
    const erhe::scene::Node& root_node,
    bool                     binary,
    const Gltf_physics_data* physics_data = nullptr
) -> std::string;

}
