#pragma once

#include "erhe_gltf/gltf.hpp"

#include <glm/glm.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace erhe {
    class Item_base;
}
namespace erhe::scene {
    class Node;
    class Scene;
}

namespace editor {

class App_context;
class Scene_root;

// A glTF file parsed once and kept as an instantiable template. The
// template nodes live in a private holding scene (no Scene_host, never
// rendered); gltf_data keeps the parsed resources (materials, textures,
// primitives) alive. Instances are deep clones of the template subtree
// whose Mesh clones share the template's Primitives, so instantiation
// performs no GPU upload.
class Prefab
{
public:
    std::filesystem::path               source_path;   // canonical
    std::string                         name;          // display name (source file name)
    erhe::gltf::Gltf_data               gltf_data;
    std::shared_ptr<erhe::scene::Scene> holding_scene;
    std::shared_ptr<erhe::scene::Node>  template_root; // parent of the template's scene roots
};

// App-wide cache of glTF prefab templates, keyed by canonical source path.
// Loading is main-thread only (it runs inside Editor::tick(), from the
// asset browser or a dispatched MCP call, and needs
// App_context::current_command_buffer for texture upload).
class Prefab_library
{
public:
    explicit Prefab_library(App_context& context);

    // Parse + mesh-finalize a glTF once; cached by canonical path. Returns
    // nullptr (with a log_parsers error) when the file is missing, produces
    // no nodes, or participates in a prefab reference cycle (prohibited by
    // glTF 2.1).
    auto get_or_load(const std::filesystem::path& path) -> std::shared_ptr<Prefab>;

    [[nodiscard]] auto get_prefabs() const -> const std::map<std::filesystem::path, std::shared_ptr<Prefab>>&;

private:
    App_context&                                             m_context;
    std::map<std::filesystem::path, std::shared_ptr<Prefab>> m_prefabs;
    std::vector<std::filesystem::path>                       m_active_load_stack; // cycle detection
};

// Instantiate a prefab into a scene: clone the template subtree under a new
// instance root node carrying a Prefab_instance attachment, register the
// prefab's shared resources in the scene's content library, and queue the
// whole insertion as one undoable Compound_operation (mirrors
// place_brush_in_scene). Returns the instance root node. When parent is
// null the scene's root node is used.
auto instantiate_prefab(
    App_context&                              context,
    const std::shared_ptr<Prefab>&            prefab,
    Scene_root&                               scene_root,
    const glm::mat4&                          world_from_node,
    const std::shared_ptr<erhe::scene::Node>& parent = {}
) -> std::shared_ptr<erhe::scene::Node>;

// Attach a Prefab_instance marker and a clone of the prefab's template
// under an existing node, retargeting cloned meshes to content_layer_id and
// appending mesh-carrying nodes to out_mesh_node_items when non-null. The
// building block shared by instantiate_prefab, glTF external-asset import,
// and .erhescene load.
void attach_prefab_instance(
    const std::shared_ptr<Prefab>&                 prefab,
    const std::shared_ptr<erhe::scene::Node>&      node,
    erhe::scene::Layer_id                          content_layer_id,
    std::vector<std::shared_ptr<erhe::Item_base>>* out_mesh_node_items
);

// Collect glTF 2.1 external-asset references for export: walks the subtree
// under root_node and maps each node carrying a Prefab_instance attachment
// to a files-array entry (URI relativized against export_directory when
// possible, MIME type by source extension). Does not descend into instance
// subtrees - their content lives in the referenced file. Pass the result to
// erhe::gltf::export_gltf via Gltf_export_arguments::external_assets.
[[nodiscard]] auto collect_prefab_external_assets(
    const erhe::scene::Node&     root_node,
    const std::filesystem::path& export_directory
) -> std::map<const erhe::scene::Node*, erhe::gltf::Gltf_export_external_asset>;

// Resolve glTF 2.1 external assets in freshly parsed gltf_data: for each
// node that instantiates an external asset, load the referenced prefab
// through the library (recursively; reference cycles are errors, per the
// glTF 2.1 spec) and clone its template under the carrier node, which is
// marked with a Prefab_instance attachment. Cloned meshes are pointed at
// content_layer_id and appended to out_mesh_node_items when non-null.
// Used by import_gltf (with the destination scene's content layer) and by
// Prefab_library itself when the loaded template contains nested external
// assets (layer 0; instances retarget on instantiation).
void resolve_external_assets(
    Prefab_library&                                prefab_library,
    const erhe::gltf::Gltf_data&                   gltf_data,
    erhe::scene::Layer_id                          content_layer_id,
    std::vector<std::shared_ptr<erhe::Item_base>>* out_mesh_node_items
);

}
