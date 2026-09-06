#pragma once

#include "erhe_physics/irigid_body.hpp"

#include <glm/glm.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace erhe::gltf  { class Gltf_data; }
namespace erhe::scene { class Xformable; using Node = Xformable; }

namespace editor {

class App_context;
class Operation;
class Scene_root;

// Import side of the editor-domain ERHE_* glTF extensions
// (doc/gltf-scene-roundtrip-plan.md phase 3); the export side is
// parsers/gltf_extensions_export.hpp.

// Parsed ERHE_physics node payload: rigid-body state the
// KHR_physics_rigid_bodies import cannot recover (both kinematic motion
// modes, per-body friction / restitution, damping). Applied onto the
// IRigid_body_create_info before Node_physics construction in
// import_gltf_physics().
class Gltf_physics_overrides
{
public:
    std::optional<erhe::physics::Motion_mode>        motion_mode;
    // The Node_physics local values (name -> text); has_properties marks a
    // file that carries the map, the attachment's complete local set.
    std::vector<std::pair<std::string, std::string>> properties;
    bool                                             has_properties{false};
};

[[nodiscard]] auto parse_gltf_physics_overrides(const erhe::gltf::Gltf_data& gltf_data)
    -> std::unordered_map<const erhe::scene::Node*, Gltf_physics_overrides>;

// Parsed ERHE_scene payload. Parsing lives here so the extension shape has
// one owner; APPLYING it is the phase-4 Open-Scene path (importing an
// asset into an existing scene must not clobber that scene's settings).
class Gltf_scene_state
{
public:
    glm::vec4   ambient_light {0.0f, 0.0f, 0.0f, 0.0f};
    bool        enable_physics{true};
    // Minified Scene_settings JSON for the codegen deserializer; empty =
    // no per-scene overrides.
    std::string settings_json;
};

[[nodiscard]] auto parse_gltf_scene_state(const erhe::gltf::Gltf_data& gltf_data) -> std::optional<Gltf_scene_state>;

// ERHE_scene physics_materials / collision_filter_names: the
// KHR_physics_rigid_bodies physicsMaterials / collisionFilters entries by
// index (the KHR entries carry no name). A physics material entry carries
// the name and the material's local property values (name -> text), the
// material's complete local set. Empty vectors when absent.
class Gltf_physics_material_record
{
public:
    std::string                                      name;
    std::vector<std::pair<std::string, std::string>> properties;
    bool                                             has_properties{false};
};

class Gltf_physics_item_names
{
public:
    std::vector<Gltf_physics_material_record> physics_materials;
    std::vector<std::string>                  collision_filters;
};

[[nodiscard]] auto parse_gltf_physics_item_names(const erhe::gltf::Gltf_data& gltf_data) -> Gltf_physics_item_names;

// Applies the editor-domain payloads that make sense on import into an
// existing scene:
// - ERHE_layout: Layout attachments on the imported nodes (legacy layout_item blocks become Layout.* attached properties)
//   (attached directly, entering the scene with the node insert operation,
//   like Node_physics).
// - ERHE_collections: item tags on the imported nodes.
// - ERHE_brushes: brushes into the content library (undoable attach
//   operations; geometry comes from the referenced unreferenced glTF
//   meshes restored through ERHE_geometry).
// - ERHE_node_graphs: Graph_texture / Graph_mesh assets (undoable attach
//   operations; graphs load born-dirty and re-bake), material slot
//   bindings and node Geometry_graph_mesh bindings.
// Must run after mesh finalization (brush geometry) and before the import
// Compound_operation is composed.
void import_gltf_editor_state(
    App_context&                             context,
    const erhe::gltf::Gltf_data&             gltf_data,
    const std::shared_ptr<Scene_root>&       scene_root,
    const std::filesystem::path&             path,
    std::vector<std::shared_ptr<Operation>>& operations
);

// ERHE_scene library_folders: the tree position of every resource the file
// places somewhere other than its kind scope (doc/content-library-folders.md
// D5/D6). A saved path may name any prim of the scene tree (C5), so this runs
// AFTER the imported nodes enter the scene - separately from
// import_gltf_editor_state, whose operations all run before that.
void append_library_folders_operation(
    const erhe::gltf::Gltf_data&             gltf_data,
    const std::shared_ptr<Scene_root>&       scene_root,
    std::vector<std::shared_ptr<Operation>>& operations
);

}
