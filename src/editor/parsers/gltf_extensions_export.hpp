#pragma once

#include "parsers/physics_export.hpp"

#include "erhe_gltf/gltf.hpp"

#include <filesystem>

namespace editor {

class Scene_root;

// Fills glTF export arguments with the editor-domain ERHE_* extension
// payloads and exclusions for full scene persistence
// (doc/editor/gltf_scene_roundtrip.md phase 3). Interchange exports (File >
// Export glTF) do NOT use this: these extensions carry editor state that
// only the erhe open / import paths consume, and ERHE_scene in
// extensionsUsed is the marker distinguishing an erhe-authored scene from
// a plain asset (phase 4 open-vs-import branch).
//
// Added on top of the caller-provided arguments:
// - excluded_meshes: graph-mesh-controlled meshes (baked artifacts the
//   graphs rebuild on load; ERHE_node_graphs re-binds them). The matching
//   Node_physics exclusion lives in build_physics_description().
// - ERHE_scene scene payload: per-scene settings (#239), ambient light
//   (#237), enable_physics.
// - extra_meshes: brush geometry as unreferenced glTF meshes, plus an
//   asset_extensions_builder emitting ERHE_brushes / ERHE_node_graphs /
//   ERHE_collections against the exported glTF indices.
// - material_asset_references (asset-manager plan phase R6): library
//   material REFERENCE entries carrying a file-scope asset key export as
//   ERHE_asset_reference proxies (name-only stub + {file, uid}) instead of
//   full data - a reference is never embedded (plan decision 3). URIs are
//   relativized against export_path's directory (the prefab externalAssets
//   convention). Reference entries without a durable file key (and
//   self-references, which the extension prohibits) fall back to full-data
//   export with a warning.
//
// scene_root must outlive the export_gltf() call; the builder callback
// captures its own copies of the collected payload data.
// physics_items pairs the top-level arrays of arguments.physics_data by index
// with the library items build_physics_description recorded, so the ERHE_scene
// physics_materials and physics_joints entries carry each item's name and its
// complete local value set.
void add_gltf_editor_state(
    erhe::gltf::Gltf_export_arguments& arguments,
    Scene_root&                        scene_root,
    const std::filesystem::path&       export_path,
    const Physics_description_items&   physics_items
);

}
