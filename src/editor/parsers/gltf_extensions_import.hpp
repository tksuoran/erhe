#pragma once

#include <glm/glm.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace erhe        { class Item_base; }
namespace erhe::gltf  { class Gltf_data; }
namespace erhe::scene { class Xformable; using Node = Xformable; }

namespace editor {

class App_context;
class Content_library;
class Operation;
class Scene_root;

// Import side of the editor-domain ERHE_* glTF extensions
// (doc/editor/gltf_scene_roundtrip.md phase 3); the export side is
// parsers/gltf_extensions_export.hpp.

// Parsed ERHE_scene payload. Parsing lives here so the extension shape has
// one owner; APPLYING it is the phase-4 Open-Scene path (importing an
// asset into an existing scene must not clobber that scene's settings).
class Gltf_scene_state
{
public:
    glm::vec3   ambient_light {0.0f, 0.0f, 0.0f};
    bool        enable_physics{true};
    // Minified Scene_settings JSON for the codegen deserializer; empty =
    // no per-scene overrides.
    std::string settings_json;
    // The scene item's local property values (name -> text) and the name of
    // the style it uses (doc/erhe/property_system.md section 4.20). The map
    // is present exactly when the file carries it, and is then the item's
    // complete local set; a file without it leaves `ambient_light` local.
    std::optional<std::vector<std::pair<std::string, std::string>>> properties;
    std::string                                                     style_name;
};

[[nodiscard]] auto parse_gltf_scene_state(const erhe::gltf::Gltf_data& gltf_data) -> std::optional<Gltf_scene_state>;

// The `properties` map of an ERHE_scene / `erhe:scene` block onto the scene
// item, after the explicit `ambient_light` field wrote its local value: the
// map is the item's complete local set, so a value it does not name is
// cleared and an ambient color a style holds stays style-held (the
// `ERHE_light` rule of doc/erhe/property_system.md section 4.18). Shared by
// the glTF and USD open-scene paths, which carry the same JSON object.
void apply_scene_item_properties(
    erhe::Item_base&                                                       scene_item,
    const std::optional<std::vector<std::pair<std::string, std::string>>>& properties
);

// The `style` member of the same block: the style item is looked up by name,
// so this runs once the file's styles exist. An empty name does nothing.
void apply_scene_item_style(
    const std::shared_ptr<Content_library>& content_library,
    erhe::Item_base&                        scene_item,
    const std::string&                      style_name
);

// ERHE_scene physics_materials / physics_joints / collision_filter_names: the
// KHR_physics_rigid_bodies physicsMaterials / physicsJoints / collisionFilters
// entries by index (the KHR entries carry no name). A physics material and a
// joint-settings entry carry the name and the item's local property values
// (name -> text), the item's complete local set. Empty vectors when absent.
class Gltf_physics_item_record
{
public:
    std::string                                      name;
    std::vector<std::pair<std::string, std::string>> properties;
    bool                                             has_properties{false};
};

class Gltf_physics_item_names
{
public:
    std::vector<Gltf_physics_item_record> physics_materials;
    std::vector<Gltf_physics_item_record> physics_joints;
    std::vector<std::string>              collision_filters;
};

[[nodiscard]] auto parse_gltf_physics_item_names(const erhe::gltf::Gltf_data& gltf_data) -> Gltf_physics_item_names;

// Applies the editor-domain payloads that make sense on import into an
// existing scene:
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

// The object-reference local values of a "properties" map that named an
// item by path and did not resolve during the parse
// (Gltf_data::unresolved_object_properties): resolved by name in the scene.
// Runs AFTER the imported nodes enter the scene - separately from
// import_gltf_editor_state, whose operations all run before that - so a
// name that belongs to a node of the file is found. A reference an
// ERHE_node "property_node_refs" index already resolved is not here.
void append_unresolved_object_property_operations(
    const erhe::gltf::Gltf_data&             gltf_data,
    const std::shared_ptr<Scene_root>&       scene_root,
    std::vector<std::shared_ptr<Operation>>& operations
);

// ERHE_scene library_folders: the tree position of every resource the file
// places somewhere other than its kind scope (doc/editor/content_library_folders.md
// D5/D6). A saved path may name any prim of the scene tree (C5), so this runs
// AFTER the imported nodes enter the scene - separately from
// import_gltf_editor_state, whose operations all run before that.
void append_library_folders_operation(
    const erhe::gltf::Gltf_data&             gltf_data,
    const std::shared_ptr<Scene_root>&       scene_root,
    std::vector<std::shared_ptr<Operation>>& operations
);

}
