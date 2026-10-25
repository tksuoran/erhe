#pragma once

#include "gltf_item_flags.hpp"
#include "erhe_scene/physics_description.hpp"

#include "erhe_graphics/image_loader.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_scene/instance_override.hpp"

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

namespace erhe {
    class Typed;
}
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
    class Xformable; using Node = Xformable;
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

// Where an image's compressed source stream is, so residency can decode it
// and a later export can re-embed it verbatim, byte-exact
// (doc/editor/gltf_scene_roundtrip.md phase 0). Either the bytes themselves
// (a data URI, a parse from memory, a standalone image file a caller read)
// or a byte range of a file, read just in time by read() - parse_gltf keeps
// no image file bytes in memory (doc/erhe/gltf.md "Image source streams").
// GPU-only textures (e.g. editor graph-texture bakes) have no
// Gltf_image_source and are never exported as images.
class Gltf_image_source
{
public:
    std::vector<std::byte> encoded_bytes; // the stream, when resident; empty when file-backed
    std::string            mime_type;     // "image/png", "image/jpeg", ...

    // File-backed stream: byte_length bytes at file_offset of file_path,
    // which had file_size and file_write_time when the range was recorded.
    std::filesystem::path           file_path;
    std::size_t                     file_offset    {0};
    std::size_t                     byte_length    {0};
    std::uintmax_t                  file_size      {0};
    std::filesystem::file_time_type file_write_time{};

    [[nodiscard]] auto is_file_backed() const -> bool { return !file_path.empty(); }
    // The stream's size, resident or not.
    [[nodiscard]] auto get_byte_count() const -> std::size_t { return is_file_backed() ? byte_length : encoded_bytes.size(); }
    // The stream: the resident bytes, or the file range read now. False,
    // with an error logged, when the file cannot be read or its size or
    // modification time differs from the recorded ones (it may hold other
    // bytes at the range). Thread-safe; opens and closes the file per call.
    [[nodiscard]] auto read(std::vector<std::byte>& out) const -> bool;
};

// Generic ERHE_* vendor-extension passthrough
// (doc/editor/gltf_scene_roundtrip.md phases 1 + 3): raw JSON payloads of
// extensions erhe::gltf has no typed support for, captured per object at
// parse time. Only `ERHE_`-prefixed extension names are captured; typed
// extensions (KHR_*, EXT_*) keep their typed paths.
class Gltf_raw_extensions
{
public:
    // (extension name, minified extension JSON value) pairs.
    std::vector<std::pair<std::string, std::string>> entries;
};

// One glTF image on its way to a Texture. parse_gltf records the encoded
// stream (`source`, the same bytes retained for export) and decodes NOTHING;
// Gltf_image_residency decodes / transcodes the pixels on executor workers
// ahead of the upload, bounded by a decoded-bytes budget, and creating the
// GPU texture from them is the residency step run by whoever owns a command
// buffer (doc/editor/async_asset_loading.md "Image residency").
class Gltf_decoded_image
{
public:
    bool                               requested{false}; // an image no material slot references is never decoded
    bool                               linear   {false}; // color space of the first referencing slot
    bool                               decoded  {false}; // decode finished: info, pixels and ok are valid
    bool                               resident {false}; // texture created and upload recorded
    bool                               ok       {false}; // decode succeeded (valid once decoded)
    erhe::graphics::Image_info         info{};
    std::vector<std::uint8_t>          pixels;           // held from decode until resident
    std::shared_ptr<Gltf_image_source> source;           // encoded stream, decoded from; null when unreadable
    std::string                        name;
    std::string                        uid;              // glTF 2.1 uid, carried onto the Texture
    std::filesystem::path              source_path;
};

