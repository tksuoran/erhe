#pragma once

#include <filesystem>
#include <memory>

namespace erhe::primitive { class Build_info; }

namespace editor {

class App_context;
class Operation;
class Scene_root;

// Builds (but does not run) the undoable compound operation that imports the
// USD file at `path` into `scene_root`: content-library attaches for the
// textures and materials the file names, and the insert of the imported node
// tree under an import_root node. The same Item_insert_remove_operation /
// Content_library_attach_operation path glTF import takes, so an undo
// announces the removals (doc/import-undo-reference-clearing.md).
//
// Returns null when USD support is not built (ERHE_USD_LIBRARY=none) or the
// file cannot be read; both cases are logged.
[[nodiscard]] auto make_import_usd_operation(
    App_context&                       context,
    erhe::primitive::Build_info        build_info,
    const std::shared_ptr<Scene_root>& scene_root,
    const std::filesystem::path&       path
) -> std::shared_ptr<Operation>;

// Queues the import compound built by make_import_usd_operation().
void import_usd(
    App_context&                       context,
    erhe::primitive::Build_info        build_info,
    const std::shared_ptr<Scene_root>& scene_root,
    const std::filesystem::path&       path
);

// True for the file extensions the USD importer accepts (.usd / .usda /
// .usdc / .usdz), case-insensitive. Answers the same in a build without USD
// support, so the asset browser classifies files the same way everywhere.
[[nodiscard]] auto is_usd_file_extension(const std::filesystem::path& path) -> bool;

} // namespace editor
