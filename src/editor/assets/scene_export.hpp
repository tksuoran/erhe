#pragma once

#include "assets/item_keys.hpp"
#include "assets/prepared_status.hpp"
#include "erhe_asset_store/item_kind.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::gltf  { class Gltf_gpu_vertex_formats; }
namespace erhe::scene { class Mesh; class Pvs_database; }

namespace editor {

class App_context;
class Prepared_build_formats;
class Scene_root;

// Per item kind, what one Export did (doc/editor/prepared_assets.md "Export").
class Export_kind_counts
{
public:
    std::size_t made   {0}; // Ensure: put into the editor asset
    std::size_t present{0}; // Ensure: already in the editor asset
    std::size_t written{0}; // Copy: written into the new prepared asset
};

// R10.1: the source files of an exported scene, each with its own editor asset.
enum class Export_file_role : unsigned int
{
    scene = 0,         // the scene being exported (its editor asset holds the use-dependent items, R10.2)
    prefab,            // a prefab source the scene instantiates, at any depth
    material_container // a file that defines a material the scene uses
};

[[nodiscard]] auto c_str(Export_file_role role) -> const char*;

// One source file of an Export: its editor asset and what Ensure did in it, in Ensure order (R10.7).
class Scene_export_file_report
{
public:
    std::filesystem::path                                      source_path;
    std::filesystem::path                                      editor_asset_path;
    Export_file_role                                           role{Export_file_role::scene};
    std::size_t                                                depth{0}; // prefab nesting depth; the scene 0
    std::string                                                location; // c_str(Editor_asset_location)
    std::map<erhe::asset_store::Item_kind, Export_kind_counts> counts;   // made and present
};

class Scene_export_report
{
public:
    std::vector<Scene_export_file_report>                      files; // Ensure order: deepest prefab first, the scene last
    std::filesystem::path                                      editor_asset_path;
    std::filesystem::path                                      prepared_asset_path;
    std::string                                                target;
    std::string                                                scene_key;
    std::map<erhe::asset_store::Item_kind, Export_kind_counts> counts;
    std::size_t                                                inline_primitive_count{0}; // soup shapes written inside the Scene item (not prepared)
    std::size_t                                                inline_image_count    {0}; // images written inside the Scene item (no Texture item)
    std::size_t                                                world_mesh_node_count {0}; // mesh nodes the world-space bake admits
    std::size_t                                                world_form_mesh_count {0}; // Bake World-Space Meshes: admitted nodes the world-space form draws
    std::uint64_t                                              prepared_item_count   {0};
    std::uint64_t                                              prepared_asset_bytes  {0};
    double                                                     ensure_ms{0.0};
    double                                                     scene_ms {0.0};
    double                                                     copy_ms  {0.0};
    double                                                     pvs_ms   {0.0}; // pvs_mode rebake: the PVS bake
    std::uint32_t                                              pvs_cell_count{0};
};

enum class Scene_export_stage : unsigned int
{
    mesh_keys = 0,
    mesh_items, // mesh items and their BVH items
    texture_keys,
    texture_items,
    scene_item,
    copy,
    world_mesh_items, // World-space mesh items and their BVH items
    world_form,       // Bake World-Space Meshes: the world-space form loaded from the items
    pvs_bake,         // pvs_mode rebake: the PVS bake on the world-space form (main thread, over frames)
    check_items, // status: the stores against the keys
    drop_items,  // status: a drop of R8.13
    basis_texture_items // the Basis texture items of texture_encoding target
};

[[nodiscard]] auto c_str(Scene_export_stage stage) -> const char*;

class Scene_export_progress_state
{
public:
    Scene_export_stage stage{Scene_export_stage::mesh_keys};
    std::size_t        done {0};
    std::size_t        total{0};
};

// One Export of a scene into its editor asset and prepared asset, split by
// thread like Gltf_scene_save (doc/editor/scene_serialization.md "Saving in
// the background").
class Scene_export
{
public:
    class Impl;
    Scene_export();
    ~Scene_export() noexcept;
    Scene_export(const Scene_export&) = delete;
    auto operator=(const Scene_export&) -> Scene_export& = delete;

