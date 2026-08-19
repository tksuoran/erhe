#pragma once

#include "gltf_physics.hpp"

#include "erhe_graphics/image_loader.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_math/aabb.hpp"

#include <cstddef>
#include <cstdint>
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

// glTF 2.1 top-level "files" array entry (unified file references,
// KhronosGroup/glTF#2590). URI entries are resolved against the glTF
// directory into resolved_path; entries carried inside the asset (data
// URIs / GLB-packed payloads) have an empty resolved_path and embedded set.
class Gltf_file_reference
{
public:
    std::string           name;
    std::string           uid; // glTF 2.1 unique ID; empty when the file does not declare one
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
    std::string uid; // glTF 2.1 unique ID; empty when the file does not declare one
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

// Decoded pixels of one glTF image plus everything needed to create and
// upload its Texture. parse_gltf produces these on the CPU only - decoding
// touches no device, so it is safe on executor workers - and creating the
// GPU texture from one is a separate residency step run by whoever owns a
// command buffer (doc/async-asset-loading-plan.md step 3, phase 3b).
class Gltf_decoded_image
{
public:
    bool                               requested{false}; // an image no material slot references is never decoded
    bool                               resident {false}; // texture created and upload recorded
    bool                               ok       {false}; // decode succeeded
    erhe::graphics::Image_info         info{};
    std::vector<std::uint8_t>          pixels;           // released once resident
    std::shared_ptr<Gltf_image_source> source;
    std::string                        name;
    std::string                        uid;              // glTF 2.1 uid, carried onto the Texture
    std::filesystem::path              source_path;
};

// The five texture slots of erhe::primitive::Material_texture_samplers.
// Material texture slots cannot be filled during the parse any more (the
// Texture does not exist yet), so the parse records which image belongs in
// which slot and residency assigns them.
enum class Gltf_material_texture_slot : unsigned int {
    base_color         = 0,
    metallic_roughness = 1,
    normal             = 2,
    occlusion          = 3,
    emissive           = 4
};

class Gltf_material_texture_binding
{
public:
    // image_index value for a texture slot that names a sampler but no
    // usable image (a glTF texture whose source cannot be resolved).
    static constexpr std::size_t no_image = ~std::size_t{0};

    std::size_t                material_index{0};
    Gltf_material_texture_slot slot{Gltf_material_texture_slot::base_color};
    std::size_t                image_index{0};
    // Index into Gltf_image_residency::sampler_create_infos. The last entry
    // is the default sampler, used by textures that name none.
    std::size_t                sampler_index{0};
};

class Gltf_data;

// The GPU half of image loading, split out of the parse. parse_gltf leaves
// Gltf_data::images empty and every decoded payload here; nothing in
// Gltf_data holds a Texture until residency has run, so a caller that needs
// textures (everything today) must drain this before using the result.
class Gltf_image_residency
{
public:
    std::vector<Gltf_decoded_image>            decoded_images;            // parallel to Gltf_data::images
    // Sampler descriptions built by the parse; residency turns them into
    // erhe::graphics::Sampler objects in Gltf_data::samplers. Creating a
    // Sampler needs the device, so the parse cannot do it and stay
    // device-free. One entry per glTF sampler plus a trailing default.
    std::vector<erhe::graphics::Sampler_create_info> sampler_create_infos;
    std::vector<Gltf_material_texture_binding> material_texture_bindings;

    [[nodiscard]] auto get_pending_image_count() const -> std::size_t;
    [[nodiscard]] auto get_pending_byte_count () const -> std::size_t;

    // Create the Texture for the next pending image and record its upload
    // through a blocking_drain Image_transfer. Returns false when nothing is
    // pending. Must run on a thread that owns the image_transfer and may
    // touch the device. Does NOT flush: a caller driving this itself must
    // flush the Image_transfer before the frame ends (drain() does).
    auto process_next_image(
        Gltf_data&              data,
        erhe::graphics::Device& graphics_device,
        Image_transfer&         image_transfer
    ) -> bool;

    // Same, for a frame_recording Image_transfer: the copies go into
    // command_buffer and the staged bytes come out of
    // remaining_budget_bytes. Returns false when nothing is pending OR when
    // the budget ran out - get_pending_image_count() distinguishes the two.
    // The texture object is always created before its pixels are uploaded,
    // so callers see a real erhe::graphics::Texture in Gltf_data::images as
    // soon as an image is processed (plan 2.7 requires this by publish).
    auto process_next_image_into_frame(
        Gltf_data&                      data,
        erhe::graphics::Device&         graphics_device,
        Image_transfer&                 image_transfer,
        erhe::graphics::Command_buffer& command_buffer,
        std::size_t&                    remaining_budget_bytes
    ) -> bool;

