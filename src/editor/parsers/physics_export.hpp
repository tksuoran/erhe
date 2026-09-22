#pragma once

#include "erhe_scene/physics_description.hpp"

#include <memory>
#include <vector>

namespace erhe::physics {
    class Collision_filter;
    class Physics_joint_settings;
    class Physics_material;
}
namespace erhe::scene { class Scene; }

namespace editor {

class Content_library;

// The content-library item behind each entry of a built description's
// top-level arrays, in the same order: what a writer needs to name the item a
// record describes - the ERHE_scene physics_materials block of a glTF save
// (add_gltf_editor_state) and the prim of each record of a USD save.
class Physics_description_items
{
public:
    std::vector<std::shared_ptr<erhe::physics::Physics_material>>       materials;
    std::vector<std::shared_ptr<erhe::physics::Collision_filter>>       collision_filters;
    std::vector<std::shared_ptr<erhe::physics::Physics_joint_settings>> joint_settings;
};

// Builds the format-neutral description of a scene's Node_physics values and
// Joint prims (erhe_scene/physics_description.hpp), which the glTF writer
// takes as KHR_implicit_shapes + KHR_physics_rigid_bodies and the USD writer
// as the UsdPhysics prims and API schemas of the mapping. The result
// references erhe nodes / meshes; each writer maps those to its own form and
// creates the extra prims the synthesized_colliders entries name (compound
// shape children and non-Y-aligned implicit shapes).
// When content_library is given, library physics materials / collision
// filters / joint settings that no body or joint references are appended to
// the top-level arrays so editor-authored assets survive save / load
// (parity with scene.json v3+; doc/editor/gltf_scene_roundtrip.md phase 0).
// When items is given it receives the library item behind each entry.
[[nodiscard]] auto build_physics_description(
    const erhe::scene::Scene&  scene,
    const Content_library*     content_library = nullptr,
    Physics_description_items* items           = nullptr
) -> erhe::scene::Physics_description;

}
