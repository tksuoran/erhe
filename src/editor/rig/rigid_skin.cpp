#include "rig/rigid_skin.hpp"

#include "rig/bone_bind.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "operations/compound_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/library_attach_operation.hpp"
#include "operations/operation.hpp"
#include "operations/operation_stack.hpp"
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

#include <fmt/format.h>

#include <algorithm>

namespace editor {

namespace {

[[nodiscard]] auto is_bone_proxy(const erhe::scene::Mesh& mesh) -> bool
{
    return (mesh.get_flag_bits() & erhe::Item_flags::bone_proxy) != 0;
}

// The vertex range one part contributed to the merged geometry.
class Vertex_range
{
public:
    std::size_t  part_index{0};
    GEO::index_t first{0};
    GEO::index_t end{0};
};

} // anonymous namespace

auto get_skinnable_mesh(const std::shared_ptr<erhe::Item_base>& item) -> std::shared_ptr<erhe::scene::Mesh>
{
    // A bone proxy is skipped: Bone_visualization hangs one under every bone,
    // so naming a bone node would otherwise pick up that proxy's geometry.
    const std::shared_ptr<erhe::scene::Mesh> mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(item);
    if (mesh) {
        return is_bone_proxy(*mesh) ? std::shared_ptr<erhe::scene::Mesh>{} : mesh;
    }
    const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
    if (node) {
        const std::shared_ptr<erhe::scene::Mesh> node_mesh = erhe::scene::get_mesh(node.get());
        if (node_mesh && !is_bone_proxy(*node_mesh)) {
            return node_mesh;
        }
    }
    return {};
}

auto make_rigid_skin(
    App_context&                 context,
    const Rigid_skin_parameters& parameters,
    std::string&                 out_error
) -> std::optional<Rigid_skin>
{
    Scene_root* const scene_root = parameters.scene_root;
    if (scene_root == nullptr) {
        out_error = "no scene";
        return std::nullopt;
    }
    const std::shared_ptr<Content_library> library = scene_root->get_content_library();
    if (!library) {
        out_error = "the scene has no content library";
        return std::nullopt;
    }
    if (parameters.joints.empty() || (parameters.inverse_bind_matrices.size() != parameters.joints.size())) {
        out_error = "a skin needs joints and one inverse bind matrix per joint";
        return std::nullopt;
    }

    // Merge every part's geometry into one, world transform baked in.
    std::shared_ptr<erhe::geometry::Geometry> combined_geometry = std::make_shared<erhe::geometry::Geometry>(parameters.name);
    erhe::primitive::Normal_style normal_style = erhe::primitive::Normal_style::point_normals;
    bool normal_style_taken = false;
    std::vector<Vertex_range> vertex_ranges;
    for (std::size_t part_index = 0, part_end = parameters.parts.size(); part_index < part_end; ++part_index) {
        const std::shared_ptr<erhe::scene::Mesh>& part = parameters.parts[part_index];
        const glm::mat4 world_from_node = part->world_from_node();
        for (const erhe::scene::Mesh_primitive& mesh_primitive : part->get_primitives()) {
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
        }
    }
    if (combined_geometry->get_mesh().vertices.nb() == 0) {
        out_error = "the parts have no vertices";
        return std::nullopt;
    }

    // Written after every merge: merge_attributes() copies the names the
    // SOURCE geometry carries, so a source without joint attributes would not
    // disturb what is written here - but writing once, at the end, keeps the
    // rule from depending on that.
    std::vector<std::size_t> joint_vertex_counts(parameters.joints.size(), 0);
    {
        const GEO::Mesh&                  geo_mesh   = combined_geometry->get_mesh();
        erhe::geometry::Mesh_attributes&  attributes = combined_geometry->get_attributes();
        const GEO::vec4f                  weights{1.0f, 0.0f, 0.0f, 0.0f};
        for (const Vertex_range& range : vertex_ranges) {
            for (GEO::index_t vertex = range.first; vertex < range.end; ++vertex) {
                const glm::vec3   position    = erhe::geometry::to_glm_vec3(erhe::geometry::get_pointf(geo_mesh.vertices, vertex));
                const std::size_t joint_index = parameters.joint_for_vertex(range.part_index, position);
                if (joint_index >= parameters.joints.size()) {
                    out_error = fmt::format("vertex {} bound to joint {} of {}", vertex, joint_index, parameters.joints.size());
                    return std::nullopt;
                }
                attributes.vertex_joint_indices_0.set(vertex, GEO::vec4u{static_cast<GEO::index_t>(joint_index), 0u, 0u, 0u});
                attributes.vertex_joint_weights_0.set(vertex, weights);
                ++joint_vertex_counts[joint_index];
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
        out_error = fmt::format(
            "geometry processing changed the vertex count from {} to {}",
            vertex_count_before_process, vertex_count_after_process
        );
        return std::nullopt;
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
        .buffer_info = context.mesh_memory->make_skinned_primitive_buffer_info()
    };
    std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(combined_geometry);
    if (!primitive->make_renderable_mesh(build_info, normal_style)) {
        out_error = "failed to build the renderable mesh";
        return std::nullopt;
    }
    if (!primitive->make_raytrace()) {
        out_error = "failed to build the raytrace mesh";
        return std::nullopt;
    }

    // Skeleton is left unset: erhe::scene::get_skin_transform_root() computes
    // the closest common ancestor of the joints when the skin names none, so
    // an explicit guess would only be able to be wrong.
    std::shared_ptr<erhe::scene::Skin> skin = std::make_shared<erhe::scene::Skin>(parameters.name + " skin");
    skin->skin_data.joints                = parameters.joints;
    skin->skin_data.inverse_bind_matrices = parameters.inverse_bind_matrices;
    skin->enable_flag_bits(
        erhe::Item_flags::content |
        erhe::Item_flags::show_in_ui |
        erhe::Item_flags::id
    );

    std::shared_ptr<erhe::scene::Mesh> new_mesh = std::make_shared<erhe::scene::Mesh>(parameters.name);
    new_mesh->add_primitive(primitive, parameters.material);
    new_mesh->layer_id = scene_root->layers().content()->id;
    new_mesh->enable_flag_bits(
        erhe::Item_flags::content |
        erhe::Item_flags::id      |
        erhe::Item_flags::show_in_ui
    );
    // glTF 2.0: the skinned mesh node's own transform is ignored by the
    // skinning; the new prim sits at world identity - the bind space, where
    // the merged geometry is - so what reads the mesh's world transform
    // (erhe::scene::get_bind_pose_parent_from_node, anchoring a skin root)
    // sees the frame the inverse bind matrices were made in. A plain insert
    // keeps the local transform, so that is the inverse of the parent's world.
    const erhe::scene::Node* const parent_node = dynamic_cast<const erhe::scene::Node*>(parameters.parent.get());
    new_mesh->set_parent_from_node((parent_node != nullptr) ? glm::inverse(parent_node->world_from_node()) : glm::mat4{1.0f});
    new_mesh->skin = skin;

    std::vector<std::shared_ptr<Operation>> operations;
    operations.push_back(make_library_insert_operation(context, library, skin));
    // The parts leave before the new mesh enters, so a new mesh named after a
    // part (Bind keeps the mesh's name) does not get a uniquified sibling name.
    for (const std::shared_ptr<erhe::scene::Mesh>& part : parameters.parts) {
        operations.push_back(
            std::make_shared<Item_insert_remove_operation>(
                Item_insert_remove_operation::Parameters{
                    .context = context,
                    .item    = part,
                    .parent  = part->get_parent().lock(),
                    .mode    = Item_insert_remove_operation::Mode::remove
                }
            )
        );
    }
    operations.push_back(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = context,
                .item    = new_mesh,
                .parent  = parameters.parent,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );

    return Rigid_skin{
        .mesh                = new_mesh,
        .skin                = skin,
        .operation           = std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)}),
        .joint_vertex_counts = std::move(joint_vertex_counts),
        .vertex_count        = static_cast<std::size_t>(vertex_count_after_process)
    };
}