class Gltf_image_decode_queue; // gltf_fastgltf.cpp: worker decode state shared with the tasks
} // namespace erhe::gltf
namespace erhe::utility {
    class Memory_budget;
}
namespace erhe::gltf {

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

// Images by the path that produced their pixels (erhe::graphics::Image_decode_path),
// for the per-load summary.
class Gltf_image_decode_path_counts
{
public:
    std::size_t decoded           {0}; // PNG / JPEG / ... decode or DDS blocks
    std::size_t basis_transcoded  {0}; // Basis Universal KTX2 transcoded
    std::size_t raw_blocks        {0}; // raw block KTX2 uploaded as stored
    std::size_t raw_blocks_decoded{0}; // raw ASTC KTX2 decoded to RGBA8 on the CPU
    std::size_t failed            {0};
};

class Gltf_data;

// The decode and GPU half of image loading, split out of the parse.
// parse_gltf leaves Gltf_data::images empty and every image's encoded stream
// here; nothing in Gltf_data holds a Texture until residency has run, so a
// caller that needs textures (everything today) must drain this before using
// the result.
//
// Decoding runs on executor workers, started by schedule_decodes() (and by
// the blocking entry points), and is bounded by a memory budget: each image's
// decoded pixels hold a lease of it from the start of the decode until the
// upload, and a decode starts only while its lease fits - except a single
// image larger than the budget, which decodes while nothing else holds it.
// So the peak is the budget, not the sum of every image
// (doc/editor/async_asset_loading.md "Image residency"). The budget is the
// one set_memory_budget() gives, shared with other users (the editor's load
// memory budget); without one, the residency uses a budget of its own sized
// by max_decoded_bytes_in_flight.
class Gltf_image_residency
{
public:
    static constexpr std::size_t default_max_decoded_bytes_in_flight = 128 * 1024 * 1024;

    std::vector<Gltf_decoded_image>            decoded_images;            // parallel to Gltf_data::images
    // Sampler descriptions built by the parse; bind_material_textures
    // writes them into the material slots as Material_sampler_state. One
    // entry per glTF sampler plus a trailing default.
    std::vector<erhe::graphics::Sampler_create_info> sampler_create_infos;
    std::vector<Gltf_material_texture_binding> material_texture_bindings;
    // Decode settings, filled by parse_gltf from its arguments. Without an
    // executor, or with parallel_decode false, images decode inline on the
    // residency thread, one per process_next_image* call.
    erhe::graphics::Transcode_format_preference transcode_format_preference{erhe::graphics::Transcode_format_preference::rgba8};
    erhe::graphics::Astc_support               astc_support{erhe::graphics::Astc_support::decode_to_rgba8};
    ::tf::Executor*                            decode_executor{nullptr};
    bool                                       parallel_decode{true};

    // Share `budget` (the editor's load memory budget) for the decoded
    // pixels. Set before the first decode; without it the residency uses a
    // budget of its own of max_decoded_bytes_in_flight bytes.
    void set_memory_budget(std::shared_ptr<erhe::utility::Memory_budget> budget);

    [[nodiscard]] auto get_pending_image_count() const -> std::size_t;
    // Decoded pixel bytes held right now: decoded and waiting for upload,
    // plus reserved by decodes in progress.
    [[nodiscard]] auto get_pending_byte_count () const -> std::size_t;
    // Largest get_pending_byte_count() seen.
    [[nodiscard]] auto get_pending_byte_high_water() const -> std::size_t;
    // Decoded images so far, by the path that produced their pixels.
    [[nodiscard]] auto get_decode_path_counts() const -> const Gltf_image_decode_path_counts& { return m_decode_path_counts; }

    // Start decode tasks for pending images, in image order, while their
    // decoded bytes fit the memory budget (at most one task per executor
    // worker). max_decoded_bytes_in_flight sizes the residency's own budget
    // when set_memory_budget() gave none. Call it from the residency thread
    // whenever residency runs; a no-op without parallel decode.
    void schedule_decodes(std::size_t max_decoded_bytes_in_flight = default_max_decoded_bytes_in_flight);
    // True when no decode task is running (a task outlives nothing it
    // references, but owners that track their workers wait for this).
    [[nodiscard]] auto is_decode_idle() const -> bool;

    // Create the Texture for the next pending image and record its upload
    // through a blocking_drain Image_transfer, decoding first (waiting for
    // the decode workers) when needed. Returns false when nothing is
    // pending. Must run on a thread that owns the image_transfer and may
    // touch the device. Does NOT flush: a caller driving this itself must
    // flush the Image_transfer before the frame ends (drain() does).
    auto process_next_image(
        Gltf_data&              data,
        erhe::graphics::Device& graphics_device,
        Image_transfer&         image_transfer,
        std::size_t             max_decoded_bytes_in_flight = default_max_decoded_bytes_in_flight
    ) -> bool;

