#pragma once

#include "erhe_gltf/gltf.hpp"

namespace editor {

class Scene_root;

// Fills glTF export arguments with the editor-domain ERHE_* extension
// payloads and exclusions for full scene persistence
// (doc/gltf-scene-roundtrip-plan.md phase 3). Interchange exports (File >
// Export glTF) do NOT use this: these extensions carry editor state that
// only the erhe open / import paths consume, and ERHE_scene in
// extensionsUsed is the marker distinguishing an erhe-authored scene from
// a plain asset (phase 4 open-vs-import branch).
//
// Added on top of the caller-provided arguments:
// - excluded_meshes: graph-mesh-controlled meshes (baked artifacts the
//   graphs rebuild on load; ERHE_node_graphs re-binds them). The matching
//   Node_physics exclusion lives in build_gltf_physics_data().
// - ERHE_physics node payloads: motion_mode (both kinematic modes),
//   per-body friction / restitution, linear / angular damping -
//   KHR_physics_rigid_bodies has no carrier for these.
// - ERHE_layout node payloads: Layout and Layout_item attachment fields
//   with their Item flags.
// - ERHE_scene scene payload: per-scene settings (#239), ambient light
//   (#237), enable_physics.
// - extra_meshes: brush geometry as unreferenced glTF meshes, plus an
//   asset_extensions_builder emitting ERHE_brushes / ERHE_node_graphs /
//   ERHE_collections against the exported glTF indices.
//
// scene_root must outlive the export_gltf() call; the builder callback
// captures its own copies of the collected payload data.
void add_gltf_editor_state(erhe::gltf::Gltf_export_arguments& arguments, Scene_root& scene_root);

}
