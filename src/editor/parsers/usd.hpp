#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace erhe::primitive { class Build_info; class Material; }
namespace erhe::scene { class Xformable; using Node = Xformable; }

namespace editor {

class App_context;
class Operation;
class Prefab_library;
class Scene_root;

// Result of make_import_usd_operation(). `operation` is null exactly when
// `error` is non-empty; the counts describe what the import will insert and
// are what the MCP reply reports.
class Usd_import_result
{
public:
    std::shared_ptr<Operation> operation;
    std::string                error;
    std::size_t                node_count    {0};
    std::size_t                mesh_count    {0};
    std::size_t                material_count{0};
    std::size_t                texture_count {0};
};

// Builds (but does not run) the undoable compound operation that imports the
// USD file at `path` into `scene_root`: content-library attaches for the
// textures and materials the file names, and the insert of the imported node
// tree under an import_root node. The same Item_insert_remove_operation /
// library attach path glTF import takes, so an undo
// announces the removals (doc/import-undo-reference-clearing.md).
//
// Failures - USD support not built (ERHE_USD_LIBRARY=none), no target scene,
// an unreadable file, a conversion error - are values in `error`, and are
// logged as well.
[[nodiscard]] auto make_import_usd_operation(
    App_context&                       context,
    erhe::primitive::Build_info        build_info,
    const std::shared_ptr<Scene_root>& scene_root,
    const std::filesystem::path&       path
) -> Usd_import_result;

// Queues the import compound built by make_import_usd_operation().
void import_usd(
    App_context&                       context,
    erhe::primitive::Build_info        build_info,
    const std::shared_ptr<Scene_root>& scene_root,
    const std::filesystem::path&       path
);

// Opens the USD file at `path` as a NEW scene: a fresh Scene_root with its
// own content library, the file's top-level prims as the scene's top-level
// nodes (no import_root wrapper - the file is the scene) and the file's
// materials and textures as the scene's own library items. Editor state
// comes from the root layer's `customLayerData` (`erhe:scene`), written by
// save_scene_usd(); when it is absent the scene keeps editor defaults. The
// scene's source format is Scene_source_format::usd, so Save Scene writes
// USDA back to `path`.
//
// Not undoable, the way opening an erhe-authored glTF scene is not: the
// caller (the Load Scene message handler, MCP open_scene / load_scene) shows
// the scene through Operations::on_scene_opened. Returns null on failure,
// which is logged.
[[nodiscard]] auto open_scene_usd(
    App_context&                 context,
    const std::filesystem::path& path
) -> std::shared_ptr<Scene_root>;

// Writes `scene_root` to `path` as one USDA layer through erhe::usd:
// the scene graph, the content library's materials with the image files
// their texture slots name, and the editor's scene state (ambient light,
// enable_physics and the per-scene setting overrides) as the `erhe:scene`
// entry of `customLayerData`, in the same JSON shape the glTF ERHE_scene
// block carries. Editor-state kinds a USD file does not carry yet - brushes,
// node graphs, library folders, styles - are logged, one line per kind.
[[nodiscard]] auto save_scene_usd(
    App_context&                 context,
    Scene_root&                  scene_root,
    const std::filesystem::path& path
) -> bool;

// What one USD file contributes as a prefab template
// (doc/usd-compatibility-plan.md X1). `root` is an unhosted node whose
// children are what an instance clones: the prim the arc named, wrapped so
// that the prim's own class, transform and content ride the instance.
class Usd_prefab_template
{
public:
    std::shared_ptr<erhe::scene::Node>                      root;
    std::vector<std::shared_ptr<erhe::primitive::Material>> materials;
    std::string                                             error;
};

// Loads the USD file at `path` as a prefab template rooted at `prim_path`
// (empty = the file's default prim, and the whole file when it names none).
// Meshes are finalized and textures created, and the composition arcs
// authored inside the template subtree are instantiated recursively through
// `prefab_library`, so a reference cycle is caught there. Failures are values
// in `error`; a build without USD support answers with one.
[[nodiscard]] auto load_usd_prefab_template(
    App_context&                 context,
    Prefab_library&              prefab_library,
    const std::filesystem::path& path,
    const std::string&           prim_path
) -> Usd_prefab_template;

// True for the file extensions the USD importer accepts (.usd / .usda /
// .usdc / .usdz), case-insensitive. Answers the same in a build without USD
// support, so the asset browser classifies files the same way everywhere.
[[nodiscard]] auto is_usd_file_extension(const std::filesystem::path& path) -> bool;

} // namespace editor