    // Same, for a frame_recording Image_transfer, without ever waiting: the
    // next image whose decode has finished is uploaded, its copies go into
    // command_buffer and the staged bytes come out of
    // remaining_budget_bytes. Returns false when nothing is pending, when no
    // decode has finished yet, or when the budget or the staging ran out -
    // get_pending_image_count() tells "done" from "try again next frame".
    // Decodes are started by schedule_decodes(), not here.
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

    // Assign the parse-recorded image and sampler bindings into the material
    // texture slots. Requires every referenced image to be resident.
    void bind_material_textures(Gltf_data& data) const;

    // Blocking convenience: make every pending image resident and bind the
    // material slots - the behavior a synchronous caller had before the
    // split. Decodes in parallel under the same decoded-bytes budget.
    void drain(
        Gltf_data&              data,
        erhe::graphics::Device& graphics_device,
        Image_transfer&         image_transfer,
        std::size_t             max_decoded_bytes_in_flight = default_max_decoded_bytes_in_flight
    );

private:
    [[nodiscard]] auto get_decode_queue() -> const std::shared_ptr<Gltf_image_decode_queue>&;
    void collect_decoded();
    void decode_inline(std::size_t image_index);
    void release_decoded(std::size_t image_index);
    [[nodiscard]] auto find_next_decoded_image() const -> std::size_t;

    std::shared_ptr<Gltf_image_decode_queue> m_decode_queue;
    std::shared_ptr<erhe::utility::Memory_budget> m_memory_budget; // shared; null: the queue makes its own
    Gltf_image_decode_path_counts            m_decode_path_counts;
    // The image whose frame-recording upload found no budget or staging
    // room; it is retried first, so the staging cap's size mark (which makes
    // smaller requests wait behind it) is always honored by its owner.
    std::size_t                              m_retry_image_index{~std::size_t{0}};
};

// One KHR_materials_variants mapping, resolved to the objects the parse
// created (doc/erhe/usd_compatibility_design.md X4): the Mesh prim a node
// instantiated from the glTF mesh, which of that prim's primitives the
// mapping names, and the material the variant binds to it. The primitive
// index is the erhe one - a glTF primitive erhe cannot build is skipped, so
// it is not always the glTF primitive index.
class Gltf_material_variant_binding
{
public:
    std::shared_ptr<erhe::scene::Mesh>         mesh;
    std::size_t                                primitive_index{0};
    std::shared_ptr<erhe::primitive::Material> material;
};

// One entry of the asset's KHR_materials_variants "variants" array with the
// bindings its index selects. A primitive that maps no material for a
// variant keeps the material its own "material" names, so it contributes no
// binding to that variant.
class Gltf_material_variant
{
public:
    std::string                                name;
    std::vector<Gltf_material_variant_binding> bindings;
};

// What the parse did with the prepared mesh data of a file
// (ERHE_mesh_optimized / ERHE_mesh_topology / ERHE_raytrace_bvh, see
// doc/plans/gltf_fast_load.md "Loader"). Counted per unique soup primitive
// carrying ERHE_mesh_optimized (primitives sharing accessors and extension
// payloads count once).
class Gltf_prepared_mesh_statistics
{
public:
    std::size_t                        used_count         {0}; // every extension the primitive carries was accepted
    std::size_t                        rejected_count     {0}; // at least one extension was rejected
    std::size_t                        topology_used_count{0}; // ERHE_mesh_topology accepted
    std::size_t                        bvh_used_count     {0}; // ERHE_raytrace_bvh accepted
    std::size_t                        ignored_count      {0}; // glTF primitives carrying ERHE_mesh_optimized while prepared_mesh_data_use is ignore
    std::map<std::string, std::size_t> rejection_reasons;      // "<extension>: <reason>" -> count
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
    // Parallel to nodes: every parsed prim, whatever its class
    // (doc/erhe/usd_compatibility_design.md C5). A glTF node whose ERHE_node
    // extension names a `prim_class` is a prim of a class that carries no
    // transform - a `Scope` or a `Typed` - so `nodes` holds null at its
    // index and this vector holds the prim; for every other node both hold
    // the same object.
    std::vector<std::shared_ptr<erhe::Typed>>               prims;
    std::vector<std::shared_ptr<erhe::primitive::Material>> materials;
    // Parallel to materials (sized on first use): the ERHE_material "style"
    // name of each material, empty for none. The editor assigns the style
    // item of that name once its library holds it (doc/editor/style_library.md D4).
    std::vector<std::string>                                material_style_names;
    std::vector<std::shared_ptr<erhe::graphics::Texture>>   images;
    // Parallel to images: the retained encoded source stream of each loaded
    // image (null for images that failed to load or were never referenced).
    std::vector<std::shared_ptr<Gltf_image_source>>         image_sources;
    // Deferred GPU half of image loading (see Gltf_image_residency): after
    // parse_gltf every entry of `images` is null and the pixels live here.
    Gltf_image_residency                                    image_residency;
    std::vector<std::string>                                extensions;
    erhe::scene::Physics_description                         physics;
    // KHR_materials_variants (see Gltf_material_variant above), empty when
    // the asset declares no variants.
    std::vector<Gltf_material_variant>                      material_variants;
    // Object-reference local values of the ERHE_* "properties" maps whose
    // name did not resolve during the parse (see Unresolved_object_property);
    // the editor resolves them in its scene once its operations ran.
    std::vector<Unresolved_object_property>                 unresolved_object_properties;

