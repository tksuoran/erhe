#pragma once

#include <filesystem>
#include <memory>
#include <vector>

namespace erhe::gltf { class Gltf_data; }

namespace editor {

class App_context;
class Operation;
class Scene_root;

// Maps KHR_physics_rigid_bodies / KHR_implicit_shapes data parsed into
// Gltf_data::physics onto editor physics: shared Physics_material /
// Collision_filter / Physics_joint_settings content-library items (attached
// via Content_library_attach_operations appended to operations), Node_physics
// attachments (rigid bodies / triggers, with compound folding of descendant
// colliders) and Node_joint attachments. Must be called from import_gltf()
// after mesh finalization (mesh-sourced collision shapes need the built
// Geometry) and before the Compound_operation is composed.
void import_gltf_physics(
    App_context&                             context,
    const erhe::gltf::Gltf_data&             gltf_data,
    const std::shared_ptr<Scene_root>&       scene_root,
    const std::filesystem::path&             path,
    std::vector<std::shared_ptr<Operation>>& operations
);

}
