// Mcp_server skinning tools (create_skin).
//
// Builds a rigidly skinned mesh from mesh prims that are already in the
// scene: each part contributes its geometry (world transform baked in) and
// names the one joint node that drives every vertex of that part. The result
// is a single new Mesh prim with one primitive, an erhe::scene::Skin listing
// the distinct joints, and the part meshes taken out of the scene.
//
// Rigid weights only: joint_indices_0 = (j, 0, 0, 0) and
// joint_weights_0 = (1, 0, 0, 0) for every vertex of a part. Smooth weights
// are the weight paint tool's job (src/editor/tools/weight_paint_tool.cpp).

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "operations/compound_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/library_attach_operation.hpp"
#include "operations/operation.hpp"
#include "operations/operation_stack.hpp"
#include "prefabs/instance_structure.hpp"
#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

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
    std::size_t                        vertex_count{0};
};

// The vertex range one part contributed to the merged geometry.
class Vertex_range
{
public:
    std::size_t  part_index{0};
    GEO::index_t first{0};
    GEO::index_t end{0};
};

[[nodiscard]] auto is_bone_proxy(const erhe::scene::Mesh& mesh) -> bool
{
    return (mesh.get_flag_bits() & erhe::Item_flags::bone_proxy) != 0;
}

// The mesh a part node names: the prim itself when it is a Mesh, otherwise
// the mesh of that node (a node whose mesh is a child prim).
//
// A bone proxy is skipped: Bone_visualization hangs one under every joint of
// a registered skin, so naming a joint node as a part would otherwise pick up
// that proxy's geometry - and a proxy is session-only visualization, not
// content to be merged into an asset.
[[nodiscard]] auto resolve_part_mesh(const std::shared_ptr<erhe::Hierarchy>& prim) -> std::shared_ptr<erhe::scene::Mesh>
{
    const std::shared_ptr<erhe::scene::Mesh> mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(prim);
    if (mesh) {
        return is_bone_proxy(*mesh) ? std::shared_ptr<erhe::scene::Mesh>{} : mesh;
    }
    const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(prim);
    if (node) {
        const std::shared_ptr<erhe::scene::Mesh> node_mesh = erhe::scene::get_mesh(node.get());
        if (node_mesh && !is_bone_proxy(*node_mesh)) {
            return node_mesh;
        }
    }
    return {};
}

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
        const std::shared_ptr<erhe::scene::Mesh> mesh = resolve_part_mesh(mesh_prim);
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

    // Merge every part's geometry into one, world transform baked in, and
    // write the part's rigid joint binding over the vertices it contributed.
    const std::string name = args.value("name", std::string{"skinned mesh"});
    std::shared_ptr<erhe::geometry::Geometry> combined_geometry = std::make_shared<erhe::geometry::Geometry>(name);
    erhe::primitive::Normal_style normal_style = erhe::primitive::Normal_style::point_normals;
    bool normal_style_taken = false;
    std::vector<Vertex_range> vertex_ranges;
    for (std::size_t part_index = 0, part_end = parts.size(); part_index < part_end; ++part_index) {
        Skin_part& part = parts[part_index];
        const glm::mat4 world_from_node = part.mesh->world_from_node();
        for (const erhe::scene::Mesh_primitive& mesh_primitive : part.mesh->get_primitives()) {
            if (!mesh_primitive.primitive) {
                continue;
            }
            const std::shared_ptr<erhe::primitive::Primitive_render_shape>& shape = mesh_primitive.primitive->render_shape;
            if (!shape) {
                continue;
            }
            const std::shared_ptr<erhe::geometry::Geometry>& geometry = shape->get_geometry();
            if (!geometry) {
                continue;
            }
            if (!normal_style_taken) {
                normal_style       = shape->get_normal_style();
                normal_style_taken = true;
            }
            const GEO::index_t base_vertex = combined_geometry->get_mesh().vertices.nb();
            combined_geometry->merge_with_transform(*geometry.get(), erhe::geometry::to_geo_mat4f(world_from_node));
            const GEO::index_t end_vertex = combined_geometry->get_mesh().vertices.nb();
            vertex_ranges.push_back(Vertex_range{.part_index = part_index, .first = base_vertex, .end = end_vertex});
            part.vertex_count += static_cast<std::size_t>(end_vertex - base_vertex);
        }
    }
    if (combined_geometry->get_mesh().vertices.nb() == 0) {
        return make_error_content("create_skin produced no vertices");
    }

    // Written after every merge: merge_attributes() copies the names the
    // SOURCE geometry carries, so a source without joint attributes would not
    // disturb what is written here - but writing once, at the end, keeps the
    // rule from depending on that.
    {
        erhe::geometry::Mesh_attributes& attributes = combined_geometry->get_attributes();
        for (const Vertex_range& range : vertex_ranges) {
            const GEO::vec4u indices{static_cast<GEO::index_t>(parts[range.part_index].joint_index), 0u, 0u, 0u};
            const GEO::vec4f weights{1.0f, 0.0f, 0.0f, 0.0f};
            for (GEO::index_t vertex = range.first; vertex < range.end; ++vertex) {
                attributes.vertex_joint_indices_0.set(vertex, indices);
                attributes.vertex_joint_weights_0.set(vertex, weights);
            }
        }
    }

    // Same finishing pass every geometry producer runs (Merge_operation): a
    // payload geometry must carry connectivity and edges.
    const GEO::index_t vertex_count_before_process = combined_geometry->get_mesh().vertices.nb();
    combined_geometry->process(
        {
            .flags =
                erhe::geometry::Geometry::process_flag_connect |
                erhe::geometry::Geometry::process_flag_build_edges |
                erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates
        }
    );
    const GEO::index_t vertex_count_after_process = combined_geometry->get_mesh().vertices.nb();
    if (vertex_count_after_process != vertex_count_before_process) {
        // The per-vertex joint binding is written by vertex index, so a pass
        // that renumbers vertices would silently rebind them.
        return make_error_content(
            "create_skin geometry processing changed the vertex count from " +
            std::to_string(vertex_count_before_process) + " to " + std::to_string(vertex_count_after_process)
        );
    }

    // Skinned vertex format: the GPU vertex buffer must carry the joint
    // indices / weights streams, which the non-skinned build info drops
    // (src/editor/parsers/gltf.cpp).
    const erhe::primitive::Build_info build_info{
        .primitive_types = {
            .fill_triangles          = true,
            .fill_triangles_expanded = true,
            .edge_lines              = true,
            .corner_points           = true,
            .centroid_points         = true
        },
        .buffer_info = m_context.mesh_memory->make_skinned_primitive_buffer_info()
    };
    std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(combined_geometry);
    if (!primitive->make_renderable_mesh(build_info, normal_style)) {
        return make_error_content("create_skin failed to build the renderable mesh");
    }
    if (!primitive->make_raytrace()) {
        return make_error_content("create_skin failed to build the raytrace mesh");
    }

    // Skeleton is left unset: erhe::scene::get_skin_transform_root() computes
    // the closest common ancestor of the joints when the skin names none, so
    // an explicit guess would only be able to be wrong.
    std::shared_ptr<erhe::scene::Skin> skin = std::make_shared<erhe::scene::Skin>(name + " skin");
    skin->skin_data.joints = joints;
    skin->skin_data.inverse_bind_matrices.reserve(joints.size());
    for (const std::shared_ptr<erhe::scene::Node>& joint : joints) {
        skin->skin_data.inverse_bind_matrices.push_back(glm::inverse(joint->world_from_node()));
    }
    skin->enable_flag_bits(
        erhe::Item_flags::content |
        erhe::Item_flags::show_in_ui |
        erhe::Item_flags::id
    );

    std::shared_ptr<erhe::scene::Mesh> new_mesh = std::make_shared<erhe::scene::Mesh>(name);
    new_mesh->add_primitive(primitive, material);
    new_mesh->layer_id = sr->layers().content()->id;
    new_mesh->enable_flag_bits(
        erhe::Item_flags::content |
        erhe::Item_flags::id      |
        erhe::Item_flags::show_in_ui
    );
    // glTF 2.0: the skinned mesh node's own transform is ignored, so the new
    // prim sits at identity and the joints alone pose it.
    new_mesh->set_world_from_node(glm::mat4{1.0f});
    new_mesh->skin = skin;

    // One undoable compound: the skin resource enters the library, the new
    // mesh prim enters the scene (its insert is what registers the skin and
    // marks the joints - Scene_root::register_skin), and the part meshes
    // leave it.
    std::vector<std::shared_ptr<Operation>> operations;
    operations.push_back(make_library_insert_operation(m_context, library, skin));
    operations.push_back(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = new_mesh,
                .parent  = parent,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );
    for (const Skin_part& part : parts) {
        operations.push_back(
            std::make_shared<Item_insert_remove_operation>(
                Item_insert_remove_operation::Parameters{
                    .context = m_context,
                    .item    = part.mesh,
                    .parent  = part.mesh->get_parent().lock(),
                    .mode    = Item_insert_remove_operation::Mode::remove
                }
            )
        );
    }
    m_context.operation_stack->queue(
        std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)})
    );

    json joints_json = json::array();
    for (std::size_t i = 0, end = joints.size(); i < end; ++i) {
        std::size_t vertex_count = 0;
        for (const Skin_part& part : parts) {
            if (part.joint_index == i) {
                vertex_count += part.vertex_count;
            }
        }
        joints_json.push_back({
            {"joint_index",  i},
            {"node_name",    joints[i]->get_name()},
            {"node_id",      joints[i]->get_id()},
            {"vertex_count", vertex_count}
        });
    }

    log_mcp->info("create_skin '{}': {} joints, {} vertices", name, joints.size(), vertex_count_after_process);

    return make_json_content({
        {"node_name",    new_mesh->get_name()},
        {"node_id",      new_mesh->get_id()},
        {"skin_name",    skin->get_name()},
        {"skin_id",      skin->get_id()},
        {"joint_count",  joints.size()},
        {"vertex_count", static_cast<std::size_t>(vertex_count_after_process)},
        {"material",     material->get_name()},
        {"joints",       joints_json},
        {"queued",       true} // the compound operation executes on the next editor frame
    }).dump();
}

} // namespace editor