    // glTF 2.1 (see Gltf_file_reference / Gltf_external_asset above).
    // node_external_assets parallels nodes: entry i holds the index into
    // external_assets instantiated by node i, or nullopt. The carrier node
    // itself is parsed as an ordinary (empty) node with its transform;
    // instantiating the referenced asset under it is the caller's job.
    std::vector<Gltf_file_reference>        files;
    std::vector<Gltf_external_asset>        external_assets;
    std::vector<std::optional<std::size_t>> node_external_assets;
    // The sparse overrides a carrier node's ERHE_node extension holds, by
    // node index (doc/erhe/usd_compatibility_design.md X2). The parse records them;
    // the caller applies them when it attaches the external asset's content.
    std::map<std::size_t, std::vector<erhe::scene::Instance_override>> node_instance_overrides;

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

    // Prepared mesh data outcome of the parse (see Gltf_prepared_mesh_statistics).
    Gltf_prepared_mesh_statistics                 prepared_mesh_statistics;
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
// (doc/editor/async_asset_loading_design.md 2.3 invariant 1). See
// query_gltf_device_options().
class Gltf_device_options
{
public:
    erhe::graphics::Transcode_format_preference transcode_format_preference{
        erhe::graphics::Transcode_format_preference::rgba8
    };
    // native when the device samples ASTC 4x4 (UNORM and SRGB): raw ASTC
    // KTX2 images then upload as stored, else they are decoded to RGBA8.
    erhe::graphics::Astc_support astc_support{erhe::graphics::Astc_support::decode_to_rgba8};
    float max_sampler_anisotropy{1.0f};
};

[[nodiscard]] auto query_gltf_device_options(erhe::graphics::Device& graphics_device) -> Gltf_device_options;

// Whether parse_gltf reads the prepared mesh data extensions
// (ERHE_mesh_optimized, ERHE_mesh_topology, ERHE_raytrace_bvh) of soup
// primitives. ignore loads them as plain soups, as a file without the
// extensions (the load.use_prepared_mesh_data editor setting).
enum class Prepared_mesh_data_use : unsigned int {
    ignore = 0,
    use    = 1
};

// Whether parse_gltf records, per soup primitive, where its accessors' bytes
// are in the source file (erhe::primitive::Triangle_soup_source), so the
// primitive may release its CPU soup after upload and read it back on demand
// (Primitive::release_triangle_soup(); the load.cpu_mesh_data editor
// setting). Only a parse from a file records; see doc/erhe/gltf.md
// "Triangle soup reload".
enum class Triangle_soup_reload : unsigned int {
    none   = 0,
    record = 1
};

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
    // Read and validate the prepared mesh data extensions and attach the
    // accepted payloads to the soup primitives' render shapes
    // (erhe::primitive::Primitive_shape::get_prepared_data()).
    Prepared_mesh_data_use                    prepared_mesh_data_use{Prepared_mesh_data_use::use};
    Triangle_soup_reload                      triangle_soup_reload{Triangle_soup_reload::none};
};

[[nodiscard]] auto parse_gltf(const Gltf_parse_arguments& arguments) -> Gltf_data;

[[nodiscard]] auto scan_gltf(std::filesystem::path path) -> Gltf_scan;

// Sniff the media type of an encoded image stream from its magic bytes
// ("image/png", "image/jpeg", ...); empty when unrecognized. Used for
// Gltf_image_source retention and export-side fallbacks.
[[nodiscard]] auto sniff_image_mime_type(const std::vector<std::byte>& bytes) -> std::string;

// A glTF 2.1 external-asset reference to write on export: nodes mapped to
// one of these are written with "externalAsset" (children are not exported -
// the instantiated content comes from the referenced file), creating deduplicated "files" / "externalAssets" entries.
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
// (doc/editor/gltf_scene_roundtrip.md phase 3). Each string holds one or more
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

// One material binding to write into an exported primitive's
// KHR_materials_variants "mappings" (doc/erhe/usd_compatibility_design.md X4). A
// binding whose mesh or primitive is not in the export is skipped with a
// warning; the primitive's own "material" stays what it is either way.
class Gltf_export_material_variant_binding
{
public:
    const erhe::scene::Mesh*                   mesh{nullptr};
    std::size_t                                primitive_index{0};
    std::shared_ptr<erhe::primitive::Material> material;
};

// One entry of the exported asset's KHR_materials_variants "variants" array.
class Gltf_export_material_variant
{
public:
    std::string                                       name;
    std::vector<Gltf_export_material_variant_binding> bindings;
};

// An extra glTF mesh to export that no node references, carrying one
// geometry-normative primitive (the ERHE_geometry accessor/dump path).
// Used by the editor for brush geometry (doc/editor/gltf_scene_roundtrip.md
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

// How the exporter writes texture images.
// - source: the retained encoded stream as loaded (PNG / JPEG / Basis KTX2
//   via KHR_texture_basisu / raw block KTX2 via ERHE_texture_ktx2).
// - ktx2_astc_4x4: "Save for Quest" (doc/gltf_extensions/ERHE_texture_ktx2.md):
//   every Basis Universal KTX2 image is transcoded to a raw ASTC 4x4 KTX2
//   (all mip levels, SRGB or UNORM from the texture's format, each level
//   Zstandard supercompressed) on the executor
//   and referenced through ERHE_texture_ktx2; PNG / JPEG images are written
//   as they are.
enum class Gltf_texture_export : unsigned int {
    source        = 0,
    ktx2_astc_4x4 = 1
};

// What the texture pass of one export did.
class Gltf_export_texture_statistics
{
public:
    std::size_t image_count          {0}; // images written
    std::size_t transcoded_count     {0}; // Basis KTX2 transcoded to raw ASTC 4x4
    std::size_t transcode_failed_count{0}; // transcode failed: written as the Basis source
    std::size_t raw_ktx2_count       {0}; // raw block KTX2 written (transcoded or retained)
    std::size_t source_byte_count    {0}; // encoded bytes of the transcoded sources
    std::size_t transcoded_byte_count{0}; // bytes of the KTX2 files they became
    double      transcode_ms         {0.0}; // wall time of the parallel transcode
};

// Whether the exporter writes triangle-soup primitives as prepared cores with
// the ERHE_mesh_optimized, ERHE_mesh_topology and ERHE_raytrace_bvh
// extensions (doc/plans/gltf_fast_load.md "Exporter") instead of the raw soup.
enum class Prepared_mesh_data : unsigned int {
    none  = 0,
    write = 1
};

// What the prepared-mesh-data pass of one export did (filled when
// Gltf_export_arguments::prepared_mesh_data is write).
class Gltf_export_prepared_statistics
{
public:
    std::size_t                        prepared_count             {0}; // unique soup primitives written as prepared cores
    std::size_t                        skipped_count              {0}; // unique soup primitives written as raw soups
    std::map<std::string, std::size_t> skip_reasons;                   // reason -> count
    std::size_t                        bvh_reused_count           {0}; // committed raytrace BVH remapped onto the core
    std::size_t                        bvh_rebuilt_count          {0}; // BVH built over the core
    std::size_t                        bvh_stored_count           {0}; // retained primitive: the BVH its raytrace loaded with
    std::size_t                        bvh_none_count             {0}; // no ERHE_raytrace_bvh written
    std::size_t                        retained_count             {0}; // prepared primitives written back unchanged (subset of prepared_count)
    std::size_t                        dropped_degenerate_triangles{0};
    double                             preparation_ms             {0.0}; // wall time of the parallel preparation
};

class Gltf_export_arguments
{
public:
    const erhe::scene::Node& root_node;
    bool                     binary{true};
    // Optional KHR_implicit_shapes + KHR_physics_rigid_bodies content built
    // by the editor (see editor parsers/gltf_physics_export.hpp).
    const erhe::scene::Physics_description* physics_data{nullptr};
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
    // Mesh prims to skip in the node pass (the node exports without
    // its mesh): baked artifacts that are rebuilt on load, e.g. graph-mesh
    // controlled meshes (doc/editor/gltf_scene_roundtrip.md phase 3
    // exclusion hook).
    std::unordered_set<const erhe::scene::Mesh*> excluded_meshes{};
    // Items the application keeps out of the file: a prim whose flag bits
    // meet excluded_item_flag_bits (the editor's rendertarget meshes, UI
    // quads rendered into every frame with no serializable source image)
    // exports without its mesh, and a prim whose type bits meet
    // excluded_item_type_bits (the editor's joint prims, written from the
    // physics description instead) is skipped in the node pass.
    uint64_t excluded_item_flag_bits{0};
    uint64_t excluded_item_type_bits{0};
    // Extra unreferenced meshes to export (see Gltf_export_extra_mesh).
    std::vector<Gltf_export_extra_mesh> extra_meshes{};
    // KHR_materials_variants to write (see Gltf_export_material_variant):
    // the asset's variant names and, per variant, the material each mapped
    // primitive binds. Emitting any variant declares the extension.
    std::vector<Gltf_export_material_variant> material_variants{};
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
    // Called in the same place as asset_extensions_builder, for per-node
    // payloads that name a glTF index (an extension field naming another
    // node): returns (node, extension members) pairs, in the "member" form
    // extension_payloads uses, merged into that node's payload. A node
    // outside the export is skipped with a warning. The caller lists the
    // extension names in extensions_used, as it does for extension_payloads.
    std::function<std::vector<std::pair<const erhe::scene::Node*, std::string>>(const Gltf_export_index_lookup&)> node_extensions_builder{};
    // Prepared mesh data for triangle-soup primitives. none (the default)
    // writes the raw soups; save_scene passes write. Geometry-normative
    // primitives (ERHE_geometry) are unaffected.
    Prepared_mesh_data prepared_mesh_data{Prepared_mesh_data::none};
    // Executor the per-primitive preparation runs on (one task per unique
    // soup); serial on the calling thread when null.
    ::tf::Executor* executor{nullptr};
    // Receives the prepared-mesh-data statistics when non-null.
    Gltf_export_prepared_statistics* prepared_statistics{nullptr};
    // Texture image form (Gltf_texture_export); source unless "Save for
    // Quest". The transcodes run on 'executor' (serial when null).
    Gltf_texture_export texture_export{Gltf_texture_export::source};
    // Receives the texture statistics when non-null.
    Gltf_export_texture_statistics* texture_statistics{nullptr};
};

// The file's bytes, or an empty string when the export failed: a texture
// source the file must carry could not be read (Gltf_image_source::read(),
// its file changed since the load). Callers write nothing then.
[[nodiscard]] auto export_gltf(const Gltf_export_arguments& arguments) -> std::string;

// Convenience wrapper without glTF 2.1 external assets.
[[nodiscard]] auto export_gltf(
    const erhe::scene::Node&                root_node,
    bool                                    binary,
    const erhe::scene::Physics_description* physics_data = nullptr
) -> std::string;

}