auto get_bind_refusal(
    const std::shared_ptr<erhe::scene::Mesh>&              mesh,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& bones
) -> std::optional<std::string>
{
    if (!mesh) {
        return std::string{"no mesh to bind"};
    }
    if (mesh->skin) {
        return fmt::format("'{}' already has skin '{}'", mesh->get_name(), mesh->skin->get_name());
    }
    if (mesh->get_primitives().empty()) {
        return fmt::format("'{}' has no primitives", mesh->get_name());
    }
    if (!mesh->get_parent().lock()) {
        return fmt::format("'{}' has no parent", mesh->get_name());
    }
    if (bones.empty()) {
        return std::string{"no bones to bind to (select the bones)"};
    }
    for (const std::shared_ptr<erhe::scene::Node>& bone : bones) {
        if (!bone) {
            return std::string{"a bone is missing"};
        }
        if (!erhe::scene::is_bone(bone.get())) {
            return fmt::format("'{}' is not a bone", bone->get_name());
        }
        if (bone->get_item_host() != mesh->get_item_host()) {
            return fmt::format("bone '{}' and mesh '{}' are in different scenes", bone->get_name(), mesh->get_name());
        }
        const std::optional<erhe::scene::Skin_joint> skin_joint = erhe::scene::find_skin_joint(*bone);
        if (skin_joint.has_value()) {
            return fmt::format(
                "'{}' is already a joint of skin '{}': a bone is bound to one skin",
                bone->get_name(), skin_joint.value().skin->get_name()
            );
        }
    }
    return std::nullopt;
}

