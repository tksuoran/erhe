#pragma once

#include "erhe_gltf/gltf_physics.hpp"

namespace erhe::scene { class Scene; }

namespace editor {

// Builds the plain-data KHR_implicit_shapes + KHR_physics_rigid_bodies
// description of a scene's Node_physics / Node_joint attachments for glTF
// export. The result references erhe nodes / meshes; Gltf_exporter maps those
// to glTF indices and creates extra glTF child nodes for the
// synthesized_colliders entries (compound shape children and non-Y-aligned
// implicit shapes; see gltf_physics.hpp).
[[nodiscard]] auto build_gltf_physics_data(const erhe::scene::Scene& scene) -> erhe::gltf::Gltf_physics_data;

}
