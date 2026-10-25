#pragma once

#include "erhe_gltf/gltf.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace erhe {
    class Hierarchy;
    class Item_base;
}
namespace erhe::gltf      { class Gltf_data; class Gltf_image_source; class Image_transfer; }
namespace erhe::graphics  { class Device; class Texture; }
namespace erhe::primitive { class Build_info; }
namespace erhe::scene     { class Animation; class Camera; class Xformable; using Node = Xformable; }
namespace tf              { class Executor; }

enum class Cpu_mesh_data_retention : unsigned int; // config/generated/cpu_mesh_data_retention.hpp

namespace editor {

// The name of the one variant set a glTF asset's KHR_materials_variants list
// becomes (doc/erhe/usd_compatibility_design.md X4). glTF holds a single asset-wide
// list of variant names, so a glTF-backed scene's variant table holds exactly
// one set under this name.
inline constexpr const char* c_gltf_variant_set_name = "materials";

class App_context;
class Content_library;
class Materials;
class Operation;
class Scene_root;

// Builds (but does not run) the undoable compound operation that imports
// the glTF at `path` into `scene_root`: content-library attaches (textures,
// materials, skins, animations), physics items, the node-tree insert,
// default camera/lights when the file has none, and the raytrace kickoff.
// Callers decide how to run it: import_gltf() queues it on the
// Operation_stack; Scene_open_operation executes it inline as part of its
// own execute() so "Open scene" stays a single undo entry (an executing
// operation must not call back into the Operation_stack except to queue()).
// materials_as_references (R7 import-as-reference): parsed materials are
// acquired from the source container through the Asset_manager and listed
// as reference entries instead of copied definitions; the default keeps
// import-as-copy semantics.
// fit_view_to_content: size the view to the imported content - the injected
// default camera is placed to frame it and gets a depth range / shadow range
// derived from the content bounds, and cameras carried by the file get their
// far plane and shadow range widened to cover it. Used when the file becomes
// a scene of its own (Scene_open_operation, e.g. --scene); importing into an
// existing scene leaves that scene's view alone.
// A glTF parse already produced by an asynchronous load
// (doc/editor/async_asset_loading_design.md step 7): the parsed data plus the
// import_root node its content hangs from, ready to hand to
// make_import_gltf_operation instead of having it parse inline. The root node
// must already carry the import_root flags and be unparented, exactly as the
// inline parse leaves it.
class Prepared_gltf_parse
{
public:
    erhe::gltf::Gltf_data              gltf_data;
    std::shared_ptr<erhe::scene::Node> root_node;
};

// Everything needed to build - or REBUILD - one glTF import
// (doc/editor/reloadable_asset_loads.md). An Import_gltf_operation keeps this after
// dropping its payload, so a redo can re-read the file.
class Gltf_import_recipe
{
public:
    std::filesystem::path       path;
    std::weak_ptr<Scene_root>   scene_root; // weak: a recorded load must not pin its target
    bool materials_as_references{false};
    bool fit_view_to_content    {false};