auto bind_mesh_to_bones_rigid(
    App_context&                                           context,
    const std::shared_ptr<erhe::scene::Mesh>&              mesh,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& bones
) -> Bone_bind_result
{
    Bone_bind_result result;
    const auto refuse = [&result](const std::string& reason) -> Bone_bind_result {
        result.refusal = fmt::format("Bind (rigid) refused: {}", reason);
        log_operations->warn("{}", result.refusal.value());
        return result;
    };

    const std::optional<std::string> refusal = get_bind_refusal(mesh, bones);
    if (refusal.has_value()) {
        return refuse(refusal.value());
    }
    Scene_root* const scene_root = dynamic_cast<Scene_root*>(mesh->get_item_host());
    if (scene_root == nullptr) {
        return refuse(fmt::format("'{}' is not in a scene", mesh->get_name()));
    }

    std::vector<std::shared_ptr<erhe::scene::Node>> joints;
    for (const std::shared_ptr<erhe::scene::Node>& bone : bones) {
        if (std::find(joints.begin(), joints.end(), bone) == joints.end()) {
            joints.push_back(bone);
        }
    }

    // Bind space = world at rest: inverse binds from the rest world
    // transforms, weights from the rest segments.
    const std::vector<glm::mat4>    rest_world = get_rest_world_transforms(joints);
    const std::vector<Bone_segment> segments   = get_rest_bone_segments(joints, rest_world);
    std::vector<glm::mat4> inverse_bind_matrices;
    inverse_bind_matrices.reserve(rest_world.size());
    for (const glm::mat4& world_from_rest : rest_world) {
        inverse_bind_matrices.push_back(glm::inverse(world_from_rest));
    }

    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    std::string error;
    std::optional<Rigid_skin> rigid_skin = make_rigid_skin(
        context,
        Rigid_skin_parameters{
            .scene_root            = scene_root,
            .name                  = mesh->get_name(),
            .parent                = mesh->get_parent().lock(),
            .material              = primitives.front().material,
            .parts                 = {mesh},
            .joints                = joints,
            .inverse_bind_matrices = inverse_bind_matrices,
            .joint_for_vertex      = [&segments](std::size_t, const glm::vec3& world_position) -> std::size_t {
                return find_nearest_bone_segment(segments, world_position);
            }
        },
        error
    );
    if (!rigid_skin.has_value()) {
        return refuse(error);
    }

    context.operation_stack->queue(rigid_skin.value().operation);
    log_operations->info(
        "Bind (rigid): '{}' to {} bone(s), skin '{}', {} vertices",
        mesh->get_name(), joints.size(), rigid_skin.value().skin->get_name(), rigid_skin.value().vertex_count
    );
    result.mesh                  = rigid_skin.value().mesh;
    result.skin                  = rigid_skin.value().skin;
    result.joints                = std::move(joints);
    result.inverse_bind_matrices = std::move(inverse_bind_matrices);
    result.joint_vertex_counts   = std::move(rigid_skin.value().joint_vertex_counts);
    result.queued                = true;
    return result;
}

} // namespace editor
