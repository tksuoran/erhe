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
namespace erhe::scene { class Node; }

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
    std::optional<erhe::physics::Motion_mode> motion_mode;
    std::optional<float>                      friction;
    std::optional<float>                      restitution;
    std::optional<float>                      linear_damping;
    std::optional<float>                      angular_damping;
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

// Applies the editor-domain payloads that make sense on import into an
// existing scene:
// - ERHE_layout: Layout / Layout_item attachments on the imported nodes
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

}
