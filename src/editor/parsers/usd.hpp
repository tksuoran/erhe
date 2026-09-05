#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

namespace erhe::primitive { class Build_info; }

namespace editor {

class App_context;
class Operation;
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
// Content_library_attach_operation path glTF import takes, so an undo
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

// True for the file extensions the USD importer accepts (.usd / .usda /
// .usdc / .usdz), case-insensitive. Answers the same in a build without USD
// support, so the asset browser classifies files the same way everywhere.
[[nodiscard]] auto is_usd_file_extension(const std::filesystem::path& path) -> bool;

} // namespace editor
