#pragma once

#include "erhe_math/aabb.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace erhe {
    class Item_base;
}
namespace erhe::gltf      { class Gltf_data; class Gltf_image_source; class Image_transfer; }
namespace erhe::graphics  { class Device; class Texture; }
namespace erhe::primitive { class Build_info; }
namespace erhe::scene     { class Animation; }
namespace tf              { class Executor; }

namespace editor {

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
[[nodiscard]] auto make_import_gltf_operation(
    App_context&                       context,
    erhe::primitive::Build_info        build_info,
    const std::shared_ptr<Scene_root>& scene_root,
    const std::filesystem::path&       path
) -> std::shared_ptr<Operation>;

// Queues the import compound built by make_import_gltf_operation().
void import_gltf(
    App_context&                       context,
    erhe::primitive::Build_info        build_info,
    const std::shared_ptr<Scene_root>& scene_root,
    const std::filesystem::path&       path
);

// The Build_info every glTF import entry point uses (asset browser, MCP
// import_gltf, scene open, prefab loading): all primitive types enabled,
// GPU buffers from the shared Mesh_memory.
[[nodiscard]] auto make_import_build_info(App_context& context) -> erhe::primitive::Build_info;

// Make freshly parsed glTF meshes renderable: build geometry edges +
// smooth vertex normals, allocate GPU buffers in Mesh_memory (skinned
// meshes get the skinned vertex format), and update raytrace primitives.
// Shared by import_gltf and Prefab_library. When out_mesh_node_items is
// non-null, nodes carrying meshes are appended to it (for the raytrace
// kickoff operation).
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
};

[[nodiscard]] auto scan_gltf(const std::filesystem::path& path) -> Gltf_scan_summary;

// True when extensionsUsed lists ERHE_scene: the file is an erhe-authored
// scene (written by save_scene_gltf / export with editor state), opened as
// a full Scene_root instead of imported as an asset.
[[nodiscard]] auto is_erhe_scene(const std::vector<std::string>& extensions_used) -> bool;

// Scene save (doc/gltf-scene-roundtrip-plan.md phase 4): one export_gltf()
// call writing the whole scene state into a single glTF file - render
// content plus physics data, prefab external-asset references, embedded
// texture sources, animations, and every editor-domain ERHE_* extension
// payload (add_gltf_editor_state). Binary .glb unless the path ends in
// .gltf. Returns false when the scene has no root node or the write fails.
[[nodiscard]] auto save_scene_gltf(Scene_root& scene_root, const std::filesystem::path& path) -> bool;

// THE scene save entry point (File > Save Scene, MCP save_scene): writes the
// scene with save_scene_gltf() above, sends Scene_saved_message (asset
// browser rescan), and, when 'path' is a loaded prefab source, reloads the
// prefab so every instance in every scene reflects the edit (this replaced
// the separate Save Prefab command).
[[nodiscard]] auto save_scene_gltf(
    App_context&                 context,
    Scene_root&                  scene_root,
    const std::filesystem::path& path
) -> bool;

// Default location for saved scene files: res/editor/scenes, relative to the
// editor working directory (repo root). Created if missing so file dialogs
// can open there and the asset browser can list saved scenes.
[[nodiscard]] auto default_scene_dir() -> std::filesystem::path;

// Where "save this scene" writes without further input: the scene's own
// source file when it was opened/loaded from one (saved back silently), else
// default_scene_dir()/<scene name>.glb (a new file; UI confirms overwrite).
[[nodiscard]] auto resolve_scene_save_path(const Scene_root& scene_root) -> std::filesystem::path;

// Scene open (doc/gltf-scene-roundtrip-plan.md phase 4): opens an
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

// Gltf_export_arguments::image_source_provider backed by the scene's
// content library (doc/gltf-scene-roundtrip-plan.md phase 0): serves the
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
