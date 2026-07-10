#pragma once

#include "erhe_math/aabb.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace erhe {
    class Item_base;
}
namespace erhe::gltf      { class Gltf_data; class Image_transfer; }
namespace erhe::graphics  { class Device; }
namespace erhe::primitive { class Build_info; }
namespace tf              { class Executor; }

namespace editor {

class App_context;
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
// viewport drag-and-drop preview and bottom-snap placement.
class Gltf_scan_summary
{
public:
    std::vector<std::string>        contents;
    std::optional<erhe::math::Aabb> bounding_box;
};

[[nodiscard]] auto scan_gltf(const std::filesystem::path& path) -> Gltf_scan_summary;

}
