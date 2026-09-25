// Mcp_server skinning tool create_skin (bind_mesh_to_bones, Bind (rigid) of
// doc/plans/rigging/skeleton_editing.md R18, is in mcp_server_rig.cpp with
// the other skeleton editing tools).
//
// create_skin builds a rigidly skinned mesh from mesh prims that are already
// in the scene: each part contributes its geometry (world transform baked in)
// and names the one joint node that drives every vertex of that part. The
// result is a single new Mesh prim with one primitive, an erhe::scene::Skin
// listing the distinct joints (inverse binds from the joints' current world
// transforms), and the part meshes taken out of the scene. It builds through
// make_rigid_skin (rig/rigid_skin.hpp), as Bind (rigid) does.
//
// Rigid weights only: joint_indices_0 = (j, 0, 0, 0) and
// joint_weights_0 = (1, 0, 0, 0) for every vertex. Smooth weights are the
// weight paint tool's job (src/editor/tools/weight_paint_tool.cpp).

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "operations/operation_stack.hpp"
#include "prefabs/instance_structure.hpp"
#include "rig/rigid_skin.hpp"
#include "scene/scene_root.hpp"

#include "erhe_primitive/material.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"

#include <glm/glm.hpp>

#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

namespace editor {

using namespace mcp_server_detail;

namespace {

// One entry of the create_skin `parts` array, resolved against the scene.
class Skin_part
{
public:
    std::shared_ptr<erhe::scene::Mesh> mesh;
    std::shared_ptr<erhe::scene::Node> joint;
    std::size_t                        joint_index{0};
};

} // anonymous namespace

auto Mcp_server::action_create_skin(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }

    // Strict arguments: a misspelled key is refused rather than silently
    // ignored (doc/agents/mcp_api_guidelines.md).
    {
        static constexpr const char* c_accepted_keys[] = {
            "scene_name", "name", "parent_node_id", "parent_node_name", "material", "parts"
        };
        std::string unrecognized;
        for (const auto& [key, value] : args.items()) {
            static_cast<void>(value);
            bool known = false;
            for (const char* accepted_key : c_accepted_keys) {
                if (key == accepted_key) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                if (!unrecognized.empty()) {
                    unrecognized += ", ";
                }
                unrecognized += key;
            }
        }
        if (!unrecognized.empty()) {
            return make_error_content(
                "create_skin: unrecognized argument(s): " + unrecognized +
                " (accepted: scene_name, name, parent_node_id, parent_node_name, material, parts)"
            );
        }
    }

    const json parts_json = args.value("parts", json::array());
    if (!parts_json.is_array() || parts_json.empty()) {
        return make_error_content("create_skin needs a non-empty 'parts' array");
    }

    // Resolve every part before anything is built, so a bad part refuses
    // without leaving a half-made mesh behind.
    std::vector<Skin_part>                          parts;
    std::vector<std::shared_ptr<erhe::scene::Node>> joints;
    parts.reserve(parts_json.size());
    for (const json& part_json : parts_json) {
        if (!part_json.is_object()) {
            return make_error_content("create_skin 'parts' entries must be objects with 'node_id' and 'joint_node_id'");
        }
        if (!part_json.contains("node_id") || !part_json.contains("joint_node_id")) {
            return make_error_content("create_skin part needs both 'node_id' and 'joint_node_id'");
        }

        const std::shared_ptr<erhe::Hierarchy> mesh_prim = find_prim_in_scene(*sr, part_json, "node_id", "node_name");
        if (!mesh_prim) {
            return make_error_content(
                "create_skin part node not found: " + std::to_string(part_json.value("node_id", std::size_t{0}))
            );
        }
        const std::shared_ptr<erhe::scene::Mesh> mesh = get_skinnable_mesh(mesh_prim);
        if (!mesh) {
            return make_error_content("create_skin part '" + mesh_prim->get_name() + "' carries no mesh");
        }
        if (mesh->skin) {
            return make_error_content("create_skin part '" + mesh->get_name() + "' is already skinned");
        }
        if (mesh->get_primitives().empty()) {
            return make_error_content("create_skin part '" + mesh->get_name() + "' has no primitives");
        }
        if (!mesh->get_parent().lock()) {
            return make_error_content("create_skin part '" + mesh->get_name() + "' has no parent to be removed from");
        }
        // A mesh listed twice would be merged twice and removed twice, so
        // the second listing is refused rather than half-applied.
        for (const Skin_part& earlier_part : parts) {
            if (earlier_part.mesh == mesh) {
                return make_error_content("create_skin part '" + mesh->get_name() + "' is listed more than once");
            }
        }

        const json joint_args = json{{"node_id", part_json.at("joint_node_id")}};
        const std::shared_ptr<erhe::Hierarchy> joint_prim = find_prim_in_scene(*sr, joint_args, "node_id", "joint_node_name");
        if (!joint_prim) {
            return make_error_content(
                "create_skin joint node not found: " + std::to_string(part_json.value("joint_node_id", std::size_t{0}))
            );
        }
        const std::shared_ptr<erhe::scene::Node> joint = std::dynamic_pointer_cast<erhe::scene::Node>(joint_prim);
        if (!joint) {
            return make_error_content("create_skin joint '" + joint_prim->get_name() + "' is not a transformable node");
        }
        if (joint->get_item_host() != mesh->get_item_host()) {
            return make_error_content(
                "create_skin joint '" + joint->get_name() + "' and part '" + mesh->get_name() + "' are in different scenes"
            );
        }

        std::size_t joint_index = joints.size();
        for (std::size_t i = 0, end = joints.size(); i < end; ++i) {
            if (joints[i] == joint) {
                joint_index = i;
                break;
            }
        }
        if (joint_index == joints.size()) {
            joints.push_back(joint);
        }

        parts.push_back(Skin_part{.mesh = mesh, .joint = joint, .joint_index = joint_index});
    }

