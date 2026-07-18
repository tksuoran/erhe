#pragma once

#include "assets/asset_key.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace erhe::gltf {
    class Gltf_data;
    class Gltf_image_source;
}
namespace erhe::graphics {
    class Texture;
}
namespace erhe::primitive {
    class Material;
}

namespace editor {

class App_context;
class Content_library;
class Scene_root;

// R7 asset workflow verbs (asset-manager plan phase R7, design D4).
// v1 scope: materials (the R6 wire format serializes material references).
// None of these are undoable: they alter container files and cross-container
// manager state; the undo boundary is documented in the plan. The
// asset-browser "Reference into scene" attach IS undoable (it is a plain
// library attach).

using Gltf_image_source_provider = std::function<std::shared_ptr<const erhe::gltf::Gltf_image_source>(const erhe::graphics::Texture*)>;

// Writes a glTF asset container file holding the given materials as full
// definitions (no scene content, no ERHE_scene marker - the file is an
// asset container, not a scene). Export stamps uids back onto the
// materials.
auto write_material_container_file(
    const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials,
    const std::filesystem::path&                                   path,
    const Gltf_image_source_provider&                              image_source_provider,
    std::string&                                                   out_error
) -> bool;

// Convenience wrapper: image sources from a content library (see
// make_gltf_image_source_provider); null library = source-path fallback
// only.
auto write_material_container_file(
    const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials,
    const std::filesystem::path&                                   path,
    const std::shared_ptr<Content_library>&                        image_source_library,
    std::string&                                                   out_error
) -> bool;

// Image-source provider backed by a parsed container's retained encoded
// image streams (Gltf_data::images / image_sources parallel vectors), so a
// reopened material container can be rewritten with its textures intact.
[[nodiscard]] auto make_gltf_data_image_source_provider(const erhe::gltf::Gltf_data& gltf_data) -> Gltf_image_source_provider;

// MAKE EXTERNAL: move a material definition out of the scene into a fresh
// asset container file at `path`. The container file is written, the
// manager re-homes THE live object (meshes keep pointing at it), and the
// scene's library entry flips definition -> reference carrying the file
// key - the next scene save writes an R6 proxy instead of the data.
auto make_material_external(
    App_context&                                      context,
    Scene_root&                                       scene_root,
    const std::shared_ptr<erhe::primitive::Material>& material,
    const std::filesystem::path&                      path,
    std::string&                                      out_error
) -> bool;

// MAKE INTERNAL: copy a shared (referenced) material's data into a
// scene-owned definition and de-link - every mesh primitive of the scene
// that used the shared object is swapped to the copy, the reference entry
// is replaced by an owning definition entry, and edits stop reaching other
// users of the shared object. The copy carries no uid (a new asset).
// Returns the copy, or null with out_error set.
auto make_material_internal(
    App_context&                                      context,
    Scene_root&                                       scene_root,
    const std::shared_ptr<erhe::primitive::Material>& material,
    std::string&                                      out_error
) -> std::shared_ptr<erhe::primitive::Material>;

// REFERENCE INTO SCENE: acquire the keyed material through the manager and
// list it in the scene's library as a reference entry carrying its file
// key (an undoable Content_library_attach_operation is queued). Acceptance
// per plan resolution 11: the defining container must be path-bound
// (is_cross_scene_referenceable). Returns the material, or null with
// out_error set. Idempotent: an already-listed material returns directly.
auto reference_material_into_scene(
    App_context&     context,
    Scene_root&      scene_root,
    const Asset_key& key,
    std::string&     out_error
) -> std::shared_ptr<erhe::primitive::Material>;

}