    // Create Gltf_data::samplers from sampler_create_infos. Cheap, no
    // uploads; must run before bind_material_textures.
    void create_samplers(Gltf_data& data, erhe::graphics::Device& graphics_device) const;

    // Assign the parse-recorded image and sampler bindings into the material
    // texture slots. Requires every referenced image to be resident and
    // create_samplers to have run.
    void bind_material_textures(Gltf_data& data) const;

    // Blocking convenience: make every pending image resident and bind the
    // material slots - the behavior a synchronous caller had before the
    // split.
    void drain(
        Gltf_data&              data,
        erhe::graphics::Device& graphics_device,
        Image_transfer&         image_transfer
    );
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
    // Deferred GPU half of image loading (see Gltf_image_residency): after
    // parse_gltf every entry of `images` is null and the pixels live here.
    Gltf_image_residency                                    image_residency;
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

    // glTF 2.1 unique IDs (KhronosGroup/glTF#2597), parallel to the name
    // vectors above; empty string when the object declares no uid. Lights
    // are extension-hosted (KHR_lights_punctual) and cannot carry uids.
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

    // Combined default-scene AABB computed from POSITION accessor min/max
    // (required by the glTF spec) transformed through the node hierarchy;
    // no buffer data is read. Bind-pose bounds: skinning, morph targets and
    // GPU instancing are not applied, and quantized (KHR_mesh_quantization)
    // positions are read as stored. nullopt when the file declares no
    // usable position bounds.
    std::optional<erhe::math::Aabb> bounding_box;
};

// The two device-derived values the parse needs. Queried by the caller on
// the main thread and passed in by value, so parse_gltf itself never touches
// an erhe::graphics::Device - which is what makes it safe to run on a worker
// (doc/async-asset-loading-plan.md 2.3 invariant 1). See
// query_gltf_device_options().
class Gltf_device_options
{
public:
    erhe::graphics::Transcode_format_preference transcode_format_preference{
        erhe::graphics::Transcode_format_preference::rgba8
    };
    float max_sampler_anisotropy{1.0f};
};

[[nodiscard]] auto query_gltf_device_options(erhe::graphics::Device& graphics_device) -> Gltf_device_options;

struct Gltf_parse_arguments
{
    ::tf::Executor&                           executor;
    Gltf_device_options                       device_options{};
    const std::shared_ptr<erhe::scene::Node>& root_node;
    erhe::scene::Layer_id                     mesh_layer_id{};
    std::filesystem::path                     path;
    // Run image decode, mesh parsing and animation parsing as parallel
    // executor tasks (GPU uploads always stay on the calling thread). When
    // false every parse step runs inline, serially.
    bool                                      parallel{true};
    // Workaround for broken glTF exports of spot lights (--fix-spot-lights):
    // when true, every parsed spot light gets its color value pushed to
    // 1.0 (hue and saturation kept), its intensity forced to 1000, its outer cone
    // angle doubled (clamped to pi) and its inner cone angle set to the
    // original outer cone angle. Other light types are untouched.
    bool                                      fix_spot_lights{false};
    // When non-empty, parse this in-memory GLB instead of reading `path`
    // (`path` is then used only for logging and base-directory resolution).
    // All buffers and images must be embedded in the GLB, as with assets
    // delivered by OpenXR XR_FB_render_model / XR_EXT_render_model.
    std::span<const std::byte>                glb_data{};
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

// A cross-container asset reference to write on export
// (doc/gltf_extensions/ERHE_asset_reference.md, asset-manager plan phase
// R6): the mapped material exports as a name-only stub carrying an
// ERHE_asset_reference extension {file: <files array index>, uid}, with the
// defining container listed in the glTF 2.1 "files" array. The stub's own
// name doubles as the 2.1 conforming-name fallback identifier, so no
// name/type fields exist here. Proxy materials are NOT uid-stamped: their
// identity lives in the defining container, and stamping would mutate the
// shared item's uid.
class Gltf_export_asset_reference
{
public:
    std::string uri;       // written into the files array as-is
    std::string mime_type; // "model/gltf+json" or "model/gltf-binary"
    std::string uid;       // target uid within the container; empty = name fallback
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
    // Materials to export as ERHE_asset_reference proxies (stub + key)
    // instead of full data (see Gltf_export_asset_reference above): library
    // reference entries whose definition lives in another container.
    // Emitting any proxy declares ERHE_asset_reference and, via the files
    // array, glTF 2.1.
    std::map<const erhe::primitive::Material*, Gltf_export_asset_reference> material_asset_references{};
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
    // Materials to export even when no exported mesh references them (the
    // exporter is otherwise lazy: process_material only runs for referenced
    // materials). R7 make-external writes single-material asset container
    // files through this.
    std::vector<std::shared_ptr<erhe::primitive::Material>> extra_materials{};
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