    // The new mesh prim's parent: any prim (doc/erhe/usd_compatibility_design.md C5).
    std::shared_ptr<erhe::Hierarchy> parent{};
    if (args.contains("parent_node_id") || args.contains("parent_node_name")) {
        parent = find_prim_in_scene(*sr, args, "parent_node_id", "parent_node_name");
        if (!parent) {
            return make_error_content("create_skin parent node not found");
        }
        const std::optional<std::string> child_refusal = instance_child_refusal(*parent);
        if (child_refusal.has_value()) {
            log_mcp->info("create_skin refused: {}", child_refusal.value());
            return make_error_content(child_refusal.value());
        }
    } else {
        parent = std::static_pointer_cast<erhe::Hierarchy>(sr->get_scene().get_root_node());
    }

    const std::shared_ptr<Content_library> library = sr->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library");
    }

    // Material: the named one, else the first material of the first part, else
    // the library's first.
    std::shared_ptr<erhe::primitive::Material> material;
    const std::string material_name = args.value("material", "");
    if (!material_name.empty()) {
        material = find_library_item<erhe::primitive::Material>(library, material_name);
        if (!material) {
            return make_error_content("Material not found: " + material_name);
        }
    }
    if (!material) {
        material = parts.front().mesh->get_primitives().front().material;
    }
    if (!material) {
        const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials = library->get_all<erhe::primitive::Material>();
        if (!materials.empty()) {
            material = materials.front();
        }
    }
    if (!material) {
        return make_error_content("No materials available");
    }

    // The joints' current world transforms are the bind pose.
    const std::string name = args.value("name", std::string{"skinned mesh"});
    std::vector<std::shared_ptr<erhe::scene::Mesh>> part_meshes;
    part_meshes.reserve(parts.size());
    for (const Skin_part& part : parts) {
        part_meshes.push_back(part.mesh);
    }
    std::vector<glm::mat4> inverse_bind_matrices;
    inverse_bind_matrices.reserve(joints.size());
    for (const std::shared_ptr<erhe::scene::Node>& joint : joints) {
        inverse_bind_matrices.push_back(glm::inverse(joint->world_from_node()));
    }
    std::string error;
    std::optional<Rigid_skin> rigid_skin = make_rigid_skin(
        m_context,
        Rigid_skin_parameters{
            .scene_root            = sr,
            .name                  = name,
            .parent                = parent,
            .material              = material,
            .parts                 = std::move(part_meshes),
            .joints                = joints,
            .inverse_bind_matrices = std::move(inverse_bind_matrices),
            .joint_for_vertex      = [&parts](const std::size_t part_index, const glm::vec3&) -> std::size_t {
                return parts[part_index].joint_index;
            }
        },
        error
    );
    if (!rigid_skin.has_value()) {
        return make_error_content("create_skin: " + error);
    }
    // One undoable compound (make_rigid_skin): the skin resource enters the
    // library, the part meshes leave the scene, the new mesh prim enters it.
    m_context.operation_stack->queue(rigid_skin.value().operation);
    const std::shared_ptr<erhe::scene::Mesh>& new_mesh = rigid_skin.value().mesh;
    const std::shared_ptr<erhe::scene::Skin>& skin     = rigid_skin.value().skin;

    json joints_json = json::array();
    for (std::size_t i = 0, end = joints.size(); i < end; ++i) {
        joints_json.push_back({
            {"joint_index",  i},
            {"node_name",    joints[i]->get_name()},
            {"node_id",      joints[i]->get_id()},
            {"vertex_count", rigid_skin.value().joint_vertex_counts[i]}
        });
    }

    log_mcp->info("create_skin '{}': {} joints, {} vertices", name, joints.size(), rigid_skin.value().vertex_count);

    return make_json_content({
        {"node_name",    new_mesh->get_name()},
        {"node_id",      new_mesh->get_id()},
        {"skin_name",    skin->get_name()},
        {"skin_id",      skin->get_id()},
        {"joint_count",  joints.size()},
        {"vertex_count", rigid_skin.value().vertex_count},
        {"material",     material->get_name()},
        {"joints",       joints_json},
        {"queued",       true} // the compound operation executes on the next editor frame
    }).dump();
}

} // namespace editor