    std::unique_ptr<Impl> impl;
};

// What a Scene_export job does: an Export (Ensure, Copy), or the status of
// the stores with an optional drop, which stores no item.
enum class Scene_export_mode : unsigned int
{
    export_items = 0,
    status,
    // Bake World-Space Meshes (R8.7): the missing World-space mesh items, then the world-space form of the open scene.
    bake_world_meshes,
    // The status without the scene held read-only (Prepared_keys): the keys from
    // stored content hashes and the stores' presence; no Scene item, no drop.
    key_check
};

// The PVS item a bake stored (R4.10).
class Stored_pvs_item
{
public:
    erhe::asset_store::Key key;
    bool                   made         {false}; // false: the editor asset held this item already
    std::uint64_t          payload_bytes{0};
};

// Main thread: the database as the PVS item of the scene's editor asset,
// replacing its previous one (at most one per scene, R4.10); nullopt (logged)
// when the scene has no editor asset or the item cannot be written.
[[nodiscard]] auto store_pvs_item(App_context& context, const Scene_root& scene_root, const erhe::scene::Pvs_database& database) -> std::optional<Stored_pvs_item>;

// How the Scene item of a target holds its images; nullopt for an unknown target.
[[nodiscard]] auto get_target_scene_item_textures(std::string_view target) -> std::optional<Scene_item_textures>;
// The Local-space mesh stamp of GPU streams in these formats.
[[nodiscard]] auto make_running_mesh_stamp(const erhe::gltf::Gltf_gpu_vertex_formats* gpu_vertex_formats) -> std::string;
// The Raytrace BVH stamp of this build's BVH library.
[[nodiscard]] auto make_running_bvh_stamp() -> std::string;

// What a prepared run of this build reads (doc/editor/prepared_assets.md "Format stamps"). Main thread.
[[nodiscard]] auto get_prepared_build_formats(const App_context& context) -> Prepared_build_formats;

// Main thread. Reads the scene's export settings. Null when the Export is
// refused (logged): get_export_refusal(), a target other than pc and quest, an
// editor asset that cannot be opened. prepared_asset_path empty: the default location (R2.4).
[[nodiscard]] auto begin_scene_export(
    App_context&                 context,
    Scene_root&                  scene_root,
    std::string_view             target,
    const std::filesystem::path& prepared_asset_path,
    Scene_export_mode            mode,
    Prepared_drop                drop
) -> std::unique_ptr<Scene_export>;
// Worker thread while the scene is read-only: Ensure, then Copy (status: keys, drop, check).
[[nodiscard]] auto run_scene_export(Scene_export& scene_export) -> bool;
[[nodiscard]] auto get_scene_export_progress(const Scene_export& scene_export) -> Scene_export_progress_state;
[[nodiscard]] auto get_scene_export_mode    (const Scene_export& scene_export) -> Scene_export_mode;
// Main thread: the worker's result can be finished (the uploads of the world-space form are done).
[[nodiscard]] auto is_scene_export_ready(const App_context& context, const Scene_export& scene_export) -> bool;
// Main thread, after run_scene_export() of an Export or a bake succeeded.
void finish_scene_export(App_context& context, Scene_export& scene_export, Scene_export_report* report);
// Main thread, after run_scene_export() of a status succeeded.
[[nodiscard]] auto finish_scene_status(Scene_export& scene_export) -> Prepared_status_report;
[[nodiscard]] auto get_scene_export_prepared_asset_path(const Scene_export& scene_export) -> const std::filesystem::path&;
// Why a worker run failed, naming the file (R10.8); empty when the log alone says.
[[nodiscard]] auto get_scene_export_error(const Scene_export& scene_export) -> const std::string&;

// A key check of a scene an Export just wrote: the Scene key that Export made, so the
// check measures the prepared asset against it with no Scene item export of its own.
void set_scene_export_known_scene_key(Scene_export& scene_export, const erhe::asset_store::Key& scene_key);
// The meshes the world-space bake of the job admits (the ones whose moves change a key).
void collect_scene_export_world_meshes(const Scene_export& scene_export, std::vector<const erhe::scene::Mesh*>& out_meshes);
// Main thread, after a job that writes the stores (an Export, a bake, a drop) ended, done or failed:
// the item catalogs of its editor assets are listed again at their next use.
void invalidate_scene_export_catalogs(Scene_export& scene_export);

// What an Export needs after a worker run of run_scene_export() succeeded.
enum class Scene_export_continuation : unsigned int
{
    done = 0, // finish_scene_export()
    pvs_bake  // pvs_mode rebake: start_scene_export_pvs_bake(), then run_scene_export() again
};
[[nodiscard]] auto get_scene_export_continuation(const Scene_export& scene_export) -> Scene_export_continuation;

enum class Scene_export_pvs_bake_state : unsigned int
{
    running = 0,
    done,
    failed
};

// Main thread (M2), once is_scene_export_ready(): installs the world-space
// form and starts the PVS bake on it (R8.9, R9.6); false (logged) when it cannot.
[[nodiscard]] auto start_scene_export_pvs_bake(App_context& context, Scene_export& scene_export, const std::shared_ptr<Scene_root>& scene_root) -> bool;
// Main thread (M2), each frame while the bake runs: its progress; done once the PVS item is stored.
[[nodiscard]] auto update_scene_export_pvs_bake(App_context& context, Scene_export& scene_export) -> Scene_export_pvs_bake_state;

}
