#pragma once

#include "gltf_item_flags.hpp"
#include "erhe_scene/physics_description.hpp"

#include "erhe_math/aabb.hpp"
#include "erhe_scene/instance_override.hpp"

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
    class Xformable; using Node = Xformable;
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
    static constexpr std::size_t default_max_decoded_bytes_in_flight = 128 * 1024 * 1024;

    [[nodiscard]] auto get_pending_byte_count () const -> std::size_t { return 0; }
    [[nodiscard]] auto get_pending_byte_high_water() const -> std::size_t { return 0; }
    void schedule_decodes(std::size_t) {}
    [[nodiscard]] auto is_decode_idle() const -> bool { return true; }

    auto process_next_image(Gltf_data&, erhe::graphics::Device&, Image_transfer&, std::size_t = default_max_decoded_bytes_in_flight) -> bool { return false; }
    auto process_next_image_into_frame(Gltf_data&, erhe::graphics::Device&, Image_transfer&, erhe::graphics::Command_buffer&, std::size_t&) -> bool { return false; }
    void bind_material_textures(Gltf_data&) const {}
    void drain(Gltf_data&, erhe::graphics::Device&, Image_transfer&, std::size_t = default_max_decoded_bytes_in_flight) {}
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
    std::vector<std::shared_ptr<erhe::primitive::Material>> materials;
    std::vector<std::string>                                material_style_names;
    std::vector<std::shared_ptr<erhe::graphics::Texture>>   images;
    std::vector<std::shared_ptr<Gltf_image_source>>         image_sources;
    Gltf_image_residency                                    image_residency;
    std::vector<std::string>                                extensions;
    erhe::scene::Physics_description                         physics;
    // Object-reference local values of the ERHE_* "properties" maps whose
    // name did not resolve during the parse (see Unresolved_object_property);
    // the editor resolves them in its scene once its operations ran.
    std::vector<Unresolved_object_property>                 unresolved_object_properties;

    std::vector<Gltf_file_reference>        files;
    std::vector<Gltf_external_asset>        external_assets;
    std::vector<std::optional<std::size_t>> node_external_assets;
    // The sparse overrides a carrier node's ERHE_node extension holds, by
    // node index (doc/erhe/usd_compatibility_design.md X2). The parse records them;
    // the caller applies them when it attaches the external asset's content.
    std::map<std::size_t, std::vector<erhe::scene::Instance_override>> node_instance_overrides;

    Gltf_raw_extensions                           asset_extensions;
    Gltf_raw_extensions                           scene_extensions;
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

// Whether parse_gltf reads the prepared mesh data extensions
// (ERHE_mesh_optimized, ERHE_mesh_topology, ERHE_raytrace_bvh) of soup
// primitives. ignore loads them as plain soups, as a file without the
// extensions (the load.use_prepared_mesh_data editor setting).
enum class Prepared_mesh_data_use : unsigned int {
    ignore = 0,
    use    = 1
};

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
    // Read and validate the prepared mesh data extensions and attach the
    // accepted payloads to the soup primitives' render shapes
    // (erhe::primitive::Primitive_shape::get_prepared_data()).
    Prepared_mesh_data_use                    prepared_mesh_data_use{Prepared_mesh_data_use::use};
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
    const erhe::scene::Node&                root_node;
    bool                                    binary{true};
    const erhe::scene::Physics_description* physics_data{nullptr};
    std::map<const erhe::scene::Node*, Gltf_export_external_asset> external_assets;
    std::map<const erhe::primitive::Material*, Gltf_export_asset_reference> material_asset_references;
    std::function<std::shared_ptr<const Gltf_image_source>(const erhe::graphics::Texture*)> image_source_provider;
    std::vector<std::shared_ptr<erhe::scene::Animation>> animations;
    Gltf_export_extension_payloads extension_payloads;
    std::vector<std::string> extensions_used;
    std::unordered_set<const erhe::scene::Mesh*> excluded_meshes;
    uint64_t excluded_item_flag_bits{0};
    uint64_t excluded_item_type_bits{0};
    std::vector<Gltf_export_extra_mesh> extra_meshes;
    std::vector<std::shared_ptr<erhe::primitive::Material>> extra_materials;
    std::function<std::vector<std::pair<std::string, std::string>>(const Gltf_export_index_lookup&)> asset_extensions_builder;
    // Prepared mesh data for triangle-soup primitives. none (the default)
    // writes the raw soups; save_scene passes write. Geometry-normative
    // primitives (ERHE_geometry) are unaffected.
    Prepared_mesh_data prepared_mesh_data{Prepared_mesh_data::none};
    // Executor the per-primitive preparation runs on (one task per unique
    // soup); serial on the calling thread when null.
    ::tf::Executor* executor{nullptr};
    // Receives the prepared-mesh-data statistics when non-null.
    Gltf_export_prepared_statistics* prepared_statistics{nullptr};
};

[[nodiscard]] auto export_gltf(const Gltf_export_arguments& arguments) -> std::string;

[[nodiscard]] auto export_gltf(
    const erhe::scene::Node&                root_node,
    bool                                    binary,
    const erhe::scene::Physics_description* physics_data = nullptr
) -> std::string;

}
