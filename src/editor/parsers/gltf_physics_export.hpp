#pragma once

#include "erhe_gltf/gltf_physics.hpp"

namespace erhe::scene { class Scene; }

namespace editor {

class Content_library;

// Builds the plain-data KHR_implicit_shapes + KHR_physics_rigid_bodies
// description of a scene's Node_physics / Node_joint attachments for glTF
// export. The result references erhe nodes / meshes; Gltf_exporter maps those
// to glTF indices and creates extra glTF child nodes for the
// synthesized_colliders entries (compound shape children and non-Y-aligned
// implicit shapes; see gltf_physics.hpp).
// When content_library is given, library physics materials / collision
// filters / joint settings that no body or joint references are appended to
// the top-level arrays so editor-authored assets survive save / load
// (parity with scene.json v3+; doc/gltf-scene-roundtrip-plan.md phase 0).
[[nodiscard]] auto build_gltf_physics_data(
    const erhe::scene::Scene& scene,
    const Content_library*    content_library = nullptr
) -> erhe::gltf::Gltf_physics_data;

}