    // Decisions the FIRST build derived from the target scene's live state:
    // an existing camera, or any non-empty light layer, suppresses the
    // corresponding default. Recorded rather than re-derived, so a redo after
    // the user added a camera still produces the node set the original import
    // produced. `decisions_recorded` is false until the first build fills them.
    bool decisions_recorded{false};
    bool add_default_camera{true};
    bool add_default_light {true};
};

[[nodiscard]] auto make_import_gltf_operation(
    App_context&                       context,
    erhe::primitive::Build_info        build_info,
    const std::shared_ptr<Scene_root>& scene_root,
    const std::filesystem::path&       path,
    bool                               materials_as_references = false,
    bool                               fit_view_to_content     = false,
    // When non-null, the parse (and its Buffer_mesh build) has already been
    // done asynchronously: this consumes it instead of parsing inline, and
    // record adoption is skipped - the caller decided that at queue time.
    Prepared_gltf_parse*               prepared_parse          = nullptr,
    // In/out. When non-null and `decisions_recorded` is set, the recorded
    // default-camera / default-light decisions are used instead of deriving
    // them from the target scene; otherwise they are derived and written back.
    Gltf_import_recipe*                recipe                  = nullptr
) -> std::shared_ptr<Operation>;

// Queues the import compound built by make_import_gltf_operation().
void import_gltf(
    App_context&                       context,
    erhe::primitive::Build_info        build_info,
    const std::shared_ptr<Scene_root>& scene_root,
    const std::filesystem::path&       path,
    bool                               materials_as_references = false
);

// The Build_info every glTF import entry point uses (asset browser, MCP
// import_gltf, scene open, prefab loading): all primitive types enabled,
// GPU buffers from the shared Mesh_memory.
// The queue selector picks which Mesh_memory transfer queue the built
// vertex / index bytes go through (doc/editor/async_asset_loading_design.md 2.6).
// Only a caller whose publish gates on the loader watermark may pass
// Mesh_memory_queue::loader; everything that publishes immediately must keep
// the default interactive queue.
[[nodiscard]] auto make_import_build_info(
    App_context&                              context,
    erhe::scene_renderer::Mesh_memory_queue   queue = erhe::scene_renderer::Mesh_memory_queue::interactive
) -> erhe::primitive::Build_info;

// The mesh optimizer cache directory of imported meshes: cache/mesh_optimizer
// under the working directory (writable on Android too, main.cpp chdir's to
// the per-app internal storage) when Mesh_memory_config::mesh_optimize_cache
// is on, empty (no cache) otherwise. Used by the soup-path optimization at
// parse and by the geometry-path optimization of the deferred finalize an
// import kicks off (Async_raytrace_kickoff_operation); builds of procedural
// or edited geometry run uncached.
[[nodiscard]] auto get_import_mesh_optimize_cache_directory(const App_context& context) -> std::filesystem::path;

// Whether glTF parses read the prepared mesh data extensions
// (Load_config::use_prepared_mesh_data, doc/plans/gltf_fast_load.md "Loader");
// use when the editor settings are not available.
[[nodiscard]] auto get_prepared_mesh_data_use(const App_context& context) -> erhe::gltf::Prepared_mesh_data_use;

// Load_config::cpu_mesh_data with platform_default resolved (keep on desktop,
// release in OpenXR), and the matching glTF parse argument: release records
// triangle soup reload sources (doc/editor/async_asset_loading.md "Releasing
// CPU mesh data").
[[nodiscard]] auto get_cpu_mesh_data_retention(const App_context& context) -> Cpu_mesh_data_retention;
[[nodiscard]] auto get_triangle_soup_reload  (const App_context& context) -> erhe::gltf::Triangle_soup_reload;

// Make freshly parsed glTF meshes renderable: build geometry edges +
// smooth vertex normals, allocate GPU buffers in Mesh_memory (skinned
// meshes get the skinned vertex format), and update raytrace primitives.
// Shared by import_gltf and Prefab_library. When out_mesh_node_items is
// non-null, nodes carrying meshes are appended to it (for the raytrace
// kickoff operation).
// One unique render shape of an asynchronous load's Buffer_mesh build
// (doc/editor/async_asset_loading.md "Load memory budget"): the primitive,
// the Build_info for its vertex format (skinned or not), and the bytes its
// build enqueues on the loader transfer queue, which the build's
// Memory_budget lease pays for until the queue has drained them.
class Imported_build_job
{
public:
    erhe::primitive::Primitive*        primitive {nullptr};
    const erhe::primitive::Build_info* build_info{nullptr};
    const erhe::scene::Mesh*           mesh      {nullptr};
    std::size_t                        upload_byte_count {0}; // waits in the loader queue until drained
    std::size_t                        payload_byte_count{0}; // the soup a deferred payload load reads, for the job's duration
};

// The build jobs of a parsed glTF, one per unique render shape (a shape
// shared by instancing meshes is built once). Main thread or worker; the
// Build_infos must outlive the jobs. out_primitive_count receives the count
// of primitives the jobs cover.
[[nodiscard]] auto collect_imported_build_jobs(
    const erhe::gltf::Gltf_data&       gltf_data,
    const erhe::primitive::Build_info& build_info,
    const erhe::primitive::Build_info& skinned_build_info,
    std::size_t&                       out_primitive_count
) -> std::vector<Imported_build_job>;

// What run_imported_build_job() did.
class Imported_build_result
{
public:
    bool        prepared{false}; // the primitive carries prepared mesh data
    bool        released{false}; // its CPU soup was released after the build
    bool        stored  {false}; // built from the GPU vertex streams stored in the file
    std::string portable_reason; // why the stored streams were not used (empty when stored)
};

// Whether a build job uploads the GPU-ready vertex streams a file stores
// (ERHE_gpu_vertex_streams, load.use_gpu_vertex_streams).
enum class Stored_vertex_streams_use : unsigned int {
    ignore = 0,
    use    = 1
};

// Builds one job's primitive on a worker, inside its own
// Scoped_worker_context: loads its payload when the parse deferred it
// (Primitive::load_payload(): the soup read from the file and the prepared
// mesh data validated), then the prepared load build or the renderable soup
// build. With release_soups (load.cpu_mesh_data release) the soup of a
// primitive that reached its final state - real raytrace, and the optimized
// variant when meshes are optimized - is released right away, so a load
// never holds every soup at once. finalize_imported_meshes still runs
// afterwards on the main thread and fast-paths over the built primitives.
// With stored_streams use and release_soups, a deferred primitive whose file
// stores GPU vertex streams in the running layouts is built from them
// (Primitive::make_stored_meshes()): no soup is read or packed.
auto run_imported_build_job(
    const Imported_build_job&  job,
    erhe::graphics::Device&    graphics_device,
    bool                       release_soups,
    Stored_vertex_streams_use  stored_streams
) -> Imported_build_result;

// The nodes overload is the implementation and is format-neutral: the USD
// import (parsers/usd.cpp) hands it its own node list.
void finalize_imported_meshes(
    App_context&                                       context,
    const erhe::primitive::Build_info&                 build_info,
    std::span<const std::shared_ptr<erhe::scene::Node>> nodes,
    std::vector<std::shared_ptr<erhe::Item_base>>*     out_mesh_node_items
);

void finalize_imported_meshes(
    App_context&                                   context,
    const erhe::primitive::Build_info&             build_info,
    const erhe::gltf::Gltf_data&                   gltf_data,
    std::vector<std::shared_ptr<erhe::Item_base>>* out_mesh_node_items
);

// Lightweight glTF file summary for the asset browser: human-readable
// content lines (tooltip) plus the combined default-scene AABB computed
// from accessor bounds in the JSON (no buffer data read) - used for the
// viewport drag-and-drop preview and bottom-snap placement. The structured
// extensionsUsed list is carried alongside the flattened tooltip text so
// callers can branch on ERHE_scene (erhe-authored scene marker, phase 4
// open-vs-import).
class Gltf_scan_summary
{
public:
    std::vector<std::string>        contents;
    std::vector<std::string>        extensions_used;
    std::optional<erhe::math::Aabb> bounding_box;
    // Material names + glTF 2.1 uids (parallel; empty uid when absent), so
    // the asset browser can offer per-material actions on scanned files
    // (R7 reference-into-scene).
    std::vector<std::string>        material_names;
    std::vector<std::string>        material_uids;
};

[[nodiscard]] auto scan_gltf(const std::filesystem::path& path) -> Gltf_scan_summary;

// True when extensionsUsed lists ERHE_scene: the file is an erhe-authored
// scene (written by save_scene_gltf / export with editor state), opened as
// a full Scene_root instead of imported as an asset.
[[nodiscard]] auto is_erhe_scene(const std::vector<std::string>& extensions_used) -> bool;

// Scene save (doc/editor/gltf_scene_roundtrip.md phase 4): one export_gltf()
// call writing the whole scene state into a single glTF file - render
// content plus physics data, prefab external-asset references, embedded
// texture sources, animations, and every editor-domain ERHE_* extension
// payload (add_gltf_editor_state). Binary .glb unless the path ends in
// .gltf. Soup primitives are written as prepared cores with the
// ERHE_mesh_optimized / ERHE_mesh_topology / ERHE_raytrace_bvh extensions
// (doc/plans/gltf_fast_load.md), prepared in parallel on 'executor' (serial
// when null). Returns false when the scene has no root node or the write
// fails.
// What one scene save did: filled by the save_scene_gltf() overloads below
// when the caller passes one (MCP get_save_status reports it).
class Gltf_scene_save_report
{
public:
    erhe::gltf::Gltf_export_prepared_statistics prepared;
    erhe::gltf::Gltf_export_texture_statistics  textures;
    double                                      export_ms {0.0};
    std::size_t                                 file_bytes{0};
    // A save onto the file the scene's sources read from (doc/editor/
    // scene_serialization.md "Saving onto the source file"): primitives
    // whose soup source now reads the written file, primitives whose soup was
    // made resident instead, and images whose source now reads the written file.
    bool                                        overwrote_sources       {false};
    std::size_t                                 rebound_primitive_count {0};
    std::size_t                                 detached_primitive_count{0};
    std::size_t                                 rebound_image_count     {0};
};

// texture_export selects the image form (erhe::gltf::Gltf_texture_export);
// source unless "Save for Quest". gpu_vertex_formats, when not null, makes
// the save write each prepared primitive's GPU-ready vertex streams in those
// layouts (ERHE_gpu_vertex_streams; "Save for Quest"). 'report', when not
// null, receives the statistics of the save.
[[nodiscard]] auto save_scene_gltf(
    Scene_root&                                scene_root,
    const std::filesystem::path&               path,
    tf::Executor*                              executor,
    erhe::gltf::Gltf_texture_export            texture_export     = erhe::gltf::Gltf_texture_export::source,
    Gltf_scene_save_report*                    report             = nullptr,
    const erhe::gltf::Gltf_gpu_vertex_formats* gpu_vertex_formats = nullptr
) -> bool;

// THE scene save entry point (File > Save Scene, MCP save_scene): writes the
// scene with save_scene_gltf() above, sends Scene_saved_message (asset
// browser rescan), and, when 'path' is a loaded prefab source, reloads the
// prefab so every instance in every scene reflects the edit (this replaced
// the separate Save Prefab command).
[[nodiscard]] auto save_scene_gltf(
    App_context&                 context,
    Scene_root&                  scene_root,
    const std::filesystem::path& path,
    Gltf_scene_save_report*      report = nullptr
) -> bool;

// "Save for Quest" (File > Save glTF for Quest, MCP save_scene_for_quest;
// doc/plans/gltf_fast_load.md follow-up item 4): writes the same erhe glTF as
// save_scene_gltf() above, but every Basis Universal KTX2 texture is
// transcoded at save time to a raw ASTC 4x4 KTX2 image referenced through
// ERHE_texture_ktx2 (doc/gltf_extensions/ERHE_texture_ktx2.md), so a device
// with ASTC uploads the blocks with no transcode. PNG / JPEG images are
// written as they are. Each prepared primitive also gets its GPU-ready vertex
// streams in the running Mesh_memory layouts (ERHE_gpu_vertex_streams,
// doc/gltf_extensions/ERHE_gpu_vertex_streams.md). An export: the scene's
// source file and dirty state are left alone; the asset browser rescans.
// texture_export is one of the two ASTC forms (decision D3:
// ktx2_astc_4x4 supercompresses the levels with Zstandard,
// ktx2_astc_4x4_uncompressed stores them as they upload).
[[nodiscard]] auto save_scene_gltf_for_quest(
    App_context&                    context,
    Scene_root&                     scene_root,
    const std::filesystem::path&    path,
    Gltf_scene_save_report*         report         = nullptr,
    erhe::gltf::Gltf_texture_export texture_export = erhe::gltf::Gltf_texture_export::ktx2_astc_4x4
) -> bool;

// Default location for saved scene files: res/editor/scenes, relative to the
// editor working directory (repo root). Created if missing so file dialogs
// can open there and the asset browser can list saved scenes.
[[nodiscard]] auto default_scene_dir() -> std::filesystem::path;

// Where "save this scene" writes without further input: the scene's own
// source file when it was opened/loaded from one (saved back silently), else
// default_scene_dir()/<scene name>.glb (a new file; UI confirms overwrite).
[[nodiscard]] auto resolve_scene_save_path(const Scene_root& scene_root) -> std::filesystem::path;

// Scene open (doc/editor/gltf_scene_roundtrip.md phase 4): opens an
// erhe-authored glTF file (see is_erhe_scene) as a full Scene_root - NOT
// undoable. Reuses the
// import machinery (parse_gltf + finalize_imported_meshes + physics import
// + import_gltf_editor_state) but constructs the Scene_root directly with a
// fresh, empty Content_library (the file carries the scene's own brushes /
// materials / textures) and applies the ERHE_scene payload (enable_physics
// at construction, ambient light, per-scene Scene_settings) which the
// import path deliberately leaves alone. The parsed top-level nodes are
// parented directly under the new scene's root (no import_root wrapper)
// and no default camera / lights are injected. The caller wires up UI
// (browser window, viewport) and sends Scene_created_message. Returns
// nullptr when the file cannot be read.
[[nodiscard]] auto open_scene_gltf(
    App_context&                 context,
    const std::filesystem::path& path
) -> std::shared_ptr<Scene_root>;

// The editor's own camera for a scene whose file authors none, so that the
// scene's viewport has something to render through: the same fitted default
// camera the foreign-glTF import injects (make_import_gltf_operation with
// fit_view_to_content), placed to frame the world bounds of every Mesh prim
// under content_root. Flagged session_only (no exporter writes it) and
// exclude_from_prefab. Returned unparented; the caller inserts it into the
// scene. Format-neutral: open_scene_usd uses it.
[[nodiscard]] auto make_default_camera_for_content(
    App_context&     context,
    erhe::Hierarchy& content_root
) -> std::shared_ptr<erhe::scene::Camera>;

// The MAIN-THREAD TAIL of open_scene_gltf, split out so the asynchronous
// path (Gltf_load_task) can share it: everything from reading the ERHE_scene
// payload to the raytrace kickoff. Constructs the Scene_root, registers it,
// resolves asset references, builds Buffer_meshes, executes the
// content-library / physics / editor-state operations and reparents the
// parsed nodes under the new scene root. Requires a live command buffer.
// `adopted` says the parse came from an Asset_manager container record, so
// the record's structure pins are severed on success. Returns nullptr when
// the file carries no ERHE_scene payload.
[[nodiscard]] auto finish_open_scene_gltf(
    App_context&                              context,
    const std::filesystem::path&              path,
    erhe::gltf::Gltf_data&                    gltf_data,
    const std::shared_ptr<erhe::scene::Node>& container_node,
    bool                                      adopted
) -> std::shared_ptr<Scene_root>;

// Gltf_export_arguments::image_source_provider backed by the scene's
// content library (doc/editor/gltf_scene_roundtrip.md phase 0): serves the
// retained compressed source image bytes stored on texture entries.
// Fallback for textures imported before retention landed: re-read the
// texture's standalone source image file (images that were embedded in a
// .glb/.gltf cannot be re-extracted and are skipped with a warning).
[[nodiscard]] auto make_gltf_image_source_provider(
    const std::shared_ptr<Content_library>& content_library
) -> std::function<std::shared_ptr<const erhe::gltf::Gltf_image_source>(const erhe::graphics::Texture*)>;

// The content-library animations, for Gltf_export_arguments::animations.
[[nodiscard]] auto collect_gltf_export_animations(
    const std::shared_ptr<Content_library>& content_library
) -> std::vector<std::shared_ptr<erhe::scene::Animation>>;

}
