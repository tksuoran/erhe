#pragma once

#include <filesystem>
#include <memory>
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
class Scene_root;

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

[[nodiscard]] auto scan_gltf(const std::filesystem::path& path) -> std::vector<std::string>;

}
