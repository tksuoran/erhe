#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace erhe {
    class Hierarchy;
    class Item_base;
}
namespace erhe::primitive {
    class Material;
}
namespace erhe::scene {
    class Mesh;
    class Skin;
    class Xformable; using Node = Xformable;
}

namespace editor {

class App_context;
class Operation;
class Scene_root;

// A rigidly skinned mesh built from mesh prims already in a scene: the
// shared core of the MCP create_skin tool and of Bind (rigid),
// doc/plans/rigging/skeleton_editing.md R18. Every part's geometry is merged
// into one geometry with its world transform baked in (the bind space is
// world space), each vertex is bound with weight 1 to one joint, and the new
// mesh prim - one primitive, carrying the new erhe::scene::Skin - sits at
// world identity under `parent`, replacing the parts.

// The mesh an item names for skinning: the item itself when it is a Mesh,
// else the mesh of that node; null for a bone proxy (session-only
// visualization, never content) and for an item without a mesh.
[[nodiscard]] auto get_skinnable_mesh(const std::shared_ptr<erhe::Item_base>& item) -> std::shared_ptr<erhe::scene::Mesh>;

class Rigid_skin_parameters
{
public:
    Scene_root*                                     scene_root{nullptr};
    // The new mesh prim's name; the skin is named "<name> skin".
    std::string                                     name;
    // The new mesh prim's parent (any prim).
    std::shared_ptr<erhe::Hierarchy>                parent;
    std::shared_ptr<erhe::primitive::Material>      material;
    // The parts: merged into the new mesh and taken out of the scene.
    std::vector<std::shared_ptr<erhe::scene::Mesh>> parts;
    std::vector<std::shared_ptr<erhe::scene::Node>> joints;
    // One per joint, in `joints` order.
    std::vector<glm::mat4>                          inverse_bind_matrices;
    // The joint index a vertex binds to, from the index of the part it came
    // from and its world position.
    std::function<std::size_t(std::size_t part_index, const glm::vec3& world_position)> joint_for_vertex;
};

class Rigid_skin
{
public:
    std::shared_ptr<erhe::scene::Mesh> mesh;
    std::shared_ptr<erhe::scene::Skin> skin;
    // ONE undoable operation, not queued: the skin enters the content
    // library, the parts leave the scene, the new mesh enters it (its insert
    // registers the skin and marks the joints as bones,
    // Scene_root::register_skin). Its undo takes the new mesh and the skin
    // out and puts the parts back.
    std::shared_ptr<Operation>         operation;
    // Vertices bound to each joint, in `joints` order, and in total.
    std::vector<std::size_t>           joint_vertex_counts;
    std::size_t                        vertex_count{0};
};

// Builds the rigid skin. Returns nullopt with `out_error` set when the parts
// produce no vertices or the geometry cannot be built; the parameters are
// the caller's to validate (the parts exist, are unskinned, have primitives
// and a parent; the joints are nodes of the same scene).
[[nodiscard]] auto make_rigid_skin(
    App_context&                 context,
    const Rigid_skin_parameters& parameters,
    std::string&                 out_error
) -> std::optional<Rigid_skin>;

// Bind (rigid), R18.
class Bone_bind_result
{
public:
    std::shared_ptr<erhe::scene::Mesh>              mesh;   // the new skinned mesh
    std::shared_ptr<erhe::scene::Skin>              skin;
    std::vector<std::shared_ptr<erhe::scene::Node>> joints; // the skin's joints, in order
    std::vector<glm::mat4>                          inverse_bind_matrices;
    std::vector<std::size_t>                        joint_vertex_counts;
    // Set when the bind was refused; the text is the message that was
    // logged. Nothing is queued then.
    std::optional<std::string>                      refusal;
    bool                                            queued{false};
};

// Why `mesh` cannot be bound to `bones`, nullopt when it can: the mesh is
// missing, already skinned, has no primitives or no parent; no bones; a bone
// is not a bone, is in another scene or is already a joint of a skin (a bone
// is bound to one skin). For greying out the menu entry.
[[nodiscard]] auto get_bind_refusal(
    const std::shared_ptr<erhe::scene::Mesh>&              mesh,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& bones
) -> std::optional<std::string>;

// Bind (rigid): creates a Skin for `mesh` from `bones` (duplicates dropped,
// order kept) as ONE undoable operation (make_rigid_skin) queued on the
// operation stack. The inverse bind matrices are the inverses of the bones'
// rest world transforms (rig/bone_bind.hpp get_rest_world_transforms), so
// the bind pose the skin implies is each bone's Rig.rest_* transform; every
// vertex binds with weight 1 to the bone whose rest head-tail segment
// (Rig.tail) is nearest to it. The new mesh keeps the mesh's name, parent
// and first material; the skin is "<mesh> skin". Once it executes the bones
// are joints of the skin, so the bound-skeleton refusals (R9) apply to them.
// Refused (get_bind_refusal) with the reason logged.
auto bind_mesh_to_bones_rigid(
    App_context&                                           context,
    const std::shared_ptr<erhe::scene::Mesh>&              mesh,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& bones
) -> Bone_bind_result;

} // namespace editor
